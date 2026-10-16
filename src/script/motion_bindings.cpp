#include "sgre/script/motion_bindings.hpp"

#include "sgre/script/motion_layer_getter_bindings.hpp"
#include "sgre/script/motion_layer_setter_bindings.hpp"
#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/motion_root_coordinate_bindings.hpp"
#include "sgre/script/motion_variable_bindings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::script {

	enum class MotionMethod {
		SetCharacter,
		GetCharacter,
		SetStealthCharacter,
		GetStealthCharacter,
		Play,
		Stop,
		SkipToSync,
		GetSyncWaiting,
		GetLoopTime,
		ReleaseSyncWait,
		GetPlaying,
		GetAllPlaying,
		SetMotion,
		GetMotion,
		SetStealthMotion,
		GetStealthMotion,
		SetTickCount,
		GetTickCount,
		SetVisible,
		GetVisible,
		SetPriority,
		GetPriority,
		SetSpeed,
		GetSpeed,
		SetEventEnabled,
		GetEventEnabled,
		RegisterAlternateTexture,
		UnregisterAlternateTexture,
		ClearAlternateTextures,
		GetLayerGetter,
		GetLayerSetter,
		GetLayerMotion,
		Progress,
		Clear
	};

	static void RequireMotionArity(std::span<const SquirrelValue> arguments, std::size_t count) {
		if (arguments.size() != count) {
			throw std::runtime_error("invalid argument.");
		}
	}

	static std::int32_t NativeMotionInteger(const SquirrelValue& value) {
		// Native043430 → 227df0/222ff0 resets its output through227920 when numeric conversion fails.
		return TryNativeInteger(value).value_or(0);
	}

	static std::string MotionStringArgument(SquirrelVm& vm, const SquirrelValue& value) {
		// Primitive tostring reuses the recovered VM formatter without introducing a user callback boundary.
		if (std::holds_alternative<SquirrelObjectHandle>(value)) {
			throw std::runtime_error("Motion object tostring requires its separately validated reentrant VM owner");
		}

		auto text = vm.ToString(value);
		text.resize(text.find('\0') == std::string::npos ? text.size() : text.find('\0'));

		return text;
	}

	static float MotionNumberArgument(const SquirrelValue& value) {
		if (const auto* real = std::get_if<float>(&value)) {
			return *real;
		}
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return static_cast<float>(*integer);
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean ? 1.0f : 0.0f;
		}

		// Native056d70 initializes zero before222e90, whose failed type conversion leaves it unchanged.
		return 0;
	}

	static bool MotionBooleanArgument(const SquirrelValue& value) {
		if (std::holds_alternative<std::monostate>(value)) {
			return false;
		}
		if (const auto* real = std::get_if<float>(&value)) {
			return *real != 0;
		}
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return *integer != 0;
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean;
		}

		return true;
	}

	class MotionClassConstructionScope {
	public:
		explicit MotionClassConstructionScope(bool& active)
		    : m_Active(active) {
			if (active) {
				throw std::runtime_error("Motion factory services cannot reenter actual construction");
			}

			m_Active = true;
		}

		~MotionClassConstructionScope() noexcept {
			m_Active = false;
		}
		MotionClassConstructionScope(const MotionClassConstructionScope&) = delete;
		MotionClassConstructionScope& operator=(const MotionClassConstructionScope&) = delete;

	private:
		bool& m_Active;
	};

	class ScriptMotionBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptMotionBindingServices services)
		    : Vm(vm), Objects(objects), Layers(layers), Services(std::move(services)) {
			if (!Services.ResolveRuntime || !Services.ReadAsynchronousMode) {
				throw std::runtime_error("Motion registration requires actual Layer runtimes and application asynchronous-mode service");
			}
			if (Vm.HasSlot(Vm.RootTable(), std::string("Motion"))) {
				throw std::runtime_error("Motion class is already installed in the actual root VM");
			}

			Class = Vm.CreateClass(Objects.ObjectClass());
			ClassRoot = Vm.Retain(Class);
			Owners = std::make_unique<ScriptMotionNativeOwners>(Vm, Objects, Layers, Class);
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		ScriptMotionBindingServices Services;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::unique_ptr<ScriptMotionNativeOwners> Owners;
		std::unique_ptr<ScriptMotionLayerGetters> Getters;
		std::unique_ptr<ScriptMotionLayerSetters> Setters;
		std::unique_ptr<ScriptMotionRootCoordinates> RootCoordinates;
		std::unique_ptr<ScriptMotionVariables> Variables;
		SquirrelHostRoot NativeTextureClass;
		std::function<const scene::NativeTextureReference&(const SquirrelValue&)> ResolveNativeTexture;
		bool Constructing = false;

		void Construct(std::span<const SquirrelValue> arguments) {
			const MotionClassConstructionScope construction(Constructing);
			if (arguments.size() < 2 || arguments.size() > 4 || !Vm.IsInstanceOf(arguments[0], Class)) {
				throw std::runtime_error("invalid argument.");
			}
			if (Vm.IsInstanceOf(arguments[1], Class)) {
				// Native273600 has a different non-retiring wrapper/listener policy from the destination-Layer copy.
				throw std::runtime_error("Motion copy factory requires the actual full selected-state clone owner for its standalone listener lifetime");
			}
			if (!Vm.IsInstanceOf(arguments[1], Layers.LayerClass())) {
				throw std::runtime_error("Motion new factory requires an actual concrete Layer; other MotionLayerBase providers remain unsupported");
			}

			if (arguments.size() >= 3 && Vm.Type(arguments[2]) == SquirrelValueType::Instance && Vm.IsInstanceOf(arguments[2], Class)) {
				const auto selector = arguments.size() == 4 ? NativeMotionInteger(arguments[3]) : 0;
				const auto source = selector == 0 ? MotionLayerSource::Secondary : MotionLayerSource::Primary;
				auto& runtime = Services.ResolveRuntime(arguments[1], source);
				const auto& resources = Layers.MotionResources(arguments[1]);
				const auto& expected_manager = source == MotionLayerSource::Secondary ? resources.Secondary() : resources.Primary();
				if (&runtime.Manager() != &expected_manager) {
					throw std::runtime_error("Motion clone factory resolver returned the wrong actual destination Layer manager");
				}

				// The factory admits at most four arguments; native insertion argument5 is consequently always zero.
				Owners->ConstructClone(arguments[0], arguments[1], arguments[2], runtime, Services.ReadAsynchronousMode());
				return;
			}

			const auto selector = arguments.size() >= 3 ? NativeMotionInteger(arguments[2]) : 0;
			const auto insertion = arguments.size() == 4 ? NativeMotionInteger(arguments[3]) : 0;
			const auto source = selector == 0 ? MotionLayerSource::Secondary : MotionLayerSource::Primary;
			auto& runtime = Services.ResolveRuntime(arguments[1], source);
			const auto& resources = Layers.MotionResources(arguments[1]);
			const auto& expected_manager = source == MotionLayerSource::Secondary ? resources.Secondary() : resources.Primary();
			if (&runtime.Manager() != &expected_manager) {
				throw std::runtime_error("Motion new factory resolver returned the wrong actual selected Layer manager");
			}

			Owners->ConstructNew(arguments[0], arguments[1], runtime, Services.ReadAsynchronousMode(), insertion == 0 ? scene::MotionTaskInsertion::Append : scene::MotionTaskInsertion::Prepend);
		}

		float TextureCoordinate(const SquirrelValue& value, std::string_view key) {
			try {
				return MotionNumberArgument(Vm.ReadSlotInContext(Vm.CurrentContext(), value, std::string(key)));
			} catch (const SquirrelVmException&) {
				// Native278770 preserves the VM error but leaves each failed member conversion at zero.
				return 0;
			}
		}

		void AlternateTexture(MotionMethod method, std::span<const SquirrelValue> arguments, bool detached) {
			const std::size_t count = method == MotionMethod::RegisterAlternateTexture ? 5 : (method == MotionMethod::UnregisterAlternateTexture ? 2 : 1);
			RequireMotionArity(arguments, count);
			// General getters and tostring can grow the active VM operand stack. Copy every root before the first callback.
			std::array<SquirrelValue, 5> retained;
			std::copy(arguments.begin(), arguments.end(), retained.begin());
			arguments = std::span<const SquirrelValue>(retained).first(arguments.size());
			const auto uid = method == MotionMethod::ClearAlternateTextures ? std::string{} : MotionStringArgument(Vm, arguments[1]);
			if (detached || Services.ReadAsynchronousMode()) {
				return;
			}
			if (method == MotionMethod::ClearAlternateTextures) {
				Owners->ClearAlternateTextures(arguments[0]);
				return;
			}
			if (method == MotionMethod::UnregisterAlternateTexture) {
				Owners->UnregisterAlternateTexture(arguments[0], uid);
				return;
			}
			if (!ResolveNativeTexture) {
				throw std::runtime_error("Motion alternate texture requires its actual NativeTextureRef adapter");
			}
			if (Vm.Type(arguments[2]) != SquirrelValueType::Instance || !Vm.IsInstanceOf(arguments[2], NativeTextureClass.Value())) {
				return;
			}
			scene::MotionBounds rectangle;
			rectangle.MaximumY = TextureCoordinate(arguments[3], "bottom");
			rectangle.MaximumX = TextureCoordinate(arguments[3], "right");
			rectangle.MinimumY = TextureCoordinate(arguments[3], "top");
			rectangle.MinimumX = TextureCoordinate(arguments[3], "left");
			resource::MotionPoint origin;
			origin.Y = TextureCoordinate(arguments[4], "y");
			origin.X = TextureCoordinate(arguments[4], "x");
			// Property getters can enter script; acquire the checked native payload only after their last callback.
			const auto& descriptor = ResolveNativeTexture(arguments[2]);
			Owners->RegisterAlternateTexture(arguments[0], descriptor, uid, arguments[2], rectangle, origin);
		}

		SquirrelValue Dispatch(MotionMethod method, std::span<const SquirrelValue> arguments) {
			if (arguments.empty() || !Owners->Contains(arguments[0])) {
				throw std::runtime_error("Motion method requires its genuine initialized native receiver");
			}
			const auto& instance = arguments[0];
			const bool detached = Owners->IsDetached(instance);
			if (method == MotionMethod::RegisterAlternateTexture || method == MotionMethod::UnregisterAlternateTexture || method == MotionMethod::ClearAlternateTextures) {
				AlternateTexture(method, arguments, detached);
				return {};
			}
			if (method == MotionMethod::Clear) {
				// Native058160 ignores extra operands; 275060 leaves detached wrappers unchanged.
				if (!detached) {
					Owners->Task(instance).Clear();
				}
				return {};
			}
			if (method == MotionMethod::Progress) {
				// Native056d70 leaves failed/missing physical arg2 conversion at zero and ignores extra arguments.
				const float elapsed_ticks = arguments.size() > 1 ? MotionNumberArgument(arguments[1]) : 0;
				if (!detached) {
					auto task = Owners->Task(instance);
					task.Progress(elapsed_ticks);
				}
				return {};
			}
			if (method == MotionMethod::GetLoopTime) {
				return detached ? 0.0f : Owners->Commands(instance).ClockState().LoopTime;
			}
			if (method == MotionMethod::GetSyncWaiting) {
				return !detached && Owners->Commands(instance).ClockState().SyncWaiting;
			}
			if (method == MotionMethod::SkipToSync || method == MotionMethod::ReleaseSyncWait) {
				if (!detached) {
					if (method == MotionMethod::SkipToSync) {
						Owners->SkipToSync(instance);
					} else {
						Owners->ReleaseSyncWait(instance);
					}
				}

				return {};
			}
			if (method == MotionMethod::GetLayerGetter || method == MotionMethod::GetLayerSetter || method == MotionMethod::GetLayerMotion) {
				if (detached) {
					throw std::runtime_error(method == MotionMethod::GetLayerMotion ? "Motion.getLayerMotion() owner layer already desructed.\n" : "Motion.getLayerGetter() owner layer already desructed.\n");
				}
				if (arguments.size() < 2 || !TryStringText(arguments[1])) {
					throw std::runtime_error(method == MotionMethod::GetLayerMotion ? "Motion.getLayerMotion requires its actual string name" : "Motion.getLayerGetter requires its actual string name");
				}
				const auto name = StringText(arguments[1]);
				if (method == MotionMethod::GetLayerMotion) {
					return Owners->GetLayerMotion(*Getters, instance, name);
				}

				if (method == MotionMethod::GetLayerSetter) {
					return Owners->GetLayerSetter(*Setters, instance, name);
				}
				return Owners->GetLayerGetter(*Getters, instance, name);
			}
			switch (method) {
			case MotionMethod::SetEventEnabled:
				RequireMotionArity(arguments, 2);
				Owners->SetEventEnabled(instance, MotionBooleanArgument(arguments[1]));
				return {};
			case MotionMethod::GetEventEnabled:
				RequireMotionArity(arguments, 1);
				return Owners->EventEnabled(instance);
			case MotionMethod::GetCharacter:
				RequireMotionArity(arguments, 1);
				return Vm.CreateString(detached ? std::string{} : Owners->Commands(instance).Names().RequestedCharacter);
			case MotionMethod::GetStealthCharacter:
				RequireMotionArity(arguments, 1);
				return Vm.CreateString(detached ? std::string{} : Owners->Commands(instance).Names().CurrentCharacter);
			case MotionMethod::GetMotion:
				RequireMotionArity(arguments, 1);
				return Vm.CreateString(detached ? std::string{} : Owners->Commands(instance).Names().RequestedMotion);
			case MotionMethod::GetStealthMotion:
				RequireMotionArity(arguments, 1);
				return Vm.CreateString(detached ? std::string{} : Owners->Commands(instance).Names().CurrentMotion);
			case MotionMethod::GetPlaying:
				RequireMotionArity(arguments, 1);
				return !detached && Owners->Commands(instance).ClockState().Playing;
			case MotionMethod::GetAllPlaying:
				RequireMotionArity(arguments, 1);
				return !detached && Owners->AllPlaying(instance);
			case MotionMethod::GetTickCount:
				RequireMotionArity(arguments, 1);
				return detached ? 0.0f : Owners->Commands(instance).ClockState().TickCount;
			case MotionMethod::GetPriority:
				RequireMotionArity(arguments, 1);
				return detached ? 0.0f : Owners->Root(instance).Priority();
			case MotionMethod::GetSpeed:
				RequireMotionArity(arguments, 1);
				return detached ? 1.0f : Owners->Commands(instance).ClockState().Speed;
			case MotionMethod::GetVisible:
				RequireMotionArity(arguments, 1);
				return !detached && Owners->Root(instance).Overrides().Visible;
			default:
				break;
			}

			// Native078690 converts the name before the typed target checks its detached Layer borrow.
			if (method == MotionMethod::SetCharacter || method == MotionMethod::SetStealthCharacter || method == MotionMethod::SetMotion || method == MotionMethod::SetStealthMotion) {
				RequireMotionArity(arguments, 2);
				const auto name = MotionStringArgument(Vm, arguments[1]);
				if (!detached) {
					auto task = Owners->Task(instance);
					if (method == MotionMethod::SetCharacter || method == MotionMethod::SetStealthCharacter) {
						task.SetCharacter(name, method == MotionMethod::SetCharacter ? scene::MotionCharacterMode::Ordinary : scene::MotionCharacterMode::Stealth);
					} else {
						task.Play(name, method == MotionMethod::SetMotion ? scene::MotionPlayFlags::None : scene::MotionPlayFlags::Stealth);
					}
				}

				return {};
			}
			// Native raw-VM play/tick methods check detachment before reading their stack arguments.
			if (detached) {
				return {};
			}
			if (method == MotionMethod::SetVisible) {
				RequireMotionArity(arguments, 2);
				Owners->SetVisible(instance, MotionBooleanArgument(arguments[1]));
				return {};
			}
			if (method == MotionMethod::Stop) {
				RequireMotionArity(arguments, 1);
				Owners->Stop(instance);
				return {};
			}
			auto task = Owners->Task(instance);
			switch (method) {
			case MotionMethod::Play:
				if (arguments.size() != 2 && arguments.size() != 3) {
					throw std::runtime_error("invalid argument.");
				}
				task.Play(MotionStringArgument(Vm, arguments[1]), static_cast<scene::MotionPlayFlags>(arguments.size() == 3 ? NativeMotionInteger(arguments[2]) : 0));
				return {};
			case MotionMethod::SetTickCount:
				RequireMotionArity(arguments, 2);
				AssignTickCount(task, arguments[1]);
				return {};
			case MotionMethod::SetPriority:
				RequireMotionArity(arguments, 2);
				task.SetPriority(MotionNumberArgument(arguments[1]));
				return {};
			case MotionMethod::SetSpeed:
				RequireMotionArity(arguments, 2);
				task.SetSpeed(MotionNumberArgument(arguments[1]));
				return {};
			default:
				throw std::runtime_error("Motion native method is outside this recovered new-only adapter");
			}
		}

		void AssignTickCount(scene::MotionPlayerTask& task, const SquirrelValue& value) {
			if (const auto* label = TryStringText(value)) {
				task.SetTickLabel(*label);

				return;
			}
			if (!std::holds_alternative<float>(value) && !std::holds_alternative<std::int32_t>(value) && !std::holds_alternative<bool>(value)) {
				throw std::runtime_error("invalid argument.");
			}

			task.SetTickCount(MotionNumberArgument(value));
		}

		void InstallConstants() {
			const auto destination = Vm.ConstantTable();
			SquirrelValue constants;
			if (Vm.HasSlot(destination, std::string("MOTION"))) {
				constants = Vm.ReadSlot(destination, std::string("MOTION"));
				if (Vm.Type(constants) != SquirrelValueType::Table) {
					throw std::runtime_error("native MOTION constants require the existing actual constant-table entry to be a table");
				}
			} else {
				constants = Vm.CreateTable();
				Vm.NewSlot(destination, std::string("MOTION"), constants);
			}
			const std::array<std::pair<std::string_view, std::int32_t>, 36> values = {{
			    {"FORCE", 1},
			    {"CHAIN", 2},
			    {"AS_CAN", 4},
			    {"JOIN", 8},
			    {"STEALTH", 16},
			    {"DELAY1", 32},
			    {"DELAY2", 64},
			    {"TRANFORM_FLIP", 0},
			    {"TRANFORM_ZOOM", 2},
			    {"TRANFORM_SLANT", 3},
			    {"TRANFORM_ANGLE", 1},
			    {"LAYER_TYPE_OBJ", 0},
			    {"LAYER_TYPE_LAYOUT", 2},
			    {"LAYER_TYPE_SHAPE", 1},
			    {"LAYER_TYPE_MOTION", 3},
			    {"LAYER_TYPE_PARTICLE", 4},
			    {"LAYER_TYPE_CAMERA", 5},
			    {"LAYER_TYPE_MODEL", 6},
			    {"LAYER_TYPE_CLIP", 7},
			    {"LAYER_TYPE_TEXT", 8},
			    {"LAYER_TYPE_ANCHOR", 9},
			    {"COORDINATE_RECUTANGULAR_XY", 0},
			    {"COORDINATE_RECUTANGULAR_XZ", 1},
			    {"SHAPE_TYPE_POINT", 0},
			    {"SHAPE_TYPE_CIRCLE", 1},
			    {"SHAPE_TYPE_RECT", 2},
			    {"SHAPE_TYPE_QUAD", 3},
			    {"COLOR_FILTER_NONE", 0},
			    {"COLOR_FILTER_BLEND", 1},
			    {"COLOR_FILTER_ADD", 2},
			    {"COLOR_FILTER_SUBTRACT", 3},
			    {"COLOR_FILTER_NEGATE", 4},
			    {"BACK", 0},
			    {"FORE", 1},
			    {"APPEND", 0},
			    {"INSERT", 1},
			}};
			for (const auto& [name, value] : values) {
				Vm.NewSlot(constants, std::string(name), value);
			}
		}

		void Install(const std::weak_ptr<Implementation>& weak) {
			const auto constructor = Vm.CreateNativeClosure("Motion.constructor", [weak](SquirrelVm&, auto arguments) {
				const auto owner = weak.lock();
				if (!owner) {
					throw std::runtime_error("Motion class owner has expired");
				}

				owner->Construct(arguments);

				return SquirrelNativeResult::NoValues();
			});
			Vm.NewSlot(Class, std::string("constructor"), constructor);
			// Native039f50 reads the actual last stack argument, independently of any Motion native payload.
			const auto weakref = Vm.CreateNativeClosure("Motion.weakref", [](SquirrelVm& vm, auto arguments) {
				if (arguments.empty()) {
					throw std::runtime_error("Motion weakref requires its actual initialized stack argument");
				}

				return SquirrelNativeResult::ReturnValue(vm.CreateWeakReference(arguments.back()));
			});
			Vm.NewSlot(Class, std::string("weakref"), weakref);
			const std::array<std::pair<std::string_view, MotionMethod>, 30> methods = {{
			    {"setChara", MotionMethod::SetCharacter},
			    {"getChara", MotionMethod::GetCharacter},
			    {"setStealthChara", MotionMethod::SetStealthCharacter},
			    {"getStealthChara", MotionMethod::GetStealthCharacter},
			    {"play", MotionMethod::Play},
			    {"stop", MotionMethod::Stop},
			    {"clear", MotionMethod::Clear},
			    {"getPlaying", MotionMethod::GetPlaying},
			    {"getAllPlaying", MotionMethod::GetAllPlaying},
			    {"setMotion", MotionMethod::SetMotion},
			    {"getMotion", MotionMethod::GetMotion},
			    {"setStealthMotion", MotionMethod::SetStealthMotion},
			    {"getStealthMotion", MotionMethod::GetStealthMotion},
			    {"setTickCount", MotionMethod::SetTickCount},
			    {"getTickCount", MotionMethod::GetTickCount},
			    {"setVisible", MotionMethod::SetVisible},
			    {"getVisible", MotionMethod::GetVisible},
			    {"setPriority", MotionMethod::SetPriority},
			    {"getPriority", MotionMethod::GetPriority},
			    {"setSpeed", MotionMethod::SetSpeed},
			    {"getSpeed", MotionMethod::GetSpeed},
			    {"setEventEnabled", MotionMethod::SetEventEnabled},
			    {"getEventEnabled", MotionMethod::GetEventEnabled},
			    {"getLayerGetter", MotionMethod::GetLayerGetter},
			    {"getLayerSetter", MotionMethod::GetLayerSetter},
			    {"getLayerMotion", MotionMethod::GetLayerMotion},
			    {"progress", MotionMethod::Progress},
			    {"registerAlternateTexture", MotionMethod::RegisterAlternateTexture},
			    {"unregisterAlternateTexture", MotionMethod::UnregisterAlternateTexture},
			    {"releaseAllAlternateTextures", MotionMethod::ClearAlternateTextures},
			}};
			for (const auto& [name, method] : methods) {
				const auto callback = Vm.CreateNativeClosure("Motion." + std::string(name), [weak, method](SquirrelVm&, auto arguments) {
					const auto owner = weak.lock();
					if (!owner) {
						throw std::runtime_error("Motion class owner has expired");
					}

					const auto value = owner->Dispatch(method, arguments);
					if (method == MotionMethod::GetCharacter || method == MotionMethod::GetStealthCharacter || method == MotionMethod::GetMotion || method == MotionMethod::GetStealthMotion || method == MotionMethod::GetPlaying || method == MotionMethod::GetAllPlaying || method == MotionMethod::GetTickCount || method == MotionMethod::GetVisible || method == MotionMethod::GetPriority || method == MotionMethod::GetSpeed || method == MotionMethod::GetEventEnabled || method == MotionMethod::GetLayerGetter || method == MotionMethod::GetLayerSetter || method == MotionMethod::GetLayerMotion) {
						return SquirrelNativeResult::ReturnValue(value);
					}

					return SquirrelNativeResult::NoValues();
				});
				Vm.NewSlot(Class, std::string(name), callback);
			}

			const std::array<std::uint8_t, 16> last_time_descriptor{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
			const std::array<SquirrelValue, 1> last_time_captures{Vm.CreateUserData(last_time_descriptor)};
			const auto last_time = Vm.CreateNativeClosureWithCaptures(
			    "Motion.getLastTime", [weak, last_time_descriptor](SquirrelVm& active, auto arguments) {
				    const auto owner = weak.lock();
				    if (!owner || &owner->Vm != &active) {
					    throw std::runtime_error("Motion class owner has expired");
				    }

				    const auto payload = active.UserDataBytes(active.NativeStackValue(-1));
				    if (payload.size() != last_time_descriptor.size() || !std::equal(payload.begin(), payload.end(), last_time_descriptor.begin())) {
					    throw std::runtime_error("Motion last-time descriptor differs from its actual typed operation");
				    }
				    if (arguments.empty() || active.Type(arguments[0]) != SquirrelValueType::Instance || !owner->Owners->Contains(arguments[0])) {
					    throw std::runtime_error("bad instance");
				    }

				    // Native0580c0 ignores extra operands; 276860 returns detached zero or the retained player+20c word.
				    return SquirrelNativeResult::ReturnValue(owner->Owners->IsDetached(arguments[0]) ? 0.0f : owner->Owners->Commands(arguments[0]).ClockState().LastTime);
			    },
			    last_time_captures);
			Vm.NewSlot(Class, "getLastTime", last_time);

			// Native static bridges retain an eight-byte function descriptor; portable captures carry checked selectors.
			for (const bool write_mode : {false, true}) {
				const std::array<std::uint8_t, 8> descriptor{1, 0, 0, 0, static_cast<std::uint8_t>(write_mode), 0, 0, 0};
				const std::array<SquirrelValue, 1> captures{Vm.CreateUserData(descriptor)};
				const auto name = write_mode ? "setDefaultMultiThread" : "getDefaultMultiThread";
				const auto callback = Vm.CreateNativeClosureWithCaptures(
				    std::string("Motion.") + name, [weak, descriptor, write_mode](SquirrelVm& vm, auto) {
					    const auto owner = weak.lock();
					    if (!owner || &owner->Vm != &vm) {
						    throw std::runtime_error("Motion class owner has expired");
					    }

					    const auto payload = vm.UserDataBytes(vm.NativeStackValue(-1));
					    if (payload.size() != descriptor.size() || !std::equal(payload.begin(), payload.end(), descriptor.begin())) {
						    throw std::runtime_error("Motion default-mode descriptor does not match its original static operation");
					    }
					    if (!write_mode) {
						    return SquirrelNativeResult::ReturnValue(owner->Services.ReadAsynchronousMode());
					    }
					    if (!owner->Services.WriteAsynchronousMode) {
						    throw std::runtime_error("Motion default-mode write requires its actual containing application state");
					    }

					    // Native043b70 uses225b00/25a420 truth testing at stack2, not222c70 numeric conversion.
					    const auto value = vm.NativeAllocatedStackValue(2);
					    owner->Services.WriteAsynchronousMode(MotionBooleanArgument(value));

					    return SquirrelNativeResult::NoValues();
				    },
				    captures);
				Vm.NewSlot(Class, std::string(name), callback, true);
			}
			const std::array<std::pair<std::string_view, MotionMethod>, 4> sync_methods{{{"skipToSync", MotionMethod::SkipToSync}, {"getSyncWaiting", MotionMethod::GetSyncWaiting}, {"getLoopTime", MotionMethod::GetLoopTime}, {"releaseSyncWait", MotionMethod::ReleaseSyncWait}}};
			for (const auto& [name, method] : sync_methods) {
				std::array<std::uint8_t, 16> descriptor{};
				descriptor[0] = static_cast<std::uint8_t>(method);
				const std::array<SquirrelValue, 1> captures{Vm.CreateUserData(descriptor)};
				const auto callback = Vm.CreateNativeClosureWithCaptures("Motion." + std::string(name), [weak, method, descriptor](SquirrelVm& active, auto arguments) {
					const auto owner = weak.lock();
					if (!owner || &owner->Vm != &active) {
						throw std::runtime_error("Motion sync control owner has expired");
					}
					const auto payload = active.UserDataBytes(active.NativeStackValue(-1));
					if (payload.size() != descriptor.size() || !std::equal(payload.begin(), payload.end(), descriptor.begin())) {
						throw std::runtime_error("Motion sync descriptor differs from its actual typed operation");
					}
					const auto result = owner->Dispatch(method, arguments);

					return method == MotionMethod::GetSyncWaiting || method == MotionMethod::GetLoopTime ? SquirrelNativeResult::ReturnValue(result) : SquirrelNativeResult::NoValues();
				},
				    captures);
				Vm.NewSlot(Class, std::string(name), callback);
			}

			const std::array<std::string_view, 60> unsupported = {{
			    "pause",
			    "setPosition",
			    "getPosition",
			    "getBoundsTop",
			    "getBoundsBottom",
			    "getBoundsLeft",
			    "getBoundsRight",
			    "setFlip",
			    "setFlipX",
			    "setFlipY",
			    "getFlipX",
			    "getFlipY",
			    "setSlant",
			    "setSlantX",
			    "setSlantY",
			    "getSlantX",
			    "getSlantY",
			    "setColorFilter",
			    "setColorWeight",
			    "getColorWeight",
			    "setEmissiveColor",
			    "getEmissiveColor",
			    "setIgnoreAmbient",
			    "getIgnoreAmbient",
			    "setAmbientFactor",
			    "getAmbientFactor",
			    "setMeshDivisionRatio",
			    "getMeshDivisionRatio",
			    "animateCoord",
			    "animatePosition",
			    "animateZoom",
			    "animateSlant",
			    "animateAngleRad",
			    "animateAngleDeg",
			    "animateOpacity",
			    "animateColorFilter",
			    "animateColorWeight",
			    "animateEmissiveColor",
			    "stopAnime",
			    "getAnimating",
			    "getModified",
			    "contains",
			    "getLayerNames",
			    "getCameraAlive",
			    "getCameraOffset",
			    "getCameraPosition",
			    "getCameraTarget",
			    "getCameraFOV",
			    "setIndependentLayerInherit",
			    "getIndependentLayerInherit",
			    "setCoordinate",
			    "getCoordinate",
			    "setDefaultTransformOrder",
			    "getDefaultTransformOrder",
			    "setTransformOrder",
			    "getTransformOrder",
			    "extractScreenSizeFromArchive",
			    "extractStereovisionProfileFromArchive",
			    "modifyRoot",
			    "getAllocedMemorySize",
			}};
			for (const auto name : unsupported) {
				const auto callback = Vm.CreateNativeClosure("Motion." + std::string(name), [weak, name](SquirrelVm&, auto) -> SquirrelNativeResult {
					if (!weak.lock()) {
						throw std::runtime_error("Motion class owner has expired");
					}

					throw std::runtime_error("Motion." + std::string(name) + " requires its actual unrecovered producer or owner");
				});
				const bool is_static = name == "getAllocedMemorySize";
				Vm.NewSlot(Class, std::string(name), callback, is_static);
			}

			InstallConstants();
			Getters = std::make_unique<ScriptMotionLayerGetters>(Vm, Objects, [weak](const auto& layer, const auto& containing, const auto& child) {
				const auto owner = weak.lock();
				if (!owner) {
					throw std::runtime_error("child Motion wrapper factory has retired");
				}

				return owner->Owners->CreateBorrowedChild(layer, containing, child);
			});
			Setters = std::make_unique<ScriptMotionLayerSetters>(Vm, Objects);
			RootCoordinates = std::make_unique<ScriptMotionRootCoordinates>(Vm, Class, *Owners);
			Variables = std::make_unique<ScriptMotionVariables>(Vm, Class, *Owners);
			Vm.NewSlot(Vm.RootTable(), std::string("Motion"), Class);
		}
	};

	ScriptMotionBindings::ScriptMotionBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptMotionBindingServices services)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, std::move(services))) {
		m_Implementation->Install(m_Implementation);
	}

	ScriptMotionBindings::~ScriptMotionBindings() = default;

	SquirrelValue ScriptMotionBindings::MotionClass() const {
		return m_Implementation->Class;
	}

	ScriptMotionNativeOwners& ScriptMotionBindings::NativeOwners() {
		return *m_Implementation->Owners;
	}

	void ScriptMotionBindings::ConfigureAlternateTextures(const SquirrelValue& actual_texture_class, std::function<const scene::NativeTextureReference&(const SquirrelValue&)> resolve_texture) {
		auto& state = *m_Implementation;
		if (state.ResolveNativeTexture || state.Constructing || state.Owners->InstanceCount() != 0 || state.Vm.Type(actual_texture_class) != SquirrelValueType::Class || !resolve_texture) {
			throw std::runtime_error("Motion alternate texture configuration requires its actual class/resolver before wrapper construction");
		}
		auto class_root = state.Vm.Retain(actual_texture_class);
		state.NativeTextureClass = std::move(class_root);
		state.ResolveNativeTexture = std::move(resolve_texture);
	}

} // namespace sgre::script
