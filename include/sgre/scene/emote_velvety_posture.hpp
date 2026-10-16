#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/emote_tress_model.hpp"
#include "sgre/scene/motion_affine_decomposition.hpp"
#include "sgre/scene/motion_layer_uids.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace sgre::scene {
	struct EmoteVelvetyInputFrame {
		// Actual driver2f0620 owns this conversion; this reader does not replace that producer.
		resource::MotionMeshMatrix WorldToPhysics;
		float DepthScale = 0;
		bool Mirror = false;
		bool ResetRotationReference = false;
	};

	struct EmoteVelvetyPartPosture {
		float Rotation = 0;
		float Scale = 1;
	};

	struct EmoteVelvetyPosture {
		EmotePhysicalPosture Physical;
		float CorrectionAngle = 0;
		float CorrectionScale = 1;
	};

	// One serialized control shares its retained rotation reference across its actual UID occurrences.
	class EmoteVelvetyPostureReader {
	public:
		// Native String2ec840 converts actual normalized metadata to local pixel points before2f4a40.
		EmoteVelvetyPostureReader(float width, float height, float origin_x, float origin_y, float pixels_per_meter, std::span<const resource::MotionPoint> fixes);
		// Synchronize2f3363 replaces only the local pixel origin before the same retained rotation reader.
		void SetLocalPixelOrigin(resource::MotionPoint origin);
		// Parent posture is the actual preceding PartsTree parent's cache, supplied only for its inherited-pose branch.
		// Reads completed selected numerics/ancestry; does not advance Motion or publish a mesh.
		[[nodiscard]] EmoteVelvetyPosture Read(const MotionLayerUidReference& actual_layer, const EmoteVelvetyInputFrame& frame, const EmoteVelvetyPartPosture* actual_parent = nullptr);
		// Actual transformed points before native2ffd10 prediction, retained until the next Read.
		[[nodiscard]] std::span<const resource::MotionPoint> WorldFixes() const;

	private:
		resource::MotionPoint m_LocalOrigin;
		std::array<resource::MotionPoint, 3> m_LocalFixes{};
		std::array<resource::MotionPoint, 3> m_WorldFixes{};
		std::size_t m_FixCount = 0;
		MotionRotationReference m_RotationReference;
		bool m_HasRead = false;
	};
} // namespace sgre::scene
