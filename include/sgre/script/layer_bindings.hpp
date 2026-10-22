#pragma once

#include "sgre/scene/layer_base_frame.hpp"
#include "sgre/scene/layer_folder_frame.hpp"
#include "sgre/scene/layer_manager_frame.hpp"
#include "sgre/scene/layers.hpp"
#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_layer_frame.hpp"
#include "sgre/scene/motion_layer_tasks.hpp"
#include "sgre/scene/motion_manager_runtime.hpp"
#include "sgre/scene/motion_resources.hpp"
#include "sgre/scene/raw_layer_owner.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace sgre::script {

	class ScriptPsbBindings;
	class ScriptMotionResourceOwner;

	struct ScriptMotionLayerProperties {
		scene::LayerClip ObjectClip;
		bool CoordinateAlignmentToInteger = false;
		bool MaskRegionClipping = false;
	};

	struct ScriptMotionFrameServices {
		std::function<scene::MotionLayerFrameState(const SquirrelValue&)> State;
		std::function<void(const SquirrelValue&, std::uint32_t)> SecondaryDraw;
		std::function<void(const SquirrelValue&, std::uint32_t)> PrimaryDraw;
	};

	struct ScriptFolderFrameServices {
		std::function<scene::LayerFolderFrameState(const SquirrelValue&)> State;
		scene::LayerManagerFrameOperations Children;
	};

	struct ScriptLayerFrameServices {
		ScriptMotionFrameServices Motion;
		ScriptFolderFrameServices Folder;
		std::size_t MotionDrawCapacity = 1024;
	};

	class ScriptLayerBindings {
	private:
		class Implementation;

	public:
		class BorrowedViewRegistration {
		public:
			BorrowedViewRegistration() = default;
			~BorrowedViewRegistration();
			BorrowedViewRegistration(const BorrowedViewRegistration&) = delete;
			BorrowedViewRegistration& operator=(const BorrowedViewRegistration&) = delete;
			BorrowedViewRegistration(BorrowedViewRegistration&& other) noexcept;
			BorrowedViewRegistration& operator=(BorrowedViewRegistration&& other) noexcept;
			void Reset() noexcept;
			[[nodiscard]] bool IsAttached() const noexcept;

		private:
			std::weak_ptr<Implementation> m_Parent;
			SquirrelObjectIdentity m_Instance;
			std::uint64_t m_Identifier = 0;
			friend class ScriptLayerBindings;
		};
		using ScreenOwnerResolver = std::function<scene::LayerScreenRootHandle(const SquirrelValue&)>;
		using MotionFrameOperation = std::function<void(const SquirrelValue&, scene::MotionLayerFrame&)>;
		using BaseFrameOperation = std::function<void(const SquirrelValue&, scene::LayerBaseFrame&)>;
		using FolderFrameOperation = std::function<void(const SquirrelValue&, scene::LayerFolderFrame&)>;
		// The VM, Object adapter, and tree outlive this owner. Parent targets use native weak wrappers.
		ScriptLayerBindings(SquirrelVm& vm, ScriptObjectBindings& objects, scene::LayerTree& tree);
		~ScriptLayerBindings();
		ScriptLayerBindings(const ScriptLayerBindings&) = delete;
		ScriptLayerBindings& operator=(const ScriptLayerBindings&) = delete;
		ScriptLayerBindings(ScriptLayerBindings&&) = delete;
		ScriptLayerBindings& operator=(ScriptLayerBindings&&) = delete;

		[[nodiscard]] SquirrelValue LayerBaseClass() const;
		[[nodiscard]] SquirrelValue LayerFolderClass() const;
		[[nodiscard]] SquirrelValue MotionLayerBaseClass() const;
		[[nodiscard]] SquirrelValue LayerClass() const;
		// PSB/catalog/cache services outlive the binding and each native motion leaf.
		void ConfigureMotionOwners(ScriptPsbBindings& psb, scene::MotionAssetCatalog& assets, scene::TextureCache& cache);
		// Configure once before the first native Layer. Camera, actual driver clock, seed and service owners enclose VM Close.
		// Required services preserve concrete derived state/source/child dispatch; this installs no onProcess or RawTex driver.
		// Callable copies must not mutate or retain the constructing receiver; direct retirement rejects that active phase.
		void ConfigureFrameOwners(scene::LayerCameraDrawScope& renderer, const core::FrameClock& clock, std::span<const std::uint32_t, scene::kMotionSharedSeedWords> runtime_seed, ScriptLayerFrameServices services);
		[[nodiscard]] const ScriptMotionResourceOwner& MotionResources(const SquirrelValue& instance) const;
		[[nodiscard]] ScriptMotionLayerProperties MotionProperties(const SquirrelValue& instance) const;
		// Read the actual native Layer state for its already scoped receiver; retain no additional script owner.
		[[nodiscard]] scene::LayerFolderFrameState FolderFrameState(const SquirrelValue& receiver) const;
		[[nodiscard]] scene::MotionLayerFrameState MotionFrameState(const SquirrelValue& receiver) const;
		// Native Affine Process publishes only scale, radians and offset into its actual common camera.
		// Independent center/clip fields and later common setters retain their existing ownership.
		void PublishAffineCamera(const SquirrelValue& instance, const scene::LayerCameraParameters& camera);
		// The returned task owner borrows its live actual Layer; services remain alive through VM Close.
		[[nodiscard]] scene::MotionLayerTasks& MotionTasks(const SquirrelValue& instance);
		// Ordinary Folder/Motion owners contain their real common serial/parallel task sets; RawTex owns its graph separately.
		// The returned reference borrows that native Layer; concrete frame execution requires its attached services.
		[[nodiscard]] scene::CommonProcessTasks& CommonTasks(const SquirrelValue& instance);
		[[nodiscard]] scene::LayerFolderTasks& FolderTasks(const SquirrelValue& instance);
		[[nodiscard]] scene::LayerFrameProcessState ProcessState(scene::LayerHandle handle) const;
		void SetProcessState(const SquirrelValue& instance, scene::LayerFrameProcessState state);
		// These actual renderer/state/source services borrow existing owners and contain no script receiver roots.
		void AttachMotionFrame(const SquirrelValue& instance, scene::LayerCameraDrawScope& renderer, ScriptMotionFrameServices services);
		// Existing registry lookup lends the checked VM receiver; operation/services/adapter outlive dispatch.
		// This does not make current-member automatic finalization safe or enable an engine frame.
		void DispatchMotionFrame(scene::LayerHandle handle, const MotionFrameOperation& operation);
		// Consume a receiver already lent by the containing manager's checked VM scope; never redispatch it.
		// An ordinary owned receiver is also valid. Neither overload adds a receiver root or permits recursive frame dispatch.
		void DispatchMotionFrame(const SquirrelValue& receiver, const MotionFrameOperation& operation);
		// Folder owns only the two common groups; its native camera contains its existing child Layer manager.
		// Child services dispatch genuine concrete owners; native Folder drawing preserves the enclosing renderer state.
		void AttachFolderFrame(const SquirrelValue& instance, scene::LayerCameraDrawScope& renderer, ScriptFolderFrameServices services);
		void DispatchFolderFrame(scene::LayerHandle handle, const FolderFrameOperation& operation);
		void DispatchFolderFrame(const SquirrelValue& receiver, const FolderFrameOperation& operation);
		[[nodiscard]] scene::LayerHandle Handle(const SquirrelValue& instance) const;
		// Direct native-owner submission marks the actual containing Layer manager dirty, without VM dispatch or retain.
		void SetPriority(scene::LayerHandle handle, float priority);
		// Native derived view overrides call this exact common Layer operation without changing VM context.
		void SetSmoothing(const SquirrelValue& instance, bool smoothing);
		void ConfigureScreenOwners(const SquirrelValue& screen_class, ScreenOwnerResolver resolver);
		void DetachScreenOwners(const SquirrelValue& screen);
		// Genuine derived factories construct the common Layer directly, without an extra VM constructor call.
		void ConstructMotionInstance(std::span<const SquirrelValue> arguments);
		// Genuine SQLayerBase descendants own common clip/camera tasks without a Motion or Folder substitute.
		void ConstructBaseInstance(std::span<const SquirrelValue> arguments);
		[[nodiscard]] scene::LayerBaseTasks& BaseTasks(const SquirrelValue& instance);
		void AttachBaseFrame(const SquirrelValue& instance, scene::LayerCameraDrawScope& renderer);
		void DispatchBaseFrame(scene::LayerHandle handle, const BaseFrameOperation& operation);
		// Both callbacks borrow the actual receiver. Preflight runs before mutation; derived cleanup precedes base graph retirement.
		void ConfigureBaseRetirement(const SquirrelValue& instance, std::function<void(const SquirrelValue&)> preflight, std::function<void(const SquirrelValue&)> cleanup);

		// Derived owners preflight all borrowed tasks/resources before dropping their own native roots.
		void RequireCanReleaseInstance(const SquirrelValue& instance) const;
		// Actual installed records/contexts are observed without retaining a receiver or creating another registry.
		void RequireCanReleaseAll(const SquirrelVm& actual_vm, const ScriptObjectBindings& actual_objects, const scene::LayerTree& actual_tree) const;
		// Runtime manager must be this leaf's actual Primary or Secondary. Bind before constructing its tasks.
		void AttachMotionRuntime(const SquirrelValue& instance, scene::MotionManagerRuntime& actual_runtime);
		// Read the existing per-slot registration only; an absent application runtime is never manufactured.
		[[nodiscard]] scene::MotionManagerRuntime& MotionRuntime(const SquirrelValue& instance, const scene::MotionResourceManager& actual_manager) const;
		// Explicit authored detach closes an already-quiescent runtime before its borrowed source owners expire.
		void DetachMotionRuntime(const SquirrelValue& instance, const scene::MotionManagerRuntime& actual_runtime);
		void ReleaseInstance(const SquirrelValue& instance);
		// RawTex owns its common task/texture graph; this registry supplies inherited LayerBase and weak-parent state.
		// Its observational preflight checks the actual owner without retiring or retaining the receiver.
		[[nodiscard]] bool IsLayerOwner(const SquirrelValue& owner) const;
		[[nodiscard]] scene::RawLayerAttachment RawTextureAttachment(const SquirrelValue& owner) const;
		void AttachRawTextureInstance(const SquirrelValue& instance, scene::LayerHandle handle, const SquirrelValue& owner, std::function<void(bool)> set_smoothing, std::function<void(const SquirrelValue&)> require_can_retire, std::function<void(const SquirrelValue&)> retire);
		// Invoke these in the RawLayerOwner parent/Object phases before its tree record is released.
		void DropRawTextureParent(const SquirrelValue& instance);
		void ReleaseRawTextureObject(const SquirrelValue& instance);

		// Callbacks borrow view state and must not retain script instances. Destroy/reset the token before that state.
		// SQMotion shares the actual borrowed-object vector; its native smoothing virtual is empty.
		[[nodiscard]] BorrowedViewRegistration BorrowMotionObject(const SquirrelValue& instance, std::function<void()> detach);
		[[nodiscard]] BorrowedViewRegistration BorrowMotionView(const SquirrelValue& instance, std::function<void(bool)> set_smoothing, std::function<void()> detach);

	private:
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
