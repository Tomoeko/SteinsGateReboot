#include "sgre/text/cff_pending_contour.hpp"

#include <stdexcept>

namespace sgre::text {

	static bool SamePoint(CffFixedPoint first, CffFixedPoint second) {
		return first.X16Dot16 == second.X16Dot16 && first.Y16Dot16 == second.Y16Dot16;
	}

	static void AddLine(CffContourFlushResult& result, CffPendingContourSegment& pending, CffFixedPoint endpoint) {
		if (SamePoint(pending.CurrentDevicePoint, endpoint)) {
			return;
		}
		if (result.SegmentCount == result.Segments.size()) {
			throw std::runtime_error("CFF pending contour flush exceeds its callback segment limit");
		}

		auto& segment = result.Segments[result.SegmentCount++];
		segment.Kind = CffPendingSegmentKind::Line;
		segment.Points[0] = pending.CurrentDevicePoint;
		segment.Points[1] = endpoint;
		pending.CurrentDevicePoint = endpoint;
	}

	CffContourFlushResult FlushCffPendingContour(CffPendingContourSegment& pending, CffHintMap& current_map, CffHintMap& first_contour_map, const CffContourJoinRequest& request, const CffHintPointTransform& transform, const CffContourJunctionLimits& limits) {
		if (pending.Kind != CffPendingSegmentKind::Line && pending.Kind != CffPendingSegmentKind::Cubic) {
			throw std::runtime_error("CFF pending contour kind is unsupported");
		}
		if (limits.MaximumMidpointDistance16Dot16 < 0 || limits.AxisSnapTolerance16Dot16 < 0) {
			throw std::runtime_error("CFF pending contour junction limits must be nonnegative");
		}
		if (&current_map == &first_contour_map) {
			throw std::runtime_error("CFF pending contour current/first snapshot maps must have distinct glyph ownership");
		}

		CffContourFlushResult result;
		result.NextStart = request.NextStart;
		const auto end_index = pending.Kind == CffPendingSegmentKind::Line ? std::size_t(1) : std::size_t(3);
		if (!SamePoint(pending.LogicalPoints[end_index], request.NextStart)) {
			CffContourJunctionSegments segments;
			segments.PreviousStart = pending.LogicalPoints[end_index - 1];
			segments.PreviousEnd = pending.LogicalPoints[end_index];
			segments.NextStart = request.NextStart;
			segments.NextEnd = request.NextEnd;
			const auto junction = FindCffContourJunction(segments, limits);
			if (junction.Accepted && junction.Candidate) {
				result.JunctionAccepted = true;
				pending.LogicalPoints[end_index] = *junction.Candidate;
				result.NextStart = *junction.Candidate;
			}
		}

		if (pending.Kind == CffPendingSegmentKind::Line) {
			auto& map = request.ClosingContour ? first_contour_map : current_map;
			AddLine(result, pending, TransformCffHintedPoint(map, transform, pending.LogicalPoints[1]));
		} else {
			auto& segment = result.Segments[result.SegmentCount++];
			segment.Kind = CffPendingSegmentKind::Cubic;
			segment.Points[0] = pending.CurrentDevicePoint;
			for (std::size_t index = 1; index < segment.Points.size(); ++index) {
				segment.Points[index] = TransformCffHintedPoint(current_map, transform, pending.LogicalPoints[index]);
			}
			pending.CurrentDevicePoint = segment.Points[3];
		}

		// Closing uses the original requested endpoint and first-contour map before publishing any accepted junction.
		if (!result.JunctionAccepted || request.ClosingContour) {
			auto& map = request.ClosingContour ? first_contour_map : current_map;
			AddLine(result, pending, TransformCffHintedPoint(map, transform, request.NextStart));
		}

		return result;
	}

} // namespace sgre::text
