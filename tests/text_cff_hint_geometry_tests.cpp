#include "sgre/text/cff_hint_geometry.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void CheckNativeRoundingOrder() {
	sgre::text::CffHintMap map;
	map.Enabled = false;
	map.BaseScale16Dot16 = 32768;
	sgre::text::CffHintPointTransform transform;
	transform.HorizontalCharacterScale16Dot16 = 32768;
	transform.VerticalCharacterShearIntoHorizontal16Dot16 = 32768;
	sgre::text::CffFixedPoint input;
	input.X16Dot16 = 1;
	input.Y16Dot16 = 1;
	auto result = sgre::text::TransformCffHintedPoint(map, transform, input);
	Require(result.X16Dot16 == 2 && result.Y16Dot16 == 1, "the first affine stage must round separate products before adding them");
	transform.OutputXFromHorizontal16Dot16 = 32768;
	transform.OutputXFromVertical16Dot16 = 32768;
	result = sgre::text::TransformCffHintedPoint(map, transform, input);
	Require(result.X16Dot16 == 2, "the output stage must retain another pair of separate fixed-round operations");
	input.X16Dot16 = -1;
	input.Y16Dot16 = -1;
	result = sgre::text::TransformCffHintedPoint(map, transform, input);
	Require(result.X16Dot16 == -2 && result.Y16Dot16 == -1, "negative intermediate halves retain native away-from-zero rounding");
}

static void CheckMapCursorAndTranslation() {
	sgre::text::CffHintMap map;
	map.EdgeCount = 2;
	map.Edges[0].Flags = sgre::text::CffHintEdgeFlags::LowerGhost;
	map.Edges[0].CharacterPosition16Dot16 = 0;
	map.Edges[0].DevicePosition16Dot16 = 65536;
	map.Edges[0].SegmentScale16Dot16 = 32768;
	map.Edges[1].Flags = sgre::text::CffHintEdgeFlags::UpperGhost;
	map.Edges[1].CharacterPosition16Dot16 = 65536;
	map.Edges[1].DevicePosition16Dot16 = 98304;
	map.Edges[1].SegmentScale16Dot16 = 65536;
	sgre::text::CffHintPointTransform transform;
	transform.OutputXFromVertical16Dot16 = 65536;
	transform.DeviceTranslation.X16Dot16 = 3 * 65536;
	transform.DeviceTranslation.Y16Dot16 = -2 * 65536;
	sgre::text::CffFixedPoint input;
	input.X16Dot16 = 2 * 65536;
	input.Y16Dot16 = 2 * 65536;
	const auto result = sgre::text::TransformCffHintedPoint(map, transform, input);
	Require(map.Cursor == 1 && result.X16Dot16 == 491520 && result.Y16Dot16 == 32768, "hinted vertical mapping must precede affine output and device translation");
	input.Y16Dot16 = 0;
	(void)sgre::text::TransformCffHintedPoint(map, transform, input);
	Require(map.Cursor == 0, "backward point evaluation must update the borrowed map cursor");
}

static void CheckExplicitNativeWrapping() {
	sgre::text::CffHintMap map;
	map.Enabled = false;
	sgre::text::CffHintPointTransform transform;
	transform.DeviceTranslation.X16Dot16 = std::numeric_limits<std::int32_t>::max();
	sgre::text::CffFixedPoint input;
	input.X16Dot16 = 1;
	auto result = sgre::text::TransformCffHintedPoint(map, transform, input);
	Require(result.X16Dot16 == std::numeric_limits<std::int32_t>::min(), "device-coordinate addition must preserve native low32 wrapping");
	transform.DeviceTranslation.X16Dot16 = 0;
	transform.HorizontalCharacterScale16Dot16 = std::numeric_limits<std::int32_t>::max();
	input.X16Dot16 = std::numeric_limits<std::int32_t>::min();
	result = sgre::text::TransformCffHintedPoint(map, transform, input);
	Require(result.X16Dot16 == 32768, "large fixed products must preserve the native narrowed rounded bits");
}

static void CheckDirectionalDarkeningAndWinding() {
	sgre::text::CffContourDarkeningState state;
	state.Enabled = true;
	state.HorizontalDarkening16Dot16 = 65536;
	state.VerticalDarkening16Dot16 = 65536;
	sgre::text::CffFixedPoint start;
	sgre::text::CffFixedPoint end;
	end.X16Dot16 = 65536;
	end.Y16Dot16 = 65536;
	auto result = sgre::text::ComputeCffContourDarkeningOffset(state, start, end);
	Require(result.X16Dot16 == 45875 && result.Y16Dot16 == 19661, "positive diagonal darkening must retain the native fixed coefficients");
	end.X16Dot16 = -65536;
	end.Y16Dot16 = -65536;
	result = sgre::text::ComputeCffContourDarkeningOffset(state, start, end);
	Require(result.X16Dot16 == -45874 && result.Y16Dot16 == 111411, "negative diagonal darkening must preserve the asymmetric fixed coefficient");
	end.X16Dot16 = -3 * 65536;
	end.Y16Dot16 = 65536;
	result = sgre::text::ComputeCffContourDarkeningOffset(state, start, end);
	Require(result.X16Dot16 == 0 && result.Y16Dot16 == 2 * 65536, "a left segment above the two-to-one slope threshold must use twice vertical darkening");
	state.ReverseWinding = true;
	result = sgre::text::ComputeCffContourDarkeningOffset(state, start, end);
	Require(result.X16Dot16 == 0 && result.Y16Dot16 == 0, "reversed winding must select the opposite side of a nearly horizontal segment");
	start.X16Dot16 = 2 * 65536;
	start.Y16Dot16 = -1;
	end.X16Dot16 = 4 * 65536;
	end.Y16Dot16 = 65535;
	state.WindingArea = std::numeric_limits<std::int32_t>::max();
	(void)sgre::text::ComputeCffContourDarkeningOffset(state, start, end);
	Require(state.WindingArea == -2147483645, "integer-unit winding must floor negative coordinates and accumulate with native32 wrapping independently of orientation");
	state.Enabled = false;
	state.WindingArea = 7;
	result = sgre::text::ComputeCffContourDarkeningOffset(state, start, end);
	Require(result.X16Dot16 == 0 && result.Y16Dot16 == 0 && state.WindingArea == 7, "disabled darkening must leave the winding owner untouched");
}

int main() {
	try {
		CheckNativeRoundingOrder();
		CheckMapCursorAndTranslation();
		CheckExplicitNativeWrapping();
		CheckDirectionalDarkeningAndWinding();
		std::cout << "CFF hint point geometry tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
