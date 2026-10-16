#include "sgre/script/captured_bindings.hpp"

#include "sgre/scene/captured_opacity_animation.hpp"
#include "sgre/scene/layer_base_frame.hpp"
#include "sgre/script/capture_bindings.hpp"
#include "sgre/script/image_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {
	enum class CapturedMethod : std::uint32_t {
		GetWidth = 1,
		GetHeight,
		SetOpacity,
		GetOpacity,
		ResetOpacityMap,
		AnimateOpacity,
		SetOpacityMap,
		SetOpacityMapSmoothing,
		GetOpacityMapSmoothing,
		SetOpacityMapRepeat
	};

	static std::array<std::uint8_t, 16> CapturedMetadata(CapturedMethod method) {
		const auto word = static_cast<std::uint32_t>(method);
		return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	}

	static void RequireCapturedMetadata(SquirrelVm& vm, CapturedMethod method) {
		const auto capture = vm.NativeStackValue(-1);
		if (vm.Type(capture) != SquirrelValueType::UserData) {
			throw std::runtime_error("Captured native member requires its actual appended method descriptor");
		}
		const auto bytes = vm.UserDataBytes(capture);
		const auto expected = CapturedMetadata(method);
		if (bytes.size() != expected.size() || !std::equal(bytes.begin(), bytes.end(), expected.begin())) {
			throw std::runtime_error("Captured native member descriptor identity is invalid");
		}
	}

	static float CapturedMapFloat(const SquirrelValue& value) {
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

	static bool CapturedMapTruth(const SquirrelValue& value) {
		// Native225b00/25a420 reads the actual allocated cell, including an appended descriptor on short calls.
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

	struct CapturedPayload {
		CapturedPayload(SquirrelHostRoot source_owner, const std::shared_ptr<scene::CaptureTask>& source) : SourceOwner(std::move(source_owner)), Task(source) {
		}
		SquirrelHostRoot SourceOwner;
		scene::CapturedTask Task;
		std::unique_ptr<scene::CapturedOpacityAnimations> Animations;
	};

	struct CapturedRecord {
		SquirrelObjectIdentity Instance;
		std::weak_ptr<CapturedPayload> Payload;
	};

	class ScriptCapturedBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptCaptureBindings& captures, scene::CapturedTask::DrawTexture drawing, std::size_t maximum) : Vm(vm), Objects(objects), Layers(layers), Captures(captures), Drawing(std::move(drawing)), Maximum(maximum) {
			if (!Drawing || maximum == 0 || maximum > 65536) {
				throw std::runtime_error("Captured requires actual sampled-frame drawing and bounded wrapper capacity");
			}
		}

		std::shared_ptr<CapturedPayload> Payload(const SquirrelValue& receiver) const {
			const auto identity = Vm.BorrowObjectIdentity(receiver);
			const auto found = Records.find(identity.Index);
			if (found == Records.end() || found->second.Instance != identity) {
				throw std::runtime_error("Captured receiver requires its actual live native Layer wrapper");
			}
			const auto payload = found->second.Payload.lock();
			if (!payload || payload->Task.State().Closed) {
				throw std::runtime_error("Captured physical camera child has retired");
			}

			return payload;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			if ((arguments.size() != 2 && arguments.size() != 3) || !Vm.IsInstanceOf(arguments[0], Class) || !Vm.IsInstanceOf(arguments.back(), Captures.CaptureClass()) || (arguments.size() == 3 && !Layers.IsLayerOwner(arguments[1]))) {
				throw std::runtime_error("Captured constructor requires its actual optional owner and one Capture receiver");
			}
			// Snapshot every operand before host rooting or base construction can enter VM lifecycle code.
			const auto receiver = arguments[0];
			const auto source = arguments.back();
			const auto parent = arguments.size() == 3 ? arguments[1] : SquirrelValue{};
			const auto identity = Vm.BorrowObjectIdentity(receiver);
			if (Records.contains(identity.Index) || Objects.HasInstance(receiver) || Records.size() == Maximum) {
				throw std::runtime_error("Captured receiver is initialized or bounded native wrapper capacity is exhausted");
			}
			auto payload = std::make_shared<CapturedPayload>(Vm.Retain(source), Captures.BorrowTask(source));
			bool attached = false;
			try {
				Layers.ConstructBaseInstance(std::array<SquirrelValue, 2>{receiver, parent});
				attached = true;
				scene::ParallelTaskCallbacks callbacks;
				callbacks.Draw = [weak, payload](auto, auto) {
					const auto owner = weak.lock();
					if (!owner) {
						throw std::runtime_error("Captured physical draw services expired before task retirement");
					}
					payload->Task.Draw(owner->Drawing, owner->MapDrawing);
				};
				callbacks.Release = [payload](auto) {
					payload->Task.Close();
					payload->SourceOwner = {};
				};
				// Native28e0ce attaches MCaptured after this SQLayerBase's clip/camera, unlike MCapture CommonFirst.
				static_cast<void>(Layers.BaseTasks(receiver).Children().Append(std::move(callbacks)));
				Records.emplace(identity.Index, CapturedRecord{identity, payload});
				Layers.ConfigureBaseRetirement(
				    receiver, [weak](const auto& actual) {
					    if (const auto owner = weak.lock(); owner && !owner->Payload(actual)->Task.CanClose()) {
						    throw std::runtime_error("Captured cannot retire during its actual sampled draw");
					    }
				    },
				    [weak](const auto& actual) {
					    if (const auto owner = weak.lock()) {
						    const auto payload = owner->Payload(actual);
						    owner->Records.erase(owner->Vm.BorrowObjectIdentity(actual).Index);
						    // Native SQCaptured releases its retained Capture ObjectInfo before base task graph deletion.
						    payload->SourceOwner = {};
					    }
				    });
			} catch (...) {
				const auto failure = std::current_exception();
				if (attached) {
					try {
						Layers.ReleaseInstance(receiver);
					} catch (...) {
					}
				}
				Records.erase(identity.Index);
				std::rethrow_exception(failure);
			}
		}

		SquirrelNativeResult Invoke(CapturedMethod method, const SquirrelValue& receiver) {
			const auto payload = Payload(receiver);
			const auto state = payload->Task.State();
			switch (method) {
			case CapturedMethod::GetWidth:
				return SquirrelNativeResult::ReturnValue(state.FixedSize.X);
			case CapturedMethod::GetHeight:
				return SquirrelNativeResult::ReturnValue(state.FixedSize.Y);
			case CapturedMethod::GetOpacity:
				return SquirrelNativeResult::ReturnValue(state.Opacity);
			case CapturedMethod::SetOpacity:
				// Native056cb0→28f0a0 reads a uint32 physical member argument, then converts unsigned to float.
				payload->Task.SetOpacity(static_cast<float>(std::bit_cast<std::uint32_t>(TryNativeInteger(Vm.NativeAllocatedStackValue(2)).value_or(0))));
				return SquirrelNativeResult::NoValues();
			case CapturedMethod::AnimateOpacity:
				if (Clock == nullptr) {
					throw std::runtime_error("Captured.animateOpacity requires its actual shared Looper clock");
				}
				if (!payload->Animations) {
					payload->Animations = std::make_unique<scene::CapturedOpacityAnimations>(payload->Task, *Clock, Layers.BaseTasks(receiver).CommonFirst());
				}
				payload->Animations->Append(TryNativeInteger(Vm.NativeAllocatedStackValue(2)).value_or(0), TryNativeInteger(Vm.NativeAllocatedStackValue(3)).value_or(0), TryNativeInteger(Vm.NativeAllocatedStackValue(4)).value_or(0));
				return SquirrelNativeResult::NoValues();
			case CapturedMethod::SetOpacityMap:
				if (Images == nullptr || !MapDrawing) {
					throw std::runtime_error("Captured.setOpacityMap requires its genuine opacity-map Image and PS2040 frame recipient");
				}
				SetOpacityMap(*payload);
				return SquirrelNativeResult::NoValues();
			case CapturedMethod::SetOpacityMapSmoothing:
				payload->Task.SetOpacityMapSmoothing(CapturedMapTruth(Vm.NativeAllocatedStackValue(2)));
				return SquirrelNativeResult::NoValues();
			case CapturedMethod::GetOpacityMapSmoothing:
				return SquirrelNativeResult::ReturnValue(payload->Task.OpacityMapState().AddressU != 0);
			case CapturedMethod::SetOpacityMapRepeat:
				SetOpacityMapRepeat(*payload);
				return SquirrelNativeResult::NoValues();
			case CapturedMethod::ResetOpacityMap:
				payload->Task.ResetOpacityMap();
				return SquirrelNativeResult::NoValues();
			}
			throw std::runtime_error("Captured native member has no recovered dispatch");
		}

		void SetOpacityMap(CapturedPayload& payload) {
			const auto width = TryNativeInteger(Vm.NativeAllocatedStackValue(3)).value_or(0);
			const auto image = Vm.NativeAllocatedStackValue(2);
			const auto borrowed = Images->BorrowNativeImage(image);
			const auto asset = borrowed.Asset();
			payload.Task.SetOpacityMap(*asset, borrowed.Source(0), width);
		}

		void SetOpacityMapRepeat(CapturedPayload& payload) {
			// Native22b6d0 converts slot3 before slot2; no script conversion is invoked by222e90.
			const auto vertical = CapturedMapFloat(Vm.NativeAllocatedStackValue(3));
			const auto horizontal = CapturedMapFloat(Vm.NativeAllocatedStackValue(2));
			payload.Task.SetOpacityMapRepeat({horizontal, vertical});
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		ScriptCaptureBindings& Captures;
		scene::CapturedTask::DrawTexture Drawing;
		scene::CapturedTask::DrawOpacityMap MapDrawing;
		ScriptImageBindings* Images = nullptr;
		std::size_t Maximum;
		const core::FrameClock* Clock = nullptr;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, CapturedRecord> Records;
	};

	ScriptCapturedBindings::ScriptCapturedBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptCaptureBindings& captures, scene::CapturedTask::DrawTexture drawing, std::size_t max_instances) : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, captures, std::move(drawing), max_instances)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(layers.LayerBaseClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(state.Class, std::string("constructor"), vm.CreateNativeClosure("Captured.constructor", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("Captured constructor services expired");
			}
			owner->Construct(arguments, weak);
			return SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("Captured.weakref", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner || arguments.empty()) {
				throw std::runtime_error("Captured weakref requires its actual final original argument");
			}
			return SquirrelNativeResult::ReturnValue(owner->Vm.CreateWeakReference(arguments.back()));
		}));
		const auto method = [&](const char* name, CapturedMethod identity) {
			const std::array<SquirrelValue, 1> capture{vm.CreateUserData(CapturedMetadata(identity))};
			vm.NewSlot(state.Class, std::string(name), vm.CreateNativeClosureWithCaptures(std::string("Captured.") + name, [weak, identity](SquirrelVm& active, auto arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("Captured native member requires its actual receiver and services");
				}
				const auto receiver = arguments[0];
				RequireCapturedMetadata(active, identity);
				return owner->Invoke(identity, receiver);
			},
			                                               capture));
		};
		method("getWidth", CapturedMethod::GetWidth);
		method("getHeight", CapturedMethod::GetHeight);
		method("setOpacity", CapturedMethod::SetOpacity);
		method("getOpacity", CapturedMethod::GetOpacity);
		method("resetOpacityMap", CapturedMethod::ResetOpacityMap);
		method("animateOpacity", CapturedMethod::AnimateOpacity);
		method("setOpacityMap", CapturedMethod::SetOpacityMap);
		method("setOpacityMapSmoothing", CapturedMethod::SetOpacityMapSmoothing);
		method("getOpacityMapSmoothing", CapturedMethod::GetOpacityMapSmoothing);
		method("setOpacityMapRepeat", CapturedMethod::SetOpacityMapRepeat);
		vm.NewSlot(state.Class, std::string("setOpacityMapRaw"), vm.CreateNativeClosure("Captured.setOpacityMapRaw", [weak](SquirrelVm&, auto arguments) -> SquirrelNativeResult {
			const auto owner = weak.lock();
			if (!owner || arguments.empty()) {
				throw std::runtime_error("Captured operation requires its actual receiver and services");
			}
			static_cast<void>(owner->Payload(arguments[0]));
			throw std::runtime_error("Captured.setOpacityMapRaw requires its actual typed RawImage conversion boundary");
		}));
		vm.NewSlot(vm.RootTable(), std::string("Captured"), state.Class);
	}

	ScriptCapturedBindings::~ScriptCapturedBindings() = default;

	void ScriptCapturedBindings::ConfigureAnimationClock(const core::FrameClock& actual_clock) {
		auto& state = *m_Implementation;
		if (state.Vm.IsClosed() || state.Clock != nullptr) {
			throw std::runtime_error("Captured animation clock requires one actual live containing Looper owner");
		}
		state.Clock = &actual_clock;
	}

	void ScriptCapturedBindings::ConfigureOpacityMaps(ScriptImageBindings& actual_images, scene::CapturedTask::DrawOpacityMap actual_drawing) {
		auto& state = *m_Implementation;
		if (state.Vm.IsClosed() || state.Images != nullptr || !actual_drawing) {
			throw std::runtime_error("Captured opacity-map services require one actual live Image owner and drawing recipient");
		}
		state.Images = &actual_images;
		state.MapDrawing = std::move(actual_drawing);
	}

	SquirrelValue ScriptCapturedBindings::CapturedClass() const {
		return m_Implementation->Class;
	}

	scene::CapturedTaskState ScriptCapturedBindings::State(const SquirrelValue& instance) const {
		return m_Implementation->Payload(instance)->Task.State();
	}

	void ScriptCapturedBindings::ReleaseInstance(const SquirrelValue& instance) {
		static_cast<void>(m_Implementation->Payload(instance));
		m_Implementation->Layers.ReleaseInstance(instance);
	}
} // namespace sgre::script
