#pragma once

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_local_frames.hpp"
#include "sgre/scene/motion_static_source.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace sgre::scene {
	class MotionSelectedLayerNodes;
	struct MotionSelectedLayerNode;

	class MotionSelectedLayerSources {
	public:
		// The actual registered asset owner supplies this borrow without adding PSB/model/VM retains.
		using ReadRegisteredAsset = std::function<const LoadedMotionAsset&(MotionProjectId)>;
		// Selected tree, its manager, loaded assets and action queue outlive this exclusive source phase.
		MotionSelectedLayerSources(MotionSelectedLayerNodes& selected, const MotionAssetCatalog& catalog, ReadRegisteredAsset read_asset);
		~MotionSelectedLayerSources() noexcept;
		MotionSelectedLayerSources(const MotionSelectedLayerSources&) = delete;
		MotionSelectedLayerSources& operator=(const MotionSelectedLayerSources&) = delete;
		[[nodiscard]] MotionLocalFrameServices FrameServices(std::function<void(std::string_view, std::string_view)> enqueue_action);
		[[nodiscard]] const MotionStaticSourceState& State(std::uint32_t layer) const;
		[[nodiscard]] const MotionStaticSourceBinding& Source(std::uint32_t layer) const;
		[[nodiscard]] MotionProjectId RequestingProject() const;
		// A resolved source may belong to another actual registration after native alias/fallback dispatch.
		[[nodiscard]] std::optional<MotionProjectId> ResolvedProject(std::uint32_t layer) const;

	private:
		friend class MotionSceneEvaluation;
		struct Registration {
			MotionProjectId Project;
			const LoadedMotionAsset* Asset = nullptr;
			std::span<const MotionSourceAlias> Aliases;
		};
		struct Entry {
			const MotionSelectedLayerNode* Node = nullptr;
			std::unique_ptr<MotionStaticSourceBinding> Binding;
			std::optional<std::size_t> Frame;
		};
		void RequireSelected() const;
		void RequireRegistrations() const;
		void RequireCurrent(std::uint32_t layer) const;
		void Bind(std::uint32_t layer, const PreparedMotionFrame& frame);
		[[nodiscard]] std::optional<RegisteredMotionSource> FindTexture(const ResolvedMotionIcon& binding) const;
		MotionSelectedLayerNodes& m_Selected;
		const MotionResourceManager& m_Manager;
		const MotionAssetCatalog& m_Catalog;
		MotionProjectId m_Project;
		MotionProjectIdentity m_Identity;
		ReadRegisteredAsset m_ReadAsset;
		std::vector<Registration> m_Registrations;
		std::unique_ptr<PreparedMotionSourceRegistry> m_Registry;
		std::vector<Entry> m_Entries;
	};

} // namespace sgre::scene
