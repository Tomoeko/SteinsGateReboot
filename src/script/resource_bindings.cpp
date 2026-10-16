#include "sgre/script/resource_bindings.hpp"

#include <array>
#include <map>
#include <span>
#include <stdexcept>
#include <utility>

namespace sgre::script {

	static ScriptResourceBindings::TypedAssetRequest WrapPsbRequest(ScriptResourceBindings::AssetRequest request) {
		if (!request) {
			throw std::runtime_error("Resource bindings require an owned request service");
		}

		return [request = std::move(request)](std::string_view name, ScriptResourceRequestMode mode) {
			if (mode != ScriptResourceRequestMode::PsbWrapped) {
				throw std::runtime_error("this Resource service supports wrapped PSB requests only");
			}

			return request(name);
		};
	}

	struct ResourceBindingTarget {
		std::string Name;
		ScriptResourceBindings::LoadPoll Poll;
		std::optional<ScriptResourceAsset> Ready;
	};

	struct ResourceBindingInstance {
		SquirrelObjectIdentity Instance;
		std::vector<ResourceBindingTarget> Targets;
	};

	class ScriptResourceBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptBinaryBindings* binary, TypedAssetRequest request, ErrorSink errors, ScriptResourceBindingLimits limits)
		    : Vm(vm), Objects(objects), Psb(psb), Binary(binary), Request(std::move(request)), Errors(std::move(errors)), Limits(limits) {
			if (!Request || !Errors || limits.MaxInstances == 0 || limits.MaxInstances > 65536 || limits.MaxTargets == 0 || limits.MaxTargets > 65536 || limits.MaxNameBytes == 0 || limits.MaxNameBytes > 65536) {
				throw std::runtime_error("Resource bindings require owned request/error services and bounded positive limits");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptPsbBindings& Psb;
		ScriptBinaryBindings* Binary = nullptr;
		TypedAssetRequest Request;
		ErrorSink Errors;
		ScriptResourceBindingLimits Limits;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, ResourceBindingInstance> Instances;

		~Implementation() {
			for (auto& [index, record] : Instances) {
				static_cast<void>(index);
				ReleaseTargets(record);
			}
		}

		static void ReleaseTargets(ResourceBindingInstance& record) {
			// Native unload releases each manager reference in target order before destroying names.
			for (auto& target : record.Targets) {
				target.Ready.reset();
				target.Poll = {};
			}

			record.Targets.clear();
		}

		ResourceBindingInstance& Record(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("Resource callback requires a native Resource instance");
			}

			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			const auto found = Instances.find(index);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("Resource instance has no retained host state");
			}

			return found->second;
		}

		void Attach(const SquirrelValue& instance) {
			if (Instances.size() >= Limits.MaxInstances) {
				throw std::runtime_error("Resource instance count exceeds its allocation limit");
			}

			if (!Vm.IsInstanceOf(instance, Class) || Instances.contains(std::get<SquirrelObjectHandle>(instance).Index)) {
				throw std::runtime_error("Resource constructor requires a fresh derived instance");
			}

			Objects.AttachInstance(instance);
			ResourceBindingInstance record;
			record.Instance = Vm.BorrowObjectIdentity(instance);
			Instances.emplace(std::get<SquirrelObjectHandle>(instance).Index, std::move(record));
		}

		void Release(const SquirrelValue& instance) {
			auto& record = Record(instance);
			Objects.RequireCanRelease(instance);
			ReleaseTargets(record);
			// Prepared/pending target ownership ends before the native Object base notification.
			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			Objects.ReleaseInstance(instance);
		}

		void AddName(std::vector<std::string>& names, const SquirrelValue& value) const {
			const auto* name = TryStringText(value);
			if (!name) {
				throw std::runtime_error("invalid argument.");
			}

			if (name->empty() || name->size() > Limits.MaxNameBytes || name->find('\0') != std::string::npos || names.size() >= Limits.MaxTargets) {
				throw std::runtime_error("Resource load name/count exceeds its supported identity bounds");
			}

			names.push_back(*name);
		}

		SquirrelNativeResult Load(std::span<const SquirrelValue> arguments, ScriptResourceRequestMode mode) {
			auto& record = Record(arguments[0]);
			if (!record.Targets.empty()) {
				throw std::runtime_error("cannot load before unload.");
			}

			std::vector<std::string> names;
			for (const auto& argument : arguments.subspan(1)) {
				if (Vm.Type(argument) == SquirrelValueType::Array) {
					const auto length = Vm.Length(argument);
					if (length > Limits.MaxTargets - names.size()) {
						throw std::runtime_error("Resource load target array exceeds its allocation limit");
					}

					for (std::size_t index = 0; index < length; ++index) {
						AddName(names, Vm.GetSlot(argument, static_cast<std::int32_t>(index)));
					}
				} else {
					AddName(names, argument);
				}
			}

			std::vector<ResourceBindingTarget> targets;
			targets.reserve(names.size());
			for (auto& name : names) {
				ResourceBindingTarget target;
				target.Name = std::move(name);
				target.Poll = Request(target.Name, mode);
				if (!target.Poll) {
					throw std::runtime_error("Resource request did not return a readiness observer: " + target.Name);
				}

				targets.push_back(std::move(target));
			}

			record.Targets = std::move(targets);

			return SquirrelNativeResult{};
		}

		bool Loading(ResourceBindingInstance& record) {
			// Native 24b970 stops at the first pending target and promotes completed targets into active ownership.
			for (auto& target : record.Targets) {
				if (target.Ready) {
					continue;
				}

				auto ready = target.Poll();
				if (!ready) {
					return true;
				}

				if (!ready->Error.empty()) {
					if (ready->Error.size() > 4096 || ready->Document || ready->Backing || ready->Font || ready->Motion || ready->Image) {
						throw std::runtime_error("Resource request returned data or an invalid diagnostic alongside its error: " + target.Name);
					}

					Errors(target.Name, ready->Error);
				} else if (ready->Kind == ScriptResourceDataKind::Psb) {
					if ((!ready->Document && !ready->Font && !ready->Motion && !ready->Image) || (ready->Font && (ready->Document || ready->Backing || ready->Motion || ready->Image || ready->RetainPsb)) || (ready->Motion && (ready->Document || ready->Backing || ready->Font || ready->Image || ready->RetainPsb)) || (ready->Image && (ready->Document || ready->Backing || ready->Font || ready->Motion || ready->RetainPsb))) {
						throw std::runtime_error("Resource PSB request lacks decoded data or a bounded diagnostic: " + target.Name);
					}
				} else if (ready->Kind == ScriptResourceDataKind::Binary) {
					if (!ready->Backing || ready->Document || ready->Font || ready->Motion || ready->Image) {
						throw std::runtime_error("Resource binary request lacks an exclusively owned byte payload: " + target.Name);
					}
				} else {
					throw std::runtime_error("Resource request returned an unsupported data kind: " + target.Name);
				}

				target.Ready = std::move(ready);
				target.Poll = {};
			}

			return false;
		}

		SquirrelNativeResult Execute(std::span<const SquirrelValue> arguments) {
			if (arguments.size() < 2 || !std::holds_alternative<SquirrelString>(arguments[1])) {
				throw std::runtime_error("Resource.exec requires an exact string identity");
			}

			auto& record = Record(arguments[0]);
			const auto& name = StringText(arguments[1]);
			for (const auto& target : record.Targets) {
				if (target.Name != name) {
					continue;
				}

				if (!target.Ready || !target.Ready->Error.empty() || target.Ready->Kind != ScriptResourceDataKind::Binary || !target.Ready->Backing) {
					throw std::runtime_error("Resource.exec requires a completed successful binary target: " + name);
				}

				const auto& bytes = *target.Ready->Backing;
				if (bytes.size() < 2 || bytes[0] != 0xfa || bytes[1] != 0xfa) {
					throw std::runtime_error("Resource.exec source-text compilation is unrecovered: " + name);
				}

				const auto closure = Vm.CreateClosure(ReadSquirrelBytecode(bytes));
				bool raise_error = true;
				if (arguments.size() >= 3) {
					// Native sq_getbool normalizes converted numbers; failed conversion leaves the initialized one.
					raise_error = TryNativeInteger(arguments[2]).value_or(1) != 0;
				}

				const std::array<SquirrelValue, 1> receiver{Vm.RootTable()};
				SquirrelNativeResult result;
				try {
					result.Value = Vm.InvokeInContext(Vm.CurrentContext(), closure, receiver, 1000000, raise_error).Value;
				} catch (const SquirrelVmException& error) {
					Errors(name, std::string_view(error.what()).substr(0, 4096));
					// Native exec pops the failed closure and returns the remaining argument-stack top.
					result.Value = arguments.back();
				}

				return result;
			}

			throw std::runtime_error("Resource.exec identity was not loaded: " + name);
		}

		SquirrelValue Find(const SquirrelValue& instance, std::string_view name) {
			auto& record = Record(instance);
			for (const auto& target : record.Targets) {
				if (target.Name != name) {
					continue;
				}

				if (!target.Ready) {
					throw std::runtime_error("Resource.find requires completed active data: " + target.Name);
				}

				if (!target.Ready->Error.empty()) {
					return {};
				}

				if (target.Ready->Image) {
					if (!target.Ready->RetainImage) {
						throw std::runtime_error("Resource.find Image requires its independent actual manager pin: " + target.Name);
					}
					return Psb.WrapImageDocument(target.Ready->RetainImage());
				}

				if (target.Ready->Motion) {
					if (!target.Ready->RetainMotion) {
						throw std::runtime_error("Resource.find Motion requires its independent actual manager pin: " + target.Name);
					}
					return Psb.WrapMotionDocument(target.Ready->RetainMotion());
				}

				if (target.Ready->Font) {
					if (!target.Ready->RetainFont) {
						throw std::runtime_error("Resource.find font requires its independent actual manager pin: " + target.Name);
					}

					return Psb.WrapFontDocument(target.Ready->RetainFont());
				}

				if (target.Ready->Kind == ScriptResourceDataKind::Psb && target.Ready->RetainPsb) {
					auto retained = target.Ready->RetainPsb();
					if (!retained.Document || !retained.Backing) {
						throw std::runtime_error("Resource.find PSB retention lacks its actual immutable owner: " + target.Name);
					}

					return Psb.WrapDocument(std::move(retained.Document), std::move(retained.Backing));
				}

				auto backing = target.Ready->Backing;
				if (target.Ready->RetainBacking) {
					backing = target.Ready->RetainBacking();
					if (!backing) {
						throw std::runtime_error("Resource.find retention service returned no owned backing: " + target.Name);
					}
				}

				if (target.Ready->Kind == ScriptResourceDataKind::Binary) {
					if (!Binary) {
						throw std::runtime_error("Resource binary result requires the configured native Binary adapter");
					}

					return Binary->WrapBuffer(std::move(backing));
				}

				return Psb.WrapDocument(target.Ready->Document, std::move(backing));
			}

			return {};
		}
	};

	ScriptResourceBindings::ScriptResourceBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, AssetRequest request, ErrorSink errors, ScriptResourceBindingLimits limits)
	    : ScriptResourceBindings(vm, objects, psb, nullptr, WrapPsbRequest(std::move(request)), std::move(errors), limits) {
	}

	ScriptResourceBindings::ScriptResourceBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptBinaryBindings& binary, TypedAssetRequest request, ErrorSink errors, ScriptResourceBindingLimits limits)
	    : ScriptResourceBindings(vm, objects, psb, &binary, std::move(request), std::move(errors), limits) {
	}

	ScriptResourceBindings::ScriptResourceBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptBinaryBindings* binary, TypedAssetRequest request, ErrorSink errors, ScriptResourceBindingLimits limits)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, psb, binary, std::move(request), std::move(errors), limits)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto method = [&](std::string name, auto operation) {
			const auto callback = vm.CreateNativeClosure("Resource." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("Resource callbacks have expired or lack their receiver");
				}

				return operation(*owner, arguments);
			});
			vm.NewSlot(state.Class, name, callback);
		};

		method("constructor", [weak](Implementation& owner, const auto arguments) {
			owner.Attach(arguments[0]);
			try {
				owner.Objects.ConfigureNativeRelease(arguments[0], [weak](const SquirrelValue& receiver) {
					if (const auto state = weak.lock()) {
						state->Release(receiver);
					}
				});
			} catch (...) {
				owner.Release(arguments[0]);

				throw;
			}

			return SquirrelNativeResult{};
		});
		method("load", [](Implementation& owner, const auto arguments) {
			return owner.Load(arguments, ScriptResourceRequestMode::PsbWrapped);
		});
		method("loadBinary", [](Implementation& owner, const auto arguments) {
			return owner.Load(arguments, ScriptResourceRequestMode::BinaryWrapped);
		});
		method("loadRaw", [](Implementation& owner, const auto arguments) {
			return owner.Load(arguments, ScriptResourceRequestMode::PsbRaw);
		});
		method("loadRawBinary", [](Implementation& owner, const auto arguments) {
			return owner.Load(arguments, ScriptResourceRequestMode::BinaryRaw);
		});
		method("getLoading", [](Implementation& owner, const auto arguments) {
			SquirrelNativeResult result;
			result.Value = owner.Loading(owner.Record(arguments[0]));

			return result;
		});
		method("getWaiting", [](Implementation& owner, const auto arguments) {
			SquirrelNativeResult result;
			result.Value = owner.Loading(owner.Record(arguments[0]));

			return result;
		});
		method("unload", [](Implementation& owner, const auto arguments) {
			owner.ReleaseTargets(owner.Record(arguments[0]));

			return SquirrelNativeResult{};
		});
		method("find", [](Implementation& owner, const auto arguments) {
			if (arguments.size() < 2 || !std::holds_alternative<SquirrelString>(arguments[1])) {
				throw std::runtime_error("Resource.find requires an exact string identity");
			}

			SquirrelNativeResult result;
			result.Value = owner.Find(arguments[0], StringText(arguments[1]));

			return result;
		});
		method("exec", [](Implementation& owner, const auto arguments) {
			return owner.Execute(arguments);
		});
		vm.NewSlot(vm.RootTable(), std::string("Resource"), state.Class);
	}

	ScriptResourceBindings::~ScriptResourceBindings() = default;

	SquirrelValue ScriptResourceBindings::ResourceClass() const {
		return m_Implementation->Class;
	}

	void ScriptResourceBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}
} // namespace sgre::script
