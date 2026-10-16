#include "sgre/script/font_bindings.hpp"

#include "sgre/script/psb_bindings.hpp"

#include <cstdint>
#include <exception>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	struct PsbFontBindingRecord {
		SquirrelObjectIdentity Instance;
		SquirrelHostRoot PsbOwner;
		bool Releasing = false;
	};

	static constexpr std::size_t kMaximumSystemFontNameBytes = 65536;
	static constexpr std::size_t kMaximumSystemFontNamesBytes = 16 * 1024 * 1024;

	struct SystemFontBindingRecord {
		SquirrelObjectIdentity Instance;
		ScriptSystemFontState State;
		bool Releasing = false;
	};

	static float SystemFontScale(const SquirrelValue& value) {
		if (const auto* number = std::get_if<float>(&value)) {
			return *number;
		}
		if (const auto* number = std::get_if<std::int32_t>(&value)) {
			return static_cast<float>(*number);
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean ? 1.0f : 0.0f;
		}

		return 0;
	}

	class ScriptFontBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, std::size_t max_instances) : Vm(vm), Objects(objects), Psb(psb), MaxInstances(max_instances) {
			if (MaxInstances == 0) {
				throw std::runtime_error("native PSBFont instance limit must be nonzero");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptPsbBindings& Psb;
		std::size_t MaxInstances = 0;
		SquirrelValue FontClass;
		SquirrelValue PsbFontClass;
		SquirrelValue SystemFontClass;
		SquirrelHostRoot FontClassRoot;
		SquirrelHostRoot PsbFontClassRoot;
		SquirrelHostRoot SystemFontClassRoot;
		std::size_t SystemFontNamesBytes = 0;
		std::map<std::uint32_t, PsbFontBindingRecord> PsbInstances;
		std::map<std::uint32_t, SystemFontBindingRecord> SystemInstances;

		PsbFontBindingRecord& PsbRecord(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("native Font provider requires a recovered PSBFont instance");
			}

			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			const auto found = PsbInstances.find(index);
			if (found == PsbInstances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance) || found->second.Releasing) {
				throw std::runtime_error("native PSBFont instance has no live provider owner");
			}

			return found->second;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& owner) {
			if (arguments.size() < 2 || !Vm.IsInstanceOf(arguments[0], PsbFontClass) || !Vm.IsInstanceOf(arguments[1], Psb.ObjectClass())) {
				throw std::runtime_error("invalid argument: PSBFont requires an actual PSBObject");
			}

			// Construction validates a live PSB wrapper, without parsing a bitmap font or creating textures.
			static_cast<void>(Psb.DocumentAsset(arguments[1]));
			const auto instance = arguments[0];
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (PsbInstances.contains(index) || PsbInstances.size() + SystemInstances.size() >= MaxInstances) {
				throw std::runtime_error("native PSBFont is already initialized or its instance limit is exhausted");
			}

			PsbFontBindingRecord record;
			record.Instance = Vm.BorrowObjectIdentity(instance);
			record.PsbOwner = Vm.Retain(arguments[1]);
			Objects.AttachInstance(instance);
			try {
				PsbInstances.emplace(index, std::move(record));
				Objects.ConfigureNativeRelease(instance, [owner](const SquirrelValue& receiver) {
					if (const auto binding = owner.lock()) {
						binding->Release(receiver);
					}
				});
			} catch (...) {
				const auto failure = std::current_exception();
				PsbInstances.erase(index);
				try {
					Objects.ReleaseInstance(instance);
				} catch (...) {
				}

				std::rethrow_exception(failure);
			}
		}

		void ReleasePsb(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			auto& record = PsbRecord(instance);
			record.Releasing = true;
			record.PsbOwner = {};
			std::exception_ptr failure;
			try {
				Objects.ReleaseInstance(instance);
			} catch (...) {
				failure = std::current_exception();
			}

			PsbInstances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		SystemFontBindingRecord& SystemRecord(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("native SystemFont requires an instance");
			}

			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			const auto found = SystemInstances.find(index);
			if (found == SystemInstances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance) || found->second.Releasing) {
				throw std::runtime_error("native SystemFont instance has no live owner");
			}

			return found->second;
		}

		void ConstructSystem(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& owner) {
			if (arguments.empty() || arguments.size() > 3 || !Vm.IsInstanceOf(arguments[0], SystemFontClass)) {
				throw std::runtime_error("invalid argument: SystemFont requires zero, one or two constructor parameters");
			}

			SystemFontBindingRecord record;
			record.Instance = Vm.BorrowObjectIdentity(arguments[0]);
			if (arguments.size() >= 2) {
				record.State.Scale = SystemFontScale(arguments[1]);
			}
			if (0.0f > record.State.Scale) {
				throw std::runtime_error("invalid argument: SystemFont scale is negative");
			}
			if (arguments.size() == 3) {
				record.State.Name = Vm.ToString(arguments[2]);
				const auto terminator = record.State.Name.find('\0');
				if (terminator != std::string::npos) {
					record.State.Name.resize(terminator);
				}
			}

			const auto instance = arguments[0];
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (SystemInstances.contains(index) || PsbInstances.size() + SystemInstances.size() >= MaxInstances) {
				throw std::runtime_error("native SystemFont is already initialized or its instance limit is exhausted");
			}
			const auto name_size = record.State.Name.size();
			if (name_size > kMaximumSystemFontNameBytes || name_size > kMaximumSystemFontNamesBytes - SystemFontNamesBytes) {
				throw std::runtime_error("native SystemFont name exceeds bounded binding storage");
			}

			Objects.AttachInstance(instance);
			bool recorded = false;
			try {
				SystemInstances.emplace(index, std::move(record));
				SystemFontNamesBytes += name_size;
				recorded = true;
				Objects.ConfigureNativeRelease(instance, [owner](const SquirrelValue& receiver) {
					if (const auto binding = owner.lock()) {
						binding->Release(receiver);
					}
				});
			} catch (...) {
				const auto failure = std::current_exception();
				if (recorded) {
					SystemFontNamesBytes -= name_size;
					SystemInstances.erase(index);
				}
				try {
					Objects.ReleaseInstance(instance);
				} catch (...) {
				}

				std::rethrow_exception(failure);
			}
		}

		void ReleaseSystem(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			auto& record = SystemRecord(instance);
			record.Releasing = true;
			SystemFontNamesBytes -= record.State.Name.size();
			// The native string owner retires before Object completion callbacks.
			std::string{}.swap(record.State.Name);
			std::exception_ptr failure;
			try {
				Objects.ReleaseInstance(instance);
			} catch (...) {
				failure = std::current_exception();
			}

			SystemInstances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void Release(const SquirrelValue& instance) {
			if (Vm.Type(instance) == SquirrelValueType::Instance && SystemInstances.contains(std::get<SquirrelObjectHandle>(instance).Index)) {
				ReleaseSystem(instance);

				return;
			}

			ReleasePsb(instance);
		}

		void Install(const std::shared_ptr<Implementation>& owner) {
			FontClass = Vm.CreateClass(Objects.ObjectClass());
			FontClassRoot = Vm.Retain(FontClass);
			const auto weak = std::weak_ptr<Implementation>(owner);
			const auto method = [&](const SquirrelValue& class_value, std::string name, auto operation) {
				const auto closure = Vm.CreateNativeClosure("Font." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
					const auto binding = weak.lock();
					if (!binding) {
						throw std::runtime_error("native Font bindings have expired");
					}

					return operation(*binding, arguments);
				});
				Vm.NewSlot(class_value, std::move(name), closure);
			};
			method(FontClass, "constructor", [](Implementation&, const auto) -> SquirrelNativeResult {
				throw std::runtime_error("can't create instance");
			});
			method(FontClass, "weakref", [](Implementation& state, const auto arguments) {
				if (arguments.empty()) {
					throw std::runtime_error("native Font weakref requires a stack value");
				}

				return SquirrelNativeResult::ReturnValue(state.Vm.CreateWeakReference(arguments.back()));
			});
			PsbFontClass = Vm.CreateClass(FontClass);
			PsbFontClassRoot = Vm.Retain(PsbFontClass);
			method(PsbFontClass, "constructor", [weak](Implementation& state, const auto arguments) {
				state.Construct(arguments, weak);

				return SquirrelNativeResult::NoValues();
			});
			SystemFontClass = Vm.CreateClass(FontClass);
			SystemFontClassRoot = Vm.Retain(SystemFontClass);
			method(SystemFontClass, "constructor", [weak](Implementation& state, const auto arguments) {
				state.ConstructSystem(arguments, weak);

				return SquirrelNativeResult::NoValues();
			});
			method(SystemFontClass, "ready", [](Implementation& state, const auto arguments) {
				if (arguments.empty()) {
					throw std::runtime_error("native SystemFont ready lacks its receiver");
				}
				static_cast<void>(state.SystemRecord(arguments[0]));

				return SquirrelNativeResult::ReturnValue(false);
			});
			Vm.NewSlot(Vm.RootTable(), std::string("Font"), FontClass);
			Vm.NewSlot(Vm.RootTable(), std::string("PSBFont"), PsbFontClass);
			Vm.NewSlot(Vm.RootTable(), std::string("SystemFont"), SystemFontClass);
		}
	};

	ScriptFontBindings::ScriptFontBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, std::size_t max_instances) : m_Implementation(std::make_shared<Implementation>(vm, objects, psb, max_instances)) {
		m_Implementation->Install(m_Implementation);
	}

	ScriptFontBindings::~ScriptFontBindings() = default;

	SquirrelValue ScriptFontBindings::FontClass() const {
		return m_Implementation->FontClass;
	}

	SquirrelValue ScriptFontBindings::PsbFontClass() const {
		return m_Implementation->PsbFontClass;
	}

	SquirrelValue ScriptFontBindings::SystemFontClass() const {
		return m_Implementation->SystemFontClass;
	}

	ScriptSystemFontState ScriptFontBindings::SystemState(const SquirrelValue& instance) const {
		return m_Implementation->SystemRecord(instance).State;
	}

	SquirrelValue ScriptFontBindings::PsbOwner(const SquirrelValue& instance) const {
		if (m_Implementation->Vm.Type(instance) == SquirrelValueType::Instance && m_Implementation->SystemInstances.contains(std::get<SquirrelObjectHandle>(instance).Index)) {
			static_cast<void>(m_Implementation->SystemRecord(instance));
			throw std::runtime_error("SystemFont has no native font provider in the supplied executable");
		}

		const auto& record = m_Implementation->PsbRecord(instance);

		return record.PsbOwner.Value();
	}

	void ScriptFontBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
