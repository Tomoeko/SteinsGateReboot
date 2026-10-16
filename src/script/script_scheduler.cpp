#include "sgre/script/script_scheduler.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::script {

	static std::int32_t AddTicks(std::int32_t left, std::int32_t right) {
		return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(left) + std::bit_cast<std::uint32_t>(right));
	}

	static std::int32_t TimeoutTicks(const SquirrelValue& value) {
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return *integer;
		}

		if (const auto* number = std::get_if<float>(&value)) {
			if (!std::isfinite(*number) || static_cast<double>(*number) < std::numeric_limits<std::int32_t>::min() || static_cast<double>(*number) > std::numeric_limits<std::int32_t>::max()) {
				throw std::runtime_error("thread timeout cannot be converted to int32");
			}

			return static_cast<std::int32_t>(*number);
		}

		throw std::runtime_error("thread timeout requires an integer or float");
	}

	struct ScheduledThread {
		std::optional<SquirrelContextHandle> Context;
		SquirrelValue Callable;
		SquirrelHostRoot CallableRoot;
		SquirrelHostRoot ArgumentsRoot;
		SquirrelObjectIdentity NativeInstance;
		bool NativeAttached = false;
		bool NativeRegistered = false;
		SquirrelHostRoot ResultRoot;
		SquirrelHostRoot ChildWeakRoot;
		SquirrelHostRoot ResumeRoot;
		ScriptScheduler::CompletionCallback Completion;
		ScriptScheduler::LoadPoll Poll;
		std::unique_ptr<ScriptBufferLoad> FileLoad;
		bool FileReadAttached = false;
		std::vector<SquirrelValue> Arguments;
		ScriptThreadInfo Info;
		ScriptThreadReceiver Receiver = ScriptThreadReceiver::Explicit;
		bool Started = false;
		bool Completing = false;
		std::optional<ScriptThreadHandle> Child;
		std::vector<SquirrelHostRoot> Notifications;
		bool NativeNotificationComparison = false;
		SquirrelValue ResumeValue;
		bool FileOrigin = false;
		bool TypedFileOrigin = false;
	};

	struct ScriptCurrentThreadScope {
		std::optional<ScriptThreadHandle>& Current;
		std::optional<ScriptThreadHandle> Previous;

		ScriptCurrentThreadScope(std::optional<ScriptThreadHandle>& current, ScriptThreadHandle actual) : Current(current), Previous(std::exchange(current, actual)) {
		}

		~ScriptCurrentThreadScope() {
			Current = Previous;
		}
	};

	class ScriptScheduler::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptSchedulerLimits limits) : Vm(vm), Limits(limits) {
			if (limits.MaxThreads == 0 || limits.MaxThreads > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) || limits.MaxWaitNotifications == 0 || limits.InstructionsPerThread == 0) {
				throw std::runtime_error("script scheduler requires bounded positive limits");
			}

			Threads.reserve(limits.MaxThreads);
			Generations.reserve(limits.MaxThreads);
			Registry.reserve(limits.MaxThreads);
			NativeInstances = Vm.Retain(Vm.CreateArray());
		}

		~Implementation() {
			// A concrete receiver cannot outlive this Thread owner through an attached reader borrow.
			for (const auto& thread : Threads) {
				if (thread && thread->FileLoad && !thread->FileLoad->CanClose()) {
					std::terminate();
				}
			}

			if (Vm.IsClosed()) {
				return;
			}

			for (const auto& thread : Threads) {
				if (thread && thread->Context) {
					try {
						Vm.ReleaseContext(*thread->Context);
					} catch (...) {
						// Destruction cannot propagate a host error; normal release validates the context.
					}
				}
			}
		}

		SquirrelVm& Vm;
		ScriptSchedulerLimits Limits;
		SquirrelHostRoot NativeInstances;
		std::vector<std::unique_ptr<ScheduledThread>> Threads;
		std::vector<std::uint64_t> Generations;
		std::vector<ScriptThreadHandle> Registry;
		std::optional<ScriptThreadHandle> Current;
		bool Ticking = false;
		bool Retiring = false;
		std::int32_t Tick = 0;
		std::int32_t Delta = 0;
		bool NativeClosing = false;
		bool NativeListRetired = false;

		ScriptThreadHandle Allocate(std::unique_ptr<ScheduledThread> thread) {
			if (NativeClosing || NativeListRetired) {
				throw std::runtime_error("cannot allocate a script thread during or after driver shutdown");
			}

			const auto free_slot = std::find(Threads.begin(), Threads.end(), nullptr);
			const auto index = static_cast<std::size_t>(free_slot - Threads.begin());
			if (index == Threads.size() && index >= Limits.MaxThreads) {
				throw std::runtime_error("script thread allocation limit exceeded");
			}

			if (index == Threads.size()) {
				Generations.push_back(1);
				Threads.push_back(std::move(thread));
			} else {
				Threads[index] = std::move(thread);
			}

			const auto handle = Handle(index);
			if (Thread(handle).Info.Status != ScriptThreadStatus::None) {
				Registry.push_back(handle);
			}

			return handle;
		}

		ScriptThreadHandle Handle(std::size_t index) const {
			ScriptThreadHandle handle;
			handle.Index = static_cast<std::uint32_t>(index);
			handle.Generation = Generations[index];
			handle.Owner = this;

			return handle;
		}

		bool HasThread(ScriptThreadHandle handle) const {
			return handle.Owner == this && handle.Index < Threads.size() && Threads[handle.Index] && handle.Generation == Generations[handle.Index];
		}

		ScheduledThread& Thread(ScriptThreadHandle handle) const {
			if (Vm.IsClosed()) {
				throw std::runtime_error("script scheduler VM is closed");
			}

			if (handle.Owner != this || handle.Index >= Threads.size() || !Threads[handle.Index] || handle.Generation != Generations[handle.Index]) {
				throw std::runtime_error("script thread handle is stale or belongs to another scheduler");
			}

			return *Threads[handle.Index];
		}

		static void ClearWait(ScheduledThread& thread) {
			thread.Child.reset();
			thread.ChildWeakRoot = {};
			thread.Notifications.clear();
			thread.NativeNotificationComparison = false;
			thread.Info.TimeoutTicks = -1;
		}

		SquirrelValue RemoveNativeRegistration(ScheduledThread& thread) {
			if (!thread.NativeRegistered) {
				return {};
			}

			const auto& list = NativeInstances.Value();
			const auto count = Vm.Length(list);
			if (count > Limits.MaxThreads) {
				throw std::runtime_error("native Thread list exceeds the scheduler allocation bound");
			}

			for (std::size_t index = 0; index < count; ++index) {
				const auto instance = Vm.GetSlot(list, static_cast<std::int32_t>(index));
				if (Vm.Type(instance) == SquirrelValueType::Instance && Vm.BorrowObjectIdentity(instance) == thread.NativeInstance) {
					// The returned value owns storage through primitive removal and guarded finalization.
					auto retiring = instance;
					static_cast<void>(Vm.RemoveArray(list, static_cast<std::int32_t>(index)));
					thread.NativeRegistered = false;

					return retiring;
				}
			}

			thread.NativeRegistered = false;

			return {};
		}

		std::optional<ScriptThreadHandle> NativeHandle(const SquirrelValue& instance) const {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				return {};
			}

			const auto identity = Vm.BorrowObjectIdentity(instance);
			for (std::size_t index = 0; index < Threads.size(); ++index) {
				if (Threads[index] && Threads[index]->NativeAttached && Threads[index]->NativeInstance == identity) {
					return Handle(index);
				}
			}

			return {};
		}

		std::vector<ScriptThreadHandle> Snapshot() const {
			std::vector<ScriptThreadHandle> handles;
			const auto count = NativeListRetired ? 0 : Vm.Length(NativeInstances.Value());
			if (count > Limits.MaxThreads) {
				throw std::runtime_error("native Thread list exceeds the scheduler allocation bound");
			}

			handles.reserve(Registry.size() + count);
			for (std::size_t index = 0; index < count; ++index) {
				if (const auto handle = NativeHandle(Vm.GetSlot(NativeInstances.Value(), static_cast<std::int32_t>(index)))) {
					handles.push_back(*handle);
				}
			}

			for (const auto handle : Registry) {
				if (!Thread(handle).NativeAttached) {
					handles.push_back(handle);
				}
			}

			return handles;
		}

		void PruneNativeList() {
			const auto& list = NativeInstances.Value();
			std::size_t remaining = Vm.Length(list);
			std::size_t index = 0;
			while (index < remaining && index < Vm.Length(list)) {
				const auto instance = Vm.GetSlot(list, static_cast<std::int32_t>(index));
				const auto handle = NativeHandle(instance);
				if (handle && Thread(*handle).Info.Status != ScriptThreadStatus::None) {
					++index;
					continue;
				}

				if (handle) {
					Thread(*handle).NativeRegistered = false;
				}

				// Native final pruning removes in current array order, with a bounded initial count.
				static_cast<void>(Vm.RemoveArray(list, static_cast<std::int32_t>(index)));
				--remaining;
			}
		}

		void Complete(ScriptThreadHandle handle, SquirrelValue value, std::string error, ScriptSchedulerTick& tick, bool native_file_load_failure = false) {
			auto& thread = Thread(handle);
			thread.Info.Result = std::move(value);
			thread.Info.Error = std::move(error);
			thread.ResultRoot = Vm.Retain(thread.Info.Result);
			std::exception_ptr failure;
			thread.Completing = true;
			try {
				if (thread.Completion) {
					thread.Completion();
				}
			} catch (...) {
				failure = std::current_exception();
			}

			thread.Completing = false;
			if (!native_file_load_failure) {
				thread.Info.Status = ScriptThreadStatus::None;
			}
			ClearWait(thread);
			thread.Callable = {};
			thread.CallableRoot = {};
			thread.Poll = {};
			thread.Arguments.clear();
			thread.ArgumentsRoot = {};
			if (native_file_load_failure) {
				thread.Info.Status = ScriptThreadStatus::None;
			}
			tick.Completed.push_back(handle);

			for (const auto observer : Registry) {
				auto& parent = Thread(observer);
				if (parent.Child == handle) {
					parent.ResumeValue = thread.Info.Result;
					parent.ResumeRoot = Vm.Retain(parent.ResumeValue);
					ClearWait(parent);
				}
			}

			if (failure) {
				thread.Info.NativeCallResult = -1;
				try {
					std::rethrow_exception(failure);
				} catch (const std::exception& error) {
					if (thread.Info.Error.empty()) {
						thread.Info.Error = std::string_view(error.what()).substr(0, 4096);
					}
				} catch (...) {
					if (thread.Info.Error.empty()) {
						thread.Info.Error = "native thread completion callback failed";
					}
				}
			}
		}

		void Execute(ScriptThreadHandle handle, ScriptSchedulerTick& tick) {
			auto& thread = Thread(handle);
			const ScriptCurrentThreadScope current_scope(Current, handle);

			try {
				SquirrelRunResult result;
				if (!thread.Started) {
					if (thread.Receiver == ScriptThreadReceiver::ChildRoot) {
						thread.Arguments.insert(thread.Arguments.begin(), Vm.RootTableInContext(*thread.Context));
					}

					thread.Started = true;
					// The context receives callable/arguments before script execution; private preparation roots end here.
					thread.CallableRoot = {};
					thread.ArgumentsRoot = {};
					result = Vm.InvokeInContext(*thread.Context, thread.Callable, thread.Arguments, Limits.InstructionsPerThread);
					thread.Callable = {};
					thread.Arguments.clear();
				} else {
					const auto value = std::exchange(thread.ResumeValue, SquirrelValue{});
					thread.ResumeRoot = {};
					result = Vm.ResumeInContext(*thread.Context, value, Limits.InstructionsPerThread);
				}

				if (result.Instructions > std::numeric_limits<std::uint64_t>::max() - tick.Instructions) {
					throw std::runtime_error("script scheduler instruction accounting overflows");
				}

				tick.Instructions += result.Instructions;
				if (result.State == SquirrelRunState::Completed) {
					Complete(handle, std::move(result.Value), {}, tick);
				}
			} catch (const SquirrelVmException& error) {
				thread.Info.NativeCallResult = -1;
				if (thread.Info.Status == ScriptThreadStatus::None) {
					thread.Info.Error = error.what();
				} else {
					Complete(handle, error.ThrownValue(), error.what(), tick);
				}
			} catch (const std::exception& error) {
				thread.Info.NativeCallResult = -1;
				if (thread.Info.Status == ScriptThreadStatus::None) {
					thread.Info.Error = error.what();
				} else {
					Complete(handle, std::string(error.what()), error.what(), tick);
				}
			}
		}

		void AdvanceThread(ScriptThreadHandle handle, std::int32_t delta_ticks, ScriptSchedulerTick& tick) {
			auto& thread = Thread(handle);
			if (thread.Info.Status == ScriptThreadStatus::None) {
				return;
			}

			if (thread.Info.Status == ScriptThreadStatus::LoadingFunction) {
				try {
					thread.Context = thread.Receiver == ScriptThreadReceiver::ChildRoot ? Vm.CreateContextFrom(Vm.RootContext()) : Vm.CreateContext();
					thread.Info.Status = ScriptThreadStatus::Running;
				} catch (const std::exception& error) {
					thread.Info.NativeCallResult = -1;
					Complete(handle, std::string(error.what()), error.what(), tick);

					return;
				}
			}

			if (thread.Info.Status == ScriptThreadStatus::LoadingFile && thread.FileLoad) {
				if (!thread.FileLoad->Buffer().Observe().Ready) {
					return;
				}

				try {
					thread.Context = Vm.CreateContextFrom(Vm.RootContext());
					thread.Callable = thread.FileLoad->LoadCompiledClosureAndReleaseBufferInContext(*thread.Context);
					thread.CallableRoot = Vm.Retain(thread.Callable);
					thread.Info.Status = ScriptThreadStatus::Running;
				} catch (const SquirrelVmException& error) {
					if (thread.FileLoad->HasBuffer()) {
						thread.FileLoad->ReleaseReadyBuffer();
					}
					thread.Info.NativeCallResult = -1;
					if (thread.Context) {
						Vm.SetLastErrorInContext(*thread.Context, error.ThrownValue());
					}
					Complete(handle, error.ThrownValue(), error.what(), tick, true);

					return;
				} catch (const std::exception& error) {
					if (thread.FileLoad->HasBuffer()) {
						thread.FileLoad->ReleaseReadyBuffer();
					}
					thread.Info.NativeCallResult = -1;
					const auto diagnostic = Vm.CreateString(std::string(error.what()).substr(0, 4096));
					if (thread.Context) {
						Vm.SetLastErrorInContext(*thread.Context, diagnostic);
					}
					Complete(handle, diagnostic, error.what(), tick, true);

					return;
				}
			}

			if (thread.Info.Status == ScriptThreadStatus::LoadingFile) {
				try {
					const auto callable = thread.Poll();
					if (!callable) {
						return;
					}

					thread.Context = thread.Receiver == ScriptThreadReceiver::ChildRoot ? Vm.CreateContextFrom(Vm.RootContext()) : Vm.CreateContext();
					thread.Callable = *callable;
					thread.CallableRoot = Vm.Retain(thread.Callable);
					thread.Poll = {};
					thread.Info.Status = ScriptThreadStatus::Running;
				} catch (const SquirrelVmException& error) {
					thread.Info.NativeCallResult = -1;
					Complete(handle, error.ThrownValue(), error.what(), tick);

					return;
				} catch (const std::exception& error) {
					thread.Info.NativeCallResult = -1;
					Complete(handle, std::string(error.what()), error.what(), tick);

					return;
				}
			}

			thread.Info.ElapsedTicks = AddTicks(thread.Info.ElapsedTicks, delta_ticks);
			if (thread.Info.TimeoutTicks >= 0) {
				thread.Info.TimeoutTicks = AddTicks(thread.Info.TimeoutTicks, -delta_ticks);
				if (thread.Info.TimeoutTicks < 0) {
					ClearWait(thread);
				}
			}

			if (thread.Info.Status == ScriptThreadStatus::Running && !thread.Info.ExecutionStopped && !thread.Child && thread.Notifications.empty() && thread.Info.TimeoutTicks < 0) {
				Execute(handle, tick);
			}
		}
	};

	ScriptScheduler::ScriptScheduler(SquirrelVm& vm, ScriptSchedulerLimits limits) : m_Implementation(std::make_unique<Implementation>(vm, limits)) {
	}

	ScriptScheduler::~ScriptScheduler() = default;

	ScriptThreadHandle ScriptScheduler::Fork(const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::int32_t priority, ScriptThreadReceiver receiver) {
		if (receiver != ScriptThreadReceiver::Explicit && receiver != ScriptThreadReceiver::ChildRoot) {
			throw std::runtime_error("script thread receiver mode is invalid");
		}

		auto& state = *m_Implementation;
		if (state.Vm.Type(callable) != SquirrelValueType::Closure) {
			throw std::runtime_error("script thread target requires a recovered bytecode closure");
		}

		if (receiver == ScriptThreadReceiver::Explicit && arguments.empty()) {
			throw std::runtime_error("script thread invocation requires its original receiver");
		}

		auto thread = std::make_unique<ScheduledThread>();
		thread->Callable = callable;
		thread->CallableRoot = state.Vm.Retain(callable);
		thread->Receiver = receiver;
		thread->Arguments.assign(arguments.begin(), arguments.end());
		thread->ArgumentsRoot = state.Vm.Retain(state.Vm.CreateArray(arguments));
		thread->Info.Priority = priority;
		thread->Info.Status = ScriptThreadStatus::LoadingFunction;

		return state.Allocate(std::move(thread));
	}

	ScriptThreadHandle ScriptScheduler::ForkLoading(LoadPoll poll, std::span<const SquirrelValue> arguments, std::int32_t priority, ScriptThreadReceiver receiver) {
		if (receiver != ScriptThreadReceiver::Explicit && receiver != ScriptThreadReceiver::ChildRoot) {
			throw std::runtime_error("script thread receiver mode is invalid");
		}

		if (!poll || (receiver == ScriptThreadReceiver::Explicit && arguments.empty())) {
			throw std::runtime_error("loading script thread requires a poll and its original receiver");
		}

		auto thread = std::make_unique<ScheduledThread>();
		thread->Poll = std::move(poll);
		thread->FileOrigin = true;
		thread->Receiver = receiver;
		thread->Arguments.assign(arguments.begin(), arguments.end());
		thread->ArgumentsRoot = m_Implementation->Vm.Retain(m_Implementation->Vm.CreateArray(arguments));
		thread->Info.Priority = priority;
		thread->Info.Status = ScriptThreadStatus::LoadingFile;

		return m_Implementation->Allocate(std::move(thread));
	}

	ScriptThreadHandle ScriptScheduler::ForkFile(const SquirrelValue& actual_filename, resource::ResourceCache& actual_cache, std::span<const SquirrelValue> arguments, std::int32_t priority, ScriptThreadReceiver receiver) {
		if (receiver != ScriptThreadReceiver::Explicit && receiver != ScriptThreadReceiver::ChildRoot) {
			throw std::runtime_error("script thread receiver mode is invalid");
		}
		if (receiver == ScriptThreadReceiver::Explicit && arguments.empty()) {
			throw std::runtime_error("file script thread requires its original receiver");
		}

		auto thread = std::make_unique<ScheduledThread>();
		thread->FileLoad = std::make_unique<ScriptBufferLoad>(m_Implementation->Vm, actual_filename, actual_cache);
		thread->TypedFileOrigin = true;
		thread->FileOrigin = true;
		thread->Receiver = receiver;
		thread->Arguments.assign(arguments.begin(), arguments.end());
		thread->ArgumentsRoot = m_Implementation->Vm.Retain(m_Implementation->Vm.CreateArray(arguments));
		thread->Info.Priority = priority;
		thread->Info.Status = ScriptThreadStatus::LoadingFile;

		return m_Implementation->Allocate(std::move(thread));
	}

	std::optional<scene::ParallelTaskHandle> ScriptScheduler::AppendFileRead(ScriptThreadHandle handle, scene::CommonProcessTasks& actual_driver, core::BackgroundCallManager& background, resource::ArchiveMounts& mounts, std::string_view seed) {
		auto& state = *m_Implementation;
		auto& thread = state.Thread(handle);
		if (state.NativeClosing || state.NativeListRetired || thread.Info.Status != ScriptThreadStatus::LoadingFile || !thread.FileLoad || thread.FileReadAttached) {
			throw std::runtime_error("file reader attachment requires one actual fresh loading Thread owner");
		}

		const auto reader = thread.FileLoad->AppendMountedRead(actual_driver, background, mounts, seed);
		thread.FileReadAttached = true;

		return reader;
	}

	ScriptThreadHandle ScriptScheduler::CreateDormant() {
		return m_Implementation->Allocate(std::make_unique<ScheduledThread>());
	}

	void ScriptScheduler::AttachNativeInstance(ScriptThreadHandle handle, const SquirrelValue& instance, CompletionCallback completion) {
		auto& state = *m_Implementation;
		auto& thread = state.Thread(handle);
		if (state.NativeClosing || state.NativeListRetired) {
			throw std::runtime_error("cannot attach a native script thread during or after driver shutdown");
		}

		if (state.Vm.Type(instance) != SquirrelValueType::Instance || thread.NativeAttached) {
			throw std::runtime_error("native thread registration requires a fresh script instance association");
		}

		thread.Completion = std::move(completion);
		thread.NativeInstance = state.Vm.BorrowObjectIdentity(instance);
		thread.NativeAttached = true;
		if (thread.Info.Status != ScriptThreadStatus::None) {
			state.Vm.AppendArray(state.NativeInstances.Value(), instance);
			thread.NativeRegistered = true;
		}
	}

	SquirrelValue ScriptScheduler::NativeThreadList() const {
		return m_Implementation->NativeListRetired ? SquirrelValue{} : m_Implementation->NativeInstances.Value();
	}

	ScriptThreadInfo ScriptScheduler::Info(ScriptThreadHandle handle) const {
		const auto& thread = m_Implementation->Thread(handle);
		auto info = thread.Info;
		info.Context = thread.Context;
		if (thread.FileLoad) {
			info.Filename = thread.FileLoad->FilenameValue();
			info.HasScriptBuffer = thread.FileLoad->HasBuffer();
		}

		return info;
	}

	ScriptThreadWaitObservation ScriptScheduler::ObserveWait(ScriptThreadHandle handle) const {
		const auto& owner = *m_Implementation;
		if (owner.Ticking || owner.Retiring || owner.NativeClosing) {
			throw std::runtime_error("script wait observation requires its actual serialized endpoint");
		}

		const auto& thread = owner.Thread(handle);
		ScriptThreadWaitObservation result;
		result.Status = thread.Info.Status;
		result.ExecutionStopped = thread.Info.ExecutionStopped;
		result.ElapsedTicks = thread.Info.ElapsedTicks;
		result.TimeoutTicks = thread.Info.TimeoutTicks;
		result.WaitingChild = thread.Child;
		result.NotificationCount = thread.Notifications.size();
		result.Context = thread.Context;
		result.FileOrigin = thread.FileOrigin;
		result.TypedFileOrigin = thread.TypedFileOrigin;
		const auto notification_count = std::min(thread.Notifications.size(), kScriptWaitObservationNotifications);
		result.NotificationsTruncated = notification_count != thread.Notifications.size();
		result.Notifications.reserve(notification_count);
		for (std::size_t index = 0; index < notification_count; ++index) {
			const auto& text = StringText(thread.Notifications[index].Value());
			result.Notifications.push_back(text.substr(0, kSquirrelObservationTextBytes));
			result.NotificationTextTruncated = result.NotificationTextTruncated || text.size() > kSquirrelObservationTextBytes;
		}
		if (thread.FileLoad) {
			const auto& filename = StringText(thread.FileLoad->FilenameValue());
			result.Filename = filename.substr(0, kSquirrelObservationTextBytes);
			result.FilenameTruncated = filename.size() > kSquirrelObservationTextBytes;
			result.HasScriptBuffer = thread.FileLoad->HasBuffer();
			if (result.HasScriptBuffer) {
				const auto& buffer = thread.FileLoad->Buffer();
				result.BufferReady = buffer.Observe().Ready;
				result.BufferCanClose = buffer.CanClose();
				result.BufferCompletion = buffer.Completion();
				result.BufferStorage = buffer.Storage();
			}
		}

		return result;
	}

	std::optional<ScriptThreadHandle> ScriptScheduler::CurrentThread() const {
		return m_Implementation->Current;
	}

	std::vector<ScriptThreadHandle> ScriptScheduler::ActiveThreads() const {
		return m_Implementation->Snapshot();
	}

	std::int32_t ScriptScheduler::CurrentTick() const {
		return m_Implementation->Tick;
	}

	std::int32_t ScriptScheduler::DiffTick() const {
		return m_Implementation->Delta;
	}

	ScriptSchedulerTick ScriptScheduler::Advance(std::int32_t delta_ticks) {
		auto& state = *m_Implementation;
		if (delta_ticks < 0 || state.Ticking || state.Current || state.Retiring || state.NativeClosing || state.NativeListRetired) {
			throw std::runtime_error("script scheduler requires nonnegative ticks and cannot advance recursively");
		}

		// Native 2686d0 snapshots the registry before sorting: forks during execution wait for the next tick.
		auto snapshot = state.Snapshot();
		std::stable_sort(snapshot.begin(), snapshot.end(), [&](const auto left, const auto right) {
			return state.Thread(left).Info.Priority < state.Thread(right).Info.Priority;
		});
		ScriptSchedulerTick tick;
		state.Tick = AddTicks(state.Tick, delta_ticks);
		state.Delta = delta_ticks;
		state.Ticking = true;

		try {
			for (const auto handle : snapshot) {
				// The native tree borrows raw pointers. A removed/released entry cannot be followed safely here.
				if (state.HasThread(handle)) {
					state.AdvanceThread(handle, delta_ticks, tick);
				}
			}

			std::erase_if(state.Registry, [&](const auto handle) {
				return state.Thread(handle).Info.Status == ScriptThreadStatus::None;
			});
			state.Ticking = false;
			state.Retiring = true;
			state.PruneNativeList();
			state.Retiring = false;
			tick.ActiveThreads = state.Snapshot().size();
		} catch (...) {
			state.Current.reset();
			state.Ticking = false;
			state.Retiring = false;

			throw;
		}

		state.Ticking = false;

		return tick;
	}

	void ScriptScheduler::Cue(ScriptThreadHandle handle) {
		auto& state = *m_Implementation;
		if (state.Thread(handle).Info.Status != ScriptThreadStatus::LoadingFunction) {
			return;
		}

		// This member neither advances the scheduler clock nor visits/prunes another registered thread.
		ScriptSchedulerTick tick;
		state.AdvanceThread(handle, 0, tick);
	}

	void ScriptScheduler::SetPriority(ScriptThreadHandle thread, std::int32_t priority) {
		m_Implementation->Thread(thread).Info.Priority = priority;
	}

	void ScriptScheduler::SetExecutionStopped(ScriptThreadHandle thread, bool stopped) {
		m_Implementation->Thread(thread).Info.ExecutionStopped = stopped;
	}

	void ScriptScheduler::Stop(ScriptThreadHandle handle) {
		RequireCanStop(handle);
		auto& state = *m_Implementation;
		auto& thread = state.Thread(handle);

		if (thread.FileLoad) {
			thread.FileLoad->Close();
			thread.FileLoad.reset();
		}

		Implementation::ClearWait(thread);
		thread.Info.Status = ScriptThreadStatus::None;
		thread.Callable = {};
		thread.CallableRoot = {};
		thread.Poll = {};
		thread.Arguments.clear();
		thread.ArgumentsRoot = {};
		thread.ResumeValue = {};
		thread.ResumeRoot = {};
		std::erase(state.Registry, handle);
		static_cast<void>(state.RemoveNativeRegistration(thread));
	}

	void ScriptScheduler::Exit(ScriptThreadHandle handle, const SquirrelValue& result) {
		RequireCanStop(handle);
		auto& state = *m_Implementation;
		auto& thread = state.Thread(handle);
		if (state.Retiring || state.NativeClosing || state.NativeListRetired || !thread.NativeAttached || !thread.Completion) {
			throw std::runtime_error("Thread.exit requires its actual live native Object notification owner");
		}
		// Native stores +110 before NotifyAll; observers still see the prior status/waits/argument owners.
		auto result_root = state.Vm.Retain(result);
		thread.Info.Result = result;
		thread.ResultRoot = std::move(result_root);
		thread.Completing = true;
		try {
			thread.Completion();
		} catch (...) {
			thread.Completing = false;
			throw;
		}
		thread.Completing = false;
		Implementation::ClearWait(thread);
		if (thread.FileLoad && thread.FileLoad->HasBuffer()) {
			thread.FileLoad->Close();
			thread.FileLoad.reset();
		}
		thread.Arguments.clear();
		thread.ArgumentsRoot = {};
		thread.Info.Status = ScriptThreadStatus::None;
		// The native active array is pruned by the actual scheduler tail, not by this member call.
	}

	void ScriptScheduler::RequireCanStop(ScriptThreadHandle handle) const {
		const auto& state = *m_Implementation;
		if (state.Thread(handle).FileLoad) {
			state.Thread(handle).FileLoad->RequireCanClose();
		}
		if (state.Thread(handle).Completing || (state.Thread(handle).Context && state.Vm.ContextState(*state.Thread(handle).Context) == SquirrelContextState::Running)) {
			throw std::runtime_error("cannot stop an executing context or a completing native thread");
		}
	}

	void ScriptScheduler::ShutdownNativeFunctionThreads() {
		ShutdownNativeThreadsImpl(false);
	}

	void ScriptScheduler::ShutdownNativeThreads() {
		ShutdownNativeThreadsImpl(true);
	}

	void ScriptScheduler::RequireCanShutdownNativeThreads() const {
		static_cast<void>(RequireCanShutdownNativeThreadsImpl(true));
	}

	std::size_t ScriptScheduler::RequireCanShutdownNativeThreadsImpl(bool allow_typed_files) const {
		auto& state = *m_Implementation;
		if (state.Vm.IsClosed() || state.Ticking || state.Retiring || state.NativeClosing) {
			throw std::runtime_error("driver thread cleanup requires a live idle serialized scheduler");
		}

		if (state.NativeListRetired) {
			return 0;
		}

		const auto& list = state.NativeInstances.Value();
		const auto initial_count = state.Vm.Length(list);
		if (initial_count > state.Limits.MaxThreads) {
			throw std::runtime_error("native Thread shutdown array exceeds its allocation bound");
		}

		for (std::size_t index = 0; index < initial_count; ++index) {
			if (const auto handle = state.NativeHandle(state.Vm.GetSlot(list, static_cast<std::int32_t>(index)))) {
				const auto& thread = state.Thread(*handle);
				RequireCanStop(*handle);
				if ((thread.FileOrigin && (!allow_typed_files || !thread.TypedFileOrigin)) || !thread.Completion) {
					throw std::runtime_error("native function shutdown requires actual Object notification and recovered file-origin ScriptBuffer ownership");
				}
			}
		}

		return initial_count;
	}

	void ScriptScheduler::ShutdownNativeThreadsImpl(bool allow_typed_files) {
		const auto initial_count = RequireCanShutdownNativeThreadsImpl(allow_typed_files);
		auto& state = *m_Implementation;
		if (state.NativeListRetired) {
			return;
		}

		const auto& list = state.NativeInstances.Value();
		state.NativeClosing = true;
		try {
			for (std::size_t index = 0; index < initial_count; ++index) {
				if (index >= state.Vm.Length(list)) {
					continue;
				}

				const auto handle = state.NativeHandle(state.Vm.GetSlot(list, static_cast<std::int32_t>(index)));
				if (!handle) {
					continue;
				}

				auto& thread = state.Thread(*handle);
				RequireCanStop(*handle);
				if ((thread.FileOrigin && (!allow_typed_files || !thread.TypedFileOrigin)) || !thread.Completion) {
					throw std::runtime_error("live native shutdown mutation introduced an unsupported thread owner");
				}

				thread.Completing = true;
				try {
					thread.Completion();
				} catch (...) {
					thread.Completing = false;
					throw;
				}

				thread.Completing = false;
				Implementation::ClearWait(thread);
				// Native +58 cleanup is conditional on the still-present +70 buffer. Loaded filenames survive this phase.
				if (thread.FileLoad && thread.FileLoad->HasBuffer()) {
					thread.FileLoad->Close();
					thread.FileLoad.reset();
				}

				thread.Arguments.clear();
				thread.ArgumentsRoot = {};
				thread.Info.Status = ScriptThreadStatus::None;
			}

			// Array clearing may run native release hooks. They must see an already detached registration.
			for (auto& thread : state.Threads) {
				if (thread && thread->NativeAttached) {
					thread->NativeRegistered = false;
				}
			}

			state.Vm.ClearArray(list);
			state.NativeInstances = {};
			state.NativeListRetired = true;
		} catch (...) {
			state.NativeClosing = false;
			throw;
		}

		state.NativeClosing = false;
	}

	void ScriptScheduler::Release(ScriptThreadHandle thread) {
		auto& state = *m_Implementation;
		if (state.Thread(thread).Info.Status != ScriptThreadStatus::None || state.Thread(thread).Completing || (state.Thread(thread).Context && state.Vm.ContextState(*state.Thread(thread).Context) == SquirrelContextState::Running)) {
			throw std::runtime_error("cannot release an active script thread or a completing native thread");
		}

		if (state.Thread(thread).FileLoad) {
			state.Thread(thread).FileLoad->RequireCanClose();
		}

		if (state.Generations[thread.Index] == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("script thread generation limit exceeded");
		}

		if (const auto context = state.Thread(thread).Context) {
			state.Vm.ReleaseContext(*context);
		}

		state.Threads[thread.Index].reset();
		std::erase(state.Registry, thread);
		++state.Generations[thread.Index];
	}

	void ScriptScheduler::WaitForChild(ScriptThreadHandle parent, ScriptThreadHandle child) {
		auto& state = *m_Implementation;
		auto& thread = state.Thread(parent);
		if (parent == child || state.Thread(child).Info.Status == ScriptThreadStatus::None || state.Current != parent) {
			throw std::runtime_error("system wait requires the current thread and an active distinct child");
		}

		Implementation::ClearWait(thread);
		thread.ResumeValue = {};
		thread.ResumeRoot = {};
		thread.Child = child;
		const auto& child_record = state.Thread(child);
		if (child_record.NativeAttached) {
			const auto registration = state.Vm.ResolveObjectIdentity(child_record.NativeInstance);
			if (registration) {
				thread.ChildWeakRoot = state.Vm.Retain(state.Vm.CreateWeakReference(*registration));
			}
		}
	}

	void ScriptScheduler::WaitForNotifications(ScriptThreadHandle handle, std::span<const std::string> notifications, std::optional<SquirrelValue> timeout) {
		auto& state = *m_Implementation;
		auto& thread = state.Thread(handle);
		if (state.Current != handle || notifications.size() > state.Limits.MaxWaitNotifications) {
			throw std::runtime_error("wait requires the current thread and bounded notifications");
		}

		std::vector<SquirrelValue> values;
		values.reserve(notifications.size());
		for (const auto& notification : notifications) {
			values.emplace_back(notification);
		}

		WaitForNotificationValues(handle, values, std::move(timeout));
		thread.NativeNotificationComparison = false;
	}

	void ScriptScheduler::WaitForNotificationValues(ScriptThreadHandle handle, std::span<const SquirrelValue> notifications, std::optional<SquirrelValue> timeout) {
		auto& state = *m_Implementation;
		auto& thread = state.Thread(handle);
		if (state.Current != handle || notifications.size() > state.Limits.MaxWaitNotifications) {
			throw std::runtime_error("wait requires the current thread and bounded notifications");
		}

		const auto ticks = timeout ? TimeoutTicks(*timeout) : -1;
		std::vector<SquirrelHostRoot> retained;
		retained.reserve(notifications.size());
		for (const auto& notification : notifications) {
			if (!TryStringText(notification)) {
				throw std::runtime_error("notification wait requires string values");
			}

			retained.push_back(state.Vm.Retain(notification));
		}

		Implementation::ClearWait(thread);
		thread.ResumeValue = {};
		thread.ResumeRoot = {};
		thread.Notifications = std::move(retained);
		thread.NativeNotificationComparison = true;
		if (ticks >= 0) {
			thread.Info.TimeoutTicks = ticks;
			thread.ResumeValue = std::move(*timeout);
			thread.ResumeRoot = state.Vm.Retain(thread.ResumeValue);
		}
	}

	void ScriptScheduler::WakeChild(ScriptThreadHandle parent, ScriptThreadHandle child) {
		auto& state = *m_Implementation;
		auto& thread = state.Thread(parent);
		if (thread.Child != child) {
			return;
		}

		thread.ResumeValue = state.Thread(child).Info.Result;
		thread.ResumeRoot = state.Vm.Retain(thread.ResumeValue);
		Implementation::ClearWait(thread);
	}

	void ScriptScheduler::Notify(std::string_view notification) {
		auto& state = *m_Implementation;
		for (const auto handle : state.Registry) {
			auto& thread = state.Thread(handle);
			bool matched = false;
			for (const auto& target : thread.Notifications) {
				// The target root owns the immutable value throughout both borrowed text comparisons.
				const auto& text = StringText(target.Value());
				const auto target_text = thread.NativeNotificationComparison ? std::string_view(text).substr(0, text.find('\0')) : std::string_view(text);
				const auto supplied_text = thread.NativeNotificationComparison ? notification.substr(0, notification.find('\0')) : notification;
				if (target_text == supplied_text) {
					// The native resume ObjectInfo receives the matched value before the wait array is cleared.
					thread.ResumeValue = target.Value();
					thread.ResumeRoot = state.Vm.Retain(thread.ResumeValue);
					matched = true;
				}
			}

			if (matched) {
				Implementation::ClearWait(thread);
			}
		}
	}

} // namespace sgre::script
