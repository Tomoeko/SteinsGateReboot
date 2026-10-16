#pragma once

#include "sgre/resource/motion.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sgre::resource {

	struct MotionBezierPatch {
		std::array<MotionPoint, 16> Controls;
	};

	struct MotionBezierLattice {
		// Low/high nibbles store horizontal/vertical degree; controls are row-major. Zero denotes no lattice.
		std::uint8_t EncodedDegree = 0;
		std::vector<MotionPoint> Controls;
	};

	struct MotionDeformation {
		std::optional<MotionBezierPatch> Patch;
		std::optional<std::uint32_t> EasingIndex;
	};

	struct MotionMeshMatrix {
		float XX = 1;
		float XY = 0;
		float TranslationX = 0;
		float YX = 0;
		float YY = 1;
		float TranslationY = 0;
	};

	struct MotionIconMesh {
		// Original vertices are normalized source coordinates; the stored matrix maps them to local geometry.
		std::vector<MotionPoint> Vertices;
		std::vector<std::uint32_t> StripIndices;
		MotionMeshMatrix Matrix;
	};

	[[nodiscard]] MotionDeformation ReadMotionDeformation(const PsbValue& mesh, const MotionResource& resource);
	[[nodiscard]] MotionIconMesh ReadMotionIconMesh(const PsbDocument& document, const PsbValue& mesh, std::span<const std::uint8_t> bytes);

} // namespace sgre::resource
