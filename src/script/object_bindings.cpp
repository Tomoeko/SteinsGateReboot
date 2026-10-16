#include "sgre/script/object_bindings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sgre::script {
	struct ObjectSubscription {
		std::uint64_t Identity = 0;
		SquirrelObjectIdentity Listener;
		ScriptObjectBindings::CompletionCallback Callback;
	};

	struct ObjectBindingRecord {
		SquirrelObjectIdentity Instance;
		SquirrelHostRoot Self;
		SquirrelHostRoot Delegate;
		SquirrelHostRoot SubscriberArray;
		std::vector<ObjectSubscription> Subscribers;
		bool IsNotifying = false;
	};

	static std::string PropertyMethod(const SquirrelValue& key, std::string_view prefix) {
		const auto* text = TryStringText(key);
		if (!text || text->empty() || text->front() == '\0') {
			throw std::runtime_error("native Object property requires a nonempty string");
		}

		const auto first = static_cast<std::uint8_t>(text->front());
		if (first >= 128) {
			throw std::runtime_error("native Object non-ASCII property capitalization is not recovered");
		}

		const auto end = text->find('\0');
		std::string name(prefix);
		name.push_back(first >= 'a' && first <= 'z' ? static_cast<char>(first - 'a' + 'A') : static_cast<char>(first));
		name.append(text->substr(1, end == std::string::npos ? end : end - 1));

		return name;
	}

	class ScriptObjectBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindingLimits limits)
		    : Vm(vm), Limits(limits) {
			if (limits.MaxInstances == 0 || limits.MaxSubscriptions == 0) {
				throw std::runtime_error("native Object binding limits must be nonzero");
			}

			if (limits.MaxSubscriptions > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
				throw std::runtime_error("native Object subscription limit exceeds the array index range");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindingLimits Limits;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, ObjectBindingRecord> Instances;
		std::uint64_t NextSubscription = 1;
		std::size_t SubscriptionCount = 0;

		ObjectBindingRecord& Record(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("native Object callback requires an Object instance");
			}

			// Attachment establishes native base ownership; Class.Finalize may already clear script ancestry.
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			const auto found = Instances.find(index);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("native Object instance has no retained base state");
			}

			return found->second;
		}

		SquirrelValue DelegateValue(const SquirrelValue& instance) {
			const auto value = Record(instance).Delegate.Value();

			return Vm.Type(value) == SquirrelValueType::WeakReference ? Vm.ResolveWeakReference(value) : value;
		}

		void SetDelegate(const SquirrelValue& instance, const SquirrelValue& delegate) {
			auto retained = Vm.Retain(Vm.CreateWeakReference(delegate));
			Record(instance).Delegate = std::move(retained);
		}

		void Attach(const SquirrelValue& instance, const SquirrelValue& delegate, const std::weak_ptr<Implementation>& owner) {
			if (!Vm.IsInstanceOf(instance, Class)) {
				throw std::runtime_error("native Object attachment requires an Object instance");
			}

			static_cast<void>(Vm.Type(delegate));
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Instances.contains(index)) {
				throw std::runtime_error("native Object base state is already initialized");
			}

			if (Instances.size() >= Limits.MaxInstances) {
				throw std::runtime_error("native Object instance limit exceeded");
			}

			ObjectBindingRecord record;
			record.Instance = Vm.BorrowObjectIdentity(instance);
			record.Self = Vm.Retain(Vm.CreateWeakReference(instance));
			record.Delegate = Vm.Retain(Vm.CreateWeakReference(delegate));
			record.SubscriberArray = Vm.Retain(Vm.CreateArray());
			Instances.emplace(index, std::move(record));
			try {
				Vm.SetInstanceReleaseHook(instance, [owner](SquirrelVm&, const SquirrelValue& receiver) {
					if (const auto state = owner.lock()) {
						state->Finalize(receiver, {});
					}
				});
			} catch (...) {
				Instances.erase(index);

				throw;
			}
		}

		void RequireCanRelease(const SquirrelValue& instance) {
			if (Record(instance).IsNotifying) {
				throw std::runtime_error("native Object release during completion notification is not recovered");
			}
		}

		void Release(const SquirrelValue& instance) {
			RequireCanRelease(instance);

			std::exception_ptr failure;
			try {
				Notify(instance, true);
			} catch (...) {
				failure = std::current_exception();
			}

			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			const auto found = Instances.find(index);
			if (found != Instances.end()) {
				SubscriptionCount -= found->second.Subscribers.size();
				Instances.erase(found);
			}

			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void Finalize(const SquirrelValue& instance, const NativeRelease& release) {
			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				return;
			}

			std::exception_ptr failure;
			try {
				// Native228e80 uses the fixed bootstrap VM from225ff0, independently of the releasing child.
				const auto base_context = Vm.RootContext();
				auto method = Vm.ReadSlotInContext(base_context, instance, std::string("destructor"));
				if (Vm.Type(method) == SquirrelValueType::WeakReference) {
					method = Vm.ResolveWeakReference(method);
				}

				const auto type = Vm.Type(method);
				if (type == SquirrelValueType::Closure || type == SquirrelValueType::NativeClosure) {
					const std::array<SquirrelValue, 1> receiver{instance};
					static_cast<void>(Vm.InvokeWithoutResultInContext(base_context, method, receiver));
				}
			} catch (const SquirrelVmException&) {
				// Native 0x140228e80 ignores failed sq_call status; the VM owns its last-error diagnostic.
			} catch (...) {
				failure = std::current_exception();
			}

			try {
				if (release) {
					release(instance);
				} else {
					Release(instance);
				}
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			// A failed derived cleanup must still release its native Object roots.
			if (Instances.contains(std::get<SquirrelObjectHandle>(instance).Index)) {
				try {
					Release(instance);
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
			}

			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		SquirrelValue ReadProperty(std::span<const SquirrelValue> arguments) {
			if (arguments.size() != 2) {
				throw std::runtime_error("native Object.get received an incorrect argument count");
			}

			const auto instance = arguments[0];
			static_cast<void>(Record(instance));
			const auto method_name = PropertyMethod(arguments[1], "get");
			if (Vm.HasSlot(instance, method_name)) {
				const auto method = Vm.GetSlot(instance, method_name);
				try {
					const std::array<SquirrelValue, 1> receiver{instance};
					return Vm.InvokeInContext(Vm.CurrentContext(), method, receiver).Value;
				} catch (const std::runtime_error&) {
					// Native 0x140228ac0 attempts delegate lookup after a getter call fails.
				}
			}

			const auto delegate = DelegateValue(instance);
			const auto type = Vm.Type(delegate);
			if (type != SquirrelValueType::Table && type != SquirrelValueType::Instance) {
				throw std::runtime_error("native Object property getter failed");
			}

			const auto& key = StringText(arguments[1]);
			const auto value = Vm.ReadSlot(delegate, key.substr(0, key.find('\0')));
			const auto value_type = Vm.Type(value);
			if (type == SquirrelValueType::Instance && (value_type == SquirrelValueType::Closure || value_type == SquirrelValueType::NativeClosure) && !Vm.HasBoundEnvironment(value)) {
				return Vm.BindEnvironment(value, delegate);
			}

			return value;
		}

		void WriteProperty(std::span<const SquirrelValue> arguments) {
			if (arguments.size() != 3) {
				throw std::runtime_error("native Object.set received an incorrect argument count");
			}

			const auto instance = arguments[0];
			static_cast<void>(Record(instance));
			const auto method_name = PropertyMethod(arguments[1], "set");
			if (Vm.HasSlot(instance, method_name)) {
				const auto method = Vm.GetSlot(instance, method_name);
				try {
					const std::array<SquirrelValue, 2> parameters{instance, arguments[2]};
					static_cast<void>(Vm.InvokeInContext(Vm.CurrentContext(), method, parameters));

					return;
				} catch (const std::runtime_error&) {
					// Native 0x140228cd0 attempts delegate assignment after a setter call fails.
				}
			}

			const auto delegate = DelegateValue(instance);
			const auto type = Vm.Type(delegate);
			if (type != SquirrelValueType::Table && type != SquirrelValueType::Instance) {
				throw std::runtime_error("native Object property setter failed");
			}

			Vm.AssignSlot(delegate, arguments[1], arguments[2]);
		}

		void Notify(const SquirrelValue& object, bool all) {
			auto& record = Record(object);
			if (record.IsNotifying) {
				throw std::runtime_error("native Object recursive completion notification is not recovered");
			}

			record.IsNotifying = true;
			try {
				NotifySubscribers(record, object, all);
			} catch (...) {
				record.IsNotifying = false;
				throw;
			}

			record.IsNotifying = false;
		}

		void NotifySubscribers(ObjectBindingRecord& record, const SquirrelValue& object, bool all) {
			// Native notification captures the initial count and reads each current numeric index.
			const auto initial_count = record.Subscribers.size();
			for (std::size_t index = 0; index < initial_count; ++index) {
				if (index >= record.Subscribers.size()) {
					continue;
				}

				const auto subscription = record.Subscribers[index];
				const auto listener = Vm.ResolveObjectIdentity(subscription.Listener);
				if (!listener) {
					throw std::runtime_error("native Object subscriber array lost its strongly retained listener");
				}

				const bool consumed = subscription.Callback(object);
				if (!all && consumed) {
					if (index < record.Subscribers.size()) {
						const auto removed = Vm.RemoveArray(record.SubscriberArray.Value(), static_cast<std::int32_t>(index));
						record.Subscribers.erase(record.Subscribers.begin() + static_cast<std::ptrdiff_t>(index));
						--SubscriptionCount;
					}

					return;
				}
			}

			if (all) {
				Vm.ClearArray(record.SubscriberArray.Value());
				SubscriptionCount -= record.Subscribers.size();
				record.Subscribers.clear();
			}
		}

		void Install(const std::shared_ptr<Implementation>& owner) {
			Class = Vm.CreateClass();
			ClassRoot = Vm.Retain(Class);
			const auto weak = std::weak_ptr<Implementation>(owner);
			const auto method = [&](std::string name, auto operation) {
				const auto closure = Vm.CreateNativeClosure("Object." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
					const auto retained = weak.lock();
					if (!retained) {
						throw std::runtime_error("native Object bindings have expired");
					}

					return operation(*retained, arguments);
				});
				Vm.NewSlot(Class, std::move(name), closure);
			};
			method("constructor", [weak](Implementation& state, const auto arguments) {
				if (arguments.empty()) {
					throw std::runtime_error("native Object constructor lacks receiver");
				}

				state.Attach(arguments[0], arguments.size() >= 2 ? arguments[1] : SquirrelValue{}, weak);

				return SquirrelNativeResult{};
			});
			method("weakref", [](Implementation& state, const auto) {
				// Sqrat registers this ordinary member before derived classes copy Object's slots.
				return SquirrelNativeResult::ReturnValue(state.Vm.CreateWeakReference(state.Vm.NativeStackValue(-1)));
			});
			method("destructor", [](Implementation&, const auto) {
				return SquirrelNativeResult{};
			});
			const auto getter = [](Implementation& state, const auto arguments) {
				SquirrelNativeResult result;
				result.Value = state.ReadProperty(arguments);

				return result;
			};
			method("_get", getter);
			method("get", getter);
			const auto setter = [](Implementation& state, const auto arguments) {
				state.WriteProperty(arguments);

				return SquirrelNativeResult{};
			};
			method("_set", setter);
			method("set", setter);
			method("getDelegate", [](Implementation& state, const auto arguments) {
				if (arguments.size() != 1) {
					throw std::runtime_error("native Object.getDelegate received an incorrect argument count");
				}

				SquirrelNativeResult result;
				result.Value = state.DelegateValue(arguments[0]);

				return result;
			});
			method("setDelegate", [](Implementation& state, const auto arguments) {
				if (arguments.empty()) {
					throw std::runtime_error("native Object.setDelegate lacks receiver");
				}

				state.SetDelegate(arguments[0], arguments.size() >= 2 ? arguments[1] : SquirrelValue{});

				return SquirrelNativeResult{};
			});
			method("hasSetProp", [](Implementation& state, const auto arguments) {
				if (arguments.empty()) {
					throw std::runtime_error("native Object.hasSetProp lacks receiver");
				}

				static_cast<void>(state.Record(arguments[0]));
				SquirrelNativeResult result;
				result.Value = false;
				if (arguments.size() >= 2) {
					const auto* text = TryStringText(arguments[1]);
					if (text && !text->empty() && text->front() != '\0') {
						const auto setter_name = PropertyMethod(arguments[1], "set");
						if (!state.Vm.HasSlot(arguments[0], setter_name) && !state.Vm.HasSlot(arguments[0], text->substr(0, text->find('\0')))) {
							throw std::runtime_error("native Object.hasSetProp index does not exist");
						}

						result.Value = true;
					}
				}

				return result;
			});
			method("notify", [](Implementation& state, const auto arguments) {
				if (arguments.empty()) {
					throw std::runtime_error("native Object.notify lacks receiver");
				}

				state.Notify(arguments[0], false);

				return SquirrelNativeResult{};
			});
			method("notifyAll", [](Implementation& state, const auto arguments) {
				if (arguments.empty()) {
					throw std::runtime_error("native Object.notifyAll lacks receiver");
				}

				state.Notify(arguments[0], true);

				return SquirrelNativeResult{};
			});
			Vm.NewSlot(Vm.RootTable(), std::string("Object"), Class);
		}
	};

	ScriptObjectBindings::ScriptObjectBindings(SquirrelVm& vm, ScriptObjectBindingLimits limits)
	    : m_Implementation(std::make_shared<Implementation>(vm, limits)) {
		m_Implementation->Install(m_Implementation);
	}

	ScriptObjectBindings::~ScriptObjectBindings() = default;

	SquirrelValue ScriptObjectBindings::ObjectClass() const {
		return m_Implementation->Class;
	}

	void ScriptObjectBindings::AttachInstance(const SquirrelValue& instance, const SquirrelValue& delegate) {
		m_Implementation->Attach(instance, delegate, m_Implementation);
	}

	void ScriptObjectBindings::ConfigureNativeRelease(const SquirrelValue& instance, NativeRelease release) {
		if (!release) {
			throw std::runtime_error("native Object release configuration requires an owner cleanup");
		}

		static_cast<void>(m_Implementation->Record(instance));
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		m_Implementation->Vm.SetInstanceReleaseHook(instance, [weak, release = std::move(release)](SquirrelVm&, const SquirrelValue& receiver) {
			if (const auto owner = weak.lock()) {
				owner->Finalize(receiver, release);
			}
		});
	}

	bool ScriptObjectBindings::HasInstance(const SquirrelValue& instance) const {
		if (m_Implementation->Vm.Type(instance) != SquirrelValueType::Instance) {
			return false;
		}

		const auto found = m_Implementation->Instances.find(std::get<SquirrelObjectHandle>(instance).Index);

		return found != m_Implementation->Instances.end() && found->second.Instance == m_Implementation->Vm.BorrowObjectIdentity(instance);
	}

	SquirrelValue ScriptObjectBindings::Delegate(const SquirrelValue& instance) const {
		return m_Implementation->DelegateValue(instance);
	}

	void ScriptObjectBindings::SetDelegate(const SquirrelValue& instance, const SquirrelValue& delegate) {
		m_Implementation->SetDelegate(instance, delegate);
	}

	void ScriptObjectBindings::Subscribe(const SquirrelValue& object, const SquirrelValue& listener, CompletionCallback callback) {
		if (m_Implementation->Vm.Type(listener) != SquirrelValueType::Instance || !callback) {
			throw std::runtime_error("native Object subscription requires a listener instance and callback");
		}

		auto& record = m_Implementation->Record(object);
		if (m_Implementation->SubscriptionCount >= m_Implementation->Limits.MaxSubscriptions || m_Implementation->NextSubscription == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("native Object subscription limit exceeded");
		}

		ObjectSubscription subscription;
		subscription.Identity = m_Implementation->NextSubscription++;
		subscription.Listener = m_Implementation->Vm.BorrowObjectIdentity(listener);
		subscription.Callback = std::move(callback);
		record.Subscribers.push_back(std::move(subscription));
		try {
			m_Implementation->Vm.AppendArray(record.SubscriberArray.Value(), listener);
		} catch (...) {
			record.Subscribers.pop_back();

			throw;
		}
		++m_Implementation->SubscriptionCount;
	}

	void ScriptObjectBindings::Unsubscribe(const SquirrelValue& object, const SquirrelValue& listener) {
		auto& record = m_Implementation->Record(object);
		auto& subscribers = record.Subscribers;
		const auto previous = subscribers.size();
		const auto identity = m_Implementation->Vm.BorrowObjectIdentity(listener);
		for (std::size_t index = subscribers.size(); index > 0; --index) {
			if (subscribers[index - 1].Listener == identity) {
				const auto removed = m_Implementation->Vm.RemoveArray(record.SubscriberArray.Value(), static_cast<std::int32_t>(index - 1));
				subscribers.erase(subscribers.begin() + static_cast<std::ptrdiff_t>(index - 1));
			}
		}
		m_Implementation->SubscriptionCount -= previous - subscribers.size();
	}

	void ScriptObjectBindings::Notify(const SquirrelValue& object, bool all) {
		m_Implementation->Notify(object, all);
	}

	void ScriptObjectBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

	void ScriptObjectBindings::RequireCanRelease(const SquirrelValue& instance) const {
		m_Implementation->RequireCanRelease(instance);
	}
} // namespace sgre::script
