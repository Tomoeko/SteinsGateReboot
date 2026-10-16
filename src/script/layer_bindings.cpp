#include "sgre/script/layer_bindings.hpp"

#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sgre::script {

	static constexpr std::size_t kMaxBorrowedViews = 65536;

	static scene::MotionBounds LayerFrameBounds(const scene::LayerClip& clip) {
		return {clip.Left, clip.Top, clip.Right, clip.Bottom};
	}

	static scene::LayerCameraParameters LayerFrameCamera(const scene::LayerState& state) {
		scene::LayerCameraParameters camera;
		camera.Pivot = {state.Transform.Center.X, state.Transform.Center.Y};
		camera.Offset = {state.Transform.Offset.X, state.Transform.Offset.Y};
		camera.Scale = {state.Transform.Scale.X, state.Transform.Scale.Y};
		camera.RotationRadians = state.Transform.AngleRadians;
		camera.Clip = LayerFrameBounds(state.Clip);

		return camera;
	}

	struct LayerBorrowedViewEntry {
		std::uint64_t Identifier = 0;
		std::function<void(bool)> SetSmoothing;
		std::function<void()> Detach;
	};

	struct LayerFrameOwnerConfiguration {
		scene::LayerCameraDrawScope& Renderer;
		const core::FrameClock& Clock;
		std::span<const std::uint32_t, scene::kMotionSharedSeedWords> Seed;
		ScriptLayerFrameServices Services;
	};

	struct LayerBindingRecord {
		SquirrelObjectIdentity Instance;
		std::optional<SquirrelHostRoot> Owner;
		scene::LayerHandle Handle;
		// MProcess ctor initializes containing status0 and ordinary/non-threaded update independently of group status.
		scene::LayerFrameProcessState ProcessState;
		ScriptMotionFrameServices FrameServices;
		ScriptFolderFrameServices FolderFrameServices;
		std::function<void(const SquirrelValue&)> FrameDispatch;
		std::function<void(const SquirrelValue&)> FolderFrameDispatch;
		const SquirrelValue* ActiveFrameReceiver = nullptr;
		const ScriptLayerBindings::MotionFrameOperation* ActiveFrameOperation = nullptr;
		const ScriptLayerBindings::FolderFrameOperation* ActiveFolderFrameOperation = nullptr;
		bool DispatchingFrame = false;
		// Borrowed frame adapters retire before groups/process/resources, while their service callables remain alive.
		std::unique_ptr<ScriptMotionResourceOwner> MotionResources;
		std::unique_ptr<scene::CommonProcessTasks> CommonProcess;
		std::unique_ptr<scene::MotionLayerTasks> MotionTasks;
		std::unique_ptr<scene::LayerFolderTasks> FolderTasks;
		std::unique_ptr<scene::MotionLayerFrame> MotionFrame;
		std::unique_ptr<scene::LayerFolderFrame> FolderFrame;
		std::unique_ptr<scene::LayerBaseTasks> BaseTasks;
		std::unique_ptr<scene::LayerBaseFrame> BaseFrame;
		std::function<void(const SquirrelValue&)> BaseDispatch;
		const ScriptLayerBindings::BaseFrameOperation* ActiveBaseOperation = nullptr;
		std::function<void(const SquirrelValue&)> BasePreflight;
		std::function<void(const SquirrelValue&)> BaseCleanup;
		ScriptMotionLayerProperties MotionProperties;
		std::vector<LayerBorrowedViewEntry> BorrowedViews;
		std::function<void(bool)> RawSmoothing;
		std::function<void(const SquirrelValue&)> RawRetirement;
		bool UpdatingBorrowedViews = false;
		bool Releasing = false;
		bool Constructing = false;
		std::array<const scene::MotionManagerRuntime*, 2> MotionRuntimeIdentities{};
		// Explicit close retires these actual manager runtimes before their borrowed source managers.
		std::array<std::unique_ptr<scene::MotionManagerRuntime>, 2> OwnedMotionRuntimes;
		// Last member retires before any source/task field drops if the enclosing registry ends prematurely.
		std::array<scene::MotionManagerRuntime::LayerBinding, 2> MotionRuntimeBindings;
	};

	struct LayerBindingOwner {
		std::optional<scene::LayerHandle> Folder;
		std::optional<scene::LayerScreenRootHandle> Screen;
	};

	static std::optional<float> LayerNumericValue(const SquirrelValue& argument) {
		if (const auto* value = std::get_if<float>(&argument)) {
			return *value;
		}

		if (const auto* value = std::get_if<std::int32_t>(&argument)) {
			return static_cast<float>(*value);
		}

		if (const auto* value = std::get_if<bool>(&argument)) {
			return *value ? 1.0f : 0.0f;
		}

		return {};
	}

	static float LayerNumericArgument(std::span<const SquirrelValue> arguments, std::size_t index) {
		return index < arguments.size() ? LayerNumericValue(arguments[index]).value_or(0) : 0;
	}

	static scene::LayerPoint LayerCoordinateArgument(SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
		SquirrelValue x;
		SquirrelValue y;
		if (arguments.size() == 2) {
			x = vm.ReadSlot(arguments[1], std::string("x"));
			y = vm.ReadSlot(arguments[1], std::string("y"));
		} else if (arguments.size() == 3) {
			x = arguments[1];
			y = arguments[2];
		} else {
			throw std::runtime_error("layer coordinate conversion requires a point object or two numeric arguments");
		}

		const auto numeric_x = LayerNumericValue(x);
		const auto numeric_y = LayerNumericValue(y);
		if (!numeric_x || !numeric_y) {
			throw std::runtime_error("layer coordinate conversion requires numeric x/y components");
		}

		return {*numeric_x, *numeric_y};
	}

	static SquirrelValue LayerCoordinateResult(SquirrelVm& vm, scene::LayerPoint point) {
		const auto result = vm.CreateTable();
		vm.NewSlot(result, std::string("x"), point.X);
		vm.NewSlot(result, std::string("y"), point.Y);

		return result;
	}

	static bool LayerBooleanArgument(std::span<const SquirrelValue> arguments, std::size_t index) {
		if (index >= arguments.size() || std::holds_alternative<std::monostate>(arguments[index])) {
			return false;
		}

		if (const auto* value = std::get_if<float>(&arguments[index])) {
			return *value != 0;
		}

		if (const auto* value = std::get_if<std::int32_t>(&arguments[index])) {
			return *value != 0;
		}

		if (const auto* value = std::get_if<bool>(&arguments[index])) {
			return *value;
		}

		return true;
	}

	static scene::LayerPoint LayerPointArgument(std::span<const SquirrelValue> arguments, std::size_t index) {
		return {LayerNumericArgument(arguments, index), LayerNumericArgument(arguments, index + 1)};
	}

	class ScriptLayerBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, scene::LayerTree& tree)
		    : Vm(vm), Objects(objects), Tree(tree) {
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		scene::LayerTree& Tree;
		SquirrelValue BaseClass;
		SquirrelValue FolderClass;
		SquirrelValue MotionBaseClass;
		SquirrelValue MotionClass;
		SquirrelValue ScreenClass;
		SquirrelHostRoot BaseClassRoot;
		SquirrelHostRoot FolderClassRoot;
		SquirrelHostRoot MotionBaseClassRoot;
		SquirrelHostRoot MotionClassRoot;
		ScriptPsbBindings* Psb = nullptr;
		scene::MotionAssetCatalog* Assets = nullptr;
		scene::TextureCache* Textures = nullptr;
		SquirrelHostRoot ScreenClassRoot;
		ScreenOwnerResolver ResolveScreen;
		std::optional<LayerFrameOwnerConfiguration> FrameOwners;
		bool FrameOwnerConfigurationSealed = false;
		std::map<std::uint32_t, LayerBindingRecord> Instances;
		std::uint64_t NextBorrowedView = 1;
		std::size_t BorrowedViewCount = 0;

		LayerBindingRecord& Record(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("layer callback requires a native LayerBase instance; receiver=" + Vm.TypeName(instance));
			}

			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance) || !Tree.Contains(found->second.Handle)) {
				throw std::runtime_error("layer instance has no live native scene state");
			}

			return found->second;
		}

		LayerBindingRecord& MotionRecord(const SquirrelValue& instance) {
			auto& record = Record(instance);
			if (!record.MotionResources || Tree.State(record.Handle).Kind != scene::LayerKind::Motion) {
				throw std::runtime_error("motion layer callback requires a live Layer leaf");
			}

			return record;
		}

		LayerBindingRecord& FolderRecord(const SquirrelValue& instance) {
			auto& record = Record(instance);
			if (!record.FolderTasks || Tree.State(record.Handle).Kind != scene::LayerKind::Folder) {
				throw std::runtime_error("Folder frame requires its live actual LayerFolder owner");
			}

			return record;
		}

		LayerBindingRecord& Record(scene::LayerHandle handle) {
			if (!Tree.Contains(handle)) {
				throw std::runtime_error("Layer frame handle has no live native scene owner");
			}
			for (auto& [index, record] : Instances) {
				static_cast<void>(index);
				if (record.Handle == handle) {
					return record;
				}
			}

			throw std::runtime_error("Layer frame handle is absent from its existing native binding registry");
		}

		bool HasBorrowedView(const SquirrelObjectIdentity& identity, std::uint64_t identifier) const noexcept {
			const auto found = Instances.find(identity.Index);
			if (found == Instances.end() || found->second.Instance.Generation != identity.Generation || found->second.Instance.Owner != identity.Owner) {
				return false;
			}

			return std::any_of(found->second.BorrowedViews.begin(), found->second.BorrowedViews.end(), [identifier](const auto& view) {
				return view.Identifier == identifier;
			});
		}

		void RemoveBorrowedView(const SquirrelObjectIdentity& identity, std::uint64_t identifier) noexcept {
			const auto found = Instances.find(identity.Index);
			if (found == Instances.end() || found->second.Instance.Generation != identity.Generation || found->second.Instance.Owner != identity.Owner) {
				return;
			}

			auto& record = found->second;
			const auto view = std::find_if(record.BorrowedViews.begin(), record.BorrowedViews.end(), [identifier](const auto& candidate) {
				return candidate.Identifier == identifier;
			});
			if (view == record.BorrowedViews.end()) {
				return;
			}

			--BorrowedViewCount;
			if (record.UpdatingBorrowedViews) {
				// Destruction during a callback retires its token; erase after the borrowed call returns.
				view->Identifier = 0;
			} else {
				record.BorrowedViews.erase(view);
			}
		}

		void SmoothBorrowedViews(LayerBindingRecord& record, bool smoothing) {
			if (record.UpdatingBorrowedViews || record.Releasing) {
				throw std::runtime_error("native borrowed views cannot reenter their smoothing traversal or retirement");
			}

			record.UpdatingBorrowedViews = true;
			std::exception_ptr failure;
			try {
				for (auto& view : record.BorrowedViews) {
					if (view.Identifier != 0) {
						if (view.SetSmoothing) {
							view.SetSmoothing(smoothing);
						}
					}
				}
			} catch (...) {
				failure = std::current_exception();
			}

			record.UpdatingBorrowedViews = false;
			std::erase_if(record.BorrowedViews, [](const auto& view) {
				return view.Identifier == 0;
			});
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void DetachBorrowedViews(LayerBindingRecord& record) {
			auto views = std::move(record.BorrowedViews);
			BorrowedViewCount -= views.size();
			std::exception_ptr failure;
			for (auto& view : views) {
				try {
					view.Detach();
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
			}

			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		LayerBindingOwner OwnerHandle(const SquirrelValue& owner) {
			if (std::holds_alternative<std::monostate>(owner)) {
				return {};
			}

			LayerBindingOwner result;
			if (ResolveScreen && Vm.IsInstanceOf(owner, ScreenClass)) {
				result.Screen = ResolveScreen(owner);
				if (!Tree.ContainsScreenRoot(*result.Screen)) {
					throw std::runtime_error("native Screen owner has no live layer manager");
				}
			} else {
				result.Folder = Record(owner).Handle;
				if (Tree.State(*result.Folder).Kind != scene::LayerKind::Folder) {
					throw std::runtime_error("native layer owner must provide a LayerFolder child manager");
				}
			}

			return result;
		}

		void AttachMotionFrame(const SquirrelValue& instance, scene::LayerCameraDrawScope& renderer, ScriptMotionFrameServices services, bool constructing = false) {
			auto& record = MotionRecord(instance);
			if (record.MotionFrame || record.Releasing || record.DispatchingFrame || !record.CommonProcess || !services.State || !services.SecondaryDraw || !services.PrimaryDraw) {
				throw std::runtime_error("Layer motion frame requires one stable unattached ordinary common owner and real state/source services");
			}
			RequireCanRelease(instance, constructing);

			const auto handle = record.Handle;
			const auto owner = this;
			scene::MotionLayerFrame::ReadState read_state = [owner, handle] {
				auto& current = owner->Record(handle);
				if (!current.ActiveFrameReceiver || !current.DispatchingFrame) {
					throw std::runtime_error("Layer frame state requires its existing receiver operation scope");
				}

				return current.FrameServices.State(*current.ActiveFrameReceiver);
			};
			scene::MotionLayerSourceDrawing drawing;
			drawing.Secondary = [owner, handle](std::uint32_t argument) {
				auto& current = owner->Record(handle);
				if (!current.ActiveFrameReceiver || !current.DispatchingFrame) {
					throw std::runtime_error("Layer secondary source drawing requires its existing receiver operation scope");
				}

				current.FrameServices.SecondaryDraw(*current.ActiveFrameReceiver, argument);
			};
			drawing.Primary = [owner, handle](std::uint32_t argument) {
				auto& current = owner->Record(handle);
				if (!current.ActiveFrameReceiver || !current.DispatchingFrame) {
					throw std::runtime_error("Layer primary source drawing requires its existing receiver operation scope");
				}

				current.FrameServices.PrimaryDraw(*current.ActiveFrameReceiver, argument);
			};
			std::function<void(const SquirrelValue&)> dispatch = [owner](const SquirrelValue& receiver) {
				auto& current = owner->Record(receiver);
				if (!current.MotionFrame || !current.ActiveFrameOperation || current.ActiveFrameReceiver) {
					throw std::runtime_error("Layer motion frame dispatch has no active concrete operation");
				}

				current.ActiveFrameReceiver = &receiver;
				try {
					(*current.ActiveFrameOperation)(receiver, *current.MotionFrame);
				} catch (...) {
					current.ActiveFrameReceiver = nullptr;

					throw;
				}
				current.ActiveFrameReceiver = nullptr;
			};
			auto frame = std::make_unique<scene::MotionLayerFrame>(*record.CommonProcess, *record.MotionTasks, renderer, std::move(read_state), std::move(drawing));
			record.FrameServices = std::move(services);
			record.FrameDispatch = std::move(dispatch);
			record.MotionFrame = std::move(frame);
		}

		void AttachFolderFrame(const SquirrelValue& instance, scene::LayerCameraDrawScope& renderer, ScriptFolderFrameServices services, bool constructing = false) {
			auto& record = FolderRecord(instance);
			if (record.FolderFrame || record.Releasing || record.DispatchingFrame || !services.State) {
				throw std::runtime_error("Folder frame requires one stable ordinary Folder owner and actual state/child services");
			}
			RequireCanRelease(instance, constructing);
			const auto handle = record.Handle;
			auto* owner = this;
			scene::LayerFolderFrame::ReadState read_state = [owner, handle] {
				auto& current = owner->Record(handle);
				if (!current.ActiveFrameReceiver || !current.DispatchingFrame) {
					throw std::runtime_error("Folder state requires its existing scoped frame receiver");
				}

				return current.FolderFrameServices.State(*current.ActiveFrameReceiver);
			};
			std::function<void(const SquirrelValue&)> dispatch = [owner, handle](const SquirrelValue& receiver) {
				auto& current = owner->Record(handle);
				if (!current.FolderFrame || !current.ActiveFolderFrameOperation || current.ActiveFrameReceiver) {
					throw std::runtime_error("Folder frame requires one non-reentrant scoped receiver operation");
				}

				current.ActiveFrameReceiver = &receiver;
				try {
					(*current.ActiveFolderFrameOperation)(receiver, *current.FolderFrame);
				} catch (...) {
					current.ActiveFrameReceiver = nullptr;

					throw;
				}
				current.ActiveFrameReceiver = nullptr;
			};
			const auto clear_children = [owner, handle] {
				owner->Clear(handle);
			};
			auto frame = std::make_unique<scene::LayerFolderFrame>(*record.CommonProcess, *record.FolderTasks, Tree, handle, renderer, std::move(read_state), std::move(services.Children), clear_children);
			record.FolderFrameServices = std::move(services);
			record.FolderFrameDispatch = std::move(dispatch);
			record.FolderFrame = std::move(frame);
		}

		void AttachMotionRuntime(const SquirrelValue& instance, scene::MotionManagerRuntime& actual_runtime, bool constructing = false) {
			RequireCanRelease(instance, constructing);
			auto& record = MotionRecord(instance);
			if (!record.MotionResources || !record.MotionTasks) {
				throw std::runtime_error("actual Motion runtime binding requires its installed concrete Layer resources/tasks");
			}

			std::size_t slot = 2;
			if (&actual_runtime.Manager() == &record.MotionResources->Secondary()) {
				slot = 0;
			} else if (&actual_runtime.Manager() == &record.MotionResources->Primary()) {
				slot = 1;
			}
			if (slot == 2 || !record.MotionRuntimeBindings[slot].IsEmpty()) {
				throw std::runtime_error("actual Motion runtime must match this Layer's singular Primary/Secondary manager identity");
			}

			record.MotionRuntimeBindings[slot] = actual_runtime.BindLayerTasks(*record.MotionTasks);
			record.MotionRuntimeIdentities[slot] = &actual_runtime;
		}

		void AttachConfiguredFrames(const SquirrelValue& instance, bool motion) {
			if (!FrameOwners) {
				return;
			}

			auto& owners = *FrameOwners;
			if (motion) {
				auto& record = MotionRecord(instance);
				scene::MotionManagerRuntimeServices runtime_services;
				runtime_services.ReadSourceAliases = [resources = record.MotionResources.get()](auto project) {
					return resources->SourceAliases(project);
				};
				runtime_services.SceneAssets = Assets;
				runtime_services.ReadRegisteredAsset = [resources = record.MotionResources.get()](auto project) -> const scene::LoadedMotionAsset& {
					return resources->BorrowAsset(project);
				};
				const std::array managers{&record.MotionResources->Secondary(), &record.MotionResources->Primary()};
				for (std::size_t slot = 0; slot < managers.size(); ++slot) {
					record.OwnedMotionRuntimes[slot] = std::make_unique<scene::MotionManagerRuntime>(*managers[slot], owners.Clock, owners.Seed, runtime_services, owners.Services.MotionDrawCapacity);
					AttachMotionRuntime(instance, *record.OwnedMotionRuntimes[slot], true);
				}
				AttachMotionFrame(instance, owners.Renderer, owners.Services.Motion, true);
			} else {
				AttachFolderFrame(instance, owners.Renderer, owners.Services.Folder, true);
			}
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& binding_owner, bool motion = false) {
			if (arguments.size() > 2 || !Vm.IsInstanceOf(arguments[0], motion ? MotionClass : FolderClass)) {
				throw std::runtime_error(motion ? "Layer constructor received invalid arguments" : "LayerFolder constructor received invalid arguments");
			}

			const auto instance = arguments[0];
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Instances.contains(index)) {
				throw std::runtime_error("native layer scene state is already initialized");
			}

			const auto owner = arguments.size() == 2 ? arguments[1] : SquirrelValue{};
			const auto destination = OwnerHandle(owner);
			const auto weak_owner = std::holds_alternative<std::monostate>(owner) ? owner : Vm.CreateWeakReference(owner);
			std::unique_ptr<ScriptMotionResourceOwner> resources;
			if (motion) {
				if (!Psb || !Assets || !Textures) {
					throw std::runtime_error("native motion Layer requires its configured resource services");
				}

				resources = std::make_unique<ScriptMotionResourceOwner>(Vm, *Psb, *Assets, *Textures);
			}

			const auto handle = motion ? (destination.Screen ? Tree.CreateMotionInScreen(*destination.Screen) : Tree.CreateMotion(destination.Folder)) : (destination.Screen ? Tree.CreateFolderInScreen(*destination.Screen) : Tree.CreateFolder(destination.Folder));
			FrameOwnerConfigurationSealed = true;
			bool attached = false;
			try {
				Objects.AttachInstance(instance);
				attached = true;
				LayerBindingRecord record;
				record.Instance = Vm.BorrowObjectIdentity(instance);
				if (!std::holds_alternative<std::monostate>(weak_owner)) {
					record.Owner = Vm.Retain(weak_owner);
				}
				record.Handle = handle;
				record.Constructing = true;
				record.MotionResources = std::move(resources);
				record.CommonProcess = std::make_unique<scene::CommonProcessTasks>();
				if (motion) {
					record.MotionTasks = std::make_unique<scene::MotionLayerTasks>();
					record.MotionTasks->m_RequiresRegisteredRuntime = true;
				} else {
					record.FolderTasks = std::make_unique<scene::LayerFolderTasks>();
				}
				Instances.emplace(index, std::move(record));
				Objects.ConfigureNativeRelease(instance, [binding_owner](const SquirrelValue& receiver) {
					if (const auto retained = binding_owner.lock()) {
						retained->Release(receiver);
					}
				});
				AttachConfiguredFrames(instance, motion);
				Record(instance).Constructing = false;
			} catch (...) {
				const auto failure = std::current_exception();
				if (Instances.contains(index)) {
					// No script or service callback ran before commit; newly installed groups/subscribers are empty.
					// Concrete retirement closes partial frame/runtime borrows before the registry record expires.
					Record(instance).Constructing = false;
					Release(instance);
				} else {
					Tree.Release(handle);
					if (attached) {
						Objects.ReleaseInstance(instance);
					}
				}

				std::rethrow_exception(failure);
			}
		}

		void AttachBaseFrame(const SquirrelValue& instance, scene::LayerCameraDrawScope& renderer, bool constructing = false) {
			auto& record = Record(instance);
			if (!record.BaseTasks || record.BaseFrame || record.Releasing || record.DispatchingFrame || Tree.State(record.Handle).Kind != scene::LayerKind::Base) {
				throw std::runtime_error("Base frame requires one actual common SQLayerBase owner");
			}
			RequireCanRelease(instance, constructing);
			const auto handle = record.Handle;
			auto* owner = this;
			auto read_state = [owner, handle] {
				const auto& current = owner->Record(handle);
				if (!current.ActiveFrameReceiver || !current.DispatchingFrame) {
					throw std::runtime_error("Base camera state requires its actual borrowed frame receiver");
				}
				const auto common = owner->Tree.State(handle);
				scene::LayerBaseFrameState state;
				state.BaseClip = LayerFrameBounds(common.BaseClip);
				state.Camera = LayerFrameCamera(common);

				return state;
			};
			std::function<void(const SquirrelValue&)> dispatch = [owner, handle](const SquirrelValue& receiver) {
				auto& current = owner->Record(handle);
				if (!current.BaseFrame || !current.ActiveBaseOperation || current.ActiveFrameReceiver) {
					throw std::runtime_error("Base frame requires one actual scoped receiver operation");
				}
				current.ActiveFrameReceiver = &receiver;
				try {
					(*current.ActiveBaseOperation)(receiver, *current.BaseFrame);
				} catch (...) {
					current.ActiveFrameReceiver = nullptr;

					throw;
				}
				current.ActiveFrameReceiver = nullptr;
			};
			auto frame = std::make_unique<scene::LayerBaseFrame>(*record.CommonProcess, *record.BaseTasks, Tree, handle, renderer, std::move(read_state));
			record.BaseDispatch = std::move(dispatch);
			record.BaseFrame = std::move(frame);
		}

		void ConstructBase(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& binding_owner) {
			if (arguments.empty() || arguments.size() > 2 || !Vm.IsInstanceOf(arguments[0], BaseClass)) {
				throw std::runtime_error("SQLayerBase constructor requires its genuine derived instance and optional owner");
			}
			const auto& instance = arguments[0];
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Instances.contains(index)) {
				throw std::runtime_error("SQLayerBase receiver is already attached");
			}
			const auto owner = arguments.size() == 2 ? arguments[1] : SquirrelValue{};
			const auto destination = OwnerHandle(owner);
			const auto weak_owner = std::holds_alternative<std::monostate>(owner) ? owner : Vm.CreateWeakReference(owner);
			const auto handle = destination.Screen ? Tree.CreateBaseInScreen(*destination.Screen) : Tree.CreateBase(destination.Folder);
			bool attached = false;
			try {
				Objects.AttachInstance(instance);
				attached = true;
				LayerBindingRecord record;
				record.Instance = Vm.BorrowObjectIdentity(instance);
				record.Handle = handle;
				record.Constructing = true;
				if (!std::holds_alternative<std::monostate>(weak_owner)) {
					record.Owner = Vm.Retain(weak_owner);
				}
				record.CommonProcess = std::make_unique<scene::CommonProcessTasks>();
				record.BaseTasks = std::make_unique<scene::LayerBaseTasks>();
				Instances.emplace(index, std::move(record));
				Objects.ConfigureNativeRelease(instance, [binding_owner](const SquirrelValue& receiver) {
					if (const auto retained = binding_owner.lock()) {
						retained->Release(receiver);
					}
				});
				if (FrameOwners) {
					AttachBaseFrame(instance, FrameOwners->Renderer, true);
				}
				Record(instance).Constructing = false;
			} catch (...) {
				const auto failure = std::current_exception();
				if (Instances.contains(index)) {
					Record(instance).Constructing = false;
					Release(instance);
				} else {
					Tree.Release(handle);
					if (attached) {
						Objects.ReleaseInstance(instance);
					}
				}

				std::rethrow_exception(failure);
			}
		}

		void SetSmoothing(const SquirrelValue& instance, bool smoothing) {
			auto& record = Record(instance);
			if (record.UpdatingBorrowedViews || record.Releasing) {
				throw std::runtime_error("native Layer cannot reenter its smoothing traversal or retirement");
			}

			Tree.SetSmoothing(record.Handle, smoothing);
			SmoothBorrowedViews(record, smoothing);
			if (record.RawSmoothing) {
				record.RawSmoothing(smoothing);
			}
			if (record.MotionResources) {
				record.MotionResources->SetFiltering(static_cast<std::int32_t>(smoothing), static_cast<std::int32_t>(smoothing));
			}
		}

		void Clear(scene::LayerHandle handle) {
			Tree.RequireCanClearChildren(handle);
			for (auto& [index, record] : Instances) {
				static_cast<void>(index);
				if (Tree.State(record.Handle).Owner == handle) {
					record.Owner.reset();
				}
			}

			Tree.Clear(handle);
		}

		void Clear(const SquirrelValue& instance) {
			Clear(Record(instance).Handle);
		}

		void RequireCanRelease(const SquirrelValue& instance, bool constructing = false) {
			Objects.RequireCanRelease(instance);
			const auto& record = Record(instance);
			if (record.Constructing != constructing) {
				throw std::runtime_error("native Layer retirement/attachment cannot reenter actual constructor ownership");
			}
			Tree.RequireCanRelease(record.Handle);
			if (record.UpdatingBorrowedViews || record.Releasing || record.DispatchingFrame) {
				throw std::runtime_error("native Layer cannot reenter its release, borrowed view traversal or frame dispatch");
			}
			if (record.CommonProcess && !record.CommonProcess->CanClear()) {
				throw std::runtime_error("native Layer cannot retire during its common process traversal");
			}
			if ((record.BaseFrame && !record.BaseFrame->CanClose()) || (record.BaseTasks && !record.BaseTasks->CanClear())) {
				throw std::runtime_error("native Base cannot retire while its concrete graph is traversing or detached");
			}
			if (record.BasePreflight) {
				record.BasePreflight(instance);
			}
			if (record.MotionFrame && !record.MotionFrame->CanClose()) {
				throw std::runtime_error("native Layer cannot retire while frame adapters remain active or detached");
			}
			if ((record.FolderFrame && !record.FolderFrame->CanClose()) || (record.FolderTasks && !record.FolderTasks->CanClear())) {
				throw std::runtime_error("native Folder cannot retire while its frame graph or common groups are active or detached");
			}

			if (record.MotionTasks) {
				for (const auto group : {scene::MotionTaskGroup::CommonFirst, scene::MotionTaskGroup::CommonSecond, scene::MotionTaskGroup::First, scene::MotionTaskGroup::Second}) {
					if (!record.MotionTasks->Group(group).CanClear()) {
						throw std::runtime_error("native Layer cannot retire while one of its task groups is active");
					}
				}
			}
			if (record.MotionResources && !record.MotionResources->CanClose()) {
				throw std::runtime_error("native Layer cannot retire while its source owner is mutating");
			}
			for (const auto& runtime : record.MotionRuntimeBindings) {
				runtime.RequireCanRetirePlayers();
			}
		}

		void Release(const SquirrelValue& instance) {
			RequireCanRelease(instance);
			auto& record = Record(instance);
			const auto handle = record.Handle;
			if (record.RawRetirement) {
				const auto retirement = record.RawRetirement;
				record.Releasing = true;
				try {
					retirement(instance);
				} catch (...) {
					const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
					if (found != Instances.end() && found->second.Instance == Vm.BorrowObjectIdentity(instance)) {
						found->second.Releasing = false;
					}

					throw;
				}

				return;
			}

			record.Releasing = true;
			std::exception_ptr failure;
			if (record.BaseCleanup) {
				try {
					record.BaseCleanup(instance);
				} catch (...) {
					failure = std::current_exception();
				}
			}
			if (record.BaseTasks) {
				Tree.Detach(handle);
				record.Owner.reset();
				try {
					if (record.BaseFrame) {
						record.BaseFrame->RetireParallel();
						record.BaseFrame.reset();
					} else {
						record.CommonProcess->ClearParallel();
					}
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
				try {
					record.BaseTasks->Clear();
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
				try {
					record.CommonProcess->ClearSerial();
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
			}
			if (record.FolderTasks) {
				// SQLayerBase unlinks the containing manager and drops its weak parent before parallel-then-serial deletion.
				Tree.Detach(handle);
				record.Owner.reset();
				try {
					if (record.FolderFrame) {
						record.FolderFrame->RetireParallel();
						record.FolderFrame.reset();
					} else {
						record.CommonProcess->ClearParallel();
					}
				} catch (...) {
					failure = std::current_exception();
				}
				try {
					record.FolderTasks->Clear();
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
				try {
					Clear(handle);
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
				try {
					record.CommonProcess->ClearSerial();
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
			}
			// Adapter removal retires no concrete resource; SQLayer then clears actual serial before parallel children.
			if (record.MotionFrame) {
				record.MotionFrame->Close();
				record.MotionFrame.reset();
			}
			try {
				if (record.CommonProcess && !record.FolderTasks && !record.BaseTasks) {
					record.CommonProcess->ClearSerial();
				}
			} catch (...) {
				failure = std::current_exception();
			}

			try {
				if (record.CommonProcess && !record.FolderTasks && !record.BaseTasks) {
					record.CommonProcess->ClearParallel();
				}
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			try {
				if (record.MotionTasks) {
					record.MotionTasks->ClearBeforeSourceManagers();
				}
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			// Authored runtime borrows end after real Second task retirement, before native source-manager order.
			for (std::size_t slot = 0; slot < record.MotionRuntimeBindings.size(); ++slot) {
				try {
					record.MotionRuntimeBindings[slot].CloseRuntime();
				} catch (...) {
					record.Releasing = false;
					throw;
				}
				record.OwnedMotionRuntimes[slot].reset();
				record.MotionRuntimeIdentities[slot] = nullptr;
			}

			try {
				if (record.MotionResources) {
					record.MotionResources->Close([&record] {
						record.MotionTasks->ClearBetweenSourceManagers();
					});
				} else if (!record.FolderTasks && !record.BaseTasks) {
					Clear(instance);
				}
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			try {
				DetachBorrowedViews(record);
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			try {
				Objects.ReleaseInstance(instance);
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			try {
				Tree.Release(handle);
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}

			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	ScriptLayerBindings::ScriptLayerBindings(SquirrelVm& vm, ScriptObjectBindings& objects, scene::LayerTree& tree)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, tree)) {
		auto& state = *m_Implementation;
		state.BaseClass = vm.CreateClass(objects.ObjectClass());
		state.BaseClassRoot = vm.Retain(state.BaseClass);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto method = [&](const SquirrelValue& class_value, std::string name, auto operation) {
			const auto callback = vm.CreateNativeClosure("LayerBase." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("layer callback owner has expired or lacks a receiver");
				}

				SquirrelNativeResult result;
				result.Value = operation(*owner, arguments);

				return result;
			});
			vm.NewSlot(class_value, name, callback);
		};

		method(state.BaseClass, "constructor", [](const Implementation&, const auto) -> SquirrelValue {
			throw std::runtime_error("can't create instance");
		});
		method(state.BaseClass, "getOwner", [](Implementation& owner, const auto arguments) {
			const auto& retained = owner.Record(arguments[0]).Owner;
			const auto value = retained ? retained->Value() : SquirrelValue{};

			return std::holds_alternative<std::monostate>(value) ? value : owner.Vm.ResolveWeakReference(value);
		});
		method(state.BaseClass, "setOwner", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (arguments.size() != 2) {
				throw std::runtime_error("LayerBase.setOwner requires one owner argument");
			}

			auto& record = owner.Record(arguments[0]);
			if (record.Releasing) {
				throw std::runtime_error("LayerBase owner cannot change during native retirement");
			}

			const auto destination = owner.OwnerHandle(arguments[1]);
			std::optional<SquirrelHostRoot> weak_owner;
			if (!std::holds_alternative<std::monostate>(arguments[1])) {
				weak_owner = owner.Vm.Retain(owner.Vm.CreateWeakReference(arguments[1]));
			}
			if (destination.Screen) {
				owner.Tree.SetScreenOwner(record.Handle, *destination.Screen);
			} else {
				owner.Tree.SetOwner(record.Handle, destination.Folder);
			}
			record.Owner = std::move(weak_owner);

			return {};
		});
		method(state.BaseClass, "setPriority", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Tree.SetPriority(owner.Record(arguments[0]).Handle, LayerNumericArgument(arguments, 1));

			return {};
		});
		method(state.BaseClass, "getPriority", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Tree.State(owner.Record(arguments[0]).Handle).Priority;
		});
		method(state.BaseClass, "setVisible", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Tree.SetVisible(owner.Record(arguments[0]).Handle, LayerBooleanArgument(arguments, 1));

			return {};
		});
		method(state.BaseClass, "getVisible", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Tree.State(owner.Record(arguments[0]).Handle).Visible;
		});
		method(state.BaseClass, "setSmoothing", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.SetSmoothing(arguments[0], LayerBooleanArgument(arguments, 1));

			return {};
		});
		method(state.BaseClass, "getSmoothing", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Tree.State(owner.Record(arguments[0]).Handle).Smoothing;
		});
		method(state.BaseClass, "setOffset", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Tree.SetOffset(owner.Record(arguments[0]).Handle, LayerPointArgument(arguments, 1));

			return {};
		});
		method(state.BaseClass, "getOffsetX", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Tree.State(owner.Record(arguments[0]).Handle).Transform.Offset.X;
		});
		method(state.BaseClass, "getOffsetY", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Tree.State(owner.Record(arguments[0]).Handle).Transform.Offset.Y;
		});
		method(state.BaseClass, "setCenter", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Tree.SetCenter(owner.Record(arguments[0]).Handle, LayerPointArgument(arguments, 1));

			return {};
		});
		method(state.BaseClass, "setScale", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Tree.SetScale(owner.Record(arguments[0]).Handle, LayerPointArgument(arguments, 1));

			return {};
		});
		method(state.BaseClass, "getScaleX", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Tree.State(owner.Record(arguments[0]).Handle).Transform.Scale.X;
		});
		method(state.BaseClass, "getScaleY", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Tree.State(owner.Record(arguments[0]).Handle).Transform.Scale.Y;
		});
		method(state.BaseClass, "setZoom", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Tree.SetZoom(owner.Record(arguments[0]).Handle, LayerNumericArgument(arguments, 1));

			return {};
		});
		method(state.BaseClass, "setAngleRad", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Tree.SetAngleRadians(owner.Record(arguments[0]).Handle, LayerNumericArgument(arguments, 1));

			return {};
		});
		method(state.BaseClass, "setAngleDeg", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Tree.SetAngleDegrees(owner.Record(arguments[0]).Handle, LayerNumericArgument(arguments, 1));

			return {};
		});
		method(state.BaseClass, "getAngleRad", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Tree.State(owner.Record(arguments[0]).Handle).Transform.AngleRadians;
		});
		method(state.BaseClass, "getAngleDeg", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.Tree.AngleDegrees(owner.Record(arguments[0]).Handle);
		});
		method(state.BaseClass, "setBaseClip", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Tree.SetBaseClip(owner.Record(arguments[0]).Handle, LayerPointArgument(arguments, 1), LayerPointArgument(arguments, 3));

			return {};
		});
		method(state.BaseClass, "resetBaseClip", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Tree.ResetBaseClip(owner.Record(arguments[0]).Handle);

			return {};
		});
		method(state.BaseClass, "setClip", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Tree.SetClip(owner.Record(arguments[0]).Handle, LayerPointArgument(arguments, 1), LayerPointArgument(arguments, 3));

			return {};
		});
		method(state.BaseClass, "resetClip", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Tree.ResetClip(owner.Record(arguments[0]).Handle);

			return {};
		});
		method(state.BaseClass, "convertLocalToWorld", [](Implementation& owner, const auto arguments) {
			const auto transform = owner.Tree.WorldTransform(owner.Record(arguments[0]).Handle);
			const auto point = LayerCoordinateArgument(owner.Vm, arguments);

			return LayerCoordinateResult(owner.Vm, scene::TransformLayerPoint(transform, point));
		});
		method(state.BaseClass, "convertWorldToLocal", [](Implementation& owner, const auto arguments) {
			const auto transform = owner.Tree.WorldTransform(owner.Record(arguments[0]).Handle);
			const auto point = LayerCoordinateArgument(owner.Vm, arguments);

			return LayerCoordinateResult(owner.Vm, scene::InverseTransformLayerPoint(transform, point));
		});
		state.FolderClass = vm.CreateClass(state.BaseClass);
		state.FolderClassRoot = vm.Retain(state.FolderClass);
		method(state.FolderClass, "constructor", [weak](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Construct(arguments, weak);

			return {};
		});
		method(state.FolderClass, "clear", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (!owner.Vm.IsInstanceOf(arguments[0], owner.FolderClass)) {
				throw std::runtime_error("LayerFolder.clear requires a folder instance");
			}

			owner.Clear(arguments[0]);

			return {};
		});
		state.MotionBaseClass = vm.CreateClass(state.BaseClass);
		state.MotionBaseClassRoot = vm.Retain(state.MotionBaseClass);
		method(state.MotionBaseClass, "constructor", [](const Implementation&, const auto) -> SquirrelValue {
			throw std::runtime_error("can't create instance");
		});
		vm.NewSlot(vm.RootTable(), std::string("MotionLayerBase"), state.MotionBaseClass);
		vm.NewSlot(vm.RootTable(), std::string("LayerBase"), state.BaseClass);
		vm.NewSlot(vm.RootTable(), std::string("LayerFolder"), state.FolderClass);
	}

	static scene::MotionProjectId MotionProjectArgument(std::span<const SquirrelValue> arguments, std::size_t index) {
		return {index < arguments.size() ? TryNativeInteger(arguments[index]).value_or(0) : 0};
	}

	void ScriptLayerBindings::ConfigureFrameOwners(scene::LayerCameraDrawScope& renderer, const core::FrameClock& clock, std::span<const std::uint32_t, scene::kMotionSharedSeedWords> runtime_seed, ScriptLayerFrameServices services) {
		auto& state = *m_Implementation;
		if (state.FrameOwners || state.FrameOwnerConfigurationSealed || !state.Instances.empty() || !state.Psb || !state.Assets || !state.Textures || !services.Motion.State || !services.Motion.SecondaryDraw || !services.Motion.PrimaryDraw || !services.Folder.State || !services.Folder.Children.State || !services.Folder.Children.Update || !services.Folder.Children.Draw || services.MotionDrawCapacity == 0 || renderer.IsDrawing() || renderer.Depth() != 1) {
			throw std::runtime_error("Layer frame owners require one configuration before native Layers, actual quiescent camera/clock/seed and complete concrete Motion/Folder services");
		}

		state.FrameOwners.emplace(LayerFrameOwnerConfiguration{renderer, clock, runtime_seed, std::move(services)});
	}

	static std::string_view MotionNameArgument(std::span<const SquirrelValue> arguments, std::size_t index) {
		const auto* text = index < arguments.size() ? TryStringText(arguments[index]) : nullptr;
		if (!text) {
			throw std::runtime_error("invalid argument");
		}

		return std::string_view(*text).substr(0, text->find('\0'));
	}

	void ScriptLayerBindings::ConfigureMotionOwners(ScriptPsbBindings& psb, scene::MotionAssetCatalog& assets, scene::TextureCache& cache) {
		auto& state = *m_Implementation;
		if (state.Psb || state.Vm.HasSlot(state.Vm.RootTable(), std::string("Layer")) || !assets.UsesCache(cache) || state.Vm.Type(psb.ObjectClass()) != SquirrelValueType::Class) {
			throw std::runtime_error("motion Layer requires one PSB/catalog/cache registration in its VM scope");
		}

		const auto motion_class = state.Vm.CreateClass(state.MotionBaseClass);
		auto motion_class_root = state.Vm.Retain(motion_class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto method = [&](std::string name, auto operation) {
			const auto callback = state.Vm.CreateNativeClosure("Layer." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("motion layer callback owner has expired or lacks a receiver");
				}

				SquirrelNativeResult result;
				result.Value = operation(*owner, arguments);

				return result;
			});
			state.Vm.NewSlot(motion_class, name, callback);
		};

		method("constructor", [weak](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.Construct(arguments, weak, true);

			return {};
		});
		method("registerMotionResource", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (arguments.size() != 2) {
				throw std::runtime_error("invalid argument");
			}

			return owner.MotionRecord(arguments[0]).MotionResources->Register(arguments[1]).Value;
		});
		method("unregisterMotionResource", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (arguments.size() != 2) {
				throw std::runtime_error("invalid argument");
			}

			owner.MotionRecord(arguments[0]).MotionResources->Unregister(MotionProjectArgument(arguments, 1));

			return {};
		});
		method("getMotionCharaNameList", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (arguments.size() != 2) {
				throw std::runtime_error("invalid argument");
			}

			const auto& resources = *owner.MotionRecord(arguments[0]).MotionResources;
			const auto project = MotionProjectArgument(arguments, 1);
			std::vector<std::string_view> names;
			if (resources.Contains(project)) {
				names.resize(resources.Asset(project)->Model->Objects.size());
			}

			names.resize(resources.Primary().WriteCharacterNames(project, names));
			const auto result = owner.Vm.CreateArray();
			for (const auto name : names) {
				owner.Vm.AppendArray(result, owner.Vm.CreateString(std::string(name)));
			}

			return result;
		});
		method("renameMotionChara", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (arguments.size() != 4) {
				throw std::runtime_error("invalid argument");
			}

			owner.MotionRecord(arguments[0]).MotionResources->RenameCharacter(MotionProjectArgument(arguments, 1), MotionNameArgument(arguments, 2), MotionNameArgument(arguments, 3));

			return {};
		});
		method("removeMotionChara", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			if (arguments.size() != 3) {
				throw std::runtime_error("invalid argument");
			}

			owner.MotionRecord(arguments[0]).MotionResources->RemoveCharacter(MotionProjectArgument(arguments, 1), MotionNameArgument(arguments, 2));

			return {};
		});
		method("setMotionCoordAlignmentToInteger", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.MotionRecord(arguments[0]).MotionProperties.CoordinateAlignmentToInteger = LayerBooleanArgument(arguments, 1);

			return {};
		});
		method("getMotionCoordAlignmentToInteger", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.MotionRecord(arguments[0]).MotionProperties.CoordinateAlignmentToInteger;
		});
		method("setMotionMaskRegionClipping", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.MotionRecord(arguments[0]).MotionProperties.MaskRegionClipping = LayerBooleanArgument(arguments, 1);

			return {};
		});
		method("getMotionMaskRegionClipping", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			return owner.MotionRecord(arguments[0]).MotionProperties.MaskRegionClipping;
		});
		method("setObjectClip", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			const auto origin = LayerPointArgument(arguments, 1);
			const auto extent = LayerPointArgument(arguments, 3);
			scene::LayerClip clip;
			clip.Left = origin.X;
			clip.Top = origin.Y;
			clip.Right = origin.X + extent.X;
			clip.Bottom = origin.Y + extent.Y;
			if (!std::isfinite(clip.Left) || !std::isfinite(clip.Top) || !std::isfinite(clip.Right) || !std::isfinite(clip.Bottom)) {
				throw std::runtime_error("motion object clip requires finite coordinates");
			}

			owner.MotionRecord(arguments[0]).MotionProperties.ObjectClip = clip;

			return {};
		});
		method("resetObjectClip", [](Implementation& owner, const auto arguments) -> SquirrelValue {
			owner.MotionRecord(arguments[0]).MotionProperties.ObjectClip = {};

			return {};
		});
		state.Vm.NewSlot(state.Vm.RootTable(), std::string("Layer"), motion_class);
		state.MotionClass = motion_class;
		state.MotionClassRoot = std::move(motion_class_root);
		state.Psb = &psb;
		state.Assets = &assets;
		state.Textures = &cache;
	}

	SquirrelValue ScriptLayerBindings::MotionLayerBaseClass() const {
		return m_Implementation->MotionBaseClass;
	}

	SquirrelValue ScriptLayerBindings::LayerClass() const {
		if (!m_Implementation->Psb) {
			throw std::runtime_error("native motion Layer requires its configured resource services");
		}

		return m_Implementation->MotionClass;
	}

	const ScriptMotionResourceOwner& ScriptLayerBindings::MotionResources(const SquirrelValue& instance) const {
		const auto& resources = m_Implementation->Record(instance).MotionResources;
		if (!resources) {
			throw std::runtime_error("native motion resources require a Layer leaf");
		}

		return *resources;
	}

	ScriptMotionLayerProperties ScriptLayerBindings::MotionProperties(const SquirrelValue& instance) const {
		static_cast<void>(MotionResources(instance));

		return m_Implementation->Record(instance).MotionProperties;
	}

	scene::LayerFolderFrameState ScriptLayerBindings::FolderFrameState(const SquirrelValue& receiver) const {
		const auto& record = m_Implementation->FolderRecord(receiver);
		if (record.Releasing) {
			throw std::runtime_error("Folder frame state cannot borrow its retiring native owner");
		}
		const auto common = m_Implementation->Tree.State(record.Handle);
		scene::LayerFolderFrameState state;
		state.BaseClip = LayerFrameBounds(common.BaseClip);
		state.Camera = LayerFrameCamera(common);

		return state;
	}

	scene::MotionLayerFrameState ScriptLayerBindings::MotionFrameState(const SquirrelValue& receiver) const {
		const auto& record = m_Implementation->MotionRecord(receiver);
		if (record.Releasing) {
			throw std::runtime_error("Motion frame state cannot borrow its retiring native owner");
		}
		const auto common = m_Implementation->Tree.State(record.Handle);
		scene::MotionLayerFrameState state;
		state.BaseClip = LayerFrameBounds(common.BaseClip);
		state.Camera = LayerFrameCamera(common);
		state.ObjectClip = LayerFrameBounds(record.MotionProperties.ObjectClip);

		return state;
	}

	void ScriptLayerBindings::PublishAffineCamera(const SquirrelValue& instance, const scene::LayerCameraParameters& camera) {
		const auto& record = m_Implementation->MotionRecord(instance);
		if (record.Releasing) {
			throw std::runtime_error("Affine camera publication cannot borrow its retiring native Layer owner");
		}

		auto& tree = m_Implementation->Tree;
		tree.SetScale(record.Handle, {camera.Scale.X, camera.Scale.Y});
		tree.SetAngleRadians(record.Handle, camera.RotationRadians);
		tree.SetOffset(record.Handle, {camera.Offset.X, camera.Offset.Y});
	}

	scene::MotionLayerTasks& ScriptLayerBindings::MotionTasks(const SquirrelValue& instance) {
		auto& record = m_Implementation->MotionRecord(instance);
		if (record.Releasing) {
			throw std::runtime_error("native motion task lookup cannot reenter Layer retirement");
		}

		return *record.MotionTasks;
	}

	ScriptLayerBindings::BorrowedViewRegistration ScriptLayerBindings::BorrowMotionObject(const SquirrelValue& instance, std::function<void()> detach) {
		auto& state = *m_Implementation;
		auto& record = state.MotionRecord(instance);
		if (!detach || record.UpdatingBorrowedViews || record.Releasing || state.BorrowedViewCount >= kMaxBorrowedViews || state.NextBorrowedView == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("native borrowed Motion requires its stable actual Layer and detach registration");
		}

		LayerBorrowedViewEntry entry;
		entry.Identifier = state.NextBorrowedView;
		entry.Detach = std::move(detach);
		record.BorrowedViews.push_back(std::move(entry));
		++state.BorrowedViewCount;
		BorrowedViewRegistration registration;
		registration.m_Parent = m_Implementation;
		registration.m_Instance = record.Instance;
		registration.m_Identifier = state.NextBorrowedView++;

		return registration;
	}

	ScriptLayerBindings::BorrowedViewRegistration ScriptLayerBindings::BorrowMotionView(const SquirrelValue& instance, std::function<void(bool)> set_smoothing, std::function<void()> detach) {
		auto& state = *m_Implementation;
		auto& record = state.MotionRecord(instance);
		if (!set_smoothing || !detach || record.UpdatingBorrowedViews || record.Releasing || state.BorrowedViewCount >= kMaxBorrowedViews || state.NextBorrowedView == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("native borrowed view requires live callbacks, stable traversal and available registration capacity");
		}

		LayerBorrowedViewEntry entry;
		entry.Identifier = state.NextBorrowedView;
		entry.SetSmoothing = std::move(set_smoothing);
		entry.Detach = std::move(detach);
		record.BorrowedViews.push_back(std::move(entry));
		++state.BorrowedViewCount;
		BorrowedViewRegistration registration;
		registration.m_Parent = m_Implementation;
		registration.m_Instance = record.Instance;
		registration.m_Identifier = state.NextBorrowedView++;

		return registration;
	}

	ScriptLayerBindings::BorrowedViewRegistration::~BorrowedViewRegistration() {
		Reset();
	}

	ScriptLayerBindings::BorrowedViewRegistration::BorrowedViewRegistration(BorrowedViewRegistration&& other) noexcept
	    : m_Parent(std::move(other.m_Parent)), m_Instance(std::move(other.m_Instance)), m_Identifier(std::exchange(other.m_Identifier, 0)) {
	}

	ScriptLayerBindings::BorrowedViewRegistration& ScriptLayerBindings::BorrowedViewRegistration::operator=(BorrowedViewRegistration&& other) noexcept {
		if (this != &other) {
			Reset();
			m_Parent = std::move(other.m_Parent);
			m_Instance = std::move(other.m_Instance);
			m_Identifier = std::exchange(other.m_Identifier, 0);
		}

		return *this;
	}

	void ScriptLayerBindings::BorrowedViewRegistration::Reset() noexcept {
		if (m_Identifier != 0) {
			if (const auto parent = m_Parent.lock()) {
				parent->RemoveBorrowedView(m_Instance, m_Identifier);
			}
		}

		m_Identifier = 0;
		m_Parent.reset();
		m_Instance = {};
	}

	bool ScriptLayerBindings::BorrowedViewRegistration::IsAttached() const noexcept {
		const auto parent = m_Parent.lock();

		return m_Identifier != 0 && parent && parent->HasBorrowedView(m_Instance, m_Identifier);
	}

	ScriptLayerBindings::~ScriptLayerBindings() = default;

	SquirrelValue ScriptLayerBindings::LayerBaseClass() const {
		return m_Implementation->BaseClass;
	}

	SquirrelValue ScriptLayerBindings::LayerFolderClass() const {
		return m_Implementation->FolderClass;
	}

	scene::LayerHandle ScriptLayerBindings::Handle(const SquirrelValue& instance) const {
		return m_Implementation->Record(instance).Handle;
	}

	scene::CommonProcessTasks& ScriptLayerBindings::CommonTasks(const SquirrelValue& instance) {
		auto& record = m_Implementation->Record(instance);
		if (!record.CommonProcess) {
			throw std::runtime_error("Layer common task owner is supplied separately by this concrete derived owner");
		}

		return *record.CommonProcess;
	}

	scene::LayerFrameProcessState ScriptLayerBindings::ProcessState(scene::LayerHandle handle) const {
		const auto& record = m_Implementation->Record(handle);
		if (!record.CommonProcess || record.Releasing) {
			throw std::runtime_error("Layer process state requires its live ordinary common owner");
		}

		return record.ProcessState;
	}

	scene::LayerFolderTasks& ScriptLayerBindings::FolderTasks(const SquirrelValue& instance) {
		return *m_Implementation->FolderRecord(instance).FolderTasks;
	}

	void ScriptLayerBindings::SetProcessState(const SquirrelValue& instance, scene::LayerFrameProcessState state) {
		auto& record = m_Implementation->Record(instance);
		if (!record.CommonProcess || record.Releasing) {
			throw std::runtime_error("Layer process state requires its live ordinary common owner");
		}
		if (state.Status != scene::TaskStatus::Active && state.Status != scene::TaskStatus::Complete && state.Status != scene::TaskStatus::Retire) {
			throw std::runtime_error("Layer containing process status is outside the recovered 0..2 domain");
		}

		record.ProcessState = state;
	}

	void ScriptLayerBindings::AttachMotionFrame(const SquirrelValue& instance, scene::LayerCameraDrawScope& renderer, ScriptMotionFrameServices services) {
		m_Implementation->AttachMotionFrame(instance, renderer, std::move(services));
	}

	void ScriptLayerBindings::DispatchMotionFrame(scene::LayerHandle handle, const MotionFrameOperation& operation) {
		auto& record = m_Implementation->Record(handle);
		if (!operation || !record.MotionFrame || !record.FrameDispatch || record.Releasing || record.DispatchingFrame) {
			throw std::runtime_error("Layer motion frame requires its actual configured services and one concrete non-reentrant operation");
		}

		record.DispatchingFrame = true;
		record.ActiveFrameOperation = &operation;
		try {
			m_Implementation->Vm.DispatchBorrowedReceiver(record.Instance, record.FrameDispatch);
		} catch (...) {
			record.ActiveFrameOperation = nullptr;
			record.DispatchingFrame = false;

			throw;
		}
		record.ActiveFrameOperation = nullptr;
		record.DispatchingFrame = false;
	}

	void ScriptLayerBindings::DispatchMotionFrame(const SquirrelValue& receiver, const MotionFrameOperation& operation) {
		auto& record = m_Implementation->Record(receiver);
		if (!operation || !record.MotionFrame || !record.FrameDispatch || record.Releasing || record.DispatchingFrame) {
			throw std::runtime_error("Layer motion frame requires its actual configured services and one concrete non-reentrant operation");
		}

		record.DispatchingFrame = true;
		record.ActiveFrameOperation = &operation;
		try {
			record.FrameDispatch(receiver);
		} catch (...) {
			record.ActiveFrameOperation = nullptr;
			record.DispatchingFrame = false;

			throw;
		}
		record.ActiveFrameOperation = nullptr;
		record.DispatchingFrame = false;
	}

	void ScriptLayerBindings::AttachFolderFrame(const SquirrelValue& instance, scene::LayerCameraDrawScope& renderer, ScriptFolderFrameServices services) {
		m_Implementation->AttachFolderFrame(instance, renderer, std::move(services));
	}

	void ScriptLayerBindings::DispatchFolderFrame(scene::LayerHandle handle, const FolderFrameOperation& operation) {
		auto& record = m_Implementation->Record(handle);
		if (!operation || !record.FolderFrame || !record.FolderFrameDispatch || record.Releasing || record.DispatchingFrame) {
			throw std::runtime_error("Folder frame requires one non-reentrant operation on its actual attached owner");
		}

		record.DispatchingFrame = true;
		record.ActiveFolderFrameOperation = &operation;
		try {
			m_Implementation->Vm.DispatchBorrowedReceiver(record.Instance, record.FolderFrameDispatch);
		} catch (...) {
			record.ActiveFolderFrameOperation = nullptr;
			record.DispatchingFrame = false;

			throw;
		}
		record.ActiveFolderFrameOperation = nullptr;
		record.DispatchingFrame = false;
	}

	void ScriptLayerBindings::DispatchFolderFrame(const SquirrelValue& receiver, const FolderFrameOperation& operation) {
		auto& record = m_Implementation->FolderRecord(receiver);
		if (!operation || !record.FolderFrame || !record.FolderFrameDispatch || record.Releasing || record.DispatchingFrame) {
			throw std::runtime_error("Folder frame requires one non-reentrant operation on its actual attached owner");
		}

		record.DispatchingFrame = true;
		record.ActiveFolderFrameOperation = &operation;
		try {
			record.FolderFrameDispatch(receiver);
		} catch (...) {
			record.ActiveFolderFrameOperation = nullptr;
			record.DispatchingFrame = false;

			throw;
		}
		record.ActiveFolderFrameOperation = nullptr;
		record.DispatchingFrame = false;
	}

	void ScriptLayerBindings::ConstructBaseInstance(std::span<const SquirrelValue> arguments) {
		m_Implementation->ConstructBase(arguments, m_Implementation);
	}

	scene::LayerBaseTasks& ScriptLayerBindings::BaseTasks(const SquirrelValue& instance) {
		auto& record = m_Implementation->Record(instance);
		if (!record.BaseTasks) {
			throw std::runtime_error("Base task lookup requires its actual SQLayerBase owner");
		}

		return *record.BaseTasks;
	}

	void ScriptLayerBindings::AttachBaseFrame(const SquirrelValue& instance, scene::LayerCameraDrawScope& renderer) {
		m_Implementation->AttachBaseFrame(instance, renderer);
	}

	void ScriptLayerBindings::ConfigureBaseRetirement(const SquirrelValue& instance, std::function<void(const SquirrelValue&)> preflight, std::function<void(const SquirrelValue&)> cleanup) {
		auto& record = m_Implementation->Record(instance);
		if (!record.BaseTasks || record.BasePreflight || record.BaseCleanup || record.Constructing || record.Releasing || record.DispatchingFrame || !preflight || !cleanup) {
			throw std::runtime_error("Base derived retirement requires one live concrete owner and both callbacks");
		}

		record.BasePreflight = std::move(preflight);
		record.BaseCleanup = std::move(cleanup);
	}

	void ScriptLayerBindings::DispatchBaseFrame(scene::LayerHandle handle, const BaseFrameOperation& operation) {
		auto& record = m_Implementation->Record(handle);
		if (!operation || !record.BaseFrame || !record.BaseDispatch || record.Releasing || record.DispatchingFrame) {
			throw std::runtime_error("Base frame requires one non-reentrant operation on its actual attached owner");
		}

		record.DispatchingFrame = true;
		record.ActiveBaseOperation = &operation;
		try {
			m_Implementation->Vm.DispatchBorrowedReceiver(record.Instance, record.BaseDispatch);
		} catch (...) {
			record.ActiveBaseOperation = nullptr;
			record.DispatchingFrame = false;

			throw;
		}
		record.ActiveBaseOperation = nullptr;
		record.DispatchingFrame = false;
	}

	void ScriptLayerBindings::SetPriority(scene::LayerHandle handle, float priority) {
		auto& record = m_Implementation->Record(handle);
		if (record.Releasing) {
			throw std::runtime_error("Layer priority requires its live native owner outside retirement");
		}

		m_Implementation->Tree.SetPriority(record.Handle, priority);
	}

	void ScriptLayerBindings::SetSmoothing(const SquirrelValue& instance, bool smoothing) {
		m_Implementation->SetSmoothing(instance, smoothing);
	}

	void ScriptLayerBindings::ConfigureScreenOwners(const SquirrelValue& screen_class, ScreenOwnerResolver resolver) {
		auto& state = *m_Implementation;
		if (state.Vm.Type(screen_class) != SquirrelValueType::Class || !resolver || state.ResolveScreen) {
			throw std::runtime_error("layer Screen ownership requires one live class/resolver registration");
		}

		state.ScreenClass = screen_class;
		state.ScreenClassRoot = state.Vm.Retain(screen_class);
		state.ResolveScreen = std::move(resolver);
	}

	void ScriptLayerBindings::DetachScreenOwners(const SquirrelValue& screen) {
		auto& state = *m_Implementation;
		if (!state.ResolveScreen) {
			throw std::runtime_error("screen detach requires a native Screen owner");
		}

		// Native release uses the established Screen owner after script Class.Finalize clears ancestry.
		const auto destination = state.ResolveScreen(screen);
		state.Tree.RequireCanClearScreenChildren(destination);
		for (auto& [index, record] : state.Instances) {
			static_cast<void>(index);
			if (state.Tree.State(record.Handle).ScreenOwner == destination) {
				record.Owner.reset();
			}
		}

		state.Tree.ClearScreenRoot(destination);
	}

	bool ScriptLayerBindings::IsLayerOwner(const SquirrelValue& owner) const {
		const auto& state = *m_Implementation;

		return std::holds_alternative<std::monostate>(owner) || state.Vm.IsInstanceOf(owner, state.FolderClass) || (state.ResolveScreen && state.Vm.IsInstanceOf(owner, state.ScreenClass));
	}

	scene::RawLayerAttachment ScriptLayerBindings::RawTextureAttachment(const SquirrelValue& owner) const {
		const auto destination = m_Implementation->OwnerHandle(owner);
		scene::RawLayerAttachment attachment;
		attachment.Folder = destination.Folder;
		attachment.Screen = destination.Screen;

		return attachment;
	}

	void ScriptLayerBindings::AttachRawTextureInstance(const SquirrelValue& instance, scene::LayerHandle handle, const SquirrelValue& owner, std::function<void(bool)> set_smoothing, std::function<void(const SquirrelValue&)> retire) {
		auto& state = *m_Implementation;
		if (!state.Vm.IsInstanceOf(instance, state.BaseClass) || !state.Tree.Contains(handle) || state.Tree.State(handle).Kind != scene::LayerKind::RawTexture || !set_smoothing || !retire) {
			throw std::runtime_error("RawTex common registration requires a genuine LayerBase descendant, raw leaf and live task bridges");
		}
		const auto index = std::get<SquirrelObjectHandle>(instance).Index;
		if (state.Instances.contains(index)) {
			throw std::runtime_error("RawTex common receiver is already attached");
		}
		const auto destination = state.OwnerHandle(owner);
		const auto layer = state.Tree.State(handle);
		if (layer.Owner != destination.Folder || layer.ScreenOwner != destination.Screen) {
			throw std::runtime_error("RawTex common receiver and actual tree parent disagree");
		}

		LayerBindingRecord record;
		record.Instance = state.Vm.BorrowObjectIdentity(instance);
		record.Handle = handle;
		record.RawSmoothing = std::move(set_smoothing);
		record.RawRetirement = std::move(retire);
		if (!std::holds_alternative<std::monostate>(owner)) {
			record.Owner = state.Vm.Retain(state.Vm.CreateWeakReference(owner));
		}
		state.FrameOwnerConfigurationSealed = true;
		state.Objects.AttachInstance(instance);
		try {
			state.Instances.emplace(index, std::move(record));
			state.Objects.ConfigureNativeRelease(instance, [weak = std::weak_ptr<Implementation>(m_Implementation)](const SquirrelValue& receiver) {
				if (const auto retained = weak.lock()) {
					retained->Release(receiver);
				}
			});
		} catch (...) {
			state.Instances.erase(index);
			state.Objects.ReleaseInstance(instance);

			throw;
		}
	}

	void ScriptLayerBindings::DropRawTextureParent(const SquirrelValue& instance) {
		auto& state = *m_Implementation;
		auto& record = state.Record(instance);
		if (!record.RawRetirement || state.Tree.State(record.Handle).Kind != scene::LayerKind::RawTexture || state.Tree.State(record.Handle).Attached) {
			throw std::runtime_error("RawTex parent retirement must follow actual manager unlink");
		}

		record.Releasing = true;
		record.Owner.reset();
	}

	void ScriptLayerBindings::ReleaseRawTextureObject(const SquirrelValue& instance) {
		auto& state = *m_Implementation;
		auto& record = state.Record(instance);
		if (!record.RawRetirement || record.Owner || state.Tree.State(record.Handle).Attached || record.UpdatingBorrowedViews || !record.BorrowedViews.empty()) {
			throw std::runtime_error("RawTex Object retirement requires completed common parent and borrowed-view cleanup");
		}

		std::exception_ptr failure;
		try {
			state.Objects.ReleaseInstance(instance);
		} catch (...) {
			failure = std::current_exception();
		}

		state.Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

	void ScriptLayerBindings::ConstructMotionInstance(std::span<const SquirrelValue> arguments) {
		if (arguments.empty()) {
			throw std::runtime_error("native derived Layer constructor requires a receiver");
		}

		m_Implementation->Construct(arguments, m_Implementation, true);
	}

	void ScriptLayerBindings::RequireCanReleaseAll(const SquirrelVm& actual_vm, const ScriptObjectBindings& actual_objects, const scene::LayerTree& actual_tree) const {
		auto& state = *m_Implementation;
		if (&state.Vm != &actual_vm || &state.Objects != &actual_objects || &state.Tree != &actual_tree) {
			throw std::runtime_error("prepared Layer preflight requires its actual VM/Object/Tree owner identities");
		}

		state.Vm.RequireCanClose();
		if (state.Vm.IsClosed()) {
			if (!state.Instances.empty()) {
				throw std::runtime_error("prepared Driver VM closed with unretired actual Layer owners");
			}

			return;
		}
		for (const auto& [index, record] : state.Instances) {
			static_cast<void>(index);
			if (record.RawRetirement) {
				throw std::runtime_error("prepared Driver RawTex retirement requires its actual separate texture/context preflight");
			}

			state.Vm.DispatchBorrowedReceiver(record.Instance, [&state](const SquirrelValue& receiver) {
				state.RequireCanRelease(receiver);
			});
		}
	}

	void ScriptLayerBindings::AttachMotionRuntime(const SquirrelValue& instance, scene::MotionManagerRuntime& actual_runtime) {
		m_Implementation->AttachMotionRuntime(instance, actual_runtime);
	}

	scene::MotionManagerRuntime& ScriptLayerBindings::MotionRuntime(const SquirrelValue& instance, const scene::MotionResourceManager& actual_manager) const {
		auto& record = m_Implementation->MotionRecord(instance);
		if (record.Releasing) {
			throw std::runtime_error("actual Motion runtime lookup cannot reenter Layer retirement");
		}

		std::size_t slot = 2;
		if (&actual_manager == &record.MotionResources->Secondary()) {
			slot = 0;
		} else if (&actual_manager == &record.MotionResources->Primary()) {
			slot = 1;
		}
		if (slot == 2) {
			throw std::runtime_error("actual Motion runtime lookup requires this Layer's owned Primary or Secondary manager");
		}
		const auto& binding = record.MotionRuntimeBindings[slot];
		if (binding.IsEmpty()) {
			throw std::runtime_error("actual requested Layer Motion runtime is unregistered");
		}

		// Check the existing retirement/expiry token before reading any saved runtime identity.
		auto& runtime = binding.BorrowRuntime();
		if (record.MotionRuntimeIdentities[slot] != &runtime || &runtime.Manager() != &actual_manager) {
			throw std::runtime_error("actual Motion runtime lookup found a mismatched registered owner/manager");
		}

		return runtime;
	}

	void ScriptLayerBindings::DetachMotionRuntime(const SquirrelValue& instance, const scene::MotionManagerRuntime& actual_runtime) {
		auto& state = *m_Implementation;
		state.RequireCanRelease(instance);
		auto& record = state.MotionRecord(instance);
		for (std::size_t slot = 0; slot < record.MotionRuntimeBindings.size(); ++slot) {
			if (record.MotionRuntimeIdentities[slot] == &actual_runtime) {
				record.MotionRuntimeBindings[slot].CloseRuntime();
				record.MotionRuntimeBindings[slot] = {};
				record.MotionRuntimeIdentities[slot] = nullptr;

				return;
			}
		}

		throw std::runtime_error("actual Motion runtime is not attached to this Layer");
	}

	void ScriptLayerBindings::RequireCanReleaseInstance(const SquirrelValue& instance) const {
		m_Implementation->RequireCanRelease(instance);
	}

	void ScriptLayerBindings::ReleaseInstance(const SquirrelValue& instance) {
		m_Implementation->Release(instance);
	}

} // namespace sgre::script
