#pragma once

#include "sgre/scene/motion_layer_composition.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace sgre::scene {

	class MotionChildConstructionState;
	class MotionParticlePlayerState;

	struct MotionClipRectangle {
		float Left = 0;
		float Top = 0;
		float Right = 0;
		float Bottom = 0;
	};

	class MotionLayerClipRectangles {
	public:
		// Actual immutable selected preparation outlives this owner. Each kind7 node owns a stable rectangle.
		explicit MotionLayerClipRectangles(const PreparedMotionTimeline& timeline);
		MotionLayerClipRectangles(const MotionLayerClipRectangles&) = delete;
		MotionLayerClipRectangles& operator=(const MotionLayerClipRectangles&) = delete;
		// Actual node composition and selected slots precede this phase; indices are not reconstructed by time search.
		// An inherited root rectangle borrows the enclosing parent's owner through use of the published links.
		void Compose(std::span<const MotionComposedLayer> layers, std::span<const std::size_t> current_indices, const MotionClipRectangle* root_clip);
		// Links borrow this selected owner or the supplied enclosing root clip until its next composition/retirement.
		[[nodiscard]] std::span<const MotionClipRectangle* const> Clips() const noexcept;
		[[nodiscard]] const MotionClipRectangle& OwnedRectangle(std::size_t layer) const;
		// Native521bf0 update/binding branches borrow this actual kind3 parent link before UpdateChild.
		// Do not call for the changed hidden/empty-source reset branch; enclosing owners remain live through child use.
		void BindChildClip(std::size_t layer, MotionChildConstructionState& child) const;
		void BindParticleClip(std::size_t layer, MotionParticlePlayerState& child) const;

	private:
		friend class MotionSceneEvaluation;
		const PreparedMotionTimeline& m_Timeline;
		std::vector<MotionClipRectangle> m_Rectangles;
		std::vector<const MotionClipRectangle*> m_Clips;
		bool m_Composed = false;
	};

} // namespace sgre::scene
