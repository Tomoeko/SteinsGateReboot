#include "sgre/script/fill_rectangle_bindings.hpp"

#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/layer_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {
	struct FillRectanglePayload {
		scene::FillRectangleTask Task;
	};

	struct FillRectangleRecord {
		SquirrelObjectIdentity Instance;
		SquirrelObjectIdentity Parent;
		std::weak_ptr<FillRectanglePayload> Payload;
		std::unique_ptr<scene::LayerScaleView> Scale;
		ScriptLayerBindings::BorrowedViewRegistration View;
		bool Releasing = false;
	};

	enum class FillRectangleMethod : std::uint32_t {
		SetOffset = 1,
		SetScale,
		SetRotation,
		SetPrimaryScale,
		SetVisible,
		GetVisible,
		SetCoordinate,
		SetLeft,
		GetLeft,
		SetTop,
		GetTop,
		SetSize,
		SetWidth,
		GetWidth,
		SetHeight,
		GetHeight,
		SetColor,
		GetColor,
		SetOpacity,
		GetOpacity,
		SetBlendMode,
		GetBlendMode
	};

	static float FillRectangleFloat(const SquirrelValue& value) {
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

	static bool FillRectangleTruth(const SquirrelValue& value) {
		if (std::holds_alternative<std::monostate>(value)) {
			return false;
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean;
		}
		if (const auto* number = std::get_if<std::int32_t>(&value)) {
			return *number != 0;
		}
		if (const auto* number = std::get_if<float>(&value)) {
			return *number != 0;
		}
		return true;
	}

	static std::array<std::uint8_t, 8> FillRectangleMetadata(FillRectangleMethod method) {
		const auto word = static_cast<std::uint32_t>(method);
		return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24), 0, 0, 0, 0};
	}

	static void RequireFillRectangleMetadata(SquirrelVm& vm, FillRectangleMethod method) {
		const auto captured = vm.NativeStackValue(-1);
		if (vm.Type(captured) != SquirrelValueType::UserData) {
			throw std::runtime_error("FillRect method requires its genuine captured userdata");
		}
		const auto bytes = vm.UserDataBytes(captured);
		const auto expected = FillRectangleMetadata(method);
		if (bytes.size() != expected.size() || !std::equal(bytes.begin(), bytes.end(), expected.begin())) {
			throw std::runtime_error("FillRect captured method identity is invalid");
		}
	}

	class ScriptFillRectangleBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptFillRectangleLimits limits) : Vm(vm), Objects(objects), Layers(layers), Limits(limits) {
			if (limits.MaxInstances == 0 || limits.MaxInstances > 65536 || limits.MaxCameraTasks == 0 || limits.MaxCameraTasks > 65536) {
				throw std::runtime_error("FillRect requires bounded positive wrapper/camera capacities");
			}
		}

		std::shared_ptr<FillRectangleRecord> Record(const SquirrelValue& instance) const {
			const auto identity = Vm.BorrowObjectIdentity(instance);
			const auto found = Records.find(identity.Index);
			if (found == Records.end() || found->second->Instance != identity || found->second->Releasing) {
				throw std::runtime_error("FillRect receiver has no live native wrapper identity");
			}
			return found->second;
		}

		std::shared_ptr<FillRectanglePayload> Payload(const FillRectangleRecord& record) const {
			const auto payload = record.Payload.lock();
			if (record.Scale->State().Detached || !payload || payload->Task.State().Closed) {
				throw std::runtime_error("FillRect actual task has detached or retired");
			}
			return payload;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			if (arguments.size() < 2 || !Vm.IsInstanceOf(arguments[0], Class) || !Vm.IsInstanceOf(arguments[1], Layers.LayerClass())) {
				throw std::runtime_error("FillRect constructor invalid argument");
			}
			const auto identity = Vm.BorrowObjectIdentity(arguments[0]);
			if (Records.contains(identity.Index) || Objects.HasInstance(arguments[0]) || Records.size() >= Limits.MaxInstances) {
				throw std::runtime_error("FillRect receiver is initialized or bounded wrapper capacity is exhausted");
			}
			Layers.RequireCanReleaseInstance(arguments[1]);
			auto record = std::make_shared<FillRectangleRecord>();
			record->Instance = identity;
			record->Parent = Vm.BorrowObjectIdentity(arguments[1]);
			auto payload = std::make_shared<FillRectanglePayload>();
			record->Payload = payload;
			const std::weak_ptr<FillRectangleRecord> weak_record = record;
			bool attached = false;
			try {
				record->View = Layers.BorrowMotionObject(arguments[1], [weak_record] {
					if (const auto current = weak_record.lock()) {
						current->Scale->DetachParent();
						current->Payload.reset();
					}
				});
				scene::LayerScaleTaskCallbacks callbacks;
				callbacks.InitiallyDrawPaused = false;
				callbacks.Draw = [payload, weak](std::uint32_t) {
					const auto owner = weak.lock();
					if (!owner) {
						throw std::runtime_error("FillRect drawing services expired before actual task retirement");
					}
					payload->Task.Draw(owner->Drawing);
				};
				callbacks.DrawWithCamera = [weak](const auto& camera, auto& children, auto argument) {
					const auto owner = weak.lock();
					if (!owner || owner->Renderer == nullptr) {
						throw std::runtime_error("FillRect camera requires its actual graphics model/scissor scope");
					}
					owner->Renderer->Draw(scene::PrepareLayerCameraParameters(camera), children, argument);
				};
				callbacks.Release = [payload] {
					payload->Task.Close();
				};
				record->Scale = std::make_unique<scene::LayerScaleView>(Layers.MotionTasks(arguments[1]), std::move(callbacks), Limits.MaxCameraTasks);
				Objects.AttachInstance(arguments[0]);
				attached = true;
				Records.emplace(identity.Index, record);
				Objects.ConfigureNativeRelease(arguments[0], [weak](const auto& receiver) {
					if (const auto owner = weak.lock()) {
						owner->Release(receiver);
					}
				});
				EverConstructed = true;
			} catch (...) {
				const auto failure = std::current_exception();
				if (record->Scale) {
					record->Scale->Retire();
				}
				record->View.Reset();
				Records.erase(identity.Index);
				if (attached) {
					try {
						Objects.ReleaseInstance(arguments[0]);
					} catch (...) {
					}
				}
				std::rethrow_exception(failure);
			}
		}

		void Release(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			const auto record = Record(instance);
			if (const auto payload = record->Payload.lock(); payload && !payload->Task.CanClose()) {
				throw std::runtime_error("FillRect cannot release the current actual draw packet borrow");
			}
			if (!record->Scale->State().Detached) {
				const auto parent = Vm.ResolveObjectIdentity(record->Parent);
				if (!parent) {
					throw std::runtime_error("FillRect retirement requires its actual stable Layer or completed detach");
				}
				Layers.RequireCanReleaseInstance(*parent);
			}
			record->Scale->Retire();
			record->Releasing = true;
			record->View.Reset();
			std::exception_ptr failure;
			try {
				Objects.ReleaseInstance(instance);
			} catch (...) {
				failure = std::current_exception();
			}
			Records.erase(record->Instance.Index);
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		SquirrelNativeResult Invoke(FillRectangleMethod method, const SquirrelValue& receiver) {
			const auto record = Record(receiver);
			const bool detached = record->Scale->State().Detached;
			switch (method) {
			case FillRectangleMethod::SetOffset:
				record->Scale->SetOffset({FillRectangleFloat(Vm.NativeAllocatedStackValue(2)), FillRectangleFloat(Vm.NativeAllocatedStackValue(3))});
				break;
			case FillRectangleMethod::SetScale:
				record->Scale->SetScale({FillRectangleFloat(Vm.NativeAllocatedStackValue(2)), FillRectangleFloat(Vm.NativeAllocatedStackValue(3))});
				break;
			case FillRectangleMethod::SetRotation:
				record->Scale->SetRotation(FillRectangleFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case FillRectangleMethod::SetPrimaryScale:
				record->Scale->SetPrimaryScale(FillRectangleFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case FillRectangleMethod::SetVisible:
				record->Scale->SetVisible(FillRectangleTruth(Vm.NativeAllocatedStackValue(2)));
				break;
			case FillRectangleMethod::GetVisible:
				return SquirrelNativeResult::ReturnValue(!detached && !record->Scale->TaskState().DrawPaused);
			default:
				return InvokePayload(method, *record, detached);
			}
			return SquirrelNativeResult::NoValues();
		}

		SquirrelNativeResult InvokePayload(FillRectangleMethod method, const FillRectangleRecord& record, bool detached) {
			if (method == FillRectangleMethod::GetLeft || method == FillRectangleMethod::GetTop || method == FillRectangleMethod::GetWidth || method == FillRectangleMethod::GetHeight) {
				const auto state = detached ? scene::FillRectangleState{} : Payload(record)->Task.State();
				float value = 0;
				if (!detached) {
					switch (method) {
					case FillRectangleMethod::GetLeft:
						value = state.Coordinate.X;
						break;
					case FillRectangleMethod::GetTop:
						value = state.Coordinate.Y;
						break;
					case FillRectangleMethod::GetWidth:
						value = state.Size.X;
						break;
					default:
						value = state.Size.Y;
						break;
					}
				}
				return SquirrelNativeResult::ReturnValue(value);
			}
			if (method == FillRectangleMethod::GetColor || method == FillRectangleMethod::GetOpacity || method == FillRectangleMethod::GetBlendMode) {
				std::uint32_t value = method == FillRectangleMethod::GetColor ? 0xffffffffu : 255u;
				if (!detached) {
					const auto state = Payload(record)->Task.State();
					value = method == FillRectangleMethod::GetColor ? state.Color : method == FillRectangleMethod::GetOpacity ? state.Opacity
					                                                                                                          : state.BlendMode;
				}
				return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(value));
			}
			if (detached) {
				return SquirrelNativeResult::NoValues();
			}
			const auto payload = Payload(record);
			auto& task = payload->Task;
			const auto first = FillRectangleFloat(Vm.NativeAllocatedStackValue(2));
			const auto second = method == FillRectangleMethod::SetCoordinate || method == FillRectangleMethod::SetSize ? FillRectangleFloat(Vm.NativeAllocatedStackValue(3)) : 0.0f;
			switch (method) {
			case FillRectangleMethod::SetCoordinate:
				task.SetCoordinate({first, second});
				break;
			case FillRectangleMethod::SetLeft:
				task.SetLeft(first);
				break;
			case FillRectangleMethod::SetTop:
				task.SetTop(first);
				break;
			case FillRectangleMethod::SetSize:
				task.SetSize({first, second});
				break;
			case FillRectangleMethod::SetWidth:
				task.SetWidth(first);
				break;
			case FillRectangleMethod::SetHeight:
				task.SetHeight(first);
				break;
			case FillRectangleMethod::SetColor:
				task.SetColor(std::bit_cast<std::uint32_t>(TryNativeInteger(Vm.NativeAllocatedStackValue(2)).value_or(0)));
				break;
			case FillRectangleMethod::SetOpacity:
				task.SetOpacity(std::bit_cast<std::uint32_t>(TryNativeInteger(Vm.NativeAllocatedStackValue(2)).value_or(0)));
				break;
			case FillRectangleMethod::SetBlendMode:
				task.SetBlendMode(std::bit_cast<std::uint32_t>(TryNativeInteger(Vm.NativeAllocatedStackValue(2)).value_or(0)));
				break;
			default:
				throw std::runtime_error("FillRect method has no recovered task dispatch");
			}
			return SquirrelNativeResult::NoValues();
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		ScriptFillRectangleLimits Limits;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, std::shared_ptr<FillRectangleRecord>> Records;
		scene::LayerCameraDrawScope* Renderer = nullptr;
		scene::FillRectangleTask::DrawRectangle Drawing;
		bool EverConstructed = false;
	};

	ScriptFillRectangleBindings::ScriptFillRectangleBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptFillRectangleLimits limits) : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, limits)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(state.Class, std::string("constructor"), vm.CreateNativeClosure("FillRect.constructor", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("FillRect constructor services expired");
			}
			owner->Construct(arguments, weak);
			return SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("FillRect.weakref", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner || arguments.empty()) {
				throw std::runtime_error("FillRect weakref requires its final original argument and live services");
			}
			return SquirrelNativeResult::ReturnValue(owner->Vm.CreateWeakReference(arguments.back()));
		}));
		const auto method = [&](const char* name, FillRectangleMethod identity) {
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(FillRectangleMetadata(identity))};
			vm.NewSlot(state.Class, std::string(name), vm.CreateNativeClosureWithCaptures(std::string("FillRect.") + name, [weak, identity](SquirrelVm& active, auto arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("FillRect method requires its live owner and original receiver");
				}
				RequireFillRectangleMetadata(active, identity);
				return owner->Invoke(identity, arguments[0]);
			},
			                                               captures));
		};
		method("setOffset", FillRectangleMethod::SetOffset);
		method("setScale", FillRectangleMethod::SetScale);
		method("setRot", FillRectangleMethod::SetRotation);
		method("setPrimaryScale", FillRectangleMethod::SetPrimaryScale);
		method("setVisible", FillRectangleMethod::SetVisible);
		method("getVisible", FillRectangleMethod::GetVisible);
		method("setCoord", FillRectangleMethod::SetCoordinate);
		method("setLeft", FillRectangleMethod::SetLeft);
		method("getLeft", FillRectangleMethod::GetLeft);
		method("setTop", FillRectangleMethod::SetTop);
		method("getTop", FillRectangleMethod::GetTop);
		method("setSize", FillRectangleMethod::SetSize);
		method("setWidth", FillRectangleMethod::SetWidth);
		method("getWidth", FillRectangleMethod::GetWidth);
		method("setHeight", FillRectangleMethod::SetHeight);
		method("getHeight", FillRectangleMethod::GetHeight);
		method("setColor", FillRectangleMethod::SetColor);
		method("getColor", FillRectangleMethod::GetColor);
		method("setOpacity", FillRectangleMethod::SetOpacity);
		method("getOpacity", FillRectangleMethod::GetOpacity);
		method("setBlendMode", FillRectangleMethod::SetBlendMode);
		method("getBlendMode", FillRectangleMethod::GetBlendMode);
		vm.NewSlot(vm.RootTable(), std::string("FillRect"), state.Class);
	}

	ScriptFillRectangleBindings::~ScriptFillRectangleBindings() = default;

	void ScriptFillRectangleBindings::ConfigureFrameOwners(scene::LayerCameraDrawScope& actual_renderer, scene::FillRectangleTask::DrawRectangle actual_drawing) {
		auto& state = *m_Implementation;
		if (!actual_drawing || state.Renderer != nullptr || state.EverConstructed || !state.Records.empty() || state.Vm.IsClosed()) {
			throw std::runtime_error("FillRect frame services require one registration before wrapper construction");
		}
		state.Drawing = std::move(actual_drawing);
		state.Renderer = &actual_renderer;
	}

	SquirrelValue ScriptFillRectangleBindings::FillRectangleClass() const {
		return m_Implementation->Class;
	}

	scene::FillRectangleState ScriptFillRectangleBindings::State(const SquirrelValue& instance) const {
		return m_Implementation->Payload(*m_Implementation->Record(instance))->Task.State();
	}

	scene::LayerScaleView& ScriptFillRectangleBindings::ScaleView(const SquirrelValue& instance) {
		return *m_Implementation->Record(instance)->Scale;
	}

	void ScriptFillRectangleBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}
} // namespace sgre::script
