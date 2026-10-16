#include "sgre/script/motion_layer_getter_bindings.hpp"

#include "sgre/scene/motion_scene_evaluation.hpp"
#include "sgre/scene/motion_scene_player.hpp"
#include "sgre/scene/motion_shape_owner.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace sgre::script {
	static constexpr std::size_t kMaximumMotionLayerGetters = 65536;

	static float ShapeNumber(const SquirrelValue& value) {
		if (const auto* number = std::get_if<float>(&value)) {
			return *number;
		}
		if (const auto* number = std::get_if<std::int32_t>(&value)) {
			return static_cast<float>(*number);
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean ? 1.0f : 0.0f;
		}

		// Native272460 initializes each float to zero before the fallible sq_getfloat conversion.
		return 0;
	}

	class ScriptMotionLayerGetters::Implementation {
	public:
		struct GetterRecord {
			SquirrelObjectIdentity Instance;
			scene::MotionPlayerTask Task;
			scene::MotionSceneLayerReference Layer;
			std::optional<SquirrelObjectIdentity> OwningLayer;
			std::optional<scene::MotionChildBorrow> Child;
		};

		struct ShapeRecord {
			SquirrelObjectIdentity Instance;
			// Object's native delegate is weak; this concrete shape owns the immutable field dictionary.
			SquirrelHostRoot Fields;
			scene::MotionShapeSnapshot Snapshot;
		};

		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ChildMotionFactory child_motion)
		    : Vm(vm), Objects(objects), MakeChildMotion(std::move(child_motion)) {
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ChildMotionFactory MakeChildMotion;
		SquirrelValue GetterClass;
		SquirrelValue ShapeClass;
		SquirrelHostRoot GetterClassRoot;
		SquirrelHostRoot ShapeClassRoot;
		std::map<std::uint32_t, GetterRecord> Getters;
		std::map<std::uint32_t, ShapeRecord> Shapes;

		static const scene::MotionSceneEvaluation& ActualScene(const scene::MotionPlayerTask& task, const std::optional<scene::MotionChildBorrow>& actual_child = {}) {
			if (!task.IsLive()) {
				throw std::runtime_error("MotionLayerGetter actual task has retired");
			}
			if (actual_child) {
				static_cast<void>(task.ChildCommands(*actual_child));
			}
			const auto* player = task.GenericPlayer();
			if (player == nullptr) {
				throw std::runtime_error("MotionLayerGetter requires its actual generic selected scene owner");
			}
			const auto* scene = player->Scene();
			if (scene == nullptr) {
				throw std::runtime_error("MotionLayerGetter actual task has no selected scene");
			}

			return *scene;
		}

		const GetterRecord& Getter(const SquirrelValue& instance) const {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("MotionLayerGetter requires its genuine native instance");
			}
			const auto found = Getters.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Getters.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance) || !Objects.HasInstance(instance)) {
				throw std::runtime_error("MotionLayerGetter native receiver has retired or changed generation");
			}

			return found->second;
		}

		const ShapeRecord& Shape(const SquirrelValue& instance) const {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("MotionShapeContains requires its genuine native instance");
			}
			const auto found = Shapes.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Shapes.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance) || !Objects.HasInstance(instance)) {
				throw std::runtime_error("MotionShapeContains native receiver has retired or changed generation");
			}

			return found->second;
		}

		void WriteBounds(const SquirrelValue& fields, const std::array<float, 4>& bounds) {
			Vm.NewSlot(fields, std::string("left"), bounds[0]);
			Vm.NewSlot(fields, std::string("top"), bounds[1]);
			Vm.NewSlot(fields, std::string("right"), bounds[2]);
			Vm.NewSlot(fields, std::string("bottom"), bounds[3]);
			Vm.NewSlot(fields, std::string("width"), bounds[2] - bounds[0]);
			Vm.NewSlot(fields, std::string("height"), bounds[3] - bounds[1]);
		}

		SquirrelValue ShapeFields(const scene::MotionShapeSnapshot& shape) {
			const auto fields = Vm.CreateTable();
			const auto fields_root = Vm.Retain(fields);
			Vm.NewSlot(fields, std::string("type"), static_cast<std::int32_t>(shape.Type));
			const auto& values = shape.Values;
			if (shape.Type == 0 || shape.Type == 1) {
				Vm.NewSlot(fields, std::string("x"), values[0]);
				Vm.NewSlot(fields, std::string("y"), values[1]);
				if (shape.Type == 1) {
					Vm.NewSlot(fields, std::string("r"), values[2]);
				}
			} else if (shape.Type == 2) {
				WriteBounds(fields, {values[0], values[1], values[2], values[3]});
			} else if (shape.Type == 3) {
				const auto points = Vm.CreateArray();
				const auto points_root = Vm.Retain(points);
				std::array<float, 4> bounds{values[0], values[1], values[0], values[1]};
				for (std::size_t index = 0; index != 4; ++index) {
					const auto point = Vm.CreateTable();
					const auto point_root = Vm.Retain(point);
					const auto x = values[index * 2];
					const auto y = values[index * 2 + 1];
					Vm.NewSlot(point, std::string("x"), x);
					Vm.NewSlot(point, std::string("y"), y);
					Vm.AppendArray(points, point);
					// Native277480 updates a maximum only in the else arm of its minimum comparison.
					if (!(bounds[0] <= x)) {
						bounds[0] = x;
					} else if (!(x <= bounds[2])) {
						bounds[2] = x;
					}
					if (!(bounds[1] <= y)) {
						bounds[1] = y;
					} else if (!(y <= bounds[3])) {
						bounds[3] = y;
					}
				}
				Vm.NewSlot(fields, std::string("p"), points);
				WriteBounds(fields, bounds);
			}

			return fields;
		}

		SquirrelValue Snapshot(const scene::MotionShapeSnapshot& shape, const std::weak_ptr<Implementation>& weak) {
			if (Shapes.size() == kMaximumMotionLayerGetters) {
				throw std::runtime_error("Motion shape snapshots exceed their bounded native instance capacity");
			}
			const auto fields = ShapeFields(shape);
			auto fields_root = Vm.Retain(fields);
			const auto instance = Vm.CreateInstance(ShapeClass);
			const auto instance_root = Vm.Retain(instance);
			Objects.AttachInstance(instance, fields);
			const auto identity = Vm.BorrowObjectIdentity(instance);
			try {
				Shapes.emplace(identity.Index, ShapeRecord{identity, std::move(fields_root), shape});
				Objects.ConfigureNativeRelease(instance, [weak, identity](const SquirrelValue&) {
					if (const auto owner = weak.lock()) {
						const auto found = owner->Shapes.find(identity.Index);
						if (found != owner->Shapes.end() && found->second.Instance == identity) {
							owner->Shapes.erase(found);
						}
					}
				});
			} catch (...) {
				Shapes.erase(identity.Index);
				Objects.ReleaseInstance(instance);
				throw;
			}

			return instance;
		}

		SquirrelValue CreateGetter(const scene::MotionPlayerTask& task, std::string_view name, std::optional<SquirrelObjectIdentity> actual_layer, std::optional<scene::MotionChildBorrow> actual_child, const std::weak_ptr<Implementation>& weak) {
			if (actual_child) {
				static_cast<void>(task.ChildCommands(*actual_child));
			}
			if (task.IsLive() && task.GenericPlayer() != nullptr && task.GenericPlayer()->Scene() == nullptr) {
				return {};
			}
			const auto& actual = ActualScene(task, actual_child);
			const auto layer = actual_child ? actual.FindLayer(name, task.ChildCommands(*actual_child)) : actual.FindLayer(name);
			if (!layer) {
				return {};
			}
			return WrapGetter(task, *layer, actual_layer, actual_child, weak);
		}

		SquirrelValue WrapGetter(const scene::MotionPlayerTask& task, scene::MotionSceneLayerReference layer, std::optional<SquirrelObjectIdentity> actual_layer, std::optional<scene::MotionChildBorrow> actual_child, const std::weak_ptr<Implementation>& weak) {
			if (Getters.size() == kMaximumMotionLayerGetters) {
				throw std::runtime_error("Motion layer getters exceed their bounded native instance capacity");
			}
			const auto instance = Vm.CreateInstance(GetterClass);
			const auto instance_root = Vm.Retain(instance);
			Objects.AttachInstance(instance);
			const auto identity = Vm.BorrowObjectIdentity(instance);
			try {
				Getters.emplace(identity.Index, GetterRecord{identity, task, layer, actual_layer, actual_child});
				Objects.ConfigureNativeRelease(instance, [weak, identity](const SquirrelValue&) {
					if (const auto owner = weak.lock()) {
						const auto found = owner->Getters.find(identity.Index);
						if (found != owner->Getters.end() && found->second.Instance == identity) {
							owner->Getters.erase(found);
						}
					}
				});
			} catch (...) {
				Getters.erase(identity.Index);
				Objects.ReleaseInstance(instance);
				throw;
			}

			return instance;
		}

		SquirrelValue CreateLayerMotion(const scene::MotionPlayerTask& task, std::string_view name, const SquirrelObjectIdentity& actual_layer, const std::optional<scene::MotionChildBorrow>& actual_child) {
			if (actual_child) {
				static_cast<void>(task.ChildCommands(*actual_child));
			}
			if (task.IsLive() && task.GenericPlayer() != nullptr && task.GenericPlayer()->Scene() == nullptr) {
				return {};
			}
			const auto& actual = ActualScene(task, actual_child);
			const auto layer = actual_child ? actual.FindLayer(name, task.ChildCommands(*actual_child)) : actual.FindLayer(name);
			// Native2769A0 returns null for a missing name or any kind other than an actual child player.
			if (!layer || actual.LayerType(*layer) != 3) {
				return {};
			}
			if (!MakeChildMotion) {
				throw std::runtime_error("Motion.getLayerMotion requires its actual Layer-backed native wrapper factory");
			}
			const auto child = actual.LayerChild(*layer).Borrow();

			return MakeChildMotion(actual_layer, task, child);
		}

		void Install(const std::shared_ptr<Implementation>& self) {
			if (Vm.HasSlot(Vm.RootTable(), std::string("MotionLayerGetter")) || Vm.HasSlot(Vm.RootTable(), std::string("MotionShapeContains"))) {
				throw std::runtime_error("Motion layer getter classes are already registered");
			}
			GetterClass = Vm.CreateClass(Objects.ObjectClass());
			GetterClassRoot = Vm.Retain(GetterClass);
			ShapeClass = Vm.CreateClass(Objects.ObjectClass());
			ShapeClassRoot = Vm.Retain(ShapeClass);
			const auto weak = std::weak_ptr<Implementation>(self);
			for (const auto& type : {GetterClass, ShapeClass}) {
				Vm.NewSlot(type, std::string("constructor"), Vm.CreateNativeClosure("Motion borrowed-view constructor", [](SquirrelVm&, std::span<const SquirrelValue>) -> SquirrelNativeResult {
					throw std::runtime_error("MotionLayerGetter and MotionShapeContains have no public native constructor");
				}));
			}
			Vm.NewSlot(GetterClass, std::string("getShape"), Vm.CreateNativeClosure("MotionLayerGetter.getShape", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("MotionLayerGetter binding owner has retired or lacks its receiver");
				}
				const auto receiver = arguments[0];
				const auto& record = owner->Getter(receiver);
				const auto snapshot = ActualScene(record.Task, record.Child).Shape(record.Layer);

				return SquirrelNativeResult::ReturnValue(owner->Snapshot(snapshot, weak));
			}));
			Vm.NewSlot(GetterClass, std::string("getType"), Vm.CreateNativeClosure("MotionLayerGetter.getType", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("MotionLayerGetter binding owner has retired or lacks its receiver");
				}
				const auto receiver = arguments[0];
				const auto& record = owner->Getter(receiver);

				return SquirrelNativeResult::ReturnValue(static_cast<std::int32_t>(ActualScene(record.Task, record.Child).LayerType(record.Layer)));
			}));
			const std::array<std::string_view, 2> coordinate_methods{"getLeft", "getTop"};
			for (std::size_t axis = 0; axis != coordinate_methods.size(); ++axis) {
				const auto method = coordinate_methods[axis];
				Vm.NewSlot(GetterClass, std::string(method), Vm.CreateNativeClosure("MotionLayerGetter." + std::string(method), [weak, axis](SquirrelVm&, std::span<const SquirrelValue> arguments) {
					const auto owner = weak.lock();
					if (!owner || arguments.empty()) {
						throw std::runtime_error("MotionLayerGetter binding owner has retired or lacks its receiver");
					}
					const auto receiver = arguments[0];
					const auto& record = owner->Getter(receiver);
					const auto coordinate = ActualScene(record.Task, record.Child).LayerCoordinate(record.Layer);

					return SquirrelNativeResult::ReturnValue(coordinate[axis]);
				}));
			}
			Vm.NewSlot(GetterClass, std::string("contains"), Vm.CreateNativeClosure("MotionLayerGetter.contains", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("MotionLayerGetter binding owner has retired or lacks its receiver");
				}
				const auto& record = owner->Getter(arguments[0]);
				const resource::MotionPoint point{arguments.size() > 1 ? ShapeNumber(arguments[1]) : 0, arguments.size() > 2 ? ShapeNumber(arguments[2]) : 0};

				return SquirrelNativeResult::ReturnValue(ActualScene(record.Task, record.Child).Contains(record.Layer, point));
			}));
			Vm.NewSlot(ShapeClass, std::string("contains"), Vm.CreateNativeClosure("MotionShapeContains.contains", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("MotionShapeContains binding owner has retired or lacks its receiver");
				}
				const auto receiver = arguments[0];
				const resource::MotionPoint point{arguments.size() > 1 ? ShapeNumber(arguments[1]) : 0, arguments.size() > 2 ? ShapeNumber(arguments[2]) : 0};

				return SquirrelNativeResult::ReturnValue(owner->Shape(receiver).Snapshot.Contains(point));
			}));
			Vm.NewSlot(GetterClass, std::string("getMotion"), Vm.CreateNativeClosure("MotionLayerGetter.getMotion", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("MotionLayerGetter binding owner has retired or lacks its receiver");
				}
				const auto record = owner->Getter(arguments[0]);
				if (!owner->MakeChildMotion || !record.OwningLayer) {
					throw std::runtime_error("MotionLayerGetter.getMotion requires its actual Layer-backed native wrapper factory");
				}
				const auto& actual = ActualScene(record.Task, record.Child);
				const auto child = actual.LayerChild(record.Layer).Borrow();

				return SquirrelNativeResult::ReturnValue(owner->MakeChildMotion(*record.OwningLayer, record.Task, child));
			}));
			Vm.NewSlot(GetterClass, std::string("getChildren"), Vm.CreateNativeClosure("MotionLayerGetter.getChildren", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("MotionLayerGetter binding owner has retired or lacks its receiver");
				}
				const auto record = owner->Getter(arguments[0]);
				const auto children = ActualScene(record.Task, record.Child).LayerChildren(record.Layer);
				const auto result = owner->Vm.CreateArray();
				const auto result_root = owner->Vm.Retain(result);
				for (const auto& child : children) {
					owner->Vm.AppendArray(result, owner->WrapGetter(record.Task, child, record.OwningLayer, record.Child, weak));
				}
				return SquirrelNativeResult::ReturnValue(result);
			}));
			static constexpr std::array<std::string_view, 3> visibility_methods{"getVisible", "getBranchVisible", "getLayerVisible"};
			for (std::size_t index = 0; index != visibility_methods.size(); ++index) {
				const auto method = visibility_methods[index];
				Vm.NewSlot(GetterClass, std::string(method), Vm.CreateNativeClosure("MotionLayerGetter." + std::string(method), [weak, index](SquirrelVm&, std::span<const SquirrelValue> arguments) {
					const auto owner = weak.lock();
					if (!owner || arguments.empty()) {
						throw std::runtime_error("MotionLayerGetter binding owner has retired or lacks its receiver");
					}
					const auto& record = owner->Getter(arguments[0]);
					const auto state = ActualScene(record.Task, record.Child).LayerVisibility(record.Layer);
					const std::array<bool, 3> values{state.Visible, state.BranchVisible, state.Visible && state.BranchVisible};

					return SquirrelNativeResult::ReturnValue(values[index]);
				}));
			}
			static constexpr std::array<std::string_view, 11> unsupported{"getFlipX", "getFlipY", "getZoomX", "getZoomY", "getSlantX", "getSlantY", "getAngleRad", "getAngleDeg", "getOpacity", "getPosition", "containsArea"};
			for (const auto method : unsupported) {
				Vm.NewSlot(GetterClass, std::string(method), Vm.CreateNativeClosure("MotionLayerGetter." + std::string(method), [weak, method](SquirrelVm&, std::span<const SquirrelValue> arguments) -> SquirrelNativeResult {
					const auto owner = weak.lock();
					if (!owner || arguments.empty()) {
						throw std::runtime_error("MotionLayerGetter binding owner has retired or lacks its receiver");
					}
					static_cast<void>(owner->Getter(arguments[0]));
					throw std::runtime_error("MotionLayerGetter." + std::string(method) + " requires its actual unrecovered native producer");
				}));
			}
			Vm.NewSlot(Vm.RootTable(), std::string("MotionLayerGetter"), GetterClass);
			Vm.NewSlot(Vm.RootTable(), std::string("MotionShapeContains"), ShapeClass);
		}
	};

	ScriptMotionLayerGetters::ScriptMotionLayerGetters(SquirrelVm& vm, ScriptObjectBindings& objects, ChildMotionFactory child_motion)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, std::move(child_motion))) {
		m_Implementation->Install(m_Implementation);
	}

	ScriptMotionLayerGetters::~ScriptMotionLayerGetters() = default;

	SquirrelValue ScriptMotionLayerGetters::GetterClass() const {
		return m_Implementation->GetterClass;
	}

	SquirrelValue ScriptMotionLayerGetters::ShapeClass() const {
		return m_Implementation->ShapeClass;
	}

	SquirrelValue ScriptMotionLayerGetters::GetLayerMotion(const scene::MotionPlayerTask& task, std::string_view name, const SquirrelObjectIdentity& actual_layer, std::optional<scene::MotionChildBorrow> actual_child) {
		return m_Implementation->CreateLayerMotion(task, name, actual_layer, actual_child);
	}

	SquirrelValue ScriptMotionLayerGetters::GetLayerGetter(const scene::MotionPlayerTask& task, std::string_view name, std::optional<SquirrelObjectIdentity> actual_layer, std::optional<scene::MotionChildBorrow> actual_child) {
		return m_Implementation->CreateGetter(task, name, actual_layer, actual_child, m_Implementation);
	}
} // namespace sgre::script
