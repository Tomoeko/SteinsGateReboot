#include "sgre/script/motion_layer_setter_bindings.hpp"

#include "sgre/scene/motion_scene_evaluation.hpp"
#include "sgre/scene/motion_scene_player.hpp"

#include <array>
#include <cstddef>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace sgre::script {
	static constexpr std::size_t kMaximumMotionLayerSetters = 65536;
	static constexpr float kNativeFullTurnRadians = 6.28318548f;

	enum class LayerControl {
		Visible,
		BranchVisible,
		Coordinate,
		Left,
		Top,
		Flip,
		FlipX,
		FlipY,
		Zoom,
		ZoomX,
		ZoomY,
		Slant,
		SlantX,
		SlantY,
		AngleRadians,
		AngleDegrees,
		Opacity
	};

	static float LayerNumber(const SquirrelValue& value) {
		if (const auto* number = std::get_if<float>(&value)) {
			return *number;
		}
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return static_cast<float>(*integer);
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean ? 1.0f : 0.0f;
		}
		return 0;
	}

	static bool LayerBoolean(const SquirrelValue& value) {
		if (std::holds_alternative<std::monostate>(value)) {
			return false;
		}
		if (std::holds_alternative<float>(value) || std::holds_alternative<std::int32_t>(value) || std::holds_alternative<bool>(value)) {
			return LayerNumber(value) != 0;
		}
		return true;
	}

	static SquirrelValue ReadControl(const scene::MotionLayerOverrides& value, LayerControl control) {
		switch (control) {
		case LayerControl::Visible:
			return value.Enabled;
		case LayerControl::BranchVisible:
			return value.Visible;
		case LayerControl::Left:
			return value.Coordinate[0];
		case LayerControl::Top:
			return value.Coordinate[1];
		case LayerControl::FlipX:
			return value.FlipX;
		case LayerControl::FlipY:
			return value.FlipY;
		case LayerControl::ZoomX:
			return value.ZoomX;
		case LayerControl::ZoomY:
			return value.ZoomY;
		case LayerControl::SlantX:
			return value.ShearX;
		case LayerControl::SlantY:
			return value.ShearY;
		case LayerControl::AngleRadians:
			return value.Angle * kNativeFullTurnRadians / 360.0f;
		case LayerControl::AngleDegrees:
			return value.Angle;
		case LayerControl::Opacity:
			return static_cast<std::int32_t>(value.Opacity);
		default:
			throw std::runtime_error("motion layer control has no native scalar getter");
		}
	}

	static void WriteControl(scene::MotionLayerOverrides& value, LayerControl control, std::span<const SquirrelValue> arguments) {
		const auto number = LayerNumber(arguments[1]);
		switch (control) {
		case LayerControl::Visible:
			value.Enabled = LayerBoolean(arguments[1]);
			break;
		case LayerControl::BranchVisible:
			value.Visible = LayerBoolean(arguments[1]);
			break;
		case LayerControl::Coordinate:
			value.Coordinate[0] = number;
			value.Coordinate[1] = LayerNumber(arguments[2]);
			break;
		case LayerControl::Left:
			value.Coordinate[0] = number;
			break;
		case LayerControl::Top:
			value.Coordinate[1] = number;
			break;
		case LayerControl::Flip:
			value.FlipX = LayerBoolean(arguments[1]);
			value.FlipY = LayerBoolean(arguments[2]);
			break;
		case LayerControl::FlipX:
			value.FlipX = LayerBoolean(arguments[1]);
			break;
		case LayerControl::FlipY:
			value.FlipY = LayerBoolean(arguments[1]);
			break;
		case LayerControl::Zoom:
			value.ZoomX = number;
			value.ZoomY = LayerNumber(arguments[2]);
			break;
		case LayerControl::ZoomX:
			value.ZoomX = number;
			break;
		case LayerControl::ZoomY:
			value.ZoomY = number;
			break;
		case LayerControl::Slant:
			value.ShearX = number;
			value.ShearY = LayerNumber(arguments[2]);
			break;
		case LayerControl::SlantX:
			value.ShearX = number;
			break;
		case LayerControl::SlantY:
			value.ShearY = number;
			break;
		case LayerControl::AngleRadians:
			value.Angle = number * 360.0f / kNativeFullTurnRadians;
			break;
		case LayerControl::AngleDegrees:
			value.Angle = number;
			break;
		case LayerControl::Opacity:
			value.Opacity = static_cast<std::uint32_t>(TryNativeInteger(arguments[1]).value_or(0));
			break;
		}
	}

	class ScriptMotionLayerSetters::Implementation {
	public:
		struct Record {
			SquirrelObjectIdentity Instance;
			scene::MotionPlayerTask Task;
			scene::MotionNumericBorrow Numeric;
			std::optional<scene::MotionChildBorrow> Child;
		};

		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects) : Vm(vm), Objects(objects) {
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::size_t, Record> Records;

		static const scene::MotionSceneEvaluation& ActualScene(const scene::MotionPlayerTask& task, const std::optional<scene::MotionChildBorrow>& child) {
			if (!task.IsLive()) {
				throw std::runtime_error("MotionLayerSetter actual task has retired");
			}
			if (child) {
				static_cast<void>(task.ChildCommands(*child));
			}
			const auto* player = task.GenericPlayer();
			if (player == nullptr || player->Scene() == nullptr) {
				throw std::runtime_error("MotionLayerSetter requires its actual selected scene");
			}
			return *player->Scene();
		}

		Record View(const SquirrelValue& instance) const {
			if (!Vm.IsInstanceOf(instance, Class) || !Objects.HasInstance(instance)) {
				throw std::runtime_error("MotionLayerSetter requires its genuine native instance");
			}
			const auto identity = Vm.BorrowObjectIdentity(instance);
			const auto found = Records.find(identity.Index);
			if (found == Records.end() || found->second.Instance != identity) {
				throw std::runtime_error("MotionLayerSetter receiver has retired or changed generation");
			}
			return found->second;
		}

		SquirrelValue Create(const scene::MotionPlayerTask& task, std::string_view name, std::optional<scene::MotionChildBorrow> child, const std::weak_ptr<Implementation>& weak) {
			if (child) {
				static_cast<void>(task.ChildCommands(*child));
			}
			if (task.IsLive() && task.GenericPlayer() != nullptr && task.GenericPlayer()->Scene() == nullptr) {
				return {};
			}
			const auto& actual = ActualScene(task, child);
			const auto layer = child ? actual.FindLayer(name, task.ChildCommands(*child)) : actual.FindLayer(name);
			if (!layer) {
				return {};
			}
			if (Records.size() >= kMaximumMotionLayerSetters) {
				throw std::runtime_error("Motion layer setters exceed their native instance capacity");
			}
			auto mutable_task = task;
			mutable_task.SetLayerOverrides(*layer, actual.LayerOverrides(*layer), true);
			const auto instance = Vm.CreateInstance(Class);
			const auto root = Vm.Retain(instance);
			Objects.AttachInstance(instance);
			const auto identity = Vm.BorrowObjectIdentity(instance);
			try {
				Records.emplace(identity.Index, Record{identity, task, actual.BorrowNumeric(*layer), child});
				Objects.ConfigureNativeRelease(instance, [weak, identity](const SquirrelValue&) {
					if (const auto owner = weak.lock()) {
						const auto found = owner->Records.find(identity.Index);
						if (found != owner->Records.end() && found->second.Instance == identity) {
							owner->Records.erase(found);
						}
					}
				});
			} catch (...) {
				Records.erase(identity.Index);
				Objects.ReleaseInstance(instance);
				throw;
			}
			return instance;
		}

		void Install(const std::shared_ptr<Implementation>& self) {
			if (Vm.HasSlot(Vm.RootTable(), std::string("MotionLayerSetter"))) {
				throw std::runtime_error("MotionLayerSetter class is already registered");
			}
			Class = Vm.CreateClass(Objects.ObjectClass());
			ClassRoot = Vm.Retain(Class);
			const auto weak = std::weak_ptr<Implementation>(self);
			Vm.NewSlot(Class, std::string("constructor"), Vm.CreateNativeClosure("MotionLayerSetter.constructor", [](SquirrelVm&, std::span<const SquirrelValue>) -> SquirrelNativeResult {
				throw std::runtime_error("MotionLayerSetter has no public native constructor");
			}));
			const std::array<std::pair<std::string_view, LayerControl>, 17> setters{{
			    {"Visible", LayerControl::Visible},
			    {"BranchVisible", LayerControl::BranchVisible},
			    {"Coord", LayerControl::Coordinate},
			    {"Left", LayerControl::Left},
			    {"Top", LayerControl::Top},
			    {"Flip", LayerControl::Flip},
			    {"FlipX", LayerControl::FlipX},
			    {"FlipY", LayerControl::FlipY},
			    {"Zoom", LayerControl::Zoom},
			    {"ZoomX", LayerControl::ZoomX},
			    {"ZoomY", LayerControl::ZoomY},
			    {"Slant", LayerControl::Slant},
			    {"SlantX", LayerControl::SlantX},
			    {"SlantY", LayerControl::SlantY},
			    {"AngleRad", LayerControl::AngleRadians},
			    {"AngleDeg", LayerControl::AngleDegrees},
			    {"Opacity", LayerControl::Opacity},
			}};
			for (const auto& [name, control] : setters) {
				const bool paired = control == LayerControl::Coordinate || control == LayerControl::Flip || control == LayerControl::Zoom || control == LayerControl::Slant;
				Vm.NewSlot(Class, "set" + std::string(name), Vm.CreateNativeClosure("MotionLayerSetter.set" + std::string(name), [weak, control, paired](SquirrelVm&, std::span<const SquirrelValue> arguments) {
					const auto owner = weak.lock();
					if (!owner || arguments.size() != (paired ? 3 : 2)) {
						throw std::runtime_error("MotionLayerSetter binding owner retired or arguments are invalid");
					}
					auto record = owner->View(arguments[0]);
					const auto& actual = ActualScene(record.Task, record.Child);
					const auto current = actual.ResolveNumeric(record.Numeric);
					auto value = actual.LayerOverrides(current);
					WriteControl(value, control, arguments);
					record.Task.SetLayerOverrides(current, value);

					return SquirrelNativeResult::NoValues();
				}));
				if (!paired) {
					Vm.NewSlot(Class, "get" + std::string(name), Vm.CreateNativeClosure("MotionLayerSetter.get" + std::string(name), [weak, control](SquirrelVm&, std::span<const SquirrelValue> arguments) {
						const auto owner = weak.lock();
						if (!owner || arguments.empty()) {
							throw std::runtime_error("MotionLayerSetter binding owner retired or receiver is missing");
						}
						const auto record = owner->View(arguments[0]);
						const auto& actual = ActualScene(record.Task, record.Child);
						const auto value = actual.LayerOverrides(actual.ResolveNumeric(record.Numeric));

						return SquirrelNativeResult::ReturnValue(ReadControl(value, control));
					}));
				}
			}
			Vm.NewSlot(Vm.RootTable(), std::string("MotionLayerSetter"), Class);
		}
	};

	ScriptMotionLayerSetters::ScriptMotionLayerSetters(SquirrelVm& vm, ScriptObjectBindings& objects) : m_Implementation(std::make_shared<Implementation>(vm, objects)) {
		m_Implementation->Install(m_Implementation);
	}

	ScriptMotionLayerSetters::~ScriptMotionLayerSetters() = default;

	SquirrelValue ScriptMotionLayerSetters::SetterClass() const {
		return m_Implementation->Class;
	}

	SquirrelValue ScriptMotionLayerSetters::GetLayerSetter(const scene::MotionPlayerTask& task, std::string_view name, std::optional<scene::MotionChildBorrow> actual_child) {
		return m_Implementation->Create(task, name.substr(0, name.find('\0')), actual_child, m_Implementation);
	}
} // namespace sgre::script
