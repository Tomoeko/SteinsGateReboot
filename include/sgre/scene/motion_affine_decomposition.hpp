#pragma once

#include "sgre/resource/motion_mesh.hpp"

#include <array>
#include <optional>

namespace sgre::scene {
	struct MotionRotationReference {
		float Radians = 0;
		float Cosine = 1;
		float Sine = 0;
	};

	struct MotionAffineDecomposition {
		MotionRotationReference Rotation;
		std::array<float, 2> Scale{};
		std::array<float, 2> Shear{};
		bool Reflected = false;
	};

	// Native 2e5770 keeps the previous unnormalized angle and chooses the nearer fitted axis.
	// Translation is unused. A finite singular linear transform has no decomposition.
	[[nodiscard]] std::optional<MotionAffineDecomposition> DecomposeMotionAffine(const resource::MotionMeshMatrix& matrix, const MotionRotationReference& reference = {});
} // namespace sgre::scene
