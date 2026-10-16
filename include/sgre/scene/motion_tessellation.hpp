#pragma once

#include <cstdint>

namespace sgre::scene {

	struct MotionMeshDivisionCache {
		float Ratio = 0;
		std::uint32_t MinimumDivision = 0;
	};

	struct InheritedMotionMeshDivision {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t AncestorWidth = 0;
		std::uint32_t AncestorHeight = 0;
		std::uint32_t AncestorDivision = 0;
		float Ratio = 1;
	};

	[[nodiscard]] std::uint32_t ScaleMotionMeshDivision(std::uint32_t division, float ratio);
	[[nodiscard]] std::uint32_t ResolveInheritedMotionMeshDivision(const InheritedMotionMeshDivision& request, MotionMeshDivisionCache& cache);

} // namespace sgre::scene
