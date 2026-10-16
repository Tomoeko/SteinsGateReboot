#pragma once

#include "sgre/text/cff_contour_junction.hpp"
#include "sgre/text/cff_hint_geometry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace sgre::text {

	enum class CffPendingSegmentKind : std::uint8_t {
		None = 0,
		Line = 2,
		Cubic = 4
	};

	struct CffPendingContourSegment {
		CffPendingSegmentKind Kind = CffPendingSegmentKind::Line;
		// A line uses start/end in slots 0/1; a cubic uses start/control1/control2/end in slots 0..3.
		std::array<CffFixedPoint, 4> LogicalPoints{};
		CffFixedPoint CurrentDevicePoint;
	};

	struct CffContourJoinRequest {
		CffFixedPoint NextStart;
		CffFixedPoint NextEnd;
		bool ClosingContour = false;
	};

	struct CffContourDeviceSegment {
		CffPendingSegmentKind Kind = CffPendingSegmentKind::Line;
		// Device-space callback arguments have the same line/cubic slot convention.
		std::array<CffFixedPoint, 4> Points{};
	};

	struct CffContourFlushResult {
		std::array<CffContourDeviceSegment, 2> Segments{};
		std::size_t SegmentCount = 0;
		CffFixedPoint NextStart;
		bool JunctionAccepted = false;
	};

	// The two maps borrow distinct glyph-owned current/first-contour snapshot state.
	// Flushes prepared pending geometry; dirty-mask rebuilding follows this operation in the caller.
	[[nodiscard]] CffContourFlushResult FlushCffPendingContour(CffPendingContourSegment& pending, CffHintMap& current_map, CffHintMap& first_contour_map, const CffContourJoinRequest& request, const CffHintPointTransform& transform, const CffContourJunctionLimits& limits = {});

} // namespace sgre::text
