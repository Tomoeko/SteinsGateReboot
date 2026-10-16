#pragma once

#include "sgre/text/cff_hint_geometry.hpp"

#include <cstdint>
#include <optional>

namespace sgre::text {

	struct CffContourJunctionSegments {
		CffFixedPoint PreviousStart;
		CffFixedPoint PreviousEnd;
		CffFixedPoint NextStart;
		CffFixedPoint NextEnd;
	};

	struct CffContourJunctionLimits {
		std::int32_t MaximumMidpointDistance16Dot16 = 0;
		std::int32_t AxisSnapTolerance16Dot16 = 0x199a;
	};

	struct CffContourJunction {
		// Nonparallel segments retain their candidate even when the midpoint-distance gate rejects it.
		std::optional<CffFixedPoint> Candidate;
		bool Accepted = false;
	};

	// Reproduces native reduced-vector intersection, axis snapping and bounded endpoint joining.
	[[nodiscard]] CffContourJunction FindCffContourJunction(const CffContourJunctionSegments& segments, const CffContourJunctionLimits& limits = {});

} // namespace sgre::text
