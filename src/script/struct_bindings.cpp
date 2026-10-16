#include "sgre/script/struct_bindings.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sgre::script {
	struct StructDocumentBinding {
		SquirrelObjectIdentity Instance;
		SquirrelHostRoot Source;
		std::unique_ptr<resource::StructStorage> Storage;
	};

	struct StructValueBinding {
		StructValueBinding(SquirrelObjectIdentity instance, SquirrelHostRoot root, resource::StructValue value)
		    : Instance(instance), Root(std::move(root)), Value(std::move(value)) {
		}

		SquirrelObjectIdentity Instance;
		SquirrelHostRoot Root;
		resource::StructValue Value;
	};

	static void RequireStructArguments(std::span<const SquirrelValue> arguments, std::size_t count) {
		if (arguments.size() != count) {
			throw std::runtime_error("invalid argument.");
		}
	}

	static std::string_view StructKey(const SquirrelValue& value) {
		const auto* text = TryStringText(value);
		if (text == nullptr) {
			throw std::runtime_error("Struct table key requires a native string");
		}

		return std::string_view(*text).substr(0, text->find('\0'));
	}

	static std::int32_t StructIndex(const SquirrelValue& value) {
		// Native227920 initializes the failed sq_getinteger adapter's output to zero.
		return TryNativeInteger(value).value_or(0);
	}

	class ScriptStructBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptBinaryBindings& binary, ScriptStructBindingLimits limits)
		    : Vm(vm), Objects(objects), Psb(psb), Binary(binary), Limits(limits) {
			if (limits.MaxDocuments == 0 || limits.MaxDocuments > 4096 || limits.MaxValues == 0 || limits.MaxValues > 65536) {
				throw std::runtime_error("Struct bindings require bounded positive document/value limits");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptPsbBindings& Psb;
		ScriptBinaryBindings& Binary;
		ScriptStructBindingLimits Limits;
		SquirrelValue Class;
		SquirrelValue ValueClass;
		SquirrelHostRoot ClassRoot;
		SquirrelHostRoot ValueClassRoot;
		std::uint8_t StructTag = 0;
		std::uint8_t ValueTag = 0;
		std::map<std::uint32_t, StructDocumentBinding> Documents;
		std::map<std::uint32_t, StructValueBinding> Values;
		std::weak_ptr<Implementation> Owner;

		StructDocumentBinding& Document(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("Struct callback requires an actual Struct instance");
			}
			const auto found = Documents.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Documents.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance) || !Objects.HasInstance(instance)) {
				throw std::runtime_error("Struct instance has no live schema-backed storage");
			}

			return found->second;
		}

		StructValueBinding& Value(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("StructValue callback requires an actual value instance");
			}
			const auto found = Values.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Values.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("bad instance");
			}
			static_cast<void>(Document(found->second.Root.Value()));

			return found->second;
		}

		void ReleaseDocument(const SquirrelValue& instance) {
			const auto found = Documents.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Documents.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("Struct release identity is stale");
			}
			const bool base_live = Objects.HasInstance(instance);
			if (base_live) {
				Objects.RequireCanRelease(instance);
			}

			Documents.erase(found);
			if (base_live) {
				Objects.ReleaseInstance(instance);
			}
		}

		void Attach(const SquirrelValue& instance, const SquirrelValue& source, std::unique_ptr<resource::StructStorage> storage) {
			if (Documents.size() >= Limits.MaxDocuments || Documents.contains(std::get<SquirrelObjectHandle>(instance).Index)) {
				throw std::runtime_error("Struct document capacity exceeded or receiver is already constructed");
			}

			StructDocumentBinding record;
			record.Instance = Vm.BorrowObjectIdentity(instance);
			record.Source = Vm.Retain(source);
			record.Storage = std::move(storage);
			Objects.AttachInstance(instance);
			const auto weak = Owner;
			try {
				Documents.emplace(std::get<SquirrelObjectHandle>(instance).Index, std::move(record));
				Objects.ConfigureNativeRelease(instance, [weak](const SquirrelValue& receiver) {
					if (const auto owner = weak.lock()) {
						owner->ReleaseDocument(receiver);
					}
				});
			} catch (...) {
				if (Documents.contains(std::get<SquirrelObjectHandle>(instance).Index)) {
					ReleaseDocument(instance);
				} else {
					Objects.ReleaseInstance(instance);
				}

				throw;
			}
		}

		SquirrelValue WrapValue(const SquirrelValue& root, resource::StructValue value) {
			if (value.Kind() != resource::StructValueKind::Table && value.Kind() != resource::StructValueKind::Array) {
				return std::visit([](const auto& scalar) -> SquirrelValue {
					return scalar;
				},
				    value.Read());
			}
			if (Values.size() >= Limits.MaxValues) {
				throw std::runtime_error("Struct retained value capacity exceeded");
			}

			const auto instance = Vm.CreateInstance(ValueClass);
			StructValueBinding record{Vm.BorrowObjectIdentity(instance), Vm.Retain(root), std::move(value)};
			Values.emplace(std::get<SquirrelObjectHandle>(instance).Index, std::move(record));
			const auto weak = Owner;
			try {
				Vm.SetInstanceReleaseHook(instance, [weak](SquirrelVm&, const SquirrelValue& receiver) {
					if (const auto owner = weak.lock()) {
						const auto found = owner->Values.find(std::get<SquirrelObjectHandle>(receiver).Index);
						if (found == owner->Values.end() || found->second.Instance != owner->Vm.BorrowObjectIdentity(receiver)) {
							throw std::runtime_error("StructValue release identity is stale");
						}

						owner->Values.erase(found);
					}
				});
			} catch (...) {
				Values.erase(std::get<SquirrelObjectHandle>(instance).Index);

				throw;
			}

			return instance;
		}

		std::optional<resource::StructValue> Select(StructValueBinding& binding, const SquirrelValue& key) {
			if (binding.Value.Kind() == resource::StructValueKind::Array) {
				const auto index = StructIndex(key);

				return binding.Value.Contains(index) ? std::optional(binding.Value.At(index)) : std::nullopt;
			}
			if (TryStringText(key) == nullptr) {
				return std::nullopt;
			}
			const auto text = StructKey(key);

			return binding.Value.Contains(text) ? std::optional(binding.Value.At(text)) : std::nullopt;
		}

		SquirrelNativeResult ReadValue(std::span<const SquirrelValue> arguments) {
			if (arguments.size() != 2) {
				return SquirrelNativeResult::StackStatus(-1);
			}
			auto& binding = Value(arguments[0]);
			const auto root = binding.Root.Value();
			const auto selected = Select(binding, arguments[1]);

			return selected ? SquirrelNativeResult::ReturnValue(WrapValue(root, *selected)) : SquirrelNativeResult::StackStatus(-1);
		}

		SquirrelNativeResult WriteValue(std::span<const SquirrelValue> arguments) {
			if (arguments.size() != 3) {
				return SquirrelNativeResult::StackStatus(-1);
			}
			auto selected = Select(Value(arguments[0]), arguments[1]);
			if (!selected || selected->Kind() == resource::StructValueKind::Table || selected->Kind() == resource::StructValueKind::Array) {
				return SquirrelNativeResult::StackStatus(-1);
			}
			const auto& input = arguments[2];
			if (selected->Kind() == resource::StructValueKind::Boolean) {
				// Native222c70 normalizes the entire integer or CVTTSS2SI float result;227940 defaults failed conversions to false.
				selected->Write(TryNativeInteger(input).value_or(0) != 0);
			} else if (selected->Kind() == resource::StructValueKind::Integer) {
				selected->Write(StructIndex(input));
			} else if (selected->Kind() == resource::StructValueKind::Float) {
				if (const auto* number = std::get_if<float>(&input)) {
					selected->Write(*number);
				} else if (const auto* integer = std::get_if<std::int32_t>(&input)) {
					selected->Write(static_cast<float>(*integer));
				} else if (const auto* boolean = std::get_if<bool>(&input)) {
					selected->Write(*boolean ? 1.0f : 0.0f);
				} else {
					selected->Write(0.0f);
				}
			} else if (selected->Kind() == resource::StructValueKind::String) {
				const auto* text = TryStringText(input);
				// Native223470 requires a string; failed227e20 conversion receives the empty string from227930.
				selected->Write(text == nullptr ? std::string() : *text);
			} else {
				throw std::runtime_error("Struct assignment cannot replace a container");
			}

			return SquirrelNativeResult::NoValues();
		}

		SquirrelNativeResult NextIndex(std::span<const SquirrelValue> arguments) {
			RequireStructArguments(arguments, 2);
			const auto& value = Value(arguments[0]).Value;
			if (value.Kind() == resource::StructValueKind::Array) {
				const auto next = std::holds_alternative<std::monostate>(arguments[1]) ? 0 : std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(StructIndex(arguments[1])) + 1u);
				if (next < 0) {
					throw std::runtime_error("Struct iterator has an unsafe negative index");
				}

				return value.Contains(next) ? SquirrelNativeResult::ReturnValue(next) : SquirrelNativeResult::NoValues();
			}
			const auto keys = value.Keys();
			std::size_t next = 0;
			if (!std::holds_alternative<std::monostate>(arguments[1])) {
				const auto previous = StructKey(arguments[1]);
				const auto found = std::find(keys.begin(), keys.end(), previous);
				// Native4f0e80 advances the field-vector index; absent predecessor ends iteration.
				next = found == keys.end() ? keys.size() : static_cast<std::size_t>(found - keys.begin()) + 1;
			}

			return next < keys.size() ? SquirrelNativeResult::ReturnValue(keys[next]) : SquirrelNativeResult::NoValues();
		}

		std::vector<std::uint32_t> FlagIndices(std::span<const SquirrelValue> arguments) {
			std::vector<std::uint32_t> indices;
			for (std::size_t argument = 1; argument < arguments.size(); ++argument) {
				const auto append = [&](const SquirrelValue& input) {
					const auto* integer = std::get_if<std::int32_t>(&input);
					if (integer == nullptr || *integer < 0 || indices.size() >= Limits.Storage.MaxArrayElements) {
						throw std::runtime_error("Struct flag selection requires bounded nonnegative native integers");
					}

					indices.push_back(static_cast<std::uint32_t>(*integer));
				};
				if (Vm.Type(arguments[argument]) == SquirrelValueType::Array) {
					const auto count = Vm.Length(arguments[argument]);
					if (count > Limits.Storage.MaxArrayElements - indices.size()) {
						throw std::runtime_error("Struct flag argument array exceeds its work budget");
					}

					for (std::size_t index = 0; index < count; ++index) {
						append(Vm.ReadSlot(arguments[argument], static_cast<std::int32_t>(index)));
					}
				} else {
					append(arguments[argument]);
				}
			}

			return indices;
		}

		void Install(const std::shared_ptr<Implementation>& owner) {
			Owner = owner;
			Class = Vm.CreateClass(Objects.ObjectClass());
			ValueClass = Vm.CreateClass();
			ClassRoot = Vm.Retain(Class);
			ValueClassRoot = Vm.Retain(ValueClass);
			Vm.SetClassTypeTag(Class, &StructTag);
			Vm.SetClassTypeTag(ValueClass, &ValueTag);
			const auto weak = Owner;
			const auto method = [&](const SquirrelValue& target, std::string name, auto operation, bool is_static = false) {
				const auto closure = Vm.CreateNativeClosure("Struct." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
					const auto state = weak.lock();
					if (!state || arguments.empty()) {
						throw std::runtime_error("Struct callbacks expired or lack their receiver");
					}

					return operation(*state, arguments);
				});
				Vm.NewSlot(target, std::move(name), closure, is_static);
			};
			method(Class, "constructor", [](Implementation& state, const auto arguments) {
				if (arguments.size() != 2 && arguments.size() != 3) {
					throw std::runtime_error("invalid argument.");
				}
				const auto asset = state.Psb.DocumentAsset(arguments[1]);
				const auto root_name = arguments.size() == 3 ? StructKey(arguments[2]) : std::string_view("root");
				auto storage = std::make_unique<resource::StructStorage>(asset.Document->Root, root_name, state.Limits.Storage);
				state.Attach(arguments[0], arguments[1], std::move(storage));

				return SquirrelNativeResult::NoValues();
			});
			method(Class, "getRoot", [](Implementation& state, const auto arguments) {
				RequireStructArguments(arguments, 1);
				const auto value = state.Document(arguments[0]).Storage->Root();

				return SquirrelNativeResult::ReturnValue(state.WrapValue(arguments[0], value));
			});
			method(Class, "getRootSize", [](Implementation& state, const auto arguments) {
				RequireStructArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(static_cast<std::int32_t>(state.Document(arguments[0]).Storage->RootSize()));
			});
			method(Class, "clear", [](Implementation& state, const auto arguments) {
				RequireStructArguments(arguments, 1);
				state.Document(arguments[0]).Storage->Clear();

				return SquirrelNativeResult::NoValues();
			});
			method(Class, "purge", [](Implementation& state, const auto arguments) {
				RequireStructArguments(arguments, 1);
				state.Document(arguments[0]).Storage->Purge();

				return SquirrelNativeResult::NoValues();
			});
			method(Class, "serialize", [](Implementation& state, const auto arguments) {
				RequireStructArguments(arguments, 1);
				const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(state.Document(arguments[0]).Storage->Serialize());

				return SquirrelNativeResult::ReturnValue(state.Binary.WrapBuffer(bytes));
			});
			method(Class, "unserialize", [](Implementation& state, const auto arguments) {
				RequireStructArguments(arguments, 2);
				state.Document(arguments[0]).Storage->Unserialize(state.Binary.Bytes(arguments[1]));

				return SquirrelNativeResult::NoValues();
			});
			method(Class, "dup", [](Implementation& state, const auto arguments) {
				RequireStructArguments(arguments, 1);
				const auto& original = state.Document(arguments[0]);
				auto duplicate = std::make_unique<resource::StructStorage>(original.Storage->Duplicate());
				const auto source = original.Source.Value();
				const auto instance = state.Vm.CreateInstance(state.Class);
				state.Attach(instance, source, std::move(duplicate));

				return SquirrelNativeResult::ReturnValue(instance);
			});
			method(Class, "_cmp", [](Implementation& state, const auto arguments) {
				RequireStructArguments(arguments, 2);
				const auto& left = *state.Document(arguments[0]).Storage;
				const auto& right = *state.Document(arguments[1]).Storage;

				return SquirrelNativeResult::ReturnValue(left.Compare(right));
			});
			method(ValueClass, "constructor", [](Implementation&, const auto) {
				// Native CopyOnly constructor04b530 returns zero and does not attach a value descriptor.
				return SquirrelNativeResult::NoValues();
			});
			method(ValueClass, "weakref", [](Implementation& state, const auto) {
				return SquirrelNativeResult::ReturnValue(state.Vm.CreateWeakReference(state.Vm.NativeStackValue(-1)));
			});
			method(ValueClass, "_get", [](Implementation& state, const auto arguments) {
				return state.ReadValue(arguments);
			});
			method(ValueClass, "_set", [](Implementation& state, const auto arguments) {
				return state.WriteValue(arguments);
			});
			method(
			    ValueClass, "getValue", [](Implementation& state, const auto arguments) {
				    if (state.Vm.Type(arguments[0]) != SquirrelValueType::Instance || !state.Vm.InstanceClassHasTypeTag(arguments[0], &state.ValueTag)) {
					    return SquirrelNativeResult::NoValues();
				    }
				    const auto found = state.Values.find(std::get<SquirrelObjectHandle>(arguments[0]).Index);
				    if (found == state.Values.end() || found->second.Instance != state.Vm.BorrowObjectIdentity(arguments[0])) {
					    return SquirrelNativeResult::NoValues();
				    }

				    return state.ReadValue(arguments);
			    },
			    true);
			method(ValueClass, "len", [](Implementation& state, const auto arguments) {
				RequireStructArguments(arguments, 1);

				return SquirrelNativeResult::ReturnValue(static_cast<std::int32_t>(state.Value(arguments[0]).Value.Length()));
			});
			method(ValueClass, "_nexti", [](Implementation& state, const auto arguments) {
				return state.NextIndex(arguments);
			});
			method(ValueClass, "_exist", [](Implementation& state, const auto arguments) {
				RequireStructArguments(arguments, 2);
				const auto& value = state.Value(arguments[0]).Value;
				bool exists = false;
				if (value.Kind() == resource::StructValueKind::Array) {
					// Original23c1a4 skips the array check for exact Integer keys; preserve that native branch.
					exists = state.Vm.Type(arguments[1]) != SquirrelValueType::Integer && value.Contains(StructIndex(arguments[1]));
				} else if (TryStringText(arguments[1]) != nullptr) {
					exists = value.Contains(StructKey(arguments[1]));
				}

				return SquirrelNativeResult::StackStatus(exists ? 0 : -1);
			});
			method(ValueClass, "_typeof", [](Implementation& state, const auto arguments) {
				RequireStructArguments(arguments, 1);
				const auto kind = state.Value(arguments[0]).Value.Kind();

				return SquirrelNativeResult::ReturnValue(std::string(kind == resource::StructValueKind::Table ? "table" : "array"));
			});
			method(ValueClass, "clear", [](Implementation& state, const auto arguments) {
				RequireStructArguments(arguments, 1);
				state.Value(arguments[0]).Value.Clear();

				return SquirrelNativeResult::NoValues();
			});
			method(ValueClass, "countFlags", [](Implementation& state, const auto arguments) {
				const auto& value = state.Value(arguments[0]).Value;
				const auto count = arguments.size() == 1 ? value.CountFlags() : value.CountFlags(state.FlagIndices(arguments));

				return SquirrelNativeResult::ReturnValue(static_cast<std::int32_t>(count));
			});
			method(ValueClass, "andFlags", [](Implementation& state, const auto arguments) {
				const auto& value = state.Value(arguments[0]).Value;

				return SquirrelNativeResult::ReturnValue(arguments.size() == 1 ? value.AndFlags() : value.AndFlags(state.FlagIndices(arguments)));
			});
			method(ValueClass, "orFlags", [](Implementation& state, const auto arguments) {
				const auto& value = state.Value(arguments[0]).Value;

				return SquirrelNativeResult::ReturnValue(arguments.size() == 1 ? value.OrFlags() : value.OrFlags(state.FlagIndices(arguments)));
			});
			const auto stream_gap = [](Implementation&, const auto) -> SquirrelNativeResult {
				throw std::runtime_error("Struct byte-array stream conversion requires the genuine native Stream owner, which remains unrecovered");
			};
			method(ValueClass, "storeBinary", stream_gap);
			method(ValueClass, "restoreBinary", stream_gap);
			Vm.NewSlot(Vm.RootTable(), std::string("Struct"), Class);
			Vm.NewSlot(Vm.RootTable(), std::string("StructValue"), ValueClass);
		}
	};

	ScriptStructBindings::ScriptStructBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptBinaryBindings& binary, ScriptStructBindingLimits limits)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, psb, binary, limits)) {
		m_Implementation->Install(m_Implementation);
	}

	ScriptStructBindings::~ScriptStructBindings() = default;

	SquirrelValue ScriptStructBindings::StructClass() const {
		return m_Implementation->Class;
	}

	resource::StructStorage& ScriptStructBindings::BorrowStructStorage(const SquirrelValue& instance) {
		return *m_Implementation->Document(instance).Storage;
	}

	SquirrelValue ScriptStructBindings::ValueClass() const {
		return m_Implementation->ValueClass;
	}
} // namespace sgre::script
