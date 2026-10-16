#include "sgre/script/affine_matrix_bindings.hpp"

#include "sgre/scene/affine_matrix_geometry.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	enum class AffineMatrixMethod : std::uint32_t {
		Identity,
		Copy,
		Reset,
		Perspective,
		Translate,
		IntegerTranslate,
		FlipX,
		FlipY,
		Rotate,
		Scale,
		Slant,
		Multiply,
		TransformX,
		TransformY,
		AreaX,
		AreaY,
		Print
	};

	struct AffineMatrixBindingRecord {
		SquirrelObjectIdentity Instance;
		scene::AffineMatrix Matrix;
	};

	static float ReadAffineMatrixFloat(SquirrelVm& vm, std::int32_t index) {
		const auto value = vm.NativeAllocatedStackValue(index);
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

	static SquirrelValue AffineMatrixMethodCapture(SquirrelVm& vm, AffineMatrixMethod method) {
		const auto id = static_cast<std::uint32_t>(method);
		const std::array<std::uint8_t, 8> payload{1, 0, 0, 0, static_cast<std::uint8_t>(id), static_cast<std::uint8_t>(id >> 8), static_cast<std::uint8_t>(id >> 16), static_cast<std::uint8_t>(id >> 24)};

		return vm.CreateUserData(payload);
	}

	static void RequireAffineMatrixCapture(SquirrelVm& vm, AffineMatrixMethod method) {
		const auto value = vm.NativeStackValue(-1);
		if (vm.Type(value) != SquirrelValueType::UserData) {
			throw std::runtime_error("AffineMatrix callback requires its actual userdata capture");
		}

		const auto bytes = vm.UserDataBytes(value);
		if (bytes.size() != 8 || bytes[0] != 1 || bytes[1] != 0 || bytes[2] != 0 || bytes[3] != 0) {
			throw std::runtime_error("AffineMatrix callback has invalid initialized method metadata");
		}

		const auto id = std::uint32_t(bytes[4]) | (std::uint32_t(bytes[5]) << 8) | (std::uint32_t(bytes[6]) << 16) | (std::uint32_t(bytes[7]) << 24);
		if (id != static_cast<std::uint32_t>(method)) {
			throw std::runtime_error("AffineMatrix callback method metadata does not match");
		}
	}

	class ScriptAffineMatrixBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, std::size_t maximum_instances) : Vm(vm), MaximumInstances(maximum_instances) {
			if (maximum_instances == 0 || maximum_instances > 65536) {
				throw std::runtime_error("AffineMatrix requires a bounded positive instance limit");
			}
		}

		SquirrelVm& Vm;
		std::size_t MaximumInstances;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, AffineMatrixBindingRecord> Instances;

		AffineMatrixBindingRecord* FindRecord(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				return nullptr;
			}

			const auto identity = Vm.BorrowObjectIdentity(instance);
			const auto found = Instances.find(identity.Index);
			if (found == Instances.end() || found->second.Instance != identity) {
				return nullptr;
			}

			return &found->second;
		}

		scene::AffineMatrix& Matrix(const SquirrelValue& instance) {
			if (auto* record = FindRecord(instance)) {
				return record->Matrix;
			}

			throw SquirrelVmException(std::string("bad instance"), "bad instance");
		}

		void Adopt(const SquirrelValue& instance, scene::AffineMatrix matrix, const std::weak_ptr<Implementation>& owner) {
			if (!Vm.IsInstanceOf(instance, Class)) {
				throw SquirrelVmException(std::string("bad instance"), "bad instance");
			}

			const auto identity = Vm.BorrowObjectIdentity(instance);
			if (Instances.contains(identity.Index) || Instances.size() >= MaximumInstances) {
				throw std::runtime_error("AffineMatrix is already initialized or exceeds its instance limit");
			}

			Instances.emplace(identity.Index, AffineMatrixBindingRecord{identity, std::move(matrix)});
			try {
				Vm.SetInstanceHostIdentity(instance, {this, identity.Generation});
				Vm.SetInstanceReleaseHook(instance, [owner, identity](SquirrelVm&, const SquirrelValue&) {
					if (const auto state = owner.lock()) {
						const auto found = state->Instances.find(identity.Index);
						if (found != state->Instances.end() && found->second.Instance == identity) {
							state->Instances.erase(found);
						}
					}
				});
			} catch (...) {
				Instances.erase(identity.Index);

				throw;
			}
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& owner) {
			if (arguments.empty()) {
				throw SquirrelVmException(std::string("bad instance"), "bad instance");
			}

			scene::AffineMatrix matrix;
			// The original factory checks top minus receiver against two, not one.
			if (Vm.NativeStackSize() >= 3) {
				const auto source = Vm.NativeAllocatedStackValue(2);
				if (const auto* record = FindRecord(source)) {
					matrix = record->Matrix;
				}
			}
			Adopt(arguments[0], std::move(matrix), owner);
		}

		SquirrelNativeResult Invoke(AffineMatrixMethod method, std::span<const SquirrelValue> arguments) {
			if (arguments.empty()) {
				throw SquirrelVmException(std::string("bad instance"), "bad instance");
			}

			auto& matrix = Matrix(arguments[0]);
			RequireAffineMatrixCapture(Vm, method);
			switch (method) {
			case AffineMatrixMethod::Identity:
				return SquirrelNativeResult::ReturnValue(scene::IsAffineMatrixIdentity(matrix));
			case AffineMatrixMethod::Copy:
			case AffineMatrixMethod::Multiply: {
				const auto source = Vm.NativeAllocatedStackValue(2);
				if (!FindRecord(source)) {
					throw std::runtime_error("AffineMatrix source requires a live typed instance; native failure would dereference null");
				}
				const auto other = Matrix(source);
				matrix = method == AffineMatrixMethod::Copy ? other : scene::MultiplyAffineMatrices(matrix, other);
				break;
			}
			case AffineMatrixMethod::Reset:
				matrix = {};
				break;
			case AffineMatrixMethod::Perspective:
				scene::InitializeAffinePerspective(matrix);
				break;
			case AffineMatrixMethod::Translate:
			case AffineMatrixMethod::IntegerTranslate:
			case AffineMatrixMethod::Scale:
			case AffineMatrixMethod::TransformX:
			case AffineMatrixMethod::TransformY: {
				const auto z = ReadAffineMatrixFloat(Vm, 4);
				const auto y = ReadAffineMatrixFloat(Vm, 3);
				const auto x = ReadAffineMatrixFloat(Vm, 2);
				if (method == AffineMatrixMethod::Translate) {
					scene::TranslateAffineMatrix(matrix, x, y, z);
				} else if (method == AffineMatrixMethod::IntegerTranslate) {
					scene::TranslateAffineMatrixToInteger(matrix, x, y, z);
				} else if (method == AffineMatrixMethod::Scale) {
					scene::ScaleAffineMatrix(matrix, x, y, z);
				} else {
					return SquirrelNativeResult::ReturnValue(method == AffineMatrixMethod::TransformX ? scene::TransformAffineX(matrix, x, y, z) : scene::TransformAffineY(matrix, x, y, z));
				}
				break;
			}
			case AffineMatrixMethod::FlipX:
				scene::FlipAffineMatrix(matrix, scene::AffineAxis::X);
				break;
			case AffineMatrixMethod::FlipY:
				scene::FlipAffineMatrix(matrix, scene::AffineAxis::Y);
				break;
			case AffineMatrixMethod::Rotate:
				scene::RotateAffineMatrix(matrix, ReadAffineMatrixFloat(Vm, 2));
				break;
			case AffineMatrixMethod::Slant: {
				const auto y = ReadAffineMatrixFloat(Vm, 3);
				const auto x = ReadAffineMatrixFloat(Vm, 2);
				scene::SlantAffineMatrix(matrix, x, y);
				break;
			}
			case AffineMatrixMethod::AreaX:
			case AffineMatrixMethod::AreaY: {
				const auto height = ReadAffineMatrixFloat(Vm, 3);
				const auto width = ReadAffineMatrixFloat(Vm, 2);
				const auto corners = method == AffineMatrixMethod::AreaX ? scene::TransformAffineAreaX(matrix, width, height) : scene::TransformAffineAreaY(matrix, width, height);
				std::array<SquirrelValue, 4> values;
				for (std::size_t index = 0; index < values.size(); ++index) {
					values[index] = corners[index];
				}

				return SquirrelNativeResult::ReturnValue(Vm.CreateArray(values));
			}
			case AffineMatrixMethod::Print:
				// The registered original target is the single RET at 0x140030f20.
				break;
			}

			return SquirrelNativeResult::NoValues();
		}
	};

	ScriptAffineMatrixBindings::ScriptAffineMatrixBindings(SquirrelVm& vm, std::size_t maximum_instances) : m_Implementation(std::make_shared<Implementation>(vm, maximum_instances)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass();
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto constructor = vm.CreateNativeClosure("AffineMatrix.constructor", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("AffineMatrix constructor requires its live service");
			}
			owner->Construct(arguments, weak);

			return SquirrelNativeResult::NoValues();
		});
		vm.NewSlot(state.Class, std::string("constructor"), constructor);
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("AffineMatrix.weakref", [weak](SquirrelVm& active, std::span<const SquirrelValue>) {
			if (weak.expired()) {
				throw std::runtime_error("AffineMatrix weakref requires its live service");
			}

			return SquirrelNativeResult::ReturnValue(active.CreateWeakReference(active.NativeStackValue(-1)));
		}));
		const auto method = [&](std::string name, AffineMatrixMethod operation) {
			const std::array<SquirrelValue, 1> captures{AffineMatrixMethodCapture(vm, operation)};
			const auto callback = vm.CreateNativeClosureWithCaptures("AffineMatrix." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner) {
					throw std::runtime_error("AffineMatrix callback requires its live service");
				}

				return owner->Invoke(operation, arguments);
			},
			    captures);
			vm.NewSlot(state.Class, name, callback);
		};
		method("isIdentity", AffineMatrixMethod::Identity);
		method("copy", AffineMatrixMethod::Copy);
		method("reset", AffineMatrixMethod::Reset);
		method("initPerspective", AffineMatrixMethod::Perspective);
		method("translate", AffineMatrixMethod::Translate);
		method("translateInteger", AffineMatrixMethod::IntegerTranslate);
		method("flipx", AffineMatrixMethod::FlipX);
		method("flipy", AffineMatrixMethod::FlipY);
		method("rotate", AffineMatrixMethod::Rotate);
		method("scale", AffineMatrixMethod::Scale);
		method("slant", AffineMatrixMethod::Slant);
		method("multiply", AffineMatrixMethod::Multiply);
		method("transformX", AffineMatrixMethod::TransformX);
		method("transformY", AffineMatrixMethod::TransformY);
		method("transformAreaX", AffineMatrixMethod::AreaX);
		method("transformAreaY", AffineMatrixMethod::AreaY);
		method("print", AffineMatrixMethod::Print);
		vm.NewSlot(vm.RootTable(), std::string("AffineMatrix"), state.Class);
	}

	ScriptAffineMatrixBindings::~ScriptAffineMatrixBindings() = default;

	SquirrelValue ScriptAffineMatrixBindings::MatrixClass() const {
		return m_Implementation->Class;
	}

	SquirrelValue ScriptAffineMatrixBindings::WrapMatrix(scene::AffineMatrix matrix) {
		auto& state = *m_Implementation;
		const auto instance = state.Vm.CreateInstance(state.Class);
		state.Adopt(instance, std::move(matrix), std::weak_ptr<Implementation>(m_Implementation));

		return instance;
	}

	scene::AffineMatrix& ScriptAffineMatrixBindings::Matrix(const SquirrelValue& instance) {
		return m_Implementation->Matrix(instance);
	}

	std::size_t ScriptAffineMatrixBindings::InstanceCount() const {
		return m_Implementation->Instances.size();
	}

} // namespace sgre::script
