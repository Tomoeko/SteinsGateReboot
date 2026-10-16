#include "sgre/scene/motion_tessellation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::scene {

	std::uint32_t ScaleMotionMeshDivision(std::uint32_t division, float ratio) {
		if (!std::isfinite(ratio) || ratio < 0) {
			throw std::runtime_error("motion mesh division ratio is nonfinite or negative");
		}

		const auto scaled = static_cast<float>(division) * ratio;
		if (!std::isfinite(scaled) || scaled >= 2147483648.0f) {
			throw std::runtime_error("motion mesh scaled division exceeds native integer conversion");
		}

		return static_cast<std::uint32_t>(static_cast<std::int32_t>(scaled));
	}

	static std::uint32_t RectangleSum(std::uint32_t width, std::uint32_t height) {
		static constexpr auto kNativeIntegerLimit = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
		if (width > kNativeIntegerLimit || height > kNativeIntegerLimit - width || width + height == 0) {
			throw std::runtime_error("motion mesh division rectangle exceeds native integer arithmetic");
		}

		return width + height;
	}

	std::uint32_t ResolveInheritedMotionMeshDivision(const InheritedMotionMeshDivision& request, MotionMeshDivisionCache& cache) {
		static constexpr auto kNativeIntegerLimit = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
		const auto extent = RectangleSum(request.Width, request.Height);
		const auto ancestor_extent = RectangleSum(request.AncestorWidth, request.AncestorHeight);
		const auto scaled = ScaleMotionMeshDivision(request.AncestorDivision, request.Ratio);
		if ((scaled != 0 && extent > kNativeIntegerLimit / scaled) || !std::isfinite(cache.Ratio) || cache.Ratio < 0 || cache.MinimumDivision > kNativeIntegerLimit) {
			throw std::runtime_error("motion mesh division product or inherited cache is invalid");
		}

		const auto requested_division = extent * scaled / ancestor_extent;
		auto updated = cache;
		// Original 0x140554da0 keeps the largest inherited count until the global ratio changes.
		if (updated.Ratio != request.Ratio) {
			updated.Ratio = request.Ratio;
			updated.MinimumDivision = 0;
		}

		updated.MinimumDivision = std::max(updated.MinimumDivision, requested_division);
		cache = updated;

		return updated.MinimumDivision;
	}

} // namespace sgre::scene
