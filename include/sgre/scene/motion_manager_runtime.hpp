#pragma once

#include "sgre/core/frame_clock.hpp"
#include "sgre/scene/motion_preparation_lease.hpp"
#include "sgre/scene/motion_random.hpp"
#include "sgre/scene/motion_selected_sprite_player.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace sgre::script {
	class ScriptLayerBindings;
}

namespace sgre::scene {
	class EmotePlayerTask;

	class MotionPlayerTask;
	class EmoteScenePlayer;
	class MotionLayerTasks;
	class MotionSceneDrawContext;
	class MotionAssetCatalog;
	struct LoadedMotionAsset;

	// Resource registrations remain borrowed. All returned frame/source references expire with the last Owner lease.
	struct MotionRuntimeSelection {
		const resource::MotionResource& Resource;
		const PreparedMotionTimeline& Timeline;
		const PreparedMotionSourceRegistry& Sources;
		MotionPreparationLease Owner;
	};

	struct MotionManagerRuntimeServices {
		// Observational borrow of the actual registered asset's alias metadata, including a legitimate empty span.
		// It adds no PSB/model reference and cannot unregister or mutate a registration.
		std::function<std::span<const MotionSourceAlias>(MotionProjectId)> ReadSourceAliases;
		// Either both are absent for the explicit legacy sprite API, or both borrow the actual registered scene owners.
		MotionAssetCatalog* SceneAssets = nullptr;
		std::function<const LoadedMotionAsset&(MotionProjectId)> ReadRegisteredAsset;
	};

	class MotionManagerRuntime {
		class LayerBindingLifetime;

	public:
		class LayerBinding {
		public:
			LayerBinding() = default;
			~LayerBinding() noexcept;
			LayerBinding(const LayerBinding&) = delete;
			LayerBinding& operator=(const LayerBinding&) = delete;
			LayerBinding(LayerBinding&& other) noexcept;
			LayerBinding& operator=(LayerBinding&& other) noexcept;
			[[nodiscard]] bool IsEmpty() const noexcept;
			[[nodiscard]] bool IsRetired() const noexcept;
			// Checked borrow of the actual runtime; the control token adds no object/source retain.
			[[nodiscard]] MotionManagerRuntime& BorrowRuntime() const;
			// Checked metadata borrows the actual runtime, not its object/model/Layer lifetime.
			void RequireCanRetirePlayers() const;
			void CloseRuntime();

		private:
			explicit LayerBinding(std::shared_ptr<LayerBindingLifetime> lifetime);
			std::shared_ptr<LayerBindingLifetime> m_Lifetime;
			friend class MotionManagerRuntime;
		};
		// One existing actual manager pairs with these shared IDs/random/context. No project/source copy or extra retain.
		// Manager, prepared looper clock, runtime seed span and alias owners outlive this runtime and its concrete tasks.
		// Clock Advance belongs to the driver; only the recovered fixed-count=1 or cached-variable domain is admitted.
		MotionManagerRuntime(const MotionResourceManager& manager, const core::FrameClock& clock, std::span<const std::uint32_t, kMotionSharedSeedWords> runtime_seed, MotionManagerRuntimeServices services, std::size_t draw_capacity = 1024);
		~MotionManagerRuntime() noexcept;
		MotionManagerRuntime(const MotionManagerRuntime&) = delete;
		MotionManagerRuntime& operator=(const MotionManagerRuntime&) = delete;
		// Closed/retired bindings cannot lend a manager that may already have ended with its Layer.
		[[nodiscard]] const MotionResourceManager& Manager() const;
		[[nodiscard]] MotionLayerIdentifierPool& Identifiers();
		[[nodiscard]] MotionSpriteDrawContext& Context();
		[[nodiscard]] bool HasSceneServices() const;
		[[nodiscard]] MotionAssetCatalog& SceneAssets() const;
		[[nodiscard]] const LoadedMotionAsset& ReadRegisteredAsset(MotionProjectId project) const;
		[[nodiscard]] MotionSceneDrawContext& SceneContext();
		[[nodiscard]] float ReadFrameDelta() const;
		[[nodiscard]] float NextUnitFloat();
		[[nodiscard]] float DepthProjection() const;
		// Dynamic projection invalidation is not yet recovered; setup precedes concrete player construction.
		void SetDepthProjection(float coefficient);
		// Keep the returned lease through every selected/frame/source view; registrations and raw backing remain external.
		[[nodiscard]] MotionRuntimeSelection Prepare(const ResolvedMotionTimeline& selection);
		[[nodiscard]] MotionRuntimeSelection PrepareScene(const ResolvedMotionTimeline& selection);
		// Clone shares immutable prepared frames while the destination owns independent actual registration borrows.
		[[nodiscard]] MotionRuntimeSelection ClonePreparation(const MotionPreparationLease& source);
		[[nodiscard]] bool ResourceBorrowsLive() const;
		[[nodiscard]] bool RegistrationOrderMatches(const PreparedMotionSourceRegistry& sources) const;
		[[nodiscard]] std::optional<RegisteredMotionSource> FindTexture(const ResolvedMotionIcon& binding) const;
		[[nodiscard]] std::size_t PlayerCount() const noexcept;
		// Live players may remain, but their actual context must be idle/empty and preparation finished.
		void RequireCanRetirePlayers() const;
		[[nodiscard]] bool CanClose() const noexcept;
		// Actual tasks/row borrowers retire before manager random/context/index storage; source ownership remains external.
		void Close();

	private:
		friend class MotionPlayerTask;
		friend class EmotePlayerTask;
		friend class EmoteScenePlayer;
		friend class script::ScriptLayerBindings;
		[[nodiscard]] LayerBinding BindLayerTasks(MotionLayerTasks& tasks);
		void RequireLayerTaskOwner(const MotionLayerTasks& tasks) const;
		void AcquirePlayer();
		void ReleasePlayer() noexcept;
		[[nodiscard]] MotionRuntimeSelection Prepare(const ResolvedMotionTimeline& selection, MotionAttributeBlendPolicy policy);
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
