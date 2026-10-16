#include "sgre/text/cff_contour_owner.hpp"

#include <array>
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

static sgre::text::CffContourConfiguration Configuration(std::int32_t scale = 65536) {
	sgre::text::CffBlueZoneInput parameters;
	parameters.VerticalScale16Dot16 = scale;
	sgre::text::CffContourConfiguration configuration;
	configuration.BlueZones = sgre::text::BuildCffBlueZones(parameters);
	configuration.BaseScale16Dot16 = scale;

	return configuration;
}

static void CheckDeferredLineAndMove() {
	sgre::text::CffContourOwner owner(Configuration());
	Require(owner.State().Pending.Kind == sgre::text::CffPendingSegmentKind::None && !owner.State().CurrentMap.Initialized, "a new owner has no active segment or prepared current map");
	Require(owner.LineTo({}).CallbackCount == 0 && !owner.State().CurrentMap.Initialized, "an unchanged initial line must remain inert");
	Require(owner.MoveTo(Point(1, 2)).CallbackCount == 0 && owner.State().CurrentMap.Initialized && owner.State().FirstContourMap.Initialized, "moving initializes and snapshots the map without emitting a device callback");
	const auto first = owner.LineTo(Point(4, 2));
	Require(first.CallbackCount == 1 && first.Callbacks[0].Kind == sgre::text::CffContourCallbackKind::Move && first.Callbacks[0].Points[1].X16Dot16 == 65536 && first.Callbacks[0].Points[1].Y16Dot16 == 2 * 65536, "the first edge emits the contour start and retains its endpoint as pending geometry");
	Require(owner.State().HasPendingSegment && owner.State().Pending.CurrentDevicePoint.X16Dot16 == 65536, "a pending endpoint must not advance device state before flush");
	const auto second = owner.LineTo(Point(4, 5));
	Require(second.CallbackCount == 1 && second.Callbacks[0].Kind == sgre::text::CffContourCallbackKind::Line && second.Callbacks[0].Points[1].X16Dot16 == 4 * 65536 && second.Callbacks[0].Points[1].Y16Dot16 == 2 * 65536, "a following edge flushes the preceding endpoint before retaining the new edge");
	const auto moved = owner.MoveTo(Point(10, 10));
	Require(moved.CallbackCount == 2 && moved.Callbacks[0].Points[1].Y16Dot16 == 5 * 65536 && moved.Callbacks[1].Points[1].X16Dot16 == 65536 && moved.Callbacks[1].Points[1].Y16Dot16 == 2 * 65536, "a move closes the active contour before adopting its new logical point");
	Require(!owner.State().ContourOpen && !owner.State().HasPendingSegment && owner.State().FirstSegment && owner.State().CurrentLogicalPoint.X16Dot16 == 10 * 65536, "moving leaves a fresh logical contour and no pending segment");
	Require(owner.CloseContour().CallbackCount == 0, "closing an inactive contour must be inert");
}

static void CheckImplicitBeginAndCoincidentCubics() {
	sgre::text::CffContourOwner owner(Configuration());
	const auto implicit = owner.LineTo(Point(2, 3));
	Require(implicit.CallbackCount == 1 && implicit.Callbacks[0].Kind == sgre::text::CffContourCallbackKind::Move && owner.State().FirstContourMap.Initialized && owner.State().FirstLogicalPoint.X16Dot16 == 0, "a first edge without an explicit move prepares the zero logical contour start");
	sgre::text::CffContourOwner coincident(Configuration());
	Require(coincident.CubicTo({}, {}, {}).CallbackCount == 1, "a coincident first cubic still opens its contour");
	const auto repeated = coincident.CubicTo({}, {}, {});
	Require(repeated.CallbackCount == 1 && repeated.Callbacks[0].Kind == sgre::text::CffContourCallbackKind::Cubic, "coincident pending cubics preserve their callback instead of line-style suppression");
	const auto closed = coincident.CloseContour();
	Require(closed.CallbackCount == 1 && closed.Callbacks[0].Kind == sgre::text::CffContourCallbackKind::Cubic && !coincident.State().HasPendingSegment, "closing flushes the coincident cubic even when no closing line is needed");
}

static void CheckCubicWindingWithoutDarkening() {
	sgre::text::CffContourOwner owner(Configuration());
	const auto result = owner.CubicTo(Point(1, 2), Point(3, 4), Point(5, 6));
	Require(result.CallbackCount == 1 && owner.State().Configuration.Darkening.WindingArea == -2, "the middle cubic control edge contributes winding even when segment darkening is disabled");
	const auto closed = owner.CloseContour();
	Require(closed.CallbackCount == 2 && closed.Callbacks[0].Kind == sgre::text::CffContourCallbackKind::Cubic && owner.State().Configuration.Darkening.WindingArea == -2, "closure preserves the accumulated middle control winding");
	auto enabled_configuration = Configuration();
	enabled_configuration.Darkening.Enabled = true;
	sgre::text::CffContourOwner enabled(enabled_configuration);
	(void)enabled.CubicTo(Point(1, 2), Point(3, 4), Point(5, 6));
	Require(enabled.State().Configuration.Darkening.WindingArea == -4, "enabled segment winding and the unconditional middle edge must accumulate in native order");
}

