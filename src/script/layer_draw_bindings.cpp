#include "sgre/script/layer_draw_bindings.hpp"

#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/layer_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	struct DrawingProxyPayload {
		SquirrelHostRoot Target;
		std::uint32_t Opacity = 255;
		bool Closed = false;

		void Close() {
			if (!Closed) {
				Target = {};
				Closed = true;
			}
		}
	};

	struct LayerDrawRecord {
		SquirrelObjectIdentity Instance;
		std::weak_ptr<DrawingProxyPayload> Payload;
		std::unique_ptr<scene::LayerScaleView> Scale;
		ScriptLayerBindings::BorrowedViewRegistration View;
		bool Releasing = false;
	};

	enum class LayerDrawMethod : std::uint32_t {
		SetOffset = 1,
		SetScale,
		SetRotation,
		SetPrimaryScale,
		SetVisible,
		GetVisible,
		SetOpacity,
		GetOpacity
	};

	static float NativeDrawFloat(const SquirrelValue& value) {
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

	static bool NativeDrawTruth(const SquirrelValue& value) {
		if (std::holds_alternative<std::monostate>(value)) {
			return false;
		}
		if (const auto* number = std::get_if<float>(&value)) {
			return *number != 0;
		}
		if (const auto* number = std::get_if<std::int32_t>(&value)) {
			return *number != 0;
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean;
		}

		return true;
	}

	static std::array<std::uint8_t, 8> DrawMethodMetadata(LayerDrawMethod method) {
		const auto word = static_cast<std::uint32_t>(method);

		return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24), 0, 0, 0, 0};
	}

	static void RequireDrawMethodMetadata(SquirrelVm& vm, LayerDrawMethod method) {
		const auto captured = vm.NativeStackValue(-1);
		if (vm.Type(captured) != SquirrelValueType::UserData) {
			throw std::runtime_error("LayerDraw method requires its actual captured userdata cell");
		}

		const auto bytes = vm.UserDataBytes(captured);
		const auto expected = DrawMethodMetadata(method);
		if (bytes.size() != expected.size() || !std::equal(bytes.begin(), bytes.end(), expected.begin())) {
			throw std::runtime_error("LayerDraw captured method metadata identity is invalid");
		}
	}

	class ScriptLayerDrawBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptDrawObjectBindings& draws, ScriptLayerDrawLimits limits, scene::LayerCameraDrawScope* camera) : Vm(vm), Objects(objects), Layers(layers), Draws(draws), Limits(limits), Camera(camera) {
			if (limits.MaxInstances == 0 || limits.MaxInstances > 65536 || limits.MaxCameraTasks == 0 || limits.MaxCameraTasks > 65536) {
				throw std::runtime_error("LayerDraw requires positive bounded wrapper and camera capacities");
			}
		}

		~Implementation() {
			for (auto& [index, record] : Instances) {
				static_cast<void>(index);
				try {
					record->Scale->Retire();
					record->View.Reset();
				} catch (...) {
					// Actual First owns the deferred task/root; explicit native release reports diagnostics.
				}
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		ScriptDrawObjectBindings& Draws;
		ScriptLayerDrawLimits Limits;
		scene::LayerCameraDrawScope* Camera;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, std::shared_ptr<LayerDrawRecord>> Instances;

		std::shared_ptr<LayerDrawRecord> Record(const SquirrelValue& instance) const {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("LayerDraw callback requires its actual native wrapper instance");
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second->Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("LayerDraw receiver has no live native wrapper identity");
			}

			return found->second;
		}

		std::shared_ptr<DrawingProxyPayload> Payload(const LayerDrawRecord& record) const {
			const auto payload = record.Payload.lock();
			if (record.Scale->State().Detached || !payload || payload->Closed) {
				throw std::runtime_error("LayerDraw opacity access would dereference a detached native task");
			}

			return payload;
		}

		SquirrelHostRoot CaptureTarget(const SquirrelValue& target) {
			if (Vm.Type(target) == SquirrelValueType::WeakReference) {
				const auto resolved = Vm.ResolveWeakReference(target);
				if (Vm.Type(resolved) == SquirrelValueType::Null) {
					// Native constructor copies the original expired weak wrapper, not the resolved null.
					return Vm.Retain(target);
				}

				return Vm.Retain(Vm.CreateWeakReference(resolved));
			}

			return Vm.Retain(Vm.CreateWeakReference(target));
		}

		void DrawTarget(const DrawingProxyPayload& payload, std::uint32_t argument) {
			if (payload.Closed || Vm.IsClosed()) {
				return;
			}

			std::optional<ScriptNativeDrawObjectBorrow> native;
			{
				const auto stored = payload.Target.Value();
				const auto target = Vm.Type(stored) == SquirrelValueType::WeakReference ? Vm.ResolveWeakReference(stored) : stored;
				if (Vm.Type(target) != SquirrelValueType::Instance) {
					Vm.SetLastErrorInContext(Vm.RootContext(), std::string("the object is not a class instance"));

					return;
				}
				if (!Vm.IsInstanceOf(target, Draws.DrawObjectClass())) {
					Vm.SetLastErrorInContext(Vm.RootContext(), std::string("invalid type tag"));

					return;
				}

				native = Draws.BorrowNativeDrawOwner(target);
			}

			// The original pops the temporary resolved target before calling its native virtual method.
			if (native) {
				native->Draw(argument, payload.Opacity);
			}
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& binding_owner) {
			if (arguments.size() < 3 || !Vm.IsInstanceOf(arguments[0], Class) || !Vm.IsInstanceOf(arguments[1], Layers.LayerClass())) {
				throw std::runtime_error("LayerDraw constructor requires its genuine instance, actual Layer and target value");
			}

			const auto index = std::get<SquirrelObjectHandle>(arguments[0]).Index;
			if (Instances.contains(index) || Instances.size() >= Limits.MaxInstances) {
				throw std::runtime_error("LayerDraw construction exceeds native wrapper capacity or duplicates its instance");
			}

			auto record = std::make_shared<LayerDrawRecord>();
			record->Instance = Vm.BorrowObjectIdentity(arguments[0]);
			const auto weak_record = std::weak_ptr<LayerDrawRecord>(record);
			auto payload = std::make_shared<DrawingProxyPayload>();
			payload->Target = CaptureTarget(arguments[2]);
			record->Payload = payload;
			bool object_attached = false;
			try {
				record->View = Layers.BorrowMotionView(arguments[1], [](bool) {
					// SQLayerDraw's native smoothing virtual method is the empty 0x140030f20.
				},
				    [weak_record] {
					    if (const auto view = weak_record.lock()) {
						    view->Scale->DetachParent();
						    view->Payload.reset();
					    }
				    });
				scene::LayerScaleTaskCallbacks callbacks;
				callbacks.InitiallyDrawPaused = false;
				callbacks.Draw = [binding_owner, payload](std::uint32_t argument) {
					const auto owner = binding_owner.lock();
					if (!owner) {
						throw std::runtime_error("LayerDraw native target lookup services have expired");
					}

					owner->DrawTarget(*payload, argument);
				};
				callbacks.Release = [payload] {
					payload->Close();
				};
				if (Camera) {
					callbacks.DrawWithCamera = [camera = Camera](const auto& state, auto& children, std::uint32_t argument) {
						camera->Draw(scene::PrepareLayerCameraParameters(state), children, argument);
					};
				}

				record->Scale = std::make_unique<scene::LayerScaleView>(Layers.MotionTasks(arguments[1]), std::move(callbacks), Limits.MaxCameraTasks);
				Objects.AttachInstance(arguments[0]);
				object_attached = true;
				Instances.emplace(index, record);
				Objects.ConfigureNativeRelease(arguments[0], [binding_owner](const SquirrelValue& receiver) {
					if (const auto owner = binding_owner.lock()) {
						owner->Release(receiver);
					}
				});
			} catch (...) {
				const auto failure = std::current_exception();
				if (record->Scale) {
					record->Scale->Retire();
				}

				record->View.Reset();
				Instances.erase(index);
				if (object_attached) {
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
			if (record->Releasing) {
				throw std::runtime_error("LayerDraw wrapper is already retiring");
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

			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		SquirrelNativeResult Invoke(LayerDrawMethod method, const SquirrelValue& receiver) {
			const auto record = Record(receiver);
			switch (method) {
			case LayerDrawMethod::SetOffset:
				record->Scale->SetOffset({NativeDrawFloat(Vm.NativeAllocatedStackValue(2)), NativeDrawFloat(Vm.NativeAllocatedStackValue(3))});
				break;
			case LayerDrawMethod::SetScale:
				record->Scale->SetScale({NativeDrawFloat(Vm.NativeAllocatedStackValue(2)), NativeDrawFloat(Vm.NativeAllocatedStackValue(3))});
				break;
			case LayerDrawMethod::SetRotation:
				record->Scale->SetRotation(NativeDrawFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case LayerDrawMethod::SetPrimaryScale:
				record->Scale->SetPrimaryScale(NativeDrawFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case LayerDrawMethod::SetVisible:
				record->Scale->SetVisible(NativeDrawTruth(Vm.NativeAllocatedStackValue(2)));
				break;
			case LayerDrawMethod::GetVisible:
				return SquirrelNativeResult::ReturnValue(!record->Scale->State().Detached && !record->Scale->TaskState().DrawPaused);
			case LayerDrawMethod::SetOpacity:
				Payload(*record)->Opacity = std::bit_cast<std::uint32_t>(TryNativeInteger(Vm.NativeAllocatedStackValue(2)).value_or(0));
				break;
			case LayerDrawMethod::GetOpacity:
				return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(Payload(*record)->Opacity));
			}

			return SquirrelNativeResult::NoValues();
		}
	};

	ScriptLayerDrawBindings::ScriptLayerDrawBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptDrawObjectBindings& draws, ScriptLayerDrawLimits limits, scene::LayerCameraDrawScope* camera) : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, draws, limits, camera)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(state.Class, std::string("constructor"), vm.CreateNativeClosure("LayerDraw.constructor", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("LayerDraw constructor services have expired");
			}

			owner->Construct(arguments, weak);

			return SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("LayerDraw.weakref", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			const auto owner = weak.lock();
			if (!owner || arguments.empty()) {
				throw std::runtime_error("LayerDraw weakref requires live services and its last stack value");
			}

			return SquirrelNativeResult::ReturnValue(owner->Vm.CreateWeakReference(arguments.back()));
		}));
		const auto register_method = [&](const char* name, LayerDrawMethod method) {
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(DrawMethodMetadata(method))};
			vm.NewSlot(state.Class, std::string(name), vm.CreateNativeClosureWithCaptures(std::string("LayerDraw.") + name, [weak, method](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("LayerDraw method services or receiver have expired");
				}

				RequireDrawMethodMetadata(active, method);

				return owner->Invoke(method, arguments[0]);
			},
			                                               captures));
		};
		register_method("setOffset", LayerDrawMethod::SetOffset);
		register_method("setScale", LayerDrawMethod::SetScale);
		register_method("setRot", LayerDrawMethod::SetRotation);
		register_method("setPrimaryScale", LayerDrawMethod::SetPrimaryScale);
		register_method("setVisible", LayerDrawMethod::SetVisible);
		register_method("getVisible", LayerDrawMethod::GetVisible);
		register_method("setOpacity", LayerDrawMethod::SetOpacity);
		register_method("getOpacity", LayerDrawMethod::GetOpacity);
		vm.NewSlot(vm.RootTable(), std::string("LayerDraw"), state.Class);
	}

	ScriptLayerDrawBindings::~ScriptLayerDrawBindings() = default;

	SquirrelValue ScriptLayerDrawBindings::LayerDrawClass() const {
		return m_Implementation->Class;
	}

	scene::LayerScaleView& ScriptLayerDrawBindings::ScaleView(const SquirrelValue& instance) {
		return *m_Implementation->Record(instance)->Scale;
	}

	void ScriptLayerDrawBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
