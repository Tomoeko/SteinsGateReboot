#pragma once

#include "sgre/resource/motion.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {
	class MotionSelectedLayerNodes;
	class MotionLayerComposition;
	class MotionLocalFrames;
	class MotionSelectedMeshVertices;

	struct MotionShapeSnapshot {
		// Native 36-byte record: constructor shape word followed by eight retained float cells.
		std::uint32_t Type = 0;
		std::array<float, 8> Values{};
		[[nodiscard]] bool Contains(resource::MotionPoint point) const;
	};

	class MotionLayerShapeOwner {
	public:
		// The actual selected type1 node and immutable raw/prepared metadata enclose this exclusive owner.
		MotionLayerShapeOwner(MotionSelectedLayerNodes& selected, std::uint32_t layer);
		MotionLayerShapeOwner(MotionSelectedLayerNodes& selected, std::uint32_t layer, const MotionLayerShapeOwner& source);
		MotionLayerShapeOwner(const MotionLayerShapeOwner&) = delete;
		MotionLayerShapeOwner& operator=(const MotionLayerShapeOwner&) = delete;
		void Compose(const MotionLayerComposition& composition, const MotionLocalFrames& local, const MotionSelectedMeshVertices& vertices);
		// Copies the last native result, including initial zeros and unchanged-geometry retention; no lazy update or retain.
		[[nodiscard]] MotionShapeSnapshot Snapshot() const;
		void RequireCurrentComposition(const MotionLayerComposition& composition) const;

	private:
		friend class MotionSceneEvaluation;
		void RequireOwner() const;
		MotionSelectedLayerNodes& m_Selected;
		std::uint32_t m_Layer;
		MotionShapeSnapshot m_Snapshot;
		const MotionLayerComposition* m_Composition = nullptr;
		std::uint64_t m_CompositionSerial = 0;
	};

} // namespace sgre::scene
