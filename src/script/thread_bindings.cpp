#include "sgre/script/thread_bindings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::script {

	static void RequireCount(std::span<const SquirrelValue> arguments, std::size_t count, std::string_view name) {
		if (arguments.size() != count) {
			throw std::runtime_error(std::string(name) + " received an incorrect argument count");
		}
	}

	static std::int32_t WaitTicks(const SquirrelValue& value) {
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return *integer;
		}

		if (const auto* number = std::get_if<float>(&value)) {
			if (!std::isfinite(*number) || static_cast<double>(*number) < std::numeric_limits<std::int32_t>::min() || static_cast<double>(*number) > std::numeric_limits<std::int32_t>::max()) {
				throw std::runtime_error("wait value cannot be converted to int32");
			}

			return static_cast<std::int32_t>(*number);
		}

		throw std::runtime_error("wait value requires a numeric type");
	}

	struct ThreadInstanceRecord {
		ScriptThreadHandle Thread;
		SquirrelObjectIdentity Instance;
		std::optional<SquirrelObjectIdentity> SubscriptionTarget;
	};

	struct StartedScriptThread {
		ScriptThreadHandle Handle;
		std::optional<ScriptThreadFileServices> Files;
	};

	class ThreadFileServicesResolution {
	public:
		explicit ThreadFileServicesResolution(bool& resolving) : m_Resolving(resolving) {
			if (resolving) {
				throw std::runtime_error("Thread file service resolution cannot reenter");
			}

			m_Resolving = true;
		}
		~ThreadFileServicesResolution() {
			m_Resolving = false;
		}
		ThreadFileServicesResolution(const ThreadFileServicesResolution&) = delete;
		ThreadFileServicesResolution& operator=(const ThreadFileServicesResolution&) = delete;

	private:
		bool& m_Resolving;
	};

	class ScriptThreadBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptScheduler& scheduler, ScriptRequest request, ScriptObjectBindings* objects, InstanceInitializer initialize, const ScriptThreadFileServices* file_services, FileServicesProvider actual_file_services) : Vm(vm), Scheduler(scheduler), Request(std::move(request)), FileServiceProvider(std::move(actual_file_services)), Objects(objects), Initialize(std::move(initialize)) {
			if (file_services) {
				FileServices.emplace(*file_services);
			}
			if (!Request && !FileServices && !FileServiceProvider) {
				throw std::runtime_error("thread bindings require a script request service");
			}
		}

		SquirrelVm& Vm;
		ScriptScheduler& Scheduler;
		ScriptRequest Request;
		std::optional<ScriptThreadFileServices> FileServices;
		FileServicesProvider FileServiceProvider;
		bool ResolvingFileServices = false;
		ScriptObjectBindings* Objects = nullptr;
		InstanceInitializer Initialize;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::weak_ptr<Implementation> Owner;
		std::uint64_t NextIdentity = 1;
		std::map<std::uint64_t, ThreadInstanceRecord> Instances;

		void Unsubscribe(const SquirrelValue& instance, ThreadInstanceRecord& record) {
			const auto target = std::exchange(record.SubscriptionTarget, {});
			if (Objects && target) {
				if (const auto child = Vm.ResolveObjectIdentity(*target); child && Objects->HasInstance(*child)) {
					Objects->Unsubscribe(*child, instance);
				}
			}
		}

		void Release(const SquirrelValue& instance) {
			RequireFileServicesIdle();
			const auto handle = Handle(instance);
			Scheduler.RequireCanStop(handle);
			if (Objects) {
				Objects->RequireCanRelease(instance);
			}

			const auto identity = Vm.InstanceHostIdentity(instance, this);
			std::exception_ptr failure;
			try {
				if (Objects) {
					// Native Thread destructor266cd0 notifies before clearing waits/loading/context.
					Objects->Notify(instance, true);
				}
			} catch (...) {
				failure = std::current_exception();
			}

			try {
				Unsubscribe(instance, Instances.at(identity.Value));
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			try {
				Scheduler.Stop(handle);
				Scheduler.Release(handle);
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			Instances.erase(identity.Value);
			try {
				if (Objects && Objects->HasInstance(instance)) {
					Objects->ReleaseInstance(instance);
				}
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void Shutdown() noexcept {
			if (ResolvingFileServices) {
				std::terminate();
			}
			if ((FileServices || FileServiceProvider) && !Vm.IsClosed()) {
				try {
					for (const auto& [identity, record] : Instances) {
						static_cast<void>(identity);
						Scheduler.RequireCanStop(record.Thread);
					}
				} catch (...) {
					std::terminate();
				}
			}

			if (Vm.IsClosed()) {
				Instances.clear();
				return;
			}

			while (!Instances.empty()) {
				const auto identity = Instances.begin()->first;
				try {
					if (const auto instance = Vm.ResolveObjectIdentity(Instances.begin()->second.Instance)) {
						Release(*instance);
					} else {
						const auto handle = Instances.begin()->second.Thread;
						Scheduler.Stop(handle);
						Scheduler.Release(handle);
						Instances.erase(identity);
					}
				} catch (...) {
					// The scheduler still owns any context rejected by its running-context guard.
					Instances.erase(identity);
				}
			}
		}

		ScriptThreadHandle Handle(const SquirrelValue& instance) const {
			const auto identity = Vm.InstanceHostIdentity(instance, this);
			const auto found = Instances.find(identity.Value);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("native Thread instance has no retained host identity");
			}

			static_cast<void>(Scheduler.Info(found->second.Thread));

			return found->second.Thread;
		}

		SquirrelValue Instance(ScriptThreadHandle handle) const {
			for (const auto& [identity, record] : Instances) {
				static_cast<void>(identity);
				if (record.Thread == handle) {
					const auto instance = Vm.ResolveObjectIdentity(record.Instance);
					if (!instance) {
						throw std::runtime_error("native Thread instance identity has expired");
					}

					return *instance;
				}
			}

			throw std::runtime_error("engine thread has no registered native Thread instance");
		}

		void Attach(const SquirrelValue& instance, ScriptThreadHandle handle, const SquirrelValue& environment) {
			bool registered = false;
			bool object_attached = false;
			try {
				if (NextIdentity == std::numeric_limits<std::uint64_t>::max()) {
					throw std::runtime_error("native Thread identity space exhausted");
				}

				if (Objects) {
					Objects->AttachInstance(instance, environment);
					object_attached = true;
				} else if (Initialize) {
					Initialize(instance, environment);
				}

				ThreadInstanceRecord record;
				record.Thread = handle;
				record.Instance = Vm.BorrowObjectIdentity(instance);
				const auto identity_value = NextIdentity++;
				Instances.emplace(identity_value, std::move(record));
				SquirrelInstanceHostIdentity identity;
				identity.TypeTag = this;
				identity.Value = identity_value;
				Vm.SetInstanceHostIdentity(instance, identity);
				registered = true;
				const auto weak = Owner;
				if (Objects) {
					Objects->ConfigureNativeRelease(instance, [weak](const SquirrelValue& receiver) {
						if (const auto owner = weak.lock()) {
							owner->Release(receiver);
						}
					});
				} else {
					Vm.SetInstanceReleaseHook(instance, [weak](SquirrelVm&, const SquirrelValue& receiver) {
						if (const auto owner = weak.lock()) {
							owner->Release(receiver);
						}
					});
				}

				const auto borrowed = Vm.BorrowObjectIdentity(instance);
				Scheduler.AttachNativeInstance(handle, instance, [weak, borrowed] {
					if (const auto owner = weak.lock(); owner && owner->Objects) {
						const auto receiver = owner->Vm.ResolveObjectIdentity(borrowed);
						if (receiver && owner->Objects->HasInstance(*receiver)) {
							owner->Objects->Notify(*receiver, true);
						}
					}
				});
			} catch (...) {
				const auto failure = std::current_exception();
				if (registered) {
					try {
						Release(instance);
					} catch (...) {
						// Release guarantees owned records/context cleanup before propagating a notification failure.
					}
				} else {
					try {
						Scheduler.Stop(handle);
						Scheduler.Release(handle);
					} catch (...) {
						// A failed attach has not entered execution; retain the original construction diagnostic.
					}

					if (object_attached) {
						try {
							Objects->ReleaseInstance(instance);
						} catch (...) {
							// Base cleanup erases its record before reporting notification failures.
						}
					}
				}

				std::rethrow_exception(failure);
			}
		}

		void RequireFileServicesIdle() const {
			if (ResolvingFileServices) {
				throw std::runtime_error("Thread file service provider cannot mutate or reenter its binding");
			}
		}

		std::optional<ScriptThreadFileServices> ResolveFileServices(const SquirrelValue& target) {
			RequireFileServicesIdle();
			if (!TryStringText(target)) {
				return std::nullopt;
			}
			if (FileServices) {
				return FileServices;
			}
			if (!FileServiceProvider) {
				return std::nullopt;
			}
			if (Vm.IsClosed() || std::holds_alternative<std::monostate>(Scheduler.NativeThreadList())) {
				throw std::runtime_error("Thread file service lookup requires its actual live VM/scheduler and application owners");
			}

			const auto native_list = Scheduler.NativeThreadList();
			const ThreadFileServicesResolution resolution(ResolvingFileServices);
			auto services = FileServiceProvider();
			if (Vm.IsClosed() || Scheduler.NativeThreadList() != native_list) {
				throw std::runtime_error("Thread file service provider retired its actual VM/scheduler during lookup");
			}

			return services;
		}

		StartedScriptThread Start(const SquirrelValue& target, std::span<const SquirrelValue> parameters, std::optional<ScriptThreadFileServices> actual_file_services) {
			RequireFileServicesIdle();
			if (const auto* filename = TryStringText(target)) {
				if (actual_file_services) {
					const auto handle = Scheduler.ForkFile(target, actual_file_services->Cache, parameters, 10, ScriptThreadReceiver::ChildRoot);

					return {handle, std::move(actual_file_services)};
				}
				if (FileServices || FileServiceProvider) {
					throw std::runtime_error("typed Thread file request requires its resolved actual service bundle");
				}

				return {Scheduler.ForkLoading(Request(*filename), parameters, 10, ScriptThreadReceiver::ChildRoot), {}};
			}

			return {Scheduler.Fork(target, parameters, 10, ScriptThreadReceiver::ChildRoot), {}};
		}

		void AttachStarted(const SquirrelValue& instance, StartedScriptThread started, const SquirrelValue& environment) {
			Attach(instance, started.Handle, environment);
			try {
				if (started.Files && Scheduler.Info(started.Handle).HasScriptBuffer) {
					static_cast<void>(Scheduler.AppendFileRead(started.Handle, started.Files->Driver, started.Files->Background, started.Files->Mounts, started.Files->Seed));
				}
			} catch (...) {
				const auto failure = std::current_exception();
				try {
					Release(instance);
				} catch (...) {
					// Failed request cleanup completes native ownership before retaining its original diagnostic.
				}
				std::rethrow_exception(failure);
			}
		}

		SquirrelValue Spawn(const SquirrelValue& target, std::span<const SquirrelValue> parameters, const SquirrelValue& environment) {
			RequireFileServicesIdle();
			// Resolve before allocating a Thread/VM receiver; failed deferred setup cannot leave a partial native owner.
			auto services = ResolveFileServices(target);
			const auto instance = Vm.CreateInstance(Class);
			auto started = Start(target, parameters, std::move(services));
			AttachStarted(instance, std::move(started), environment);

			return instance;
		}

		ScriptThreadHandle Current() const {
			const auto current = Scheduler.CurrentThread();
			if (!current) {
				throw std::runtime_error("no thread");
			}

			return *current;
		}

		SquirrelNativeResult Fork(std::span<const SquirrelValue> arguments, bool wait) {
			if (arguments.size() < 2) {
				throw std::runtime_error("failed to fork: missing target");
			}

			const auto parent = wait ? std::optional<ScriptThreadHandle>(Current()) : std::nullopt;
			SquirrelNativeResult result;
			result.Value = Spawn(arguments[1], arguments.subspan(2), {});
			if (parent) {
				const auto child = Handle(result.Value);
				Scheduler.WaitForChild(*parent, child);
				if (Objects) {
					const auto parent_instance = Instance(*parent);
					const auto parent_identity = Vm.InstanceHostIdentity(parent_instance, this).Value;
					Instances.at(parent_identity).SubscriptionTarget = Vm.BorrowObjectIdentity(result.Value);
					const auto weak = Owner;
					Objects->Subscribe(result.Value, parent_instance, [weak, parent = *parent, child, parent_identity](const SquirrelValue&) {
						if (const auto owner = weak.lock()) {
							owner->Scheduler.WakeChild(parent, child);
							const auto found = owner->Instances.find(parent_identity);
							if (found != owner->Instances.end()) {
								if (const auto listener = owner->Vm.ResolveObjectIdentity(found->second.Instance)) {
									owner->Unsubscribe(*listener, found->second);
								}
							}
						}

						return true;
					});
				}
				return Vm.RequestNativeSuspension();
			}

			return result;
		}

		SquirrelNativeResult Wait(std::span<const SquirrelValue> arguments) {
			const auto current = Current();
			std::optional<SquirrelValue> timeout;
			std::int32_t minimum = -1;
			std::vector<SquirrelValue> notifications;
			for (const auto& argument : arguments.subspan(1)) {
				if (std::holds_alternative<std::int32_t>(argument) || std::holds_alternative<float>(argument)) {
					const auto ticks = WaitTicks(argument);
					if (ticks >= 0 && (minimum < 0 || ticks < minimum)) {
						minimum = ticks;
						timeout = argument;
					}
				} else if (TryStringText(argument)) {
					notifications.push_back(argument);
				} else if (Vm.Type(argument) == SquirrelValueType::Instance) {
					throw std::runtime_error("native object-notification wait has not been recovered");
				}
			}

			Scheduler.WaitForNotificationValues(current, notifications, timeout);

			return Vm.RequestNativeSuspension();
		}

		void Install(const std::shared_ptr<Implementation>& owner, const SquirrelValue& base) {
			Owner = owner;
			Class = Vm.CreateClass(base);
			ClassRoot = Vm.Retain(Class);
			const auto weak = std::weak_ptr<Implementation>(owner);
			const auto callback = [weak](auto operation) {
				return [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
					const auto locked = weak.lock();
					if (!locked) {
						throw std::runtime_error("native Thread bindings have expired");
					}

					locked->RequireFileServicesIdle();

					return operation(*locked, arguments);
				};
			};
			const auto method = [&](std::string name, auto operation) {
				const auto closure = Vm.CreateNativeClosure("Thread." + name, callback(operation));
				Vm.NewSlot(Class, name, closure);
			};

			method("constructor", [](Implementation& state, const auto arguments) {
				if (arguments.empty()) {
					throw std::runtime_error("Thread constructor is missing its receiver");
				}

				const auto environment = arguments.size() >= 2 ? arguments[1] : SquirrelValue{};
				auto started = arguments.size() >= 3 ? state.Start(arguments[2], arguments.subspan(3), state.ResolveFileServices(arguments[2])) : StartedScriptThread{state.Scheduler.CreateDormant(), {}};
				state.AttachStarted(arguments[0], std::move(started), environment);

				return SquirrelNativeResult{};
			});
			const std::array<std::uint8_t, 8> exit_metadata{1, 0, 0, 0, 1, 0, 0, 0};
			const std::array<SquirrelValue, 1> exit_capture{Vm.CreateUserData(exit_metadata)};
			Vm.NewSlot(Class, std::string("exit"), Vm.CreateNativeClosureWithCaptures("Thread.exit", callback([exit_metadata](Implementation& state, const auto) {
				const auto captured = state.Vm.NativeStackValue(-1);
				if (state.Vm.Type(captured) != SquirrelValueType::UserData) {
					throw std::runtime_error("Thread.exit requires its genuine native function capture");
				}
				const auto bytes = state.Vm.UserDataBytes(captured);
				if (bytes.size() != exit_metadata.size() || !std::equal(bytes.begin(), bytes.end(), exit_metadata.begin())) {
					throw std::runtime_error("Thread.exit native function capture identity is invalid");
				}
				// Native04dc10 pops its pointer capture before267bd0 tests logical argument count.
				state.Vm.SetNativeStackSize(state.Vm.NativeStackSize() - 1);
				const auto receiver = state.Vm.NativeStackValue(1);
				const auto result = state.Vm.NativeStackSize() < 2 ? SquirrelValue{} : state.Vm.NativeStackValue(2);
				state.Scheduler.Exit(state.Handle(receiver), result);
				return SquirrelNativeResult::NoValues();
			}),
			                                           exit_capture));
			const std::array<std::uint8_t, 8> cue_metadata{2, 0, 0, 0, 1, 0, 0, 0};
			const std::array<SquirrelValue, 1> cue_capture{Vm.CreateUserData(cue_metadata)};
			const auto cue = Vm.CreateNativeClosureWithCaptures("Thread.cue", callback([cue_metadata](Implementation& state, const auto) {
				const auto handle = state.Handle(state.Vm.NativeStackValue(1));
				const auto captured = state.Vm.NativeStackValue(-1);
				if (state.Vm.Type(captured) != SquirrelValueType::UserData) {
					throw std::runtime_error("Thread.cue requires its genuine native function capture");
				}
				const auto bytes = state.Vm.UserDataBytes(captured);
				if (bytes.size() != cue_metadata.size() || !std::equal(bytes.begin(), bytes.end(), cue_metadata.begin())) {
					throw std::runtime_error("Thread.cue native function capture identity is invalid");
				}

				state.Scheduler.Cue(handle);

				return SquirrelNativeResult::NoValues();
			}), cue_capture);
			Vm.NewSlot(Class, std::string("cue"), cue);
			method("getStatus", [](Implementation& state, const auto arguments) {
				RequireCount(arguments, 1, "Thread.getStatus");
				SquirrelNativeResult result;
				result.Value = static_cast<std::int32_t>(state.Scheduler.Info(state.Handle(arguments[0])).Status);

				return result;
			});
			method("getCurrentTick", [](Implementation& state, const auto arguments) {
				RequireCount(arguments, 1, "Thread.getCurrentTick");
				SquirrelNativeResult result;
				result.Value = state.Scheduler.Info(state.Handle(arguments[0])).ElapsedTicks;

				return result;
			});
			method("getPriority", [](Implementation& state, const auto arguments) {
				RequireCount(arguments, 1, "Thread.getPriority");
				SquirrelNativeResult result;
				result.Value = state.Scheduler.Info(state.Handle(arguments[0])).Priority;

				return result;
			});
			method("setPriority", [](Implementation& state, const auto arguments) {
				RequireCount(arguments, 2, "Thread.setPriority");
				state.Scheduler.SetPriority(state.Handle(arguments[0]), WaitTicks(arguments[1]));

				return SquirrelNativeResult{};
			});
			method("getExitCode", [](Implementation& state, const auto arguments) {
				RequireCount(arguments, 1, "Thread.getExitCode");
				SquirrelNativeResult result;
				result.Value = state.Scheduler.Info(state.Handle(arguments[0])).Result;

				return result;
			});
			method("getResult", [](Implementation& state, const auto arguments) {
				RequireCount(arguments, 1, "Thread.getResult");
				SquirrelNativeResult result;
				result.Value = state.Scheduler.Info(state.Handle(arguments[0])).NativeCallResult;

				return result;
			});

			Vm.NewSlot(Vm.RootTable(), std::string("Thread"), Class);
			Vm.NewSlot(Vm.RootTable(), std::string("THREAD_DEFAULT_PRIORITY"), std::int32_t{10});
			Vm.RegisterNative("fork", callback([](Implementation& state, const auto arguments) {
				return state.Fork(arguments, false);
			}));
			Vm.RegisterNative("system", callback([](Implementation& state, const auto arguments) {
				return state.Fork(arguments, true);
			}));
			Vm.RegisterNative("wait", callback([](Implementation& state, const auto arguments) {
				return state.Wait(arguments);
			}));
			Vm.RegisterNative("execOnBase", callback([](Implementation& state, const auto arguments) {
				if (arguments.size() < 2) {
					throw std::runtime_error("invalid param");
				}

				std::vector<SquirrelValue> parameters{state.Vm.RootTableInContext(state.Vm.RootContext())};
				parameters.insert(parameters.end(), arguments.begin() + 2, arguments.end());
				const auto nested = state.Vm.InvokeInContext(state.Vm.RootContext(), arguments[1], parameters);
				if (nested.State != SquirrelRunState::Completed) {
					throw std::runtime_error("cannot suspend through execOnBase");
				}

				// Native 268120 returns sq_call status zero, so 254850 discards its scratch-stack value.
				return SquirrelNativeResult{};
			}));
			Vm.RegisterNative("notify", callback([](Implementation& state, const auto arguments) {
				RequireCount(arguments, 2, "notify");
				const auto* text = TryStringText(arguments[1]);
				if (!text) {
					throw std::runtime_error("notify requires a string");
				}

				state.Scheduler.Notify(*text);

				return SquirrelNativeResult{};
			}));
			Vm.RegisterNative("getCurrentThread", callback([](Implementation& state, const auto arguments) {
				RequireCount(arguments, 1, "getCurrentThread");
				SquirrelNativeResult result;
				result.Value = state.Instance(state.Current());

				return result;
			}));
			Vm.RegisterNative("getThreadList", callback([](Implementation& state, const auto arguments) {
				RequireCount(arguments, 1, "getThreadList");
				SquirrelNativeResult result;
				result.Value = state.Scheduler.NativeThreadList();

				return result;
			}));
			Vm.RegisterNative("getCurrentTick", callback([](Implementation& state, const auto arguments) {
				RequireCount(arguments, 1, "getCurrentTick");
				SquirrelNativeResult result;
				result.Value = state.Scheduler.CurrentTick();

				return result;
			}));
			Vm.RegisterNative("getDiffTick", callback([](Implementation& state, const auto arguments) {
				RequireCount(arguments, 1, "getDiffTick");
				SquirrelNativeResult result;
				result.Value = state.Scheduler.DiffTick();

				return result;
			}));
		}
	};

	ScriptThreadBindings::ScriptThreadBindings(SquirrelVm& vm, ScriptScheduler& scheduler, ScriptRequest request, ScriptObjectBindings& objects) : ScriptThreadBindings(vm, scheduler, std::move(request), &objects, objects.ObjectClass(), {}) {
	}

	ScriptThreadBindings::ScriptThreadBindings(SquirrelVm& vm, ScriptScheduler& scheduler, ScriptThreadFileServices file_services, ScriptObjectBindings& objects) : ScriptThreadBindings(vm, scheduler, {}, &objects, objects.ObjectClass(), {}, &file_services) {
	}

	ScriptThreadBindings::ScriptThreadBindings(SquirrelVm& vm, ScriptScheduler& scheduler, FileServicesProvider actual_file_services, ScriptObjectBindings& objects) : ScriptThreadBindings(vm, scheduler, {}, &objects, objects.ObjectClass(), {}, nullptr, std::move(actual_file_services)) {
	}

	ScriptThreadBindings::ScriptThreadBindings(SquirrelVm& vm, ScriptScheduler& scheduler, ScriptRequest request, const SquirrelValue& object_class, InstanceInitializer initialize) : ScriptThreadBindings(vm, scheduler, std::move(request), nullptr, object_class, std::move(initialize)) {
	}

	ScriptThreadBindings::ScriptThreadBindings(SquirrelVm& vm, ScriptScheduler& scheduler, ScriptRequest request, ScriptObjectBindings* objects, const SquirrelValue& object_class, InstanceInitializer initialize, const ScriptThreadFileServices* file_services, FileServicesProvider actual_file_services) : m_Implementation(std::make_shared<Implementation>(vm, scheduler, std::move(request), objects, std::move(initialize), file_services, std::move(actual_file_services))) {
		m_Implementation->Install(m_Implementation, object_class);
	}

	ScriptThreadBindings::~ScriptThreadBindings() {
		m_Implementation->Shutdown();
	}

	SquirrelValue ScriptThreadBindings::Spawn(const SquirrelValue& target, std::span<const SquirrelValue> parameters, const SquirrelValue& environment) {
		return m_Implementation->Spawn(target, parameters, environment);
	}

	ScriptThreadHandle ScriptThreadBindings::ThreadHandle(const SquirrelValue& instance) const {
		return m_Implementation->Handle(instance);
	}

	SquirrelValue ScriptThreadBindings::ThreadClass() const {
		return m_Implementation->Class;
	}

	void ScriptThreadBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