static void CheckMaskChangeFlushAndClosingSnapshot() {
	std::array<sgre::text::CffStemHint, 1> stems{};
	stems[0].Start16Dot16 = 3 * 65536;
	stems[0].End16Dot16 = 8 * 65536;
	sgre::text::CffContourOwner owner(Configuration(4096), stems);
	(void)owner.MoveTo(Point(0, 3));
	(void)owner.LineTo(Point(1, 8));
	Require(owner.State().CurrentMap.EdgeCount == 2 && owner.State().FirstContourMap.EdgeCount == 2, "the first contour retains its active stem map snapshot");
	const std::array<std::uint8_t, 1> no_hints{0};
	Require(owner.UpdateHintMask(no_hints) == 1 && owner.State().Mask.New, "reading a primary mask marks the current mapping dirty");
	const auto changed = owner.LineTo(Point(1, 8));
	Require(changed.CallbackCount == 1 && changed.Callbacks[0].Points[1].Y16Dot16 == 20480, "an unchanged logical endpoint with a dirty mask must flush its predecessor through the old map");
	Require(owner.State().CurrentMap.EdgeCount == 0 && owner.State().FirstContourMap.EdgeCount == 2 && !owner.State().Mask.New, "map rebuilding must follow flush and preserve the first-contour snapshot");
	const auto closed = owner.CloseContour();
	Require(closed.CallbackCount == 2 && closed.Callbacks[0].Points[1].Y16Dot16 == 32768 && closed.Callbacks[1].Points[1].Y16Dot16 == 0, "closure uses the current map for the prior pending line and the saved map for the closing endpoint");
	Require(owner.State().Pending.CurrentDevicePoint.Y16Dot16 == 0 && !owner.State().ContourOpen && !owner.State().ClosingContour, "closure returns to the original device start and clears contour flags");
}

static void CheckDirtyMaskDuringClosure() {
	std::array<sgre::text::CffStemHint, 1> stems{};
	stems[0].Start16Dot16 = 3 * 65536;
	stems[0].End16Dot16 = 8 * 65536;
	sgre::text::CffContourOwner owner(Configuration(4096), stems);
	(void)owner.MoveTo(Point(0, 3));
	(void)owner.LineTo(Point(1, 8));
	const std::array<std::uint8_t, 1> no_hints{0};
	(void)owner.UpdateHintMask(no_hints);
	const auto closed = owner.CloseContour();
	Require(closed.CallbackCount == 2 && closed.Callbacks[0].Points[1].Y16Dot16 == 20480 && closed.Callbacks[1].Points[1].Y16Dot16 == 0, "a mask change during closure must not replace the current contour map");
	Require(owner.State().Mask.New && owner.State().CurrentMap.EdgeCount == 2, "the dirty mask must survive closure for the next logical contour");
	(void)owner.MoveTo(Point(10, 10));
	Require(!owner.State().Mask.New && owner.State().CurrentMap.EdgeCount == 0 && owner.State().FirstContourMap.EdgeCount == 0, "the following move consumes the dirty mask and snapshots its new map");
}

static void CheckCounterMaskOwnership() {
	std::array<sgre::text::CffStemHint, 1> stems{};
	stems[0].Start16Dot16 = 3 * 65536;
	stems[0].End16Dot16 = 8 * 65536;
	sgre::text::CffHintMask primary;
	primary.Valid = true;
	primary.New = false;
	primary.HintCount = 1;
	sgre::text::CffContourOwner owner(Configuration(4096), stems, 0, primary);
	(void)owner.MoveTo(Point(0, 3));
	(void)owner.LineTo(Point(1, 8));
	const std::array<std::uint8_t, 1> all_hints{0x80};
	const auto counter = owner.ApplyCounterMask(all_hints);
	Require(counter.Map.EdgeCount == 2 && owner.State().HorizontalStems.Stems[0].HasCachedDevicePositions, "a counter mask prepares a temporary map and updates glyph stem caches");
	Require(owner.State().CurrentMap.EdgeCount == 0 && owner.State().FirstContourMap.EdgeCount == 0 && owner.State().Mask.Bytes[0] == 0 && !owner.State().Mask.New, "counter processing must preserve the primary mask, current map, and first-contour snapshot");
	const auto flushed = owner.LineTo(Point(2, 8));
	Require(flushed.CallbackCount == 1 && flushed.Callbacks[0].Points[1].Y16Dot16 == 32768, "a counter mask must not replace the map used by existing pending geometry");
}

static void CheckOwnedInputsAndMalformedMasks() {
	std::array<sgre::text::CffStemHint, 1> stems{};
	stems[0].Start16Dot16 = 3 * 65536;
	stems[0].End16Dot16 = 8 * 65536;
	sgre::text::CffContourOwner owner(Configuration(4096), stems);
	stems[0].End16Dot16 = 20 * 65536;
	Require(owner.State().HorizontalStems.Stems[0].End16Dot16 == 8 * 65536, "prepared stems must be copied into glyph ownership");
	(void)owner.MoveTo({});
	const auto before = owner.State().Mask;
	bool failed = false;
	try {
		(void)owner.UpdateHintMask({});
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed && owner.State().Mask.Bytes == before.Bytes && owner.State().Mask.Valid == before.Valid && owner.State().Mask.New == before.New, "truncated primary mask input must fail before mutating the owned mask");
	failed = false;
	try {
		auto invalid = Configuration();
		invalid.JunctionLimits.AxisSnapTolerance16Dot16 = -1;
		sgre::text::CffContourOwner rejected(invalid);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed, "negative prepared geometry bounds must be rejected at owner construction");
	failed = false;
	try {
		sgre::text::CffHintMask invalid;
		invalid.Valid = true;
		invalid.HintCount = 2;
		sgre::text::CffContourOwner rejected(Configuration(), stems, 0, invalid);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed, "a valid mask must agree with the copied stem declaration count");
}

int main() {
	try {
		CheckDeferredLineAndMove();
		CheckImplicitBeginAndCoincidentCubics();
		CheckCubicWindingWithoutDarkening();
		CheckMaskChangeFlushAndClosingSnapshot();
		CheckDirtyMaskDuringClosure();
		CheckCounterMaskOwnership();
		CheckOwnedInputsAndMalformedMasks();
		std::cout << "CFF contour owner tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
