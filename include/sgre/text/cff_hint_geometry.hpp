#pragma once

#include "sgre/text/cff_hint_map.hpp"

#include <cstdint>

namespace sgre::text {

	struct CffFixedPoint {
		std::int32_t X16Dot16 = 0;
		std::int32_t Y16Dot16 = 0;
	};

	struct CffHintPointTransform {
		std::int32_t HorizontalCharacterScale16Dot16 = 65536;
		std::int32_t VerticalCharacterShearIntoHorizontal16Dot16 = 0;
		std::int32_t OutputXFromHorizontal16Dot16 = 65536;
		std::int32_t OutputYFromHorizontal16Dot16 = 0;
		std::int32_t OutputXFromVertical16Dot16 = 0;
		std::int32_t OutputYFromVertical16Dot16 = 65536;
		CffFixedPoint DeviceTranslation;
	};

	struct CffContourDarkeningState {
		std::int32_t HorizontalDarkening16Dot16 = 0;
		std::int32_t VerticalDarkening16Dot16 = 0;
		std::int32_t WindingArea = 0;
		bool Enabled = false;
		bool ReverseWinding = false;
	};

	// Computes a segment's logical offset and accumulates native integer-unit winding when enabled.
	[[nodiscard]] CffFixedPoint ComputeCffContourDarkeningOffset(CffContourDarkeningState& state, CffFixedPoint start, CffFixedPoint end);

	// Preserves separate native fixed multiply/round/wrap stages and updates the vertical map's cursor.
	// Pending-contour map selection and conversion to callback outline units are separate phases.
	[[nodiscard]] CffFixedPoint TransformCffHintedPoint(CffHintMap& vertical_map, const CffHintPointTransform& transform, CffFixedPoint character_point);

} // namespace sgre::text
