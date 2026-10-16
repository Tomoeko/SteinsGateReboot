#include "sgre/text/cff_pending_contour.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::text::CffFixedPoint Point(std::int32_t x, std::int32_t y) {
	sgre::text::CffFixedPoint point;
	point.X16Dot16 = x * 65536;
	point.Y16Dot16 = y * 65536;

	return point;
}

static void CheckLineSnapshotAndSuppression() {
	sgre::text::CffHintMap current;
	current.Enabled = false;
	sgre::text::CffHintMap first;
	first.Enabled = false;
	first.BaseScale16Dot16 = 32768;
	sgre::text::CffHintPointTransform transform;
	sgre::text::CffPendingContourSegment pending;
	pending.LogicalPoints[1] = Point(10, 10);
	sgre::text::CffContourJoinRequest request;
	request.NextStart = pending.LogicalPoints[1];
	request.NextEnd = Point(11, 10);
	request.ClosingContour = true;
	const auto closing = sgre::text::FlushCffPendingContour(pending, current, first, request, transform);
	Require(closing.SegmentCount == 1 && closing.Segments[0].Kind == sgre::text::CffPendingSegmentKind::Line && closing.Segments[0].Points[1].X16Dot16 == 10 * 65536 && closing.Segments[0].Points[1].Y16Dot16 == 5 * 65536, "a closing line must map its pending endpoint through the first-contour snapshot");
	const auto repeated = sgre::text::FlushCffPendingContour(pending, current, first, request, transform);
	Require(repeated.SegmentCount == 0, "identical mapped device endpoints must suppress line callbacks");
}

static void CheckCubicThenClosingConnection() {
	sgre::text::CffHintMap current;
	current.Enabled = false;
	sgre::text::CffHintMap first;
	first.Enabled = false;
	first.BaseScale16Dot16 = 32768;
	sgre::text::CffHintPointTransform transform;
	sgre::text::CffPendingContourSegment pending;
	pending.Kind = sgre::text::CffPendingSegmentKind::Cubic;
	pending.LogicalPoints[1] = Point(1, 2);
	pending.LogicalPoints[2] = Point(3, 4);
	pending.LogicalPoints[3] = Point(5, 6);
	sgre::text::CffContourJoinRequest request;
	request.NextStart = pending.LogicalPoints[3];
	request.NextEnd = Point(6, 6);
	request.ClosingContour = true;
	const auto result = sgre::text::FlushCffPendingContour(pending, current, first, request, transform);
	Require(result.SegmentCount == 2 && result.Segments[0].Kind == sgre::text::CffPendingSegmentKind::Cubic && result.Segments[0].Points[1].Y16Dot16 == 2 * 65536 && result.Segments[0].Points[3].Y16Dot16 == 6 * 65536, "closing cubic controls and endpoint must use the current map");
	Require(result.Segments[1].Kind == sgre::text::CffPendingSegmentKind::Line && result.Segments[1].Points[0].Y16Dot16 == 6 * 65536 && result.Segments[1].Points[1].Y16Dot16 == 3 * 65536 && pending.CurrentDevicePoint.Y16Dot16 == 3 * 65536, "the following closing connection must switch to the first-contour map and update device state");
	pending = {};
	pending.Kind = sgre::text::CffPendingSegmentKind::Cubic;
	request = {};
	const auto coincident = sgre::text::FlushCffPendingContour(pending, current, first, request, transform);
	Require(coincident.SegmentCount == 1 && coincident.Segments[0].Kind == sgre::text::CffPendingSegmentKind::Cubic, "a coincident cubic still emits its original callback");
}

static void CheckAcceptedClosingJunctionOrder() {
	sgre::text::CffHintMap current;
	current.Enabled = false;
	sgre::text::CffHintMap first;
	first.Enabled = false;
	first.BaseScale16Dot16 = 32768;
	sgre::text::CffHintPointTransform transform;
	sgre::text::CffPendingContourSegment pending;
	pending.LogicalPoints[1] = Point(10, 0);
	sgre::text::CffContourJoinRequest request;
	request.NextStart = Point(10, -1);
	request.NextStart.X16Dot16 += 123;
	request.NextEnd = Point(10, 1);
	request.NextEnd.X16Dot16 += 123;
	request.ClosingContour = true;
	sgre::text::CffContourJunctionLimits limits;
	limits.MaximumMidpointDistance16Dot16 = 2 * 65536;
	const auto result = sgre::text::FlushCffPendingContour(pending, current, first, request, transform, limits);
	Require(result.JunctionAccepted && result.SegmentCount == 2 && pending.LogicalPoints[1].Y16Dot16 == 0 && result.NextStart.Y16Dot16 == 0, "an accepted junction must update both logical endpoints");
	Require(result.Segments[1].Points[1].Y16Dot16 == -32768 && pending.CurrentDevicePoint.Y16Dot16 == -32768, "closing must map the original requested endpoint before publishing its accepted replacement");
	pending.CurrentDevicePoint = {};
	pending.LogicalPoints[1] = Point(10, 0);
	request.ClosingContour = false;
	const auto ordinary = sgre::text::FlushCffPendingContour(pending, current, first, request, transform, limits);
	Require(ordinary.JunctionAccepted && ordinary.SegmentCount == 1 && ordinary.NextStart.Y16Dot16 == 0 && pending.CurrentDevicePoint.Y16Dot16 == 0, "an ordinary accepted junction must suppress a separate connection");
}

static void CheckDistinctMapOwnership() {
	sgre::text::CffHintMap map;
	sgre::text::CffPendingContourSegment pending;
	sgre::text::CffContourJoinRequest request;
	sgre::text::CffHintPointTransform transform;
	bool failed = false;
	try {
		(void)sgre::text::FlushCffPendingContour(pending, map, map, request, transform);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed, "current and first-contour snapshots must have separate glyph ownership");
}

int main() {
	try {
		CheckLineSnapshotAndSuppression();
		CheckCubicThenClosingConnection();
		CheckAcceptedClosingJunctionOrder();
		CheckDistinctMapOwnership();
		std::cout << "CFF pending contour tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
