#include "sgre/script/motion_root_coordinate_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::script {
	enum class MotionRootCoordinateMethod {
		SetCoordinate,
		SetCoordinate3,
		SetLeft,
		GetLeft,
		SetTop,
		GetTop,
		SetDepth,
		GetDepth,
		SetZoom,
		SetZoomX,
		SetZoomY,
		GetZoomX,
		GetZoomY,
		SetAngleDegrees,
		SetAngleRadians,
		GetAngleDegrees,
		GetAngleRadians,
		SetOpacity,
		GetOpacity
	};

	static float RootCoordinateNumber(SquirrelVm& vm, std::int32_t index) {
		// Native222e90 indexes real allocated cells, including captured userdata and retained cells beyond logical top.
		const auto value = vm.NativeAllocatedStackValue(index);
		if (const auto* real = std::get_if<float>(&value)) {
			return *real;
		}
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return static_cast<float>(*integer);
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean ? 1.0f : 0.0f;
		}

		return 0;
	}

	class ScriptMotionRootCoordinates::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptMotionNativeOwners& owners)
		    : Vm(vm), Owners(owners) {
		}

		SquirrelVm& Vm;
		ScriptMotionNativeOwners& Owners;

		SquirrelNativeResult Dispatch(MotionRootCoordinateMethod method, std::span<const SquirrelValue> arguments) {
			if (arguments.empty() || Vm.Type(arguments[0]) != SquirrelValueType::Instance || !Owners.Contains(arguments[0])) {
				throw std::runtime_error("bad instance");
			}
			const auto& instance = arguments[0];
			const bool detached = Owners.IsDetached(instance);
			if (method == MotionRootCoordinateMethod::GetOpacity) {
				return SquirrelNativeResult::ReturnValue(detached ? std::int32_t{255} : std::bit_cast<std::int32_t>(Owners.Root(instance).Overrides().Opacity));
			}
			if (method == MotionRootCoordinateMethod::SetOpacity) {
				// Native056cb0 initializes zero and converts fixed physical2 before testing the detached target.
				const auto value = TryNativeInteger(Vm.NativeAllocatedStackValue(2)).value_or(0);
				if (!detached) {
					Owners.SetOpacity(instance, std::bit_cast<std::uint32_t>(value));
				}

				return SquirrelNativeResult::NoValues();
			}
			if (method == MotionRootCoordinateMethod::GetLeft || method == MotionRootCoordinateMethod::GetTop || method == MotionRootCoordinateMethod::GetDepth) {
				const std::size_t index = method == MotionRootCoordinateMethod::GetLeft ? 0 : (method == MotionRootCoordinateMethod::GetTop ? 1 : 2);

				return SquirrelNativeResult::ReturnValue(detached ? 0.0f : Owners.Root(instance).Overrides().Coordinate[index]);
			}

			if (method == MotionRootCoordinateMethod::GetAngleDegrees || method == MotionRootCoordinateMethod::GetAngleRadians) {
				if (detached) {
					return SquirrelNativeResult::ReturnValue(0.0f);
				}
				const auto& root = Owners.Root(instance);

				return SquirrelNativeResult::ReturnValue(method == MotionRootCoordinateMethod::GetAngleDegrees ? root.AngleDegrees() : root.AngleRadians());
			}
			if (method == MotionRootCoordinateMethod::SetAngleDegrees || method == MotionRootCoordinateMethod::SetAngleRadians) {
				const auto value = RootCoordinateNumber(Vm, 2);
				if (!detached) {
					if (method == MotionRootCoordinateMethod::SetAngleDegrees) {
						Owners.SetAngleDegrees(instance, value);
					} else {
						Owners.SetAngleRadians(instance, value);
					}
				}

				return SquirrelNativeResult::NoValues();
			}

			if (method == MotionRootCoordinateMethod::GetZoomX || method == MotionRootCoordinateMethod::GetZoomY) {
				if (detached) {
					return SquirrelNativeResult::ReturnValue(0.0f);
				}
				const auto& root = Owners.Root(instance).Overrides();
				return SquirrelNativeResult::ReturnValue(method == MotionRootCoordinateMethod::GetZoomX ? root.ZoomX : root.ZoomY);
			}
			if (method == MotionRootCoordinateMethod::SetZoom || method == MotionRootCoordinateMethod::SetZoomX || method == MotionRootCoordinateMethod::SetZoomY) {
				// 22b6d0 reads physical3 then2; short calls can read the actual inactive allocated cell.
				const auto vertical = method == MotionRootCoordinateMethod::SetZoom ? RootCoordinateNumber(Vm, 3) : 0.0f;
				const auto horizontal = RootCoordinateNumber(Vm, 2);
				if (detached) {
					return SquirrelNativeResult::NoValues();
				}
				if (method == MotionRootCoordinateMethod::SetZoom) {
					Owners.SetZoom(instance, horizontal, vertical);
				} else {
					const auto& root = Owners.Root(instance).Overrides();
					Owners.SetZoom(instance, method == MotionRootCoordinateMethod::SetZoomX ? horizontal : root.ZoomX, method == MotionRootCoordinateMethod::SetZoomY ? horizontal : root.ZoomY);
				}
				return SquirrelNativeResult::NoValues();
			}

			// The registered typed bridges accept extra operands, convert fixed numeric slots, then test the detached target.
			const auto z = method == MotionRootCoordinateMethod::SetCoordinate3 ? RootCoordinateNumber(Vm, 4) : 0.0f;
			const auto y = method == MotionRootCoordinateMethod::SetCoordinate || method == MotionRootCoordinateMethod::SetCoordinate3 ? RootCoordinateNumber(Vm, 3) : 0.0f;
			const auto x = RootCoordinateNumber(Vm, 2);
			if (detached) {
				return SquirrelNativeResult::NoValues();
			}
			if (method == MotionRootCoordinateMethod::SetCoordinate) {
				Owners.SetCoordinate(instance, {x, y});

				return SquirrelNativeResult::NoValues();
			}

			auto task = Owners.Task(instance);
			switch (method) {
			case MotionRootCoordinateMethod::SetCoordinate3:
				task.SetCoordinate3({x, y, z});
				break;
			case MotionRootCoordinateMethod::SetLeft:
				task.SetLeft(x);
				break;
			case MotionRootCoordinateMethod::SetTop:
				task.SetTop(x);
				break;
			case MotionRootCoordinateMethod::SetDepth:
				task.SetDepth(x);
				break;
			default:
				throw std::runtime_error("invalid Motion coordinate operation");
			}

			return SquirrelNativeResult::NoValues();
		}
	};

	ScriptMotionRootCoordinates::ScriptMotionRootCoordinates(SquirrelVm& vm, const SquirrelValue& actual_motion_class, ScriptMotionNativeOwners& actual_owners)
	    : m_Implementation(std::make_shared<Implementation>(vm, actual_owners)) {
		if (vm.Type(actual_motion_class) != SquirrelValueType::Class) {
			throw std::runtime_error("Motion root coordinates require the actual registered class");
		}
		const std::array<std::pair<std::string_view, MotionRootCoordinateMethod>, 19> methods{{{"setCoord", MotionRootCoordinateMethod::SetCoordinate},
		    {"setCoord3", MotionRootCoordinateMethod::SetCoordinate3},
		    {"setLeft", MotionRootCoordinateMethod::SetLeft},
		    {"getLeft", MotionRootCoordinateMethod::GetLeft},
		    {"setTop", MotionRootCoordinateMethod::SetTop},
		    {"getTop", MotionRootCoordinateMethod::GetTop},
		    {"setZ", MotionRootCoordinateMethod::SetDepth},
		    {"getZ", MotionRootCoordinateMethod::GetDepth},
		    {"setZoom", MotionRootCoordinateMethod::SetZoom},
		    {"setZoomX", MotionRootCoordinateMethod::SetZoomX},
		    {"setZoomY", MotionRootCoordinateMethod::SetZoomY},
		    {"getZoomX", MotionRootCoordinateMethod::GetZoomX},
		    {"getZoomY", MotionRootCoordinateMethod::GetZoomY},
		    {"setAngleDeg", MotionRootCoordinateMethod::SetAngleDegrees},
		    {"setAngleRad", MotionRootCoordinateMethod::SetAngleRadians},
		    {"getAngleDeg", MotionRootCoordinateMethod::GetAngleDegrees},
		    {"getAngleRad", MotionRootCoordinateMethod::GetAngleRadians},
		    {"setOpacity", MotionRootCoordinateMethod::SetOpacity},
		    {"getOpacity", MotionRootCoordinateMethod::GetOpacity}}};
		const std::weak_ptr<Implementation> weak = m_Implementation;
		for (const auto& [name, method] : methods) {
			// Native typed bridges each carry a 16-byte member descriptor as their final actual captured userdata.
			std::array<std::uint8_t, 16> descriptor{};
			descriptor[0] = static_cast<std::uint8_t>(method);
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(descriptor)};
			const auto callback = vm.CreateNativeClosureWithCaptures(
			    "Motion." + std::string(name), [weak, method, descriptor](SquirrelVm& active, auto arguments) {
				    const auto owner = weak.lock();
				    if (!owner) {
					    throw std::runtime_error("Motion root coordinate owner has expired");
				    }

				    const auto payload = active.UserDataBytes(active.NativeStackValue(-1));
				    if (&active != &owner->Vm || payload.size() != descriptor.size() || !std::equal(payload.begin(), payload.end(), descriptor.begin())) {
					    throw std::runtime_error("Motion coordinate descriptor does not match its actual typed operation");
				    }

				    return owner->Dispatch(method, arguments);
			    },
			    captures);
			vm.NewSlot(actual_motion_class, std::string(name), callback);
		}
	}

	ScriptMotionRootCoordinates::~ScriptMotionRootCoordinates() = default;
} // namespace sgre::script
