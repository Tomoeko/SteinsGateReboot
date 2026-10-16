#include "sgre/script/layer_raw_texture_bindings.hpp"

#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/native_math.hpp"
#include "sgre/script/psb_bindings.hpp"
#include "sgre/script/raw_image_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <exception>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	struct LayerRawTextureBudget {
		std::size_t Bytes = 0;
	};

	struct LayerRawTexturePayload {
		~LayerRawTexturePayload() {
			try {
				Close();
			} catch (...) {
				// Parent retirement reports failure after releasing texture/process ownership.
			}
		}

		std::unique_ptr<scene::RawTextureTask> Task;
		std::shared_ptr<LayerRawTextureBudget> Budget;
		std::size_t ShadowBytes = 0;

		void Close() {
			if (!Task) {
				return;
			}

			std::exception_ptr failure;
			try {
				Task->Close();
			} catch (...) {
				failure = std::current_exception();
			}

			Task.reset();
			if (Budget) {
				Budget->Bytes -= ShadowBytes;
			}

			ShadowBytes = 0;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	struct LayerRawTextureRecord {
		SquirrelObjectIdentity Instance;
		std::weak_ptr<LayerRawTexturePayload> Payload;
		std::unique_ptr<scene::LayerScaleView> Scale;
		ScriptLayerBindings::BorrowedViewRegistration View;
		bool Releasing = false;
		bool Processing = false;
	};

	enum class LayerRawTextureMethod : std::uint32_t {
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
		GetWidth,
		GetHeight,
		SetOpacity,
		GetOpacity,
		Restore,
		GenerateNoise,
		DrawBlend2D
	};

	static float NativeLayerRawFloat(const SquirrelValue& value) {
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

	static bool NativeLayerRawTruth(const SquirrelValue& value) {
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

	static std::array<std::uint8_t, 8> MethodMetadata(LayerRawTextureMethod method) {
		const auto word = static_cast<std::uint32_t>(method);

		return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24), 0, 0, 0, 0};
	}

	static void RequireMethodMetadata(SquirrelVm& vm, LayerRawTextureMethod expected) {
		const auto userdata = vm.NativeStackValue(-1);
		if (vm.Type(userdata) != SquirrelValueType::UserData) {
			throw std::runtime_error("LayerRawTex method requires its actual captured userdata cell");
		}

		const auto payload = vm.UserDataBytes(userdata);
		const auto metadata = MethodMetadata(expected);
		if (payload.size() != metadata.size() || !std::equal(payload.begin(), payload.end(), metadata.begin())) {
			throw std::runtime_error("LayerRawTex method metadata identity is invalid");
		}
	}

	class ScriptLayerRawTextureBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptRawImageBindings& images, ScriptPsbBindings& psb, ScriptMathBindings& math, scene::LayerTree& tree, scene::MutableTextureBackend& backend, ScriptLayerRawTextureLimits limits) : Vm(vm), Objects(objects), Layers(layers), Images(images), Psb(psb), Math(math), Tree(tree), Backend(backend), Limits(limits), Budget(std::make_shared<LayerRawTextureBudget>()) {
			if (limits.MaxInstances == 0 || limits.MaxInstances > 65536 || limits.MaxShadowBytes == 0 || limits.MaxShadowBytes > 256 * 1024 * 1024 || limits.MaxTasksPerGroup == 0 || limits.MaxTasksPerGroup > 65536 || limits.OnProcessInstructionBudget == 0) {
				throw std::runtime_error("LayerRawTex requires positive bounded wrapper/shadow/task/instruction limits");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		ScriptRawImageBindings& Images;
		ScriptPsbBindings& Psb;
		ScriptMathBindings& Math;
		scene::LayerTree& Tree;
		scene::MutableTextureBackend& Backend;
		ScriptLayerRawTextureLimits Limits;
		std::shared_ptr<LayerRawTextureBudget> Budget;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, std::shared_ptr<LayerRawTextureRecord>> Instances;

		std::shared_ptr<LayerRawTextureRecord> Record(const SquirrelValue& instance) const {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("LayerRawTex callback requires its actual native instance");
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second->Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("LayerRawTex receiver has no live native wrapper identity");
			}

			return found->second;
		}

		std::shared_ptr<LayerRawTexturePayload> Payload(const LayerRawTextureRecord& record) const {
			const auto payload = record.Payload.lock();
			if (record.Scale->State().Detached || !payload || !payload->Task || payload->Task->State().Closed) {
				throw std::runtime_error("LayerRawTex task is detached or its native parent owner has expired");
			}

			return payload;
		}

		void OnProcess(const std::shared_ptr<LayerRawTextureRecord>& record) {
			if (record->Releasing || record->Scale->State().Detached || Vm.IsClosed()) {
				return;
			}
			if (record->Processing || Vm.CurrentContext() != Vm.RootContext()) {
				throw std::runtime_error("LayerRawTex onProcess requires a stable base-VM host update boundary");
			}

			const auto receiver = Vm.ResolveObjectIdentity(record->Instance);
			if (!receiver) {
				return;
			}

			record->Processing = true;
			try {
				SquirrelValue method;
				try {
					method = Vm.ReadSlot(*receiver, std::string("onProcess"), Limits.OnProcessInstructionBudget);
				} catch (const SquirrelVmException&) {
					// Original lookup failure pushes null; its last-error value remains owned by the base VM.
				}

				auto method_owner = Vm.Retain(method);
				auto type = Vm.Type(method);
				if (type == SquirrelValueType::WeakReference) {
					const auto resolved = Vm.ResolveWeakReference(method);
					type = Vm.Type(resolved);
				}
				if (type == SquirrelValueType::Closure || type == SquirrelValueType::NativeClosure) {
					const auto callable = Vm.Type(method_owner.Value()) == SquirrelValueType::WeakReference ? Vm.ResolveWeakReference(method_owner.Value()) : method_owner.Value();
					try {
						static_cast<void>(Vm.InvokeWithoutResultInContext(Vm.RootContext(), callable, std::array<SquirrelValue, 1>{*receiver}, Limits.OnProcessInstructionBudget, true));
					} catch (const SquirrelVmException&) {
						// The original ignores VM call status after raising through its normal error handler.
					}
				}
			} catch (...) {
				record->Processing = false;

				throw;
			}

			record->Processing = false;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& binding_owner) {
			if (arguments.size() < 4 || !Vm.IsInstanceOf(arguments[0], Class) || !Vm.IsInstanceOf(arguments[1], Layers.LayerClass())) {
				throw std::runtime_error("LayerRawTex constructor requires its genuine instance, actual Layer, width and height");
			}

			const auto width_word = TryNativeInteger(arguments[2]);
			const auto height_word = TryNativeInteger(arguments[3]);
			if (!width_word || !height_word) {
				throw std::runtime_error("LayerRawTex dimensions require native integer-convertible values");
			}

			const auto width = std::bit_cast<std::uint32_t>(*width_word);
			const auto height = std::bit_cast<std::uint32_t>(*height_word);
			const auto index = std::get<SquirrelObjectHandle>(arguments[0]).Index;
			const auto available = Limits.MaxShadowBytes - Budget->Bytes;
			if (Instances.contains(index) || Instances.size() >= Limits.MaxInstances || width == 0 || height == 0 || width > 16384 || height > 16384 || std::size_t(width) > available / 4 / height) {
				throw std::runtime_error("LayerRawTex construction exceeds positive native extent or aggregate shadow limits");
			}

			auto record = std::make_shared<LayerRawTextureRecord>();
			record->Instance = Vm.BorrowObjectIdentity(arguments[0]);
			const auto weak_record = std::weak_ptr<LayerRawTextureRecord>(record);
			auto payload = std::make_shared<LayerRawTexturePayload>();
			payload->Task = std::make_unique<scene::RawTextureTask>(Backend, resource::MotionPoint{static_cast<float>(width), static_cast<float>(height)}, "8888", Limits.MaxTasksPerGroup);
			payload->Budget = Budget;
			payload->ShadowBytes = payload->Task->Texture().State().ShadowBytes;
			Budget->Bytes += payload->ShadowBytes;
			record->Payload = payload;
			bool object_attached = false;
			try {
				record->View = Layers.BorrowMotionView(arguments[1], [weak_record](bool smoothing) {
					if (const auto view = weak_record.lock(); view && !view->Scale->State().Detached) {
						if (const auto task = view->Payload.lock(); task && task->Task) {
							task->Task->SetFiltering(smoothing ? 1u : 0u, smoothing ? 1u : 0u);
						}
					}
				},
				    [weak_record] {
					    if (const auto view = weak_record.lock()) {
						    view->Scale->DetachParent();
						    view->Payload.reset();
					    }
				    });
				scene::LayerScaleTaskCallbacks callbacks;
				callbacks.Update = [binding_owner, weak_record, payload] {
					if (const auto owner = binding_owner.lock()) {
						if (const auto view = weak_record.lock(); view && payload->Task->SerialTasks().Count() == 0) {
							owner->OnProcess(view);
						}
					}

					payload->Task->Update();
				};
				callbacks.Release = [payload] {
					payload->Close();
				};
				record->Scale = std::make_unique<scene::LayerScaleView>(Layers.MotionTasks(arguments[1]), std::move(callbacks), Limits.MaxTasksPerGroup);
				const auto smoothing = Tree.State(Layers.Handle(arguments[1])).Smoothing;
				payload->Task->SetFiltering(smoothing ? 1u : 0u, smoothing ? 1u : 0u);
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
			if (record->Releasing || record->Processing) {
				throw std::runtime_error("LayerRawTex cannot retire its borrowed wrapper during an active callback");
			}

			record->Scale->Retire();
			if (!record->Scale->State().Detached) {
				Payload(*record)->Task->SetStatus(scene::TaskStatus::Retire);
			}

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

		void Restore(const LayerRawTextureRecord& record) {
			const auto payload = Payload(record);
			// Native4dc10 removes only the real captured userdata before exact top2 validation.
			Vm.SetNativeStackSize(Vm.NativeStackSize() - 1);
			if (Vm.NativeStackSize() != 2) {
				throw std::runtime_error("invalid argument.");
			}

			const auto source = Vm.NativeStackValue(2);
			if (Vm.IsInstanceOf(source, Images.RawImageClass())) {
				const auto& image = Images.Image(source);
				const auto extent = payload->Task->State().LogicalExtent;
				if (static_cast<float>(image.Width()) == extent.X && static_cast<float>(image.Height()) == extent.Y) {
					payload->Task->RestoreImage(image);
				}

				return;
			}
			if (!Vm.IsInstanceOf(source, Psb.ValueClass())) {
				throw std::runtime_error("invalid argument.");
			}

			const auto asset = Psb.ValueAsset(source);
			const auto* chunk = std::get_if<resource::PsbChunkReference>(&asset.Value->Data);
			if (!chunk) {
				throw std::runtime_error("require char array.");
			}

			const auto& chunks = chunk->Extra ? asset.Owner.Document->ExtraChunks : asset.Owner.Document->Chunks;
			if (chunk->Index >= chunks.size() || !asset.Owner.Backing) {
				throw std::runtime_error("LayerRawTex PSB char array requires its bounded original backing");
			}

			const auto range = chunks[chunk->Index];
			const auto description = payload->Task->Texture().State().Description;
			const auto byte_count = std::size_t{description.Width} * description.Height * 3;
			if (range.Size != byte_count) {
				throw std::runtime_error("char array size mismatch.");
			}
			if (range.Offset > asset.Owner.Backing->size() || range.Size > asset.Owner.Backing->size() - range.Offset) {
				throw std::runtime_error("LayerRawTex PSB char array range exceeds its original backing");
			}

			payload->Task->RestoreRgb(std::span(*asset.Owner.Backing).subspan(static_cast<std::size_t>(range.Offset), byte_count));
		}

		void GenerateNoise(const LayerRawTextureRecord& record) {
			if (record.Scale->State().Detached) {
				return;
			}

			const auto payload = Payload(record);
			auto& texture = payload->Task->Texture();
			const auto description = texture.State().Description;
			const auto mapping = texture.Map();
			try {
				const auto pitch = texture.RowPitch();
				if (!mapping.Succeeded || pitch < description.Width * 4 || description.Height > mapping.Bytes.size() / pitch) {
					throw std::runtime_error("LayerRawTex noise mapping is smaller than its bounded BGRA rows");
				}

				for (std::uint32_t y = 0; y != description.Height; ++y) {
					for (std::uint32_t x = 0; x != description.Width; ++x) {
						const auto value = static_cast<std::byte>(Math.Random() / 128);
						const auto offset = std::size_t{y} * pitch + x * 4;
						mapping.Bytes[offset] = value;
						mapping.Bytes[offset + 1] = value;
						mapping.Bytes[offset + 2] = value;
						mapping.Bytes[offset + 3] = std::byte{255};
					}
				}
			} catch (...) {
				texture.Unmap();

				throw;
			}

			texture.Unmap();
		}

		SquirrelNativeResult Invoke(LayerRawTextureMethod method, const SquirrelValue& receiver) {
			const auto record = Record(receiver);
			const auto detached = record->Scale->State().Detached;
			switch (method) {
			case LayerRawTextureMethod::SetOffset:
				record->Scale->SetOffset({NativeLayerRawFloat(Vm.NativeAllocatedStackValue(2)), NativeLayerRawFloat(Vm.NativeAllocatedStackValue(3))});
				break;
			case LayerRawTextureMethod::SetScale:
				record->Scale->SetScale({NativeLayerRawFloat(Vm.NativeAllocatedStackValue(2)), NativeLayerRawFloat(Vm.NativeAllocatedStackValue(3))});
				break;
			case LayerRawTextureMethod::SetRotation:
				record->Scale->SetRotation(NativeLayerRawFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case LayerRawTextureMethod::SetPrimaryScale:
				record->Scale->SetPrimaryScale(NativeLayerRawFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case LayerRawTextureMethod::SetVisible:
				if (!detached) {
					const auto visible = NativeLayerRawTruth(Vm.NativeAllocatedStackValue(2));
					record->Scale->SetVisible(visible);
					Payload(*record)->Task->SetDrawPaused(!visible);
				}
				break;
			case LayerRawTextureMethod::GetVisible:
				return SquirrelNativeResult::ReturnValue(!detached && !record->Scale->TaskState().DrawPaused);
			case LayerRawTextureMethod::SetCoordinate:
				if (!detached) {
					Payload(*record)->Task->SetCoordinate({NativeLayerRawFloat(Vm.NativeAllocatedStackValue(2)), NativeLayerRawFloat(Vm.NativeAllocatedStackValue(3))});
				}
				break;
			case LayerRawTextureMethod::SetLeft:
				if (!detached) {
					const auto payload = Payload(*record);
					payload->Task->SetCoordinate({NativeLayerRawFloat(Vm.NativeAllocatedStackValue(2)), payload->Task->State().Coordinate.Y});
				}
				break;
			case LayerRawTextureMethod::GetLeft:
				return SquirrelNativeResult::ReturnValue(detached ? 0.0f : Payload(*record)->Task->State().Coordinate.X);
			case LayerRawTextureMethod::SetTop:
				if (!detached) {
					const auto payload = Payload(*record);
					payload->Task->SetCoordinate({payload->Task->State().Coordinate.X, NativeLayerRawFloat(Vm.NativeAllocatedStackValue(2))});
				}
				break;
			case LayerRawTextureMethod::GetTop:
				return SquirrelNativeResult::ReturnValue(detached ? 0.0f : Payload(*record)->Task->State().Coordinate.Y);
			case LayerRawTextureMethod::GetWidth:
				return SquirrelNativeResult::ReturnValue(detached ? 0 : static_cast<std::int32_t>(Payload(*record)->Task->State().LogicalExtent.X));
			case LayerRawTextureMethod::GetHeight:
				return SquirrelNativeResult::ReturnValue(detached ? 0 : static_cast<std::int32_t>(Payload(*record)->Task->State().LogicalExtent.Y));
			case LayerRawTextureMethod::SetOpacity:
				if (!detached) {
					Payload(*record)->Task->SetOpacity(std::bit_cast<std::uint32_t>(TryNativeInteger(Vm.NativeAllocatedStackValue(2)).value_or(0)));
				}
				break;
			case LayerRawTextureMethod::GetOpacity:
				return SquirrelNativeResult::ReturnValue(detached ? 0 : std::bit_cast<std::int32_t>(Payload(*record)->Task->State().Opacity));
			case LayerRawTextureMethod::Restore:
				Restore(*record);
				break;
			case LayerRawTextureMethod::GenerateNoise:
				GenerateNoise(*record);
				break;
			case LayerRawTextureMethod::DrawBlend2D:
				static_cast<void>(Payload(*record));
				throw std::runtime_error("LayerRawTex drawBlend2D requires its unrecovered scoped native Blend2D operation");
			}

			return SquirrelNativeResult::NoValues();
		}
	};

	ScriptLayerRawTextureBindings::ScriptLayerRawTextureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptRawImageBindings& images, ScriptPsbBindings& psb, ScriptMathBindings& math, scene::LayerTree& tree, scene::MutableTextureBackend& backend, ScriptLayerRawTextureLimits limits) : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, images, psb, math, tree, backend, limits)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(state.Class, std::string("constructor"), vm.CreateNativeClosure("LayerRawTex.constructor", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("LayerRawTex constructor services have expired");
			}

			owner->Construct(arguments, weak);

			return SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("LayerRawTex.weakref", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			const auto owner = weak.lock();
			if (!owner || arguments.empty()) {
				throw std::runtime_error("LayerRawTex weakref requires a live native service and last stack value");
			}

			return SquirrelNativeResult::ReturnValue(owner->Vm.CreateWeakReference(arguments.back()));
		}));
		const auto register_method = [&](const char* name, LayerRawTextureMethod method) {
			const auto metadata = vm.CreateUserData(MethodMetadata(method));
			const std::array<SquirrelValue, 1> captures{metadata};
			vm.NewSlot(state.Class, std::string(name), vm.CreateNativeClosureWithCaptures(std::string("LayerRawTex.") + name, [weak, method](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("LayerRawTex method services or native receiver have expired");
				}

				RequireMethodMetadata(active, method);

				return owner->Invoke(method, arguments[0]);
			},
			                                               captures));
		};
		register_method("setOffset", LayerRawTextureMethod::SetOffset);
		register_method("setScale", LayerRawTextureMethod::SetScale);
		register_method("setRot", LayerRawTextureMethod::SetRotation);
		register_method("setPrimaryScale", LayerRawTextureMethod::SetPrimaryScale);
		register_method("setVisible", LayerRawTextureMethod::SetVisible);
		register_method("getVisible", LayerRawTextureMethod::GetVisible);
		register_method("setCoord", LayerRawTextureMethod::SetCoordinate);
		register_method("setLeft", LayerRawTextureMethod::SetLeft);
		register_method("getLeft", LayerRawTextureMethod::GetLeft);
		register_method("setTop", LayerRawTextureMethod::SetTop);
		register_method("getTop", LayerRawTextureMethod::GetTop);
		register_method("getWidth", LayerRawTextureMethod::GetWidth);
		register_method("getHeight", LayerRawTextureMethod::GetHeight);
		register_method("setOpacity", LayerRawTextureMethod::SetOpacity);
		register_method("getOpacity", LayerRawTextureMethod::GetOpacity);
		register_method("restore", LayerRawTextureMethod::Restore);
		register_method("generateNoise", LayerRawTextureMethod::GenerateNoise);
		register_method("drawBlend2D", LayerRawTextureMethod::DrawBlend2D);
		vm.NewSlot(vm.RootTable(), std::string("LayerRawTex"), state.Class);
	}

	ScriptLayerRawTextureBindings::~ScriptLayerRawTextureBindings() = default;

	SquirrelValue ScriptLayerRawTextureBindings::LayerRawTextureClass() const {
		return m_Implementation->Class;
	}

	scene::RawTextureTask& ScriptLayerRawTextureBindings::Task(const SquirrelValue& instance) {
		return *m_Implementation->Payload(*m_Implementation->Record(instance))->Task;
	}

	scene::LayerScaleView& ScriptLayerRawTextureBindings::ScaleView(const SquirrelValue& instance) {
		return *m_Implementation->Record(instance)->Scale;
	}

	void ScriptLayerRawTextureBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
