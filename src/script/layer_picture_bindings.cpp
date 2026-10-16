#include "sgre/script/layer_picture_bindings.hpp"

#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/layer_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sgre::script {

	struct PictureTaskPayload {
		ScriptNativeImageBorrow Image;
		std::unique_ptr<scene::PictureLayerTask> Task;
		std::vector<scene::MotionTextureWrapper::Borrowed> Sources;
		bool Drawing = false;

		void Close() noexcept {
			if (Task) {
				Task->Close();
				Task.reset();
			}
			Sources.clear();
			Image = {};
		}
	};

	struct LayerPictureRecord {
		SquirrelObjectIdentity Instance;
		SquirrelObjectIdentity Parent;
		SquirrelHostRoot ImageOwner;
		std::weak_ptr<PictureTaskPayload> Payload;
		std::unique_ptr<scene::LayerScaleView> Scale;
		ScriptLayerBindings::BorrowedViewRegistration View;
		bool Releasing = false;
	};

	enum class PictureMethod : std::uint32_t {
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
		SetBlendMode,
		GetBlendMode,
		SetMeshSize,
		ClearImageRange,
		AssignImageRange,
		Contains,
		RegisterVertexModulator,
		UnregisterVertexModulator
	};

	static float PictureFloat(const SquirrelValue& value) {
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

	static bool PictureTruth(const SquirrelValue& value) {
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

	static std::array<std::uint8_t, 8> PictureMetadata(PictureMethod method) {
		const auto word = static_cast<std::uint32_t>(method);

		return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24), 0, 0, 0, 0};
	}

	static void RequirePictureMetadata(SquirrelVm& vm, PictureMethod method) {
		const auto captured = vm.NativeStackValue(-1);
		if (vm.Type(captured) != SquirrelValueType::UserData) {
			throw std::runtime_error("LayerPicture method requires its genuine captured userdata cell");
		}

		const auto bytes = vm.UserDataBytes(captured);
		const auto expected = PictureMetadata(method);
		if (bytes.size() != expected.size() || !std::equal(bytes.begin(), bytes.end(), expected.begin())) {
			throw std::runtime_error("LayerPicture captured method identity is invalid");
		}
	}

	class ScriptLayerPictureBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptImageBindings& images, scene::LayerTree& tree, ScriptLayerPictureLimits limits)
		    : Vm(vm), Objects(objects), Layers(layers), Images(images), Tree(tree), Limits(limits) {
			if (limits.MaxInstances == 0 || limits.MaxInstances > 65536 || limits.MaxRegions == 0 || limits.MaxRegions > 65536 || limits.MaxCameraTasks == 0 || limits.MaxCameraTasks > 65536) {
				throw std::runtime_error("LayerPicture requires positive bounded wrapper/region/camera capacities");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		ScriptImageBindings& Images;
		scene::LayerTree& Tree;
		ScriptLayerPictureLimits Limits;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, std::shared_ptr<LayerPictureRecord>> Instances;
		scene::LayerCameraDrawScope* Renderer = nullptr;
		std::function<void(const scene::PictureDrawFrame&)> Draw;
		bool EverConstructed = false;

		std::shared_ptr<LayerPictureRecord> Record(const SquirrelValue& instance) const {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("LayerPicture requires its genuine native script instance");
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second->Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("LayerPicture receiver has no live native wrapper identity");
			}

			return found->second;
		}

		std::shared_ptr<PictureTaskPayload> Payload(const LayerPictureRecord& record) const {
			const auto payload = record.Payload.lock();
			if (record.Scale->State().Detached || !payload || !payload->Task || payload->Task->State().Closed || payload->Drawing) {
				throw std::runtime_error("LayerPicture native task has detached or retired");
			}

			return payload;
		}

		bool Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			if (arguments.size() < 3 || !Vm.IsInstanceOf(arguments[1], Layers.LayerClass())) {
				throw std::runtime_error("invalid argument");
			}
			if (arguments.size() != 3 && arguments.size() != 7) {
				// Original factory returns failure without replacing last error for unsupported positive arity.
				return false;
			}
			if (!Vm.IsInstanceOf(arguments[0], Class) || !Vm.IsInstanceOf(arguments[2], Images.ImageClass())) {
				throw std::runtime_error("invalid argument.");
			}

			const auto index = std::get<SquirrelObjectHandle>(arguments[0]).Index;
			if (Instances.contains(index) || Instances.size() == Limits.MaxInstances) {
				throw std::runtime_error("LayerPicture wrapper capacity is exhausted or its receiver is already initialized");
			}
			std::optional<std::array<float, 4>> range;
			if (arguments.size() == 7) {
				const auto x = PictureFloat(arguments[3]);
				const auto y = PictureFloat(arguments[4]);
				range = {x, y, PictureFloat(arguments[5]) + x, PictureFloat(arguments[6]) + y};
			}

			auto record = std::make_shared<LayerPictureRecord>();
			record->Instance = Vm.BorrowObjectIdentity(arguments[0]);
			record->Parent = Vm.BorrowObjectIdentity(arguments[1]);
			record->ImageOwner = Vm.Retain(arguments[2]);
			auto payload = std::make_shared<PictureTaskPayload>();
			payload->Image = Images.BorrowNativeImage(arguments[2]);
			payload->Task = std::make_unique<scene::PictureLayerTask>(payload->Image.Asset(), range, Limits.MaxRegions);
			payload->Sources.reserve(payload->Image.Asset()->Model.Tiles.size());
			for (std::size_t source = 0; source < payload->Image.Asset()->Model.Tiles.size(); ++source) {
				payload->Sources.push_back(payload->Image.Source(source));
			}
			record->Payload = payload;
			const auto weak_record = std::weak_ptr<LayerPictureRecord>(record);
			bool object_attached = false;
			try {
				record->View = Layers.BorrowMotionView(
				    arguments[1], [weak_payload = std::weak_ptr<PictureTaskPayload>(payload)](bool smoothing) {
					    if (const auto owner = weak_payload.lock(); owner && owner->Task) {
						    owner->Task->SetFiltering(smoothing, smoothing);
					    }
				    },
				    [weak_record] {
					    if (const auto owner = weak_record.lock()) {
						    owner->Scale->DetachParent();
						    owner->Payload.reset();
					    }
				    });
				scene::LayerScaleTaskCallbacks callbacks;
				callbacks.InitiallyDrawPaused = false;
				callbacks.Draw = [payload, draw = Draw](std::uint32_t) {
					if (!draw) {
						throw std::runtime_error("LayerPicture drawing requires its actual configured frame receiver");
					}
					if (payload->Drawing || !payload->Image.IsLive() || !payload->Task || payload->Task->State().Closed) {
						throw std::runtime_error("LayerPicture draw requires its live task and genuine Image outside reentry");
					}
					payload->Drawing = true;
					try {
						scene::PictureDrawFrame frame;
						frame.State = payload->Task->State();
						frame.Regions = payload->Task->Regions();
						frame.Sources = payload->Sources;
						draw(frame);
					} catch (...) {
						payload->Drawing = false;
						throw;
					}
					payload->Drawing = false;
				};
				if (Renderer != nullptr) {
					callbacks.DrawWithCamera = [renderer = Renderer](const auto& camera, auto& children, auto argument) {
						renderer->Draw(scene::PrepareLayerCameraParameters(camera), children, argument);
					};
				}
				callbacks.Release = [payload] {
					payload->Close();
				};
				record->Scale = std::make_unique<scene::LayerScaleView>(Layers.MotionTasks(arguments[1]), std::move(callbacks), Limits.MaxCameraTasks);
				const auto smoothing = Tree.State(Layers.Handle(arguments[1])).Smoothing;
				payload->Task->SetFiltering(smoothing, smoothing);
				Objects.AttachInstance(arguments[0]);
				object_attached = true;
				Instances.emplace(index, record);
				EverConstructed = true;
				Objects.ConfigureNativeRelease(arguments[0], [weak](const SquirrelValue& receiver) {
					if (const auto owner = weak.lock()) {
						owner->Release(receiver);
					}
				});
			} catch (...) {
				const auto failure = std::current_exception();
				record->ImageOwner = {};
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

			return true;
		}

		void Release(const SquirrelValue& instance) {
			Objects.RequireCanRelease(instance);
			const auto record = Record(instance);
			if (const auto payload = record->Payload.lock(); payload && payload->Drawing) {
				throw std::runtime_error("LayerPicture cannot retire during its actual receiving call");
			}
			if (record->Releasing) {
				throw std::runtime_error("LayerPicture native wrapper is already retiring");
			}
			if (!record->Scale->State().Detached) {
				const auto parent = Vm.ResolveObjectIdentity(record->Parent);
				if (!parent) {
					throw std::runtime_error("LayerPicture source retirement requires its stable native parent or completed detach");
				}

				Layers.RequireCanReleaseInstance(*parent);
			}

			record->Releasing = true;
			// The native wrapper drops its strong Image before marking its borrowed task for deferred retirement.
			record->ImageOwner = {};
			record->Scale->Retire();
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

		void AssignRange(const LayerPictureRecord& record) {
			if (record.Scale->State().Detached) {
				return;
			}
			const auto count = Vm.NativeStackSize();
			if (count != 5 && count != 7) {
				throw std::runtime_error("invalid argument.");
			}
			const auto payload = Payload(record);
			if (!payload->Image.IsLive()) {
				throw std::runtime_error("LayerPicture image source retired before range preparation");
			}

			const std::array<float, 4> rectangle{PictureFloat(Vm.NativeStackValue(2)), PictureFloat(Vm.NativeStackValue(3)), PictureFloat(Vm.NativeStackValue(4)), PictureFloat(Vm.NativeStackValue(5))};
			std::array<float, 2> destination{};
			if (count == 7) {
				destination = {PictureFloat(Vm.NativeStackValue(6)), PictureFloat(Vm.NativeStackValue(7))};
			}

			payload->Task->AppendImageRange(rectangle, destination);
		}

		bool Contains(const LayerPictureRecord& record) const {
			if (record.Scale->State().Detached) {
				return false;
			}

			const auto state = record.Scale->State();
			auto x = PictureFloat(Vm.NativeAllocatedStackValue(2)) + state.Offset.X;
			auto y = PictureFloat(Vm.NativeAllocatedStackValue(3)) + state.Offset.Y;
			if (state.Scale.X != 0) {
				x /= state.Scale.X;
			}
			if (state.Scale.Y != 0) {
				y /= state.Scale.Y;
			}

			return Payload(record)->Task->Contains({x, y});
		}

		SquirrelNativeResult Invoke(PictureMethod method, const SquirrelValue& receiver) {
			const auto record = Record(receiver);
			if (const auto payload = record->Payload.lock(); payload && payload->Drawing) {
				throw std::runtime_error("LayerPicture cannot reenter during its actual receiving call");
			}

			if (method == PictureMethod::AssignImageRange || method == PictureMethod::RegisterVertexModulator || method == PictureMethod::UnregisterVertexModulator) {
				// Original 0x14004dc10 pops appended metadata before forwarding the user stack to the VM-taking method.
				Vm.SetNativeStackSize(Vm.NativeStackSize() - 1);
			}
			if (method == PictureMethod::AssignImageRange) {
				AssignRange(*record);

				return SquirrelNativeResult::NoValues();
			}
			if (method == PictureMethod::RegisterVertexModulator || method == PictureMethod::UnregisterVertexModulator) {
				throw std::runtime_error("LayerPicture vertex modulators require their genuine class and native owner before registration");
			}

			const auto detached = record->Scale->State().Detached;
			switch (method) {
			case PictureMethod::SetOffset:
				record->Scale->SetOffset({PictureFloat(Vm.NativeAllocatedStackValue(2)), PictureFloat(Vm.NativeAllocatedStackValue(3))});
				break;
			case PictureMethod::SetScale:
				record->Scale->SetScale({PictureFloat(Vm.NativeAllocatedStackValue(2)), PictureFloat(Vm.NativeAllocatedStackValue(3))});
				break;
			case PictureMethod::SetRotation:
				record->Scale->SetRotation(PictureFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case PictureMethod::SetPrimaryScale:
				record->Scale->SetPrimaryScale(PictureFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case PictureMethod::SetVisible:
				record->Scale->SetVisible(PictureTruth(Vm.NativeAllocatedStackValue(2)));
				break;
			case PictureMethod::GetVisible:
				return SquirrelNativeResult::ReturnValue(!detached && !record->Scale->TaskState().DrawPaused);
			case PictureMethod::Contains:
				return SquirrelNativeResult::ReturnValue(Contains(*record));
			default:
				return InvokePayload(method, *record, detached);
			}

			return SquirrelNativeResult::NoValues();
		}

		SquirrelNativeResult InvokePayload(PictureMethod method, const LayerPictureRecord& record, bool detached) {
			if (method == PictureMethod::GetLeft || method == PictureMethod::GetTop) {
				if (detached) {
					return SquirrelNativeResult::ReturnValue(0.0f);
				}

				const auto coordinate = Payload(record)->Task->State().Coordinate;

				return SquirrelNativeResult::ReturnValue(method == PictureMethod::GetLeft ? coordinate.X : coordinate.Y);
			}
			if (method == PictureMethod::GetWidth || method == PictureMethod::GetHeight || method == PictureMethod::GetOpacity || method == PictureMethod::GetBlendMode) {
				if (detached) {
					return SquirrelNativeResult::ReturnValue(std::int32_t(0));
				}

				const auto state = Payload(record)->Task->State();
				std::uint32_t value = state.Width;
				if (method == PictureMethod::GetHeight) {
					value = state.Height;
				}
				if (method == PictureMethod::GetOpacity) {
					value = state.Opacity;
				}
				if (method == PictureMethod::GetBlendMode) {
					value = state.BlendMode;
				}

				return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(value));
			}
			if (detached) {
				return SquirrelNativeResult::NoValues();
			}

			auto& task = *Payload(record)->Task;
			switch (method) {
			case PictureMethod::SetCoordinate:
				task.SetCoordinate({PictureFloat(Vm.NativeAllocatedStackValue(2)), PictureFloat(Vm.NativeAllocatedStackValue(3))});
				break;
			case PictureMethod::SetLeft:
				task.SetLeft(PictureFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case PictureMethod::SetTop:
				task.SetTop(PictureFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case PictureMethod::SetOpacity:
				task.SetOpacity(std::bit_cast<std::uint32_t>(TryNativeInteger(Vm.NativeAllocatedStackValue(2)).value_or(0)));
				break;
			case PictureMethod::SetBlendMode:
				task.SetBlendMode(std::bit_cast<std::uint32_t>(TryNativeInteger(Vm.NativeAllocatedStackValue(2)).value_or(0)));
				break;
			case PictureMethod::SetMeshSize:
				task.SetMeshSize(PictureFloat(Vm.NativeAllocatedStackValue(2)));
				break;
			case PictureMethod::ClearImageRange:
				task.ClearImageRanges();
				break;
			default:
				throw std::runtime_error("LayerPicture method has no recovered payload dispatch");
			}

			return SquirrelNativeResult::NoValues();
		}
	};

	ScriptLayerPictureBindings::ScriptLayerPictureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptImageBindings& images, scene::LayerTree& tree, ScriptLayerPictureLimits limits)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, images, tree, limits)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(state.Class, std::string("constructor"), vm.CreateNativeClosure("LayerPicture.constructor", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("LayerPicture constructor services have expired");
			}

			return owner->Construct(arguments, weak) ? SquirrelNativeResult::NoValues() : SquirrelNativeResult::StackStatus(-1);
		}));
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("LayerPicture.weakref", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner || arguments.empty()) {
				throw std::runtime_error("LayerPicture weakref requires its live services and final original argument");
			}

			return SquirrelNativeResult::ReturnValue(owner->Vm.CreateWeakReference(arguments.back()));
		}));
		const auto method = [&](const char* name, PictureMethod identity) {
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(PictureMetadata(identity))};
			vm.NewSlot(state.Class, std::string(name), vm.CreateNativeClosureWithCaptures(
			                                               std::string("LayerPicture.") + name, [weak, identity](SquirrelVm& active, auto arguments) {
				                                               const auto owner = weak.lock();
				                                               if (!owner || arguments.empty()) {
					                                               throw std::runtime_error("LayerPicture method requires its live services and original receiver");
				                                               }
				                                               RequirePictureMetadata(active, identity);

				                                               return owner->Invoke(identity, arguments[0]);
			                                               },
			                                               captures));
		};
		method("setOffset", PictureMethod::SetOffset);
		method("setScale", PictureMethod::SetScale);
		method("setRot", PictureMethod::SetRotation);
		method("setPrimaryScale", PictureMethod::SetPrimaryScale);
		method("setVisible", PictureMethod::SetVisible);
		method("getVisible", PictureMethod::GetVisible);
		method("setCoord", PictureMethod::SetCoordinate);
		method("setLeft", PictureMethod::SetLeft);
		method("getLeft", PictureMethod::GetLeft);
		method("setTop", PictureMethod::SetTop);
		method("getTop", PictureMethod::GetTop);
		method("getWidth", PictureMethod::GetWidth);
		method("getHeight", PictureMethod::GetHeight);
		method("setOpacity", PictureMethod::SetOpacity);
		method("getOpacity", PictureMethod::GetOpacity);
		method("setBlendMode", PictureMethod::SetBlendMode);
		method("getBlendMode", PictureMethod::GetBlendMode);
		method("setMeshSize", PictureMethod::SetMeshSize);
		method("clearImageRange", PictureMethod::ClearImageRange);
		method("assignImageRange", PictureMethod::AssignImageRange);
		method("contains", PictureMethod::Contains);
		method("registerVertexModulator", PictureMethod::RegisterVertexModulator);
		method("unregisterVertexModulator", PictureMethod::UnregisterVertexModulator);
		vm.NewSlot(vm.RootTable(), std::string("LayerPicture"), state.Class);
	}

	ScriptLayerPictureBindings::~ScriptLayerPictureBindings() = default;

	void ScriptLayerPictureBindings::ConfigureFrameOwners(scene::LayerCameraDrawScope& actual_renderer, std::function<void(const scene::PictureDrawFrame&)> draw) {
		auto& state = *m_Implementation;
		if (!draw || state.Renderer != nullptr || state.EverConstructed || !state.Instances.empty() || state.Vm.IsClosed()) {
			throw std::runtime_error("LayerPicture frame services require complete actual owners once before construction");
		}
		state.Renderer = &actual_renderer;
		state.Draw = std::move(draw);
	}

	SquirrelValue ScriptLayerPictureBindings::LayerPictureClass() const {
		return m_Implementation->Class;
	}

	scene::PictureLayerTaskState ScriptLayerPictureBindings::PreparedState(const SquirrelValue& instance) const {
		const auto& state = *m_Implementation;

		return state.Payload(*state.Record(instance))->Task->State();
	}

	scene::LayerScaleView& ScriptLayerPictureBindings::ScaleView(const SquirrelValue& instance) {
		return *m_Implementation->Record(instance)->Scale;
	}

	void ScriptLayerPictureBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
