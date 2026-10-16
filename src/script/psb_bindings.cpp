#include "sgre/script/psb_bindings.hpp"

#include "sgre/scene/bitmap_font_receiving_document.hpp"
#include "sgre/scene/image_receiving_document.hpp"
#include "sgre/scene/motion_receiving_document.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::script {
	struct PsbDocumentBinding {
		SquirrelObjectIdentity Instance;
		std::shared_ptr<const resource::PsbDocument> Document;
		std::shared_ptr<const std::vector<std::uint8_t>> Backing;
		std::shared_ptr<const scene::BitmapFontReceivingDocument> Font;
		std::shared_ptr<const scene::MotionReceivingDocument> Motion;
		std::shared_ptr<const scene::ImageReceivingDocument> Image;
	};

	struct PsbValueBinding {
		SquirrelObjectIdentity Instance;
		SquirrelHostRoot Owner;
		std::shared_ptr<const resource::PsbDocument> Document;
		const resource::PsbValue* Value = nullptr;
		std::vector<std::string_view> IteratorKeys;
	};

	static SquirrelNativeResult PsbNativeValue(SquirrelValue value) {
		SquirrelNativeResult result;
		result.Value = std::move(value);

		return result;
	}

	static void RequirePsbArguments(std::span<const SquirrelValue> arguments, std::size_t count) {
		if (arguments.size() != count) {
			throw std::runtime_error("native PSB callback received an incorrect argument count");
		}
	}

	static std::string_view PsbStringKey(const SquirrelValue& value) {
		const auto* text = TryStringText(value);
		if (!text) {
			throw std::runtime_error("native PSB table access requires a string key");
		}

		return std::string_view(*text).substr(0, text->find('\0'));
	}

	static void ValidatePsbAtom(const resource::PsbValue& value) {
		const auto tag = value.SerializedTag;
		if (tag >= 13 && tag <= 20) {
			throw std::runtime_error("native PSB conversion rejects a packed-array header or key atom as a value");
		}
	}

	class ScriptPsbBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindingLimits limits)
		    : Vm(vm), Objects(objects), Limits(limits) {
			if (limits.MaxDocuments == 0 || limits.MaxValues == 0 || limits.MaxIteratorKeys == 0) {
				throw std::runtime_error("native PSB binding limits must be nonzero");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptPsbBindingLimits Limits;
		SquirrelValue ObjectClass;
		SquirrelValue ValueClass;
		SquirrelHostRoot ObjectClassRoot;
		SquirrelHostRoot ValueClassRoot;
		std::weak_ptr<Implementation> Owner;
		std::map<std::uint32_t, PsbDocumentBinding> Documents;
		std::map<std::uint32_t, PsbValueBinding> Values;

		PsbValueBinding& Value(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("native PSBValue callback requires a PSBValue instance");
			}

			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			const auto found = Values.find(index);
			if (found == Values.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("native PSBValue instance has no retained value");
			}

			return found->second;
		}

		PsbDocumentBinding& Document(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("native PSBObject callback requires a PSBObject instance");
			}

			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			const auto found = Documents.find(index);
			if (found == Documents.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("native PSBObject instance has no retained document");
			}

			return found->second;
		}

		void ReleaseDocument(const SquirrelValue& instance) {
			static_cast<void>(Document(instance));
			Objects.RequireCanRelease(instance);
			Documents.erase(std::get<SquirrelObjectHandle>(instance).Index);
			Objects.ReleaseInstance(instance);
		}

		SquirrelValue AttachDocument(PsbDocumentBinding binding) {
			const auto instance = Vm.CreateInstance(ObjectClass);
			Objects.AttachInstance(instance);
			binding.Instance = Vm.BorrowObjectIdentity(instance);
			Documents.emplace(std::get<SquirrelObjectHandle>(instance).Index, std::move(binding));
			const auto weak = Owner;
			try {
				Objects.ConfigureNativeRelease(instance, [weak](const SquirrelValue& receiver) {
					if (const auto owner = weak.lock()) {
						owner->ReleaseDocument(receiver);
					}
				});
			} catch (...) {
				ReleaseDocument(instance);

				throw;
			}

			return instance;
		}

		SquirrelValue WrapValue(const SquirrelValue& owner, const std::shared_ptr<const resource::PsbDocument>& document, const resource::PsbValue& value) {
			if (Values.size() >= Limits.MaxValues) {
				throw std::runtime_error("native PSB value binding limit exceeded");
			}

			const auto instance = Vm.CreateInstance(ValueClass);
			PsbValueBinding binding;
			binding.Instance = Vm.BorrowObjectIdentity(instance);
			binding.Owner = Vm.Retain(owner);
			binding.Document = document;
			binding.Value = &value;
			Values.emplace(std::get<SquirrelObjectHandle>(instance).Index, std::move(binding));
			const auto weak = Owner;
			try {
				Vm.SetInstanceReleaseHook(instance, [weak](SquirrelVm&, const SquirrelValue& receiver) {
					if (const auto state = weak.lock()) {
						static_cast<void>(state->Value(receiver));
						state->Values.erase(std::get<SquirrelObjectHandle>(receiver).Index);
					}
				});
			} catch (...) {
				Values.erase(std::get<SquirrelObjectHandle>(instance).Index);

				throw;
			}

			return instance;
		}

		SquirrelValue Convert(const SquirrelValue& owner, const std::shared_ptr<const resource::PsbDocument>& document, const resource::PsbValue& value, bool is_root = false) {
			ValidatePsbAtom(value);
			if (const auto* boolean = std::get_if<bool>(&value.Data)) {
				return *boolean;
			}

			if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
				return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(*integer)));
			}

			if (const auto* number = std::get_if<double>(&value.Data)) {
				return static_cast<float>(*number);
			}

			if (const auto* text = std::get_if<std::string>(&value.Data)) {
				return text->substr(0, text->find('\0'));
			}

			if (std::holds_alternative<resource::PsbArray>(value.Data) || std::holds_alternative<resource::PsbObject>(value.Data)) {
				return WrapValue(owner, document, value);
			}

			if (std::holds_alternative<resource::PsbChunkReference>(value.Data) && !is_root) {
				return WrapValue(owner, document, value);
			}

			return {};
		}

		SquirrelValue ReadValue(std::span<const SquirrelValue> arguments) {
			RequirePsbArguments(arguments, 2);
			const auto& binding = Value(arguments[0]);
			const resource::PsbValue* selected = nullptr;
			if (const auto* array = std::get_if<resource::PsbArray>(&binding.Value->Data)) {
				const auto index = TryNativeInteger(arguments[1]);
				if (!index || *index < 0 || static_cast<std::size_t>(*index) >= array->size()) {
					throw std::runtime_error("native PSB array index does not exist");
				}

				selected = &(*array)[static_cast<std::size_t>(*index)];
			} else if (const auto* object = std::get_if<resource::PsbObject>(&binding.Value->Data)) {
				const auto key = PsbStringKey(arguments[1]);
				const auto found = std::find_if(object->begin(), object->end(), [key](const auto& member) {
					return member.first == key;
				});
				if (found != object->end()) {
					selected = &found->second;
				}
			} else {
				return {};
			}

			if (!selected) {
				throw std::runtime_error("native PSB table index does not exist");
			}

			return Convert(binding.Owner.Value(), binding.Document, *selected);
		}

		SquirrelValue NextIndex(std::span<const SquirrelValue> arguments) {
			RequirePsbArguments(arguments, 2);
			auto& binding = Value(arguments[0]);
			if (const auto* array = std::get_if<resource::PsbArray>(&binding.Value->Data)) {
				std::int32_t next = 0;
				if (!std::holds_alternative<std::monostate>(arguments[1])) {
					const auto previous = TryNativeInteger(arguments[1]);
					if (!previous) {
						throw std::runtime_error("native PSB array iterator requires a numeric cursor");
					}

					next = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*previous) + 1);
				}

				if (next < 0) {
					throw std::runtime_error("native PSB array iterator produced an unsafe negative index");
				}

				return static_cast<std::size_t>(next) < array->size() ? SquirrelValue(next) : SquirrelValue{};
			}

			if (const auto* object = std::get_if<resource::PsbObject>(&binding.Value->Data)) {
				std::size_t next = 0;
				if (std::holds_alternative<std::monostate>(arguments[1])) {
					if (object->size() > Limits.MaxIteratorKeys) {
						throw std::runtime_error("native PSB table iterator key limit exceeded");
					}

					binding.IteratorKeys.clear();
					binding.IteratorKeys.reserve(object->size());
					for (const auto& member : *object) {
						binding.IteratorKeys.push_back(member.first);
					}
				} else {
					const auto previous = PsbStringKey(arguments[1]);
					const auto found = std::find(binding.IteratorKeys.begin(), binding.IteratorKeys.end(), previous);
					next = found == binding.IteratorKeys.end() ? binding.IteratorKeys.size() : static_cast<std::size_t>(found - binding.IteratorKeys.begin()) + 1;
				}

				return next < binding.IteratorKeys.size() ? SquirrelValue(std::string(binding.IteratorKeys[next])) : SquirrelValue{};
			}

			throw std::runtime_error("psbvalue is not iteratable.");
		}

		bool Contains(std::span<const SquirrelValue> arguments) {
			RequirePsbArguments(arguments, 2);
			const auto& binding = Value(arguments[0]);
			if (const auto* array = std::get_if<resource::PsbArray>(&binding.Value->Data)) {
				if (std::holds_alternative<float>(arguments[1])) {
					return false;
				}

				const auto index = TryNativeInteger(arguments[1]);

				return index && *index >= 0 && static_cast<std::size_t>(*index) < array->size();
			}

			if (const auto* object = std::get_if<resource::PsbObject>(&binding.Value->Data)) {
				if (!std::holds_alternative<SquirrelString>(arguments[1])) {
					return false;
				}

				const auto key = PsbStringKey(arguments[1]);

				return std::any_of(object->begin(), object->end(), [key](const auto& member) {
					return member.first == key;
				});
			}

			return false;
		}

		void Install(const std::shared_ptr<Implementation>& owner) {
			Owner = owner;
			ObjectClass = Vm.CreateClass(Objects.ObjectClass());
			ValueClass = Vm.CreateClass();
			ObjectClassRoot = Vm.Retain(ObjectClass);
			ValueClassRoot = Vm.Retain(ValueClass);
			const auto weak = std::weak_ptr<Implementation>(owner);
			const auto method = [&](const SquirrelValue& class_value, std::string name, auto operation, bool is_static = false) {
				const auto closure = Vm.CreateNativeClosure("PSB." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
					const auto retained = weak.lock();
					if (!retained) {
						throw std::runtime_error("native PSB bindings have expired");
					}

					return operation(*retained, arguments);
				});
				Vm.NewSlot(class_value, std::move(name), closure, is_static);
			};
			method(ObjectClass, "constructor", [](Implementation&, const auto) -> SquirrelNativeResult {
				throw std::runtime_error("can't create instance");
			});
			method(ObjectClass, "getRoot", [](Implementation& state, const auto arguments) {
				RequirePsbArguments(arguments, 1);
				const auto& document = state.Document(arguments[0]);

				const auto owner = state.Vm.ResolveObjectIdentity(document.Instance);
				if (!owner) {
					throw std::runtime_error("native PSB document identity has expired");
				}

				return PsbNativeValue(state.Convert(*owner, document.Document, document.Document->Root, true));
			});
			method(ObjectClass, "totalStreamEntitySize", [](Implementation& state, const auto arguments) {
				RequirePsbArguments(arguments, 1);
				const auto& document = state.Document(arguments[0]);
				if (document.Font || document.Motion || document.Image) {
					const auto size = static_cast<std::uint32_t>(document.Document->SerializedSize - document.Document->Header.DataOffset);

					return PsbNativeValue(std::bit_cast<std::int32_t>(size));
				}

				if (!document.Backing || document.Backing->size() > std::numeric_limits<std::uint32_t>::max()) {
					throw std::runtime_error("native PSB stream size requires complete retained 32-bit backing");
				}

				const auto size = static_cast<std::uint32_t>(document.Backing->size()) - document.Document->Header.DataOffset;

				return PsbNativeValue(std::bit_cast<std::int32_t>(size));
			});
			method(ValueClass, "constructor", [](Implementation&, const auto) {
				return SquirrelNativeResult{};
			});
			method(ValueClass, "weakref", [](Implementation& state, const auto) {
				return SquirrelNativeResult::ReturnValue(state.Vm.CreateWeakReference(state.Vm.NativeStackValue(-1)));
			});
			const auto getter = [](Implementation& state, const auto arguments) {
				return PsbNativeValue(state.ReadValue(arguments));
			};
			method(ValueClass, "_get", getter);
			method(
			    ValueClass, "getValue", [](Implementation& state, const auto arguments) {
				    if (arguments.empty()) {
					    return SquirrelNativeResult{};
				    }

				    if (!state.Vm.IsInstanceOf(arguments[0], state.ValueClass)) {
					    return SquirrelNativeResult{};
				    }

				    const auto found = state.Values.find(std::get<SquirrelObjectHandle>(arguments[0]).Index);
				    if (found == state.Values.end() || found->second.Instance != state.Vm.BorrowObjectIdentity(arguments[0])) {
					    return SquirrelNativeResult{};
				    }

				    return PsbNativeValue(state.ReadValue(arguments));
			    },
			    true);
			method(ValueClass, "len", [](Implementation& state, const auto arguments) {
				RequirePsbArguments(arguments, 1);
				const auto* array = std::get_if<resource::PsbArray>(&state.Value(arguments[0]).Value->Data);
				if (!array || array->size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
					throw std::runtime_error("native PSBValue.len requires an array with a signed 32-bit count");
				}

				return PsbNativeValue(static_cast<std::int32_t>(array->size()));
			});
			method(ValueClass, "_nexti", [](Implementation& state, const auto arguments) {
				return PsbNativeValue(state.NextIndex(arguments));
			});
			method(ValueClass, "_exist", [](Implementation& state, const auto arguments) {
				if (!state.Contains(arguments)) {
					throw SquirrelVmException({}, "native PSB index does not exist");
				}

				return SquirrelNativeResult{};
			});
			method(ValueClass, "_typeof", [](Implementation& state, const auto arguments) {
				RequirePsbArguments(arguments, 1);
				const auto& value = state.Value(arguments[0]).Value->Data;

				return PsbNativeValue(std::string(std::holds_alternative<resource::PsbArray>(value) ? "array" : std::holds_alternative<resource::PsbObject>(value) ? "table"
				                                                                                                                                                   : "null"));
			});
			Vm.NewSlot(Vm.RootTable(), std::string("PSBObject"), ObjectClass);
			Vm.NewSlot(Vm.RootTable(), std::string("PSBValue"), ValueClass);
		}
	};

	ScriptPsbBindings::ScriptPsbBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindingLimits limits)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, limits)) {
		m_Implementation->Install(m_Implementation);
	}

	ScriptPsbBindings::~ScriptPsbBindings() = default;

	SquirrelValue ScriptPsbBindings::ObjectClass() const {
		return m_Implementation->ObjectClass;
	}

	SquirrelValue ScriptPsbBindings::ValueClass() const {
		return m_Implementation->ValueClass;
	}

	SquirrelValue ScriptPsbBindings::WrapDocument(std::shared_ptr<const resource::PsbDocument> document, std::shared_ptr<const std::vector<std::uint8_t>> backing) {
		auto& state = *m_Implementation;
		if (!document || state.Documents.size() >= state.Limits.MaxDocuments) {
			throw std::runtime_error("native PSB document is absent or exceeds binding limit");
		}

		if (backing && document->SerializedSize > backing->size()) {
			throw std::runtime_error("native PSB backing is shorter than its retained document");
		}

		PsbDocumentBinding binding;
		binding.Document = std::move(document);
		binding.Backing = std::move(backing);

		return state.AttachDocument(std::move(binding));
	}

	SquirrelValue ScriptPsbBindings::WrapFontDocument(std::shared_ptr<const scene::BitmapFontReceivingDocument> document) {
		auto& state = *m_Implementation;
		if (!document || state.Documents.size() >= state.Limits.MaxDocuments) {
			throw std::runtime_error("native PSB font receiving document is absent or exceeds binding limit");
		}

		PsbDocumentBinding binding;
		binding.Document = std::shared_ptr<const resource::PsbDocument>(document, &document->Document());
		binding.Font = std::move(document);

		return state.AttachDocument(std::move(binding));
	}

	SquirrelValue ScriptPsbBindings::WrapMotionDocument(std::shared_ptr<const scene::MotionReceivingDocument> document) {
		auto& state = *m_Implementation;
		if (!document || state.Documents.size() >= state.Limits.MaxDocuments) {
			throw std::runtime_error("native Motion PSB receiving document is absent or exceeds binding limit");
		}
		PsbDocumentBinding binding;
		binding.Document = std::shared_ptr<const resource::PsbDocument>(document, &document->Document());
		binding.Motion = std::move(document);
		return m_Implementation->AttachDocument(std::move(binding));
	}

	SquirrelValue ScriptPsbBindings::WrapImageDocument(std::shared_ptr<const scene::ImageReceivingDocument> document) {
		auto& state = *m_Implementation;
		if (!document || state.Documents.size() >= state.Limits.MaxDocuments) {
			throw std::runtime_error("native Image PSB receiving document is absent or exceeds binding limit");
		}

		PsbDocumentBinding binding;
		binding.Document = std::shared_ptr<const resource::PsbDocument>(document, &document->Document());
		binding.Image = std::move(document);

		return state.AttachDocument(std::move(binding));
	}

	ScriptPsbDocumentAsset ScriptPsbBindings::DocumentAsset(const SquirrelValue& instance) const {
		const auto& record = m_Implementation->Document(instance);
		ScriptPsbDocumentAsset asset;
		asset.Document = record.Document;
		asset.Backing = record.Backing;
		asset.Font = record.Font;
		asset.Motion = record.Motion;
		asset.Image = record.Image;

		return asset;
	}

	ScriptPsbValueAsset ScriptPsbBindings::ValueAsset(const SquirrelValue& instance) const {
		const auto& record = m_Implementation->Value(instance);
		const auto& owner = m_Implementation->Document(record.Owner.Value());
		ScriptPsbValueAsset asset;
		asset.Owner.Document = record.Document;
		asset.Owner.Backing = owner.Backing;
		asset.Owner.Font = owner.Font;
		asset.Owner.Motion = owner.Motion;
		asset.Owner.Image = owner.Image;
		asset.Value = record.Value;

		return asset;
	}
} // namespace sgre::script
