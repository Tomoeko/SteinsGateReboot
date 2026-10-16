#include "sgre/script/draw_object_bindings.hpp"

#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	class ScriptNativeDrawObjectState {
	public:
		SquirrelObjectIdentity Instance;
		ScriptNativeDrawObjectCallbacks Callbacks;
		bool Drawing = false;
		bool Releasing = false;
		bool Closed = false;

		~ScriptNativeDrawObjectState() {
			try {
				Close();
			} catch (...) {
				// Explicit release reports errors; no native callbacks escape owner destruction.
			}
		}

		void Close() {
			if (Closed) {
				return;
			}

			if (Drawing) {
				throw std::runtime_error("native DrawObject owner cannot retire during its active virtual draw");
			}

			Releasing = true;
			std::exception_ptr failure;
			try {
				if (Callbacks.Release) {
					Callbacks.Release();
				}
			} catch (...) {
				failure = std::current_exception();
			}

			Callbacks = {};
			Closed = true;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	ScriptNativeDrawObjectBorrow::ScriptNativeDrawObjectBorrow(std::weak_ptr<ScriptNativeDrawObjectState> owner) : m_Owner(std::move(owner)) {
	}

	bool ScriptNativeDrawObjectBorrow::IsLive() const noexcept {
		const auto owner = m_Owner.lock();

		return owner && !owner->Closed && !owner->Releasing;
	}

	void ScriptNativeDrawObjectBorrow::Draw(std::uint32_t argument, std::uint32_t opacity) const {
		const auto owner = m_Owner.lock();
		if (!owner || owner->Closed || owner->Releasing) {
			throw std::runtime_error("native DrawObject payload expired after its temporary script target was popped");
		}
		if (owner->Drawing) {
			throw std::runtime_error("recursive native DrawObject virtual draw is outside the recovered owner contract");
		}

		owner->Drawing = true;
		try {
			owner->Callbacks.Draw(argument, opacity);
		} catch (...) {
			owner->Drawing = false;

			throw;
		}

		owner->Drawing = false;
	}

	class ScriptDrawObjectBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, std::size_t max_instances) : Vm(vm), Objects(objects), MaxInstances(max_instances) {
			if (max_instances == 0 || max_instances > 65536) {
				throw std::runtime_error("DrawObject requires a positive bounded native owner capacity");
			}
		}

		~Implementation() {
			for (auto& [index, owner] : Instances) {
				static_cast<void>(index);
				try {
					owner->Close();
				} catch (...) {
					// Explicit Object release reports diagnostics; adapter destruction still retires owned callbacks.
				}
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		std::size_t MaxInstances;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, std::shared_ptr<ScriptNativeDrawObjectState>> Instances;

		std::shared_ptr<ScriptNativeDrawObjectState> Record(const SquirrelValue& instance) const {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("DrawObject requires a genuine native instance receiver");
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second->Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("DrawObject receiver has no live registered native owner identity");
			}

			return found->second;
		}

		void Release(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			const auto owner = Record(instance);
			if (owner->Drawing || owner->Releasing) {
				throw std::runtime_error("native DrawObject cannot retire during active draw or owner cleanup");
			}

			std::exception_ptr failure;
			try {
				owner->Close();
			} catch (...) {
				failure = std::current_exception();
			}

			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			try {
				Objects.ReleaseInstance(instance);
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	ScriptDrawObjectBindings::ScriptDrawObjectBindings(SquirrelVm& vm, ScriptObjectBindings& objects, std::size_t max_instances) : m_Implementation(std::make_shared<Implementation>(vm, objects, max_instances)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		vm.NewSlot(state.Class, std::string("constructor"), vm.CreateNativeClosure("DrawObject.constructor", [](SquirrelVm&, std::span<const SquirrelValue>) -> SquirrelNativeResult {
			throw std::runtime_error("can't create instance");
		}));
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("DrawObject.weakref", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			const auto owner = weak.lock();
			if (!owner || arguments.empty()) {
				throw std::runtime_error("DrawObject weakref requires live services and its last stack value");
			}

			return SquirrelNativeResult::ReturnValue(owner->Vm.CreateWeakReference(arguments.back()));
		}));
		vm.NewSlot(vm.RootTable(), std::string("DrawObject"), state.Class);
	}

	ScriptDrawObjectBindings::~ScriptDrawObjectBindings() = default;

	SquirrelValue ScriptDrawObjectBindings::DrawObjectClass() const {
		return m_Implementation->Class;
	}

	void ScriptDrawObjectBindings::AttachNativeDrawOwner(const SquirrelValue& instance, ScriptNativeDrawObjectCallbacks callbacks) {
		auto& state = *m_Implementation;
		if (!state.Vm.IsInstanceOf(instance, state.Class) || !callbacks.Draw) {
			throw std::runtime_error("DrawObject native attachment requires genuine ancestry and a concrete first-party draw owner");
		}

		const auto index = std::get<SquirrelObjectHandle>(instance).Index;
		if (state.Instances.contains(index) || state.Instances.size() >= state.MaxInstances || state.Objects.HasInstance(instance)) {
			throw std::runtime_error("DrawObject native attachment exceeds owner capacity or duplicates an initialized native instance");
		}

		auto owner = std::make_shared<ScriptNativeDrawObjectState>();
		owner->Instance = state.Vm.BorrowObjectIdentity(instance);
		owner->Callbacks = std::move(callbacks);
		state.Objects.AttachInstance(instance);
		try {
			state.Instances.emplace(index, owner);
			const auto weak = std::weak_ptr<Implementation>(m_Implementation);
			state.Objects.ConfigureNativeRelease(instance, [weak](const SquirrelValue& receiver) {
				if (const auto binding = weak.lock()) {
					binding->Release(receiver);
				}
			});
		} catch (...) {
			const auto failure = std::current_exception();
			state.Instances.erase(index);
			try {
				owner->Close();
			} catch (...) {
			}
			try {
				state.Objects.ReleaseInstance(instance);
			} catch (...) {
			}

			std::rethrow_exception(failure);
		}
	}

	std::optional<ScriptNativeDrawObjectBorrow> ScriptDrawObjectBindings::BorrowNativeDrawOwner(const SquirrelValue& instance) const {
		const auto& state = *m_Implementation;
		if (!state.Vm.IsInstanceOf(instance, state.Class)) {
			throw std::runtime_error("DrawObject native owner lookup requires genuine class type tag");
		}

		const auto found = state.Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
		if (found == state.Instances.end() || found->second->Instance != state.Vm.BorrowObjectIdentity(instance) || found->second->Closed) {
			return {};
		}

		return ScriptNativeDrawObjectBorrow(found->second);
	}

	void ScriptDrawObjectBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
