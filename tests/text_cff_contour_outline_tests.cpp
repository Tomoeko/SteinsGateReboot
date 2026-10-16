#include "sgre/text/cff_contour_outline.hpp"
#include "sgre/text/cff_glyph_path.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::text::CffFixedPoint Point(std::int32_t x, std::int32_t y) {
	sgre::text::CffFixedPoint point;
	point.X16Dot16 = x;
	point.Y16Dot16 = y;

	return point;
}

static void CheckFloorAndContourClosure() {
	std::array<sgre::text::CffContourCallback, 2> callbacks{};
	callbacks[0].Kind = sgre::text::CffContourCallbackKind::Cubic;
	callbacks[0].Points[0] = Point(-1, 1023);
	callbacks[0].Points[1] = Point(1024, -1024);
	callbacks[0].Points[2] = Point(-1025, 2049);
	callbacks[0].Points[3] = callbacks[0].Points[0];
	const auto outline = sgre::text::BuildCffContourOutline(callbacks);
	Require(outline.Points.size() == 3 && outline.ContourEnds.size() == 1 && outline.ContourEnds[0] == 2, "a closing cubic removes its repeated endpoint and retains both implicit-end controls");
	Require(outline.Points[0].X26Dot6 == -1 && outline.Points[0].Y26Dot6 == 0 && outline.Points[1].X26Dot6 == 1 && outline.Points[1].Y26Dot6 == -1 && outline.Points[2].X26Dot6 == -2 && outline.Points[2].Y26Dot6 == 2, "callback coordinates use signed floor conversion rather than host division truncation");
	Require(outline.Points[0].Kind == sgre::resource::CffPointKind::OnCurve && outline.Points[1].Kind == sgre::resource::CffPointKind::CubicControl && outline.Points[2].Kind == sgre::resource::CffPointKind::CubicControl, "native outline tags preserve paired cubic controls");
	const auto path = sgre::text::PrepareCffGlyphPath(outline.Points, outline.ContourEnds);
	Require(path.Commands.size() == 2 && path.Commands[1].Kind == sgre::text::CffGlyphPathCommandKind::CubicTo && path.Commands[1].Points[2].X == -1.0 / 64 && path.Commands[1].Points[2].Y == 0, "the geometry-only path boundary must preserve a final cubic's implicit endpoint");
}

static void CheckMoveAndSinglePointContours() {
	std::array<sgre::text::CffContourCallback, 4> callbacks{};
	callbacks[0].Points[1] = Point(100, 200);
	callbacks[1].Kind = sgre::text::CffContourCallbackKind::Line;
	callbacks[1].Points[0] = Point(1024, 2048);
	callbacks[1].Points[1] = callbacks[1].Points[0];
	callbacks[2].Points[1] = Point(300, 400);
	callbacks[3].Kind = sgre::text::CffContourCallbackKind::Line;
	callbacks[3].Points[0] = Point(10 * 1024, 20 * 1024);
	callbacks[3].Points[1] = Point(30 * 1024, 40 * 1024);
	const auto outline = sgre::text::BuildCffContourOutline(callbacks);
	Require(outline.Points.size() == 2 && outline.ContourEnds.size() == 1 && outline.ContourEnds[0] == 1, "moving finalizes and discards a single-point contour before starting the next builder contour");
	Require(outline.Points[0].X26Dot6 == 10 && outline.Points[0].Y26Dot6 == 20 && outline.Points[1].X26Dot6 == 30 && outline.Points[1].Y26Dot6 == 40, "move callbacks allocate no points; a first segment supplies its actual device start");
	const std::array<sgre::text::CffContourCallback, 1> move_only{};
	Require(sgre::text::BuildCffContourOutline(move_only).Points.empty(), "a move-only contour must remain empty");
}

static void CheckLineTagsAndBounds() {
	std::array<sgre::text::CffContourCallback, 3> callbacks{};
	for (auto& callback : callbacks) {
		callback.Kind = sgre::text::CffContourCallbackKind::Line;
	}
	callbacks[0].Points[0] = Point(0, 0);
	callbacks[0].Points[1] = Point(65536, 0);
	callbacks[1].Points[0] = callbacks[0].Points[1];
	callbacks[1].Points[1] = Point(65536, 65536);
	callbacks[2].Points[0] = callbacks[1].Points[1];
	callbacks[2].Points[1] = callbacks[0].Points[0];
	const auto outline = sgre::text::BuildCffContourOutline(callbacks);
	Require(outline.Points.size() == 3 && outline.ContourEnds[0] == 2, "repeated closing line endpoints must normalize to the preceding point");
	const auto path = sgre::text::PrepareCffGlyphPath(outline.Points, outline.ContourEnds);
	Require(path.Commands.size() == 4 && path.Bounds.Left == 0 && path.Bounds.Top == -1 && path.Bounds.Right == 1 && path.Bounds.Bottom == 0, "the native callback outline must feed the recovered downward-positive pixel path without rescaling");
	const auto bitmap = sgre::text::PlanCffGlyphBitmap(path.Bounds);
	Require(bitmap && bitmap->WidthPixels == 1 && bitmap->HeightPixels == 1 && bitmap->TopPixel == -1, "hinted device paths must feed native bitmap extent planning directly");
}

static void CheckMalformedAndExcessiveCallbacks() {
	std::array<sgre::text::CffContourCallback, 1> invalid{};
	invalid[0].Kind = static_cast<sgre::text::CffContourCallbackKind>(3);
	bool failed = false;
	try {
		(void)sgre::text::BuildCffContourOutline(invalid);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed, "unknown callback kinds must fail before producing an outline");
	std::vector<sgre::text::CffContourCallback> excessive(32767);
	for (auto& callback : excessive) {
		callback.Kind = sgre::text::CffContourCallbackKind::Line;
	}
	failed = false;
	try {
		(void)sgre::text::BuildCffContourOutline(excessive);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed, "point storage must remain within the native signed 16-bit limit");
	excessive.resize(65537);
	failed = false;
	try {
		(void)sgre::text::BuildCffContourOutline(excessive);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed, "callback preparation must keep a bounded work count");
}

int main() {
	try {
		CheckFloorAndContourClosure();
		CheckMoveAndSinglePointContours();
		CheckLineTagsAndBounds();
		CheckMalformedAndExcessiveCallbacks();
		std::cout << "CFF contour outline tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
