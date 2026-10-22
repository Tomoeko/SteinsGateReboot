#include "sgre/script/raw_texture_bindings.hpp"

#include "sgre/script/binary_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/raw_image_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace sgre::script {

	struct RawTextureConstructorPlan {
		SquirrelValue Parent;
		std::int32_t Width = 0;
		std::int32_t Height = 0;
		std::string ColorMode = "8888";
		const scene::RawImage* InitialImage = nullptr;
		bool HasSpecFlag = false;
		bool SpecFlag = false;
		std::int32_t MipCount = 1;
	};

	struct RawTextureBindingRecord {
		SquirrelObjectIdentity Instance;
		std::unique_ptr<scene::RawLayerOwner> Owner;
		// Frame dies before its concrete owner; no VM receiver is retained by these callbacks.
		std::unique_ptr<scene::RawLayerFrame> Frame;
		ScriptRawTextureBindings::DrawTexture Drawing;
		std::function<void(const SquirrelValue&)> Dispatch;
		const ScriptRawTextureBindings::FrameOperation* ActiveOperation = nullptr;
		const SquirrelValue* FrameReceiver = nullptr;
		scene::LayerFrameProcessState Process;
		bool Dispatching = false;
		std::weak_ptr<scene::ParallelTaskSet> Background;
		const SquirrelValue* ClosingReceiver = nullptr;
		std::size_t ShadowBytes = 0;
	};

	static float RawTextureNumericArgument(std::span<const SquirrelValue> arguments, std::size_t index) {
		if (index >= arguments.size()) {
			return 0;
		}
		if (const auto* value = std::get_if<float>(&arguments[index])) {
			return *value;
		}
		if (const auto* value = std::get_if<std::int32_t>(&arguments[index])) {
			return static_cast<float>(*value);
		}
		if (const auto* value = std::get_if<bool>(&arguments[index])) {
			return *value ? 1.0f : 0.0f;
		}

		return 0;
	}

	static ScriptRawTextureBindings::PriorityTaskProvider BorrowRawTexturePriority(scene::ParallelTaskSet& driver) {
		return [&driver]() -> scene::ParallelTaskSet& {
			return driver;
		};
	}

	class RawTexturePriorityResolution {
	public:
		explicit RawTexturePriorityResolution(bool& resolving) : m_Resolving(resolving) {
			if (resolving) {
				throw std::runtime_error("RawTex Priority service resolution cannot reenter");
			}

			m_Resolving = true;
		}
		~RawTexturePriorityResolution() {
			m_Resolving = false;
		}
		RawTexturePriorityResolution(const RawTexturePriorityResolution&) = delete;
		RawTexturePriorityResolution& operator=(const RawTexturePriorityResolution&) = delete;

	private:
		bool& m_Resolving;
	};

	class ScriptRawTextureBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptRawImageBindings& images, ScriptBinaryBindings& binary, scene::LayerTree& tree, scene::MutableTextureBackend& backend, PriorityTaskProvider priority, ScriptRawTextureLimits limits) : Vm(vm), Objects(objects), Layers(layers), Images(images), Binary(binary), Tree(tree), Backend(backend), Priority(std::move(priority)), Limits(limits) {
			if (!Priority || limits.MaxInstances == 0 || limits.MaxInstances > 65536 || limits.MaxShadowBytes == 0 || limits.MaxShadowBytes > 256 * 1024 * 1024 || limits.MaxBackgroundTasks == 0 || limits.MaxBackgroundTasks > 65536 || limits.OnProcessInstructionBudget == 0) {
				throw std::runtime_error("RawTex services require an actual Priority provider and positive bounded instance/shadow/task limits");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		ScriptRawImageBindings& Images;
		ScriptBinaryBindings& Binary;
		scene::LayerTree& Tree;
		scene::MutableTextureBackend& Backend;
		PriorityTaskProvider Priority;
		bool ResolvingPriority = false;
		ScriptRawTextureLimits Limits;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, RawTextureBindingRecord> Instances;
		std::size_t ShadowBytes = 0;
		scene::LayerCameraDrawScope* FrameRenderer = nullptr;
		DrawTexture FrameDrawing;
		bool EverConstructed = false;

		void RequirePriorityIdle() const {
			if (ResolvingPriority) {
				throw std::runtime_error("RawTex Priority service provider cannot mutate or reenter its binding");
			}
		}

		scene::ParallelTaskSet& ResolvePriority(const SquirrelValue& instance) {
			RequirePriorityIdle();
			if (Vm.IsClosed()) {
				throw std::runtime_error("RawTex Priority lookup requires its actual live VM and constructor receiver");
			}

			const auto identity = Vm.BorrowObjectIdentity(instance);
			const RawTexturePriorityResolution resolution(ResolvingPriority);
			auto& priority = Priority();
			if (Vm.IsClosed() || Vm.BorrowObjectIdentity(instance) != identity || priority.Status() == scene::TaskStatus::Retire) {
				throw std::runtime_error("RawTex Priority provider retired its actual VM, receiver or task owner during lookup");
			}

			return priority;
		}

		RawTextureBindingRecord& Record(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("RawTex callback requires its actual native receiver");
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("RawTex receiver has no live native owner");
			}

			return found->second;
		}

		void ParseOptions(RawTextureConstructorPlan& plan, const SquirrelValue& options) {
			if (Vm.FindRawSlot(options, std::string("specFlag"))) {
				plan.HasSpecFlag = true;
				const auto value = Vm.ReadSlot(options, std::string("specFlag"));
				plan.SpecFlag = std::get_if<bool>(&value) && std::get<bool>(value);
			}
			if (Vm.FindRawSlot(options, std::string("mipCount"))) {
				const auto value = Vm.ReadSlot(options, std::string("mipCount"));
				const auto* text = TryStringText(value);
				if (text && std::string_view(*text).substr(0, text->find('\0')) == "auto") {
					auto extent = std::max(plan.Width, plan.Height);
					while (extent > 1) {
						extent >>= 1;
						++plan.MipCount;
					}
				} else {
					plan.MipCount = TryNativeInteger(value).value_or(0);
				}
			}
			if (Vm.FindRawSlot(options, std::string("colorMode"))) {
				const auto value = Vm.ReadSlot(options, std::string("colorMode"));
				const auto* text = TryStringText(value);
				if (!text) {
					throw std::runtime_error("RawTex colorMode requires a valid C-string rather than native undefined input");
				}

				plan.ColorMode = text->substr(0, text->find('\0'));
			}
		}

		RawTextureBindingRecord& Record(scene::LayerHandle handle) {
			RequirePriorityIdle();
			for (auto& [index, record] : Instances) {
				static_cast<void>(index);
				if (record.Owner->Handle() == handle && !record.Owner->IsClosed() && !record.ClosingReceiver && Tree.Contains(handle)) {
					return record;
				}
			}

			throw std::runtime_error("RawTex handle does not identify its actual live registered owner");
		}

		scene::RawLayerFrameState ReadFrameState(const RawTextureBindingRecord& record) const {
			if (record.ClosingReceiver || record.Owner->IsClosed()) {
				throw std::runtime_error("RawTex frame state cannot borrow a retiring owner");
			}

			const auto common = Tree.State(record.Owner->Handle());
			scene::RawLayerFrameState state;
			state.BaseClip = {common.BaseClip.Left, common.BaseClip.Top, common.BaseClip.Right, common.BaseClip.Bottom};
			state.Camera.Pivot = {common.Transform.Center.X, common.Transform.Center.Y};
			state.Camera.Offset = {common.Transform.Offset.X, common.Transform.Offset.Y};
			state.Camera.Scale = {common.Transform.Scale.X, common.Transform.Scale.Y};
			state.Camera.RotationRadians = common.Transform.AngleRadians;
			state.Camera.Clip = {common.Clip.Left, common.Clip.Top, common.Clip.Right, common.Clip.Bottom};

			return state;
		}

		void OnProcess(const SquirrelValue& receiver) {
			if (Vm.CurrentContext() != Vm.RootContext()) {
				throw std::runtime_error("RawTex onProcess requires its actual base-VM frame boundary");
			}

			SquirrelValue method;
			try {
				method = Vm.ReadSlot(receiver, std::string("onProcess"), Limits.OnProcessInstructionBudget);
			} catch (const SquirrelVmException&) {
				// Native291090 leaves failed lookup as null, preserving the base VM last error.
			}

			auto method_owner = Vm.Retain(method);
			const auto callable = Vm.Type(method_owner.Value()) == SquirrelValueType::WeakReference ? Vm.ResolveWeakReference(method_owner.Value()) : method_owner.Value();
			const auto type = Vm.Type(callable);
			if (type == SquirrelValueType::Closure || type == SquirrelValueType::NativeClosure) {
				try {
					// This argument is a real VM call-stack value, not a root around the common frame.
					static_cast<void>(Vm.InvokeWithoutResultInContext(Vm.RootContext(), callable, std::array<SquirrelValue, 1>{receiver}, Limits.OnProcessInstructionBudget, true));
				} catch (const SquirrelVmException&) {
					// Native227810 raises through the normal handler;291090 ignores its returned call status.
				}
			}
		}

		void AttachFrame(const SquirrelValue& receiver, scene::LayerCameraDrawScope& renderer, DrawTexture drawing) {
			RequirePriorityIdle();
			auto& record = Record(receiver);
			if (!drawing || record.Frame || record.ClosingReceiver || record.Dispatching || !record.Owner->CanClose()) {
				throw std::runtime_error("RawTex frame requires one stable unattached concrete owner and actual texture drawing service");
			}

			const auto handle = record.Owner->Handle();
			scene::RawLayerFrame::ReadState state = [this, handle] {
				auto& current = Record(handle);
				if (!current.Dispatching || current.FrameReceiver == nullptr) {
					throw std::runtime_error("RawTex camera state requires its actual borrowed frame receiver");
				}

				return ReadFrameState(current);
			};
			scene::RawLayerFrame::DrawTexture draw = [this, handle](scene::RawTextureTask& task, const scene::RawTextureDrawPacket& packet, std::uint32_t argument) {
				auto& current = Record(handle);
				if (!current.Dispatching || current.FrameReceiver == nullptr || &task != &current.Owner->TextureTask()) {
					throw std::runtime_error("RawTex submission requires its exact live mutable texture and borrowed receiver");
				}

				current.Drawing(*current.FrameReceiver, task, packet, argument);
			};
			std::function<void(const SquirrelValue&)> dispatch = [this](const SquirrelValue& borrowed) {
				auto& current = Record(borrowed);
				if (!current.Frame || !current.ActiveOperation || current.FrameReceiver != nullptr || !current.Dispatching) {
					throw std::runtime_error("RawTex frame has no actual operation or has reentered its receiver");
				}

				current.FrameReceiver = &borrowed;
				try {
					(*current.ActiveOperation)(borrowed, *current.Frame);
				} catch (...) {
					current.FrameReceiver = nullptr;

					throw;
				}
				current.FrameReceiver = nullptr;
			};
			auto frame = std::make_unique<scene::RawLayerFrame>(*record.Owner, renderer, std::move(state), std::move(draw));
			record.Drawing = std::move(drawing);
			record.Dispatch = std::move(dispatch);
			record.Frame = std::move(frame);
		}

		void DispatchFrame(RawTextureBindingRecord& record, const FrameOperation& operation, const SquirrelValue* receiver) {
			RequirePriorityIdle();
			if (!operation || !record.Frame || record.Owner->IsClosed() || record.ClosingReceiver || record.Dispatching || !record.Dispatch) {
				throw std::runtime_error("RawTex frame requires one non-reentrant operation on its actual attached owner");
			}

			record.Dispatching = true;
			record.ActiveOperation = &operation;
			try {
				if (receiver != nullptr) {
					record.Dispatch(*receiver);
				} else {
					Vm.DispatchBorrowedReceiver(record.Instance, record.Dispatch);
				}
			} catch (...) {
				record.ActiveOperation = nullptr;
				record.Dispatching = false;

				throw;
			}
			record.ActiveOperation = nullptr;
			record.Dispatching = false;
		}

		RawTextureConstructorPlan ParseConstructor(std::span<const SquirrelValue> arguments) {
			RawTextureConstructorPlan plan;
			auto values = arguments.subspan(1);
			if (!values.empty() && Layers.IsLayerOwner(values.front())) {
				plan.Parent = values.front();
				values = values.subspan(1);
			}
			if (values.size() == 1 || (values.size() == 2 && Vm.Type(values[1]) == SquirrelValueType::Table)) {
				if (!Vm.IsInstanceOf(values[0], Images.RawImageClass())) {
					throw std::runtime_error("invalid argument");
				}

				plan.InitialImage = &Images.Image(values[0]);
				plan.Width = static_cast<std::int32_t>(plan.InitialImage->Width());
				plan.Height = static_cast<std::int32_t>(plan.InitialImage->Height());
				if (values.size() == 2) {
					ParseOptions(plan, values[1]);
				}
			} else if (values.size() == 2 || values.size() == 3) {
				plan.Width = TryNativeInteger(values[0]).value_or(0);
				plan.Height = TryNativeInteger(values[1]).value_or(0);
				if (values.size() == 3 && Vm.Type(values[2]) == SquirrelValueType::Table) {
					ParseOptions(plan, values[2]);
				} else if (values.size() == 3 && std::holds_alternative<bool>(values[2])) {
					plan.HasSpecFlag = true;
					plan.SpecFlag = std::get<bool>(values[2]);
				}
			} else {
				throw std::runtime_error("screen not found.");
			}

			return plan;
		}

		void RequireCanRelease(const SquirrelValue& instance) {
			RequirePriorityIdle();
			Objects.RequireCanRelease(instance);
			const auto& record = Record(instance);
			if (record.ClosingReceiver || record.Dispatching || (record.Frame && !record.Frame->CanClose()) || !record.Owner->CanClose()) {
				throw std::runtime_error("RawTex cannot retire during its own task callback or cleanup");
			}
		}

		void Release(const SquirrelValue& instance) {
			RequireCanRelease(instance);
			auto& record = Record(instance);
			record.ClosingReceiver = &instance;
			std::exception_ptr failure;
			try {
				record.Owner->Close();
			} catch (...) {
				failure = std::current_exception();
			}

			record.Frame.reset();
			const auto bytes = record.ShadowBytes;
			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			ShadowBytes -= bytes;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			RequirePriorityIdle();
			if (arguments.empty() || !Vm.IsInstanceOf(arguments[0], Class)) {
				throw std::runtime_error("RawTex constructor requires its genuine native class");
			}
			const auto index = std::get<SquirrelObjectHandle>(arguments[0]).Index;
			if (Instances.contains(index)) {
				throw std::runtime_error("RawTex receiver is already initialized");
			}

			const auto plan = ParseConstructor(arguments);
			const auto available = Limits.MaxShadowBytes - ShadowBytes;
			if (plan.Width <= 0 || plan.Height <= 0 || std::size_t(plan.Width) > available / 4 / std::size_t(plan.Height) || Instances.size() >= Limits.MaxInstances) {
				throw std::runtime_error("RawTex construction exceeds positive extent or aggregate shadow limits");
			}

			auto& driver = ResolvePriority(arguments[0]);

			// Native290e30 consumes colorMode for the same one-mip CPU shadow in both specFlag branches.
			auto task = std::make_unique<scene::RawTextureTask>(Backend, resource::MotionPoint{static_cast<float>(plan.Width), static_cast<float>(plan.Height)}, plan.ColorMode, Limits.MaxBackgroundTasks);
			task->SetDrawPaused(false);
			const auto shadow_bytes = task->Texture().State().ShadowBytes;
			auto background = std::make_shared<scene::ParallelTaskSet>(Limits.MaxBackgroundTasks);
			background->SetRetireWhenEmpty(false);
			scene::ParallelTaskCallbacks callbacks;
			callbacks.Update = [background](scene::ParallelTaskHandle) {
				background->Update();
			};
			callbacks.Draw = [background](scene::ParallelTaskHandle, std::uint32_t argument) {
				background->Draw(argument);
			};
			callbacks.Release = [background](scene::ParallelTaskHandle) {
				background->Clear();
			};
			const auto driver_entry = driver.Append(std::move(callbacks));
			bool inserted = false;
			try {
				scene::RawLayerRetirement retirement;
				retirement.DropParent = [weak, index] {
					const auto owner = weak.lock();
					if (!owner || !owner->Instances.at(index).ClosingReceiver) {
						throw std::runtime_error("RawTex weak-parent phase requires the actual borrowed retirement receiver");
					}

					owner->Layers.DropRawTextureParent(*owner->Instances.at(index).ClosingReceiver);
				};
				retirement.ReleaseObject = [weak, index] {
					const auto owner = weak.lock();
					if (!owner || !owner->Instances.at(index).ClosingReceiver) {
						throw std::runtime_error("RawTex Object phase requires the actual borrowed retirement receiver");
					}

					owner->Layers.ReleaseRawTextureObject(*owner->Instances.at(index).ClosingReceiver);
				};
				RawTextureBindingRecord record;
				record.Instance = Vm.BorrowObjectIdentity(arguments[0]);
				record.Owner = std::make_unique<scene::RawLayerOwner>(Tree, std::move(task), driver, driver_entry, std::move(retirement), Layers.RawTextureAttachment(plan.Parent), Limits.MaxBackgroundTasks);
				record.Background = background;
				record.ShadowBytes = shadow_bytes;
				Instances.emplace(index, std::move(record));
				inserted = true;
				ShadowBytes += shadow_bytes;
				Layers.AttachRawTextureInstance(arguments[0], Instances.at(index).Owner->Handle(), plan.Parent, [weak, index](bool smoothing) {
					if (const auto owner = weak.lock()) {
						owner->Instances.at(index).Owner->TextureTask().SetFiltering(smoothing ? 1u : 0u, smoothing ? 1u : 0u);
					}
				},
				    [weak](const SquirrelValue& receiver) {
					    const auto owner = weak.lock();
					    if (!owner) {
						    throw std::runtime_error("RawTex retirement preflight requires its actual live texture services");
					    }

					    owner->RequireCanRelease(receiver);
				    },
				    [weak](const SquirrelValue& receiver) {
					    if (const auto owner = weak.lock()) {
						    owner->Release(receiver);
					    }
				    });
				if (plan.InitialImage) {
					Instances.at(index).Owner->TextureTask().RestoreImage(*plan.InitialImage);
				}
				if (FrameRenderer != nullptr) {
					AttachFrame(arguments[0], *FrameRenderer, FrameDrawing);
				}
				EverConstructed = true;
			} catch (...) {
				if (inserted) {
					const auto found = Instances.find(index);
					found->second.ClosingReceiver = &arguments[0];
					try {
						found->second.Owner->Close();
					} catch (...) {
						// Preserve the construction failure after completing valid ordered ownership cleanup.
					}

					Instances.erase(found);
					ShadowBytes -= shadow_bytes;
				}
				driver.SetStatus(driver_entry, scene::TaskStatus::Retire);

				throw;
			}
		}
	};

	ScriptRawTextureBindings::ScriptRawTextureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptRawImageBindings& images, ScriptBinaryBindings& binary, scene::LayerTree& tree, scene::MutableTextureBackend& backend, scene::ParallelTaskSet& driver, ScriptRawTextureLimits limits) : ScriptRawTextureBindings(vm, objects, layers, images, binary, tree, backend, BorrowRawTexturePriority(driver), limits) {
	}

	ScriptRawTextureBindings::ScriptRawTextureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptRawImageBindings& images, ScriptBinaryBindings& binary, scene::LayerTree& tree, scene::MutableTextureBackend& backend, PriorityTaskProvider priority, ScriptRawTextureLimits limits) : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, images, binary, tree, backend, std::move(priority), limits)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(layers.LayerBaseClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto method = [&](std::string name, auto operation) {
			const auto callback = vm.CreateNativeClosure("RawTex." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("RawTex callbacks require a live service and native receiver");
				}

				owner->RequirePriorityIdle();

				return operation(*owner, arguments);
			});
			vm.NewSlot(state.Class, name, callback);
		};
		method("constructor", [weak](Implementation& owner, auto arguments) {
			owner.Construct(arguments, weak);

			return SquirrelNativeResult::NoValues();
		});
		method("weakref", [](Implementation& owner, auto arguments) {
			return SquirrelNativeResult::ReturnValue(owner.Vm.CreateWeakReference(arguments.back()));
		});
		method("getWidth", [](Implementation& owner, auto arguments) {
			return SquirrelNativeResult::ReturnValue(static_cast<std::int32_t>(owner.Record(arguments[0]).Owner->TextureTask().Texture().State().Description.Width));
		});
		method("getHeight", [](Implementation& owner, auto arguments) {
			return SquirrelNativeResult::ReturnValue(static_cast<std::int32_t>(owner.Record(arguments[0]).Owner->TextureTask().Texture().State().Description.Height));
		});
		method("getOpacity", [](Implementation& owner, auto arguments) {
			return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(owner.Record(arguments[0]).Owner->TextureTask().State().Opacity));
		});
		method("setOpacity", [](Implementation& owner, auto arguments) {
			const auto value = arguments.size() > 1 ? TryNativeInteger(arguments[1]).value_or(0) : 0;
			owner.Record(arguments[0]).Owner->TextureTask().SetOpacity(std::bit_cast<std::uint32_t>(value));

			return SquirrelNativeResult::NoValues();
		});
		method("setColor", [](Implementation& owner, auto arguments) {
			owner.Record(arguments[0]).Owner->TextureTask().SetColor({RawTextureNumericArgument(arguments, 1), RawTextureNumericArgument(arguments, 2), RawTextureNumericArgument(arguments, 3)});

			return SquirrelNativeResult::NoValues();
		});
		method("restore", [](Implementation& owner, auto arguments) {
			if (arguments.size() != 2) {
				throw std::runtime_error("invalid argument.");
			}

			auto& task = owner.Record(arguments[0]).Owner->TextureTask();
			if (owner.Vm.IsInstanceOf(arguments[1], owner.Images.RawImageClass())) {
				task.RestoreImage(owner.Images.Image(arguments[1]));
			} else if (owner.Vm.IsInstanceOf(arguments[1], owner.Binary.BinaryClass())) {
				task.RestoreRgb(owner.Binary.Bytes(arguments[1]));
			} else {
				throw std::runtime_error("invalid argument.");
			}

			return SquirrelNativeResult::NoValues();
		});
		method("getRunning", [](Implementation& owner, auto arguments) {
			const auto background = owner.Record(arguments[0]).Background.lock();
			if (!background) {
				throw std::runtime_error("RawTex running query requires its live actual driver group");
			}

			return SquirrelNativeResult::ReturnValue(background->Count() != 0);
		});
		vm.NewSlot(vm.RootTable(), std::string("RawTex"), state.Class);
	}

	ScriptRawTextureBindings::~ScriptRawTextureBindings() = default;

	SquirrelValue ScriptRawTextureBindings::RawTextureClass() const {
		return m_Implementation->Class;
	}

	scene::RawLayerOwner& ScriptRawTextureBindings::Owner(const SquirrelValue& instance) {
		m_Implementation->RequirePriorityIdle();

		return *m_Implementation->Record(instance).Owner;
	}

	void ScriptRawTextureBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

	void ScriptRawTextureBindings::ConfigureFrameOwners(scene::LayerCameraDrawScope& renderer, DrawTexture drawing) {
		auto& state = *m_Implementation;
		state.RequirePriorityIdle();
		if (!drawing || state.FrameRenderer != nullptr || state.EverConstructed || !state.Instances.empty() || state.Vm.IsClosed()) {
			throw std::runtime_error("RawTex frame services require one registration before actual wrapper construction");
		}

		state.FrameDrawing = std::move(drawing);
		state.FrameRenderer = &renderer;
	}

	void ScriptRawTextureBindings::AttachFrame(const SquirrelValue& instance, scene::LayerCameraDrawScope& renderer, DrawTexture drawing) {
		m_Implementation->AttachFrame(instance, renderer, std::move(drawing));
	}

	scene::RawLayerFrameState ScriptRawTextureBindings::FrameState(const SquirrelValue& receiver) const {
		return m_Implementation->ReadFrameState(m_Implementation->Record(receiver));
	}

	scene::LayerFrameProcessState ScriptRawTextureBindings::ProcessState(scene::LayerHandle handle) const {
		return m_Implementation->Record(handle).Process;
	}

	void ScriptRawTextureBindings::SetProcessState(const SquirrelValue& instance, scene::LayerFrameProcessState state) {
		m_Implementation->RequirePriorityIdle();
		if (state.Status != scene::TaskStatus::Active && state.Status != scene::TaskStatus::Complete && state.Status != scene::TaskStatus::Retire) {
			throw std::runtime_error("RawTex containing status is outside the recovered native process domain");
		}

		auto& record = m_Implementation->Record(instance);
		if (record.ClosingReceiver || record.Owner->IsClosed()) {
			throw std::runtime_error("RawTex containing status cannot mutate its retiring owner");
		}
		record.Process = state;
	}

	void ScriptRawTextureBindings::DispatchFrame(scene::LayerHandle handle, const FrameOperation& operation) {
		m_Implementation->DispatchFrame(m_Implementation->Record(handle), operation, nullptr);
	}

	void ScriptRawTextureBindings::DispatchFrame(const SquirrelValue& receiver, const FrameOperation& operation) {
		m_Implementation->DispatchFrame(m_Implementation->Record(receiver), operation, &receiver);
	}

	void ScriptRawTextureBindings::Update(scene::LayerHandle handle) {
		const auto state = ProcessState(handle);
		if (state.Status != scene::TaskStatus::Active) {
			return;
		}
		if (state.ThreadedUpdate) {
			throw std::runtime_error("RawTex threaded update requires its recovered actual worker dispatcher");
		}

		DispatchFrame(handle, [this](const SquirrelValue& receiver, scene::RawLayerFrame& frame) {
			frame.Update([this, &receiver] {
				m_Implementation->OnProcess(receiver);
			});
		});
	}

	void ScriptRawTextureBindings::Update(const SquirrelValue& receiver) {
		const auto state = m_Implementation->Record(receiver).Process;
		if (state.Status != scene::TaskStatus::Active) {
			return;
		}
		if (state.ThreadedUpdate) {
			throw std::runtime_error("RawTex threaded update requires its recovered actual worker dispatcher");
		}

		DispatchFrame(receiver, [this](const SquirrelValue& borrowed, scene::RawLayerFrame& frame) {
			frame.Update([this, &borrowed] {
				m_Implementation->OnProcess(borrowed);
			});
		});
	}

	void ScriptRawTextureBindings::Draw(scene::LayerHandle handle, std::uint32_t argument) {
		DispatchFrame(handle, [argument](const SquirrelValue&, scene::RawLayerFrame& frame) {
			frame.Draw(argument);
		});
	}

	void ScriptRawTextureBindings::Draw(const SquirrelValue& receiver, std::uint32_t argument) {
		DispatchFrame(receiver, [argument](const SquirrelValue&, scene::RawLayerFrame& frame) {
			frame.Draw(argument);
		});
	}

} // namespace sgre::script
