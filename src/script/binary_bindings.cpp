#include "sgre/script/binary_bindings.hpp"

#include <bit>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {
	struct BinaryBindingInstance {
		SquirrelObjectIdentity Instance;
		std::shared_ptr<const std::vector<std::uint8_t>> Backing;
	};

	class ScriptBinaryBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptBinaryBindingLimits limits) : Vm(vm), Objects(objects), Limits(limits) {
			if (limits.MaxInstances == 0 || limits.MaxInstances > 65536 || limits.MaxBufferBytes == 0 || limits.MaxBufferBytes > std::numeric_limits<std::uint32_t>::max()) {
				throw std::runtime_error("Binary bindings require bounded positive instance/buffer limits");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptBinaryBindingLimits Limits;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, BinaryBindingInstance> Instances;

		const BinaryBindingInstance& Record(const SquirrelValue& instance) const {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("Binary callback requires a native Binary instance");
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("Binary instance has no retained host buffer");
			}

			return found->second;
		}

		void Release(const SquirrelValue& instance) {
			static_cast<void>(Record(instance));
			Objects.RequireCanRelease(instance);
			// Release the native buffer before the Object base notifies completion observers.
			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			Objects.ReleaseInstance(instance);
		}
	};

	ScriptBinaryBindings::ScriptBinaryBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptBinaryBindingLimits limits) : m_Implementation(std::make_shared<Implementation>(vm, objects, limits)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto method = [&](std::string name, auto operation) {
			const auto callback = vm.CreateNativeClosure("Binary." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("Binary callbacks have expired or lack their receiver");
				}

				return operation(*owner, arguments);
			});
			vm.NewSlot(state.Class, name, callback);
		};

		method("constructor", [](const Implementation&, const auto) -> SquirrelNativeResult {
			throw std::runtime_error("can't create instance");
		});
		method("at", [](const Implementation& owner, const auto arguments) {
			const auto& record = owner.Record(arguments[0]);
			const auto index = arguments.size() >= 2 ? TryNativeInteger(arguments[1]).value_or(0) : 0;
			SquirrelNativeResult result;
			result.Value = std::int32_t(0);
			if (index >= 0 && static_cast<std::size_t>(index) < record.Backing->size()) {
				result.Value = static_cast<std::int32_t>((*record.Backing)[static_cast<std::size_t>(index)]);
			}

			return result;
		});
		method("size", [](const Implementation& owner, const auto arguments) {
			const auto size = static_cast<std::uint32_t>(owner.Record(arguments[0]).Backing->size());
			SquirrelNativeResult result;
			result.Value = std::bit_cast<std::int32_t>(size);

			return result;
		});
		vm.NewSlot(vm.RootTable(), std::string("Binary"), state.Class);
	}

	ScriptBinaryBindings::~ScriptBinaryBindings() = default;

	SquirrelValue ScriptBinaryBindings::BinaryClass() const {
		return m_Implementation->Class;
	}

	std::span<const std::uint8_t> ScriptBinaryBindings::Bytes(const SquirrelValue& instance) const {
		return *m_Implementation->Record(instance).Backing;
	}

	SquirrelValue ScriptBinaryBindings::WrapBuffer(std::shared_ptr<const std::vector<std::uint8_t>> backing) {
		auto& state = *m_Implementation;
		if (!backing || backing->size() > state.Limits.MaxBufferBytes || state.Instances.size() >= state.Limits.MaxInstances) {
			throw std::runtime_error("Binary backing is absent or exceeds its instance/buffer limit");
		}

		const auto instance = state.Vm.CreateInstance(state.Class);
		state.Objects.AttachInstance(instance);
		BinaryBindingInstance record;
		record.Instance = state.Vm.BorrowObjectIdentity(instance);
		record.Backing = std::move(backing);
		state.Instances.emplace(std::get<SquirrelObjectHandle>(instance).Index, std::move(record));
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		try {
			state.Objects.ConfigureNativeRelease(instance, [weak](const SquirrelValue& receiver) {
				if (const auto owner = weak.lock()) {
					owner->Release(receiver);
				}
			});
		} catch (...) {
			state.Release(instance);

			throw;
		}

		return instance;
	}
} // namespace sgre::script
