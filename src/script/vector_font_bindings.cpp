#include "sgre/script/vector_font_bindings.hpp"

#include <exception>
#include <map>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sgre::script {

	struct VectorFontBindingRecord {
		SquirrelObjectIdentity Instance;
		ScriptVectorFontState State;
		ScriptResourceBindings::LoadPoll Poll;
		std::optional<ScriptResourceAsset> Ready;
		std::unique_ptr<text::VectorFont> Provider;
		bool Releasing = false;
	};

	static std::string VectorFontString(const SquirrelValue& value, std::string_view field, std::size_t maximum_bytes) {
		const auto* text = TryStringText(value);
		if (!text) {
			throw std::runtime_error("VectorFont " + std::string(field) + " requires an actual string; native failure would dereference null");
		}

		const auto terminator = text->find('\0');
		const auto count = terminator == std::string::npos ? text->size() : terminator;
		if (count > maximum_bytes) {
			throw std::runtime_error("VectorFont " + std::string(field) + " exceeds bounded binding storage");
		}

		return text->substr(0, count);
	}

	static float VectorFontFloat(std::span<const SquirrelValue> arguments) {
		if (arguments.size() >= 2) {
			if (const auto* number = std::get_if<float>(&arguments[1])) {
				return *number;
			}
			if (const auto* integer = std::get_if<std::int32_t>(&arguments[1])) {
				return static_cast<float>(*integer);
			}
			if (const auto* boolean = std::get_if<bool>(&arguments[1])) {
				return *boolean ? 1.0f : 0.0f;
			}
		}

		return 0;
	}

	class ScriptVectorFontBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptResourceBindings::TypedAssetRequest request, ScriptResourceBindings::ErrorSink errors, ScriptVectorFontLimits limits, ActiveAssetFind find_active) : Vm(vm), Objects(objects), Request(std::move(request)), Errors(std::move(errors)), Limits(limits), FindActive(std::move(find_active)) {
			if (!Request || !Errors || Limits.MaxInstances == 0 || Limits.MaxInstances > 65536 || Limits.MaxNameBytes == 0 || Limits.MaxNameBytes > 65536 || Limits.MaxTotalNameBytes < Limits.MaxNameBytes || Limits.MaxTotalNameBytes > 16 * 1024 * 1024) {
				throw std::runtime_error("VectorFont requires owned request/error services and bounded positive limits");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptResourceBindings::TypedAssetRequest Request;
		ScriptResourceBindings::ErrorSink Errors;
		ScriptVectorFontLimits Limits;
		ActiveAssetFind FindActive;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::size_t NameBytes = 0;
		std::map<std::uint32_t, VectorFontBindingRecord> Instances;

		~Implementation() {
			for (auto& [index, record] : Instances) {
				static_cast<void>(index);
				RetireResource(record);
			}
		}

		static void RetireResource(VectorFontBindingRecord& record) {
			// Native SQVectorFont deletes its private core before releasing borrowed font bytes.
			record.Provider.reset();
			record.Ready.reset();
			record.Poll = {};
		}

		VectorFontBindingRecord& LiveRecord(const SquirrelObjectIdentity& identity) {
			const auto found = Instances.find(identity.Index);
			if (found == Instances.end() || found->second.Instance != identity || found->second.Releasing) {
				throw std::runtime_error("VectorFont instance has no live native owner");
			}

			return found->second;
		}

		VectorFontBindingRecord& Record(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("VectorFont requires an actual instance");
			}

			return LiveRecord(Vm.BorrowObjectIdentity(instance));
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& owner) {
			if (arguments.size() < 2 || !Vm.IsInstanceOf(arguments[0], Class)) {
				throw std::runtime_error("invalid argument: VectorFont requires a resource name");
			}

			const auto instance = arguments[0];
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Instances.contains(index) || Instances.size() >= Limits.MaxInstances) {
				throw std::runtime_error("VectorFont is already initialized or its instance limit is exhausted");
			}

			VectorFontBindingRecord record;
			record.Instance = Vm.BorrowObjectIdentity(instance);
			record.State.ResourceName = VectorFontString(arguments[1], "resource name", Limits.MaxNameBytes);
			if (arguments.size() >= 3) {
				record.State.Mode = TryNativeInteger(arguments[2]).value_or(0);
			}
			if (arguments.size() >= 4) {
				record.State.Language = VectorFontString(arguments[3], "language", Limits.MaxNameBytes);
			}
			const auto bytes = record.State.ResourceName.size() + record.State.Language.size();
			if (record.State.ResourceName.size() > Limits.MaxNameBytes || record.State.Language.size() > Limits.MaxNameBytes || bytes > Limits.MaxTotalNameBytes - NameBytes) {
				throw std::runtime_error("VectorFont name/language exceeds bounded binding storage");
			}

			Objects.AttachInstance(instance);
			bool recorded = false;
			try {
				record.Poll = Request(record.State.ResourceName, ScriptResourceRequestMode::BinaryWrapped);
				if (!record.Poll) {
					throw std::runtime_error("VectorFont request did not return a readiness observer");
				}

				Instances.emplace(index, std::move(record));
				NameBytes += bytes;
				recorded = true;
				Objects.ConfigureNativeRelease(instance, [owner](const SquirrelValue& receiver) {
					if (const auto binding = owner.lock()) {
						binding->Release(receiver);
					}
				});
			} catch (...) {
				const auto failure = std::current_exception();
				if (recorded) {
					RetireResource(Instances.at(index));
					Instances.erase(index);
					NameBytes -= bytes;
				} else {
					RetireResource(record);
				}
				try {
					Objects.ReleaseInstance(instance);
				} catch (...) {
				}

				std::rethrow_exception(failure);
			}
		}

		static void ValidateAsset(const ScriptResourceAsset& asset) {
			if (!asset.Error.empty()) {
				if (asset.Error.size() > 4096 || asset.Backing || asset.Document) {
					throw std::runtime_error("VectorFont completed resource mixes a diagnostic with data or exceeds its bound");
				}

			} else if (asset.Kind == ScriptResourceDataKind::Binary) {
				if (!asset.Backing || asset.Document) {
					throw std::runtime_error("VectorFont completed binary resource lacks an exclusive raw payload");
				}
			} else if (asset.Kind == ScriptResourceDataKind::Psb) {
				if (!asset.Document) {
					throw std::runtime_error("VectorFont completed PSB resource lacks its document");
				}
			} else {
				throw std::runtime_error("VectorFont resource kind is unsupported");
			}
		}

		bool Loading(VectorFontBindingRecord& record) {
			if (record.Ready) {
				return false;
			}

			const auto identity = record.Instance;
			const auto poll = record.Poll;
			auto ready = poll();
			static_cast<void>(LiveRecord(identity));
			if (!ready) {
				return true;
			}

			ValidateAsset(*ready);

			const auto diagnostic = ready->Error;
			const auto resource_name = diagnostic.empty() ? std::string{} : record.State.ResourceName;
			record.Ready = std::move(ready);
			record.Poll = {};
			record.State.ResourceCompleted = true;
			if (!diagnostic.empty()) {
				// A fallible/reentrant diagnostic consumer must not leave borrowed record data live here.
				Errors(resource_name, diagnostic);
			}

			return false;
		}

		text::VectorFont* TryPrepare(VectorFontBindingRecord& record) {
			if (record.Provider) {
				return record.Provider.get();
			}
			if (!record.Ready && FindActive) {
				const auto identity = record.Instance;
				const auto name = record.State.ResourceName;
				auto active = FindActive(name);
				static_cast<void>(LiveRecord(identity));
				if (active) {
					ValidateAsset(*active);
					// A different resource owner may already have promoted this exact manager identity.
					// Keep this request lease: active lookup borrows, rather than acquiring a second reference.
					record.Ready = std::move(active);
					record.State.ResourceCompleted = true;
				}
			}
			if (!record.Ready) {
				return nullptr;
			}
			if (!record.Ready->Error.empty() || record.Ready->Kind != ScriptResourceDataKind::Binary || !record.Ready->Backing) {
				return nullptr;
			}

			record.Provider = std::make_unique<text::VectorFont>(record.Ready->Backing, record.State.Mode, record.State.Language);
			record.State.ProviderPrepared = true;

			return record.Provider.get();
		}

		text::VectorFont& Prepare(VectorFontBindingRecord& record) {
			if (auto* provider = TryPrepare(record)) {
				return *provider;
			}
			if (!record.Ready) {
				throw std::runtime_error("VectorFont provider requires a promoted completed resource; native pending use would dereference null");
			}

			throw std::runtime_error("VectorFont provider requires successful raw font bytes; native failed or PSB use would dereference null");
		}

		void Release(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			auto& record = Record(instance);
			record.Releasing = true;
			RetireResource(record);
			NameBytes -= record.State.ResourceName.size() + record.State.Language.size();
			std::string{}.swap(record.State.Language);
			std::string{}.swap(record.State.ResourceName);
			std::exception_ptr failure;
			try {
				Objects.ReleaseInstance(instance);
			} catch (...) {
				failure = std::current_exception();
			}

			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void Install(const std::shared_ptr<Implementation>& owner) {
			Class = Vm.CreateClass(Objects.ObjectClass());
			ClassRoot = Vm.Retain(Class);
			const auto weak = std::weak_ptr<Implementation>(owner);
			const auto method = [&](std::string name, auto operation) {
				const auto closure = Vm.CreateNativeClosure("VectorFont." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
					const auto binding = weak.lock();
					if (!binding || arguments.empty()) {
						throw std::runtime_error("VectorFont bindings expired or its callback lacks a receiver");
					}

					return operation(*binding, arguments);
				});
				Vm.NewSlot(Class, std::move(name), closure);
			};
			method("constructor", [weak](Implementation& state, const auto arguments) {
				state.Construct(arguments, weak);

				return SquirrelNativeResult::NoValues();
			});
			method("weakref", [](Implementation& state, const auto arguments) {
				return SquirrelNativeResult::ReturnValue(state.Vm.CreateWeakReference(arguments.back()));
			});
			method("getLoading", [](Implementation& state, const auto arguments) {
				return SquirrelNativeResult::ReturnValue(state.Loading(state.Record(arguments[0])));
			});
			method("getSize", [](Implementation& state, const auto arguments) {
				return SquirrelNativeResult::ReturnValue(state.Prepare(state.Record(arguments[0])).Size());
			});
			method("setSize", [](Implementation& state, const auto arguments) {
				state.Prepare(state.Record(arguments[0])).SetSize(VectorFontFloat(arguments));

				return SquirrelNativeResult::NoValues();
			});
			Vm.NewSlot(Vm.RootTable(), std::string("VectorFont"), Class);
		}
	};

	ScriptVectorFontBindings::ScriptVectorFontBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptResourceBindings::TypedAssetRequest request, ScriptResourceBindings::ErrorSink errors, ScriptVectorFontLimits limits, ActiveAssetFind find_active) : m_Implementation(std::make_shared<Implementation>(vm, objects, std::move(request), std::move(errors), limits, std::move(find_active))) {
		m_Implementation->Install(m_Implementation);
	}

	ScriptVectorFontBindings::~ScriptVectorFontBindings() = default;

	SquirrelValue ScriptVectorFontBindings::VectorFontClass() const {
		return m_Implementation->Class;
	}

	ScriptVectorFontState ScriptVectorFontBindings::State(const SquirrelValue& instance) const {
		return m_Implementation->Record(instance).State;
	}

	text::VectorFont& ScriptVectorFontBindings::Provider(const SquirrelValue& instance) {
		return m_Implementation->Prepare(m_Implementation->Record(instance));
	}

	text::VectorFont* ScriptVectorFontBindings::TryProvider(const SquirrelValue& instance) {
		return m_Implementation->TryPrepare(m_Implementation->Record(instance));
	}

	void ScriptVectorFontBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
