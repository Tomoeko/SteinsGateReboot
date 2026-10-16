#include "sgre/text/cff_hint_geometry.hpp"

#include <bit>

namespace sgre::text {

	static constexpr std::int32_t kFixedUnit = 65536;
	static constexpr std::int32_t kFixedHalf = 32768;
	static constexpr std::int32_t kPositiveDiagonalHorizontal = 0xb333;
	static constexpr std::int32_t kNegativeDiagonalHorizontal = -0xb332;
	static constexpr std::int32_t kHighDiagonalVertical = 0x1b333;
	static constexpr std::int32_t kLowDiagonalVertical = 0x4ccd;

	static std::int32_t MultiplyNativeFixed(std::int32_t value, std::int32_t factor) {
		const auto product = std::int64_t(value) * factor;
		const auto magnitude = static_cast<std::uint64_t>(product < 0 ? -product : product);
		const auto rounded_bits = static_cast<std::uint32_t>((magnitude + kFixedHalf) / kFixedUnit);

		return std::bit_cast<std::int32_t>(product < 0 ? 0U - rounded_bits : rounded_bits);
	}

	static std::int32_t AddNativeFixed(std::int32_t first, std::int32_t second) {
		return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(first) + std::bit_cast<std::uint32_t>(second));
	}

	static std::int32_t NegateNativeFixed(std::int32_t value) {
		return std::bit_cast<std::int32_t>(0U - std::bit_cast<std::uint32_t>(value));
	}

	static std::int32_t FloorFontUnits(std::int32_t value) {
		return value >= 0 ? value / kFixedUnit : static_cast<std::int32_t>(-((-std::int64_t(value) + kFixedUnit - 1) / kFixedUnit));
	}

	static void AccumulateWinding(CffContourDarkeningState& state, CffFixedPoint start, CffFixedPoint delta) {
		const auto horizontal_product = std::bit_cast<std::uint32_t>(FloorFontUnits(delta.Y16Dot16)) * std::bit_cast<std::uint32_t>(FloorFontUnits(start.X16Dot16));
		const auto vertical_product = std::bit_cast<std::uint32_t>(FloorFontUnits(delta.X16Dot16)) * std::bit_cast<std::uint32_t>(FloorFontUnits(start.Y16Dot16));
		state.WindingArea = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(state.WindingArea) + horizontal_product - vertical_product);
	}

	static CffFixedPoint DiagonalOffset(const CffContourDarkeningState& state, std::int32_t horizontal_factor, std::int32_t vertical_factor) {
		CffFixedPoint result;
		result.X16Dot16 = MultiplyNativeFixed(state.HorizontalDarkening16Dot16, horizontal_factor);
		result.Y16Dot16 = MultiplyNativeFixed(state.VerticalDarkening16Dot16, vertical_factor);

		return result;
	}

	CffFixedPoint ComputeCffContourDarkeningOffset(CffContourDarkeningState& state, CffFixedPoint start, CffFixedPoint end) {
		if (!state.Enabled) {
			return {};
		}

		CffFixedPoint delta;
		delta.X16Dot16 = AddNativeFixed(end.X16Dot16, NegateNativeFixed(start.X16Dot16));
		delta.Y16Dot16 = AddNativeFixed(end.Y16Dot16, NegateNativeFixed(start.Y16Dot16));
		AccumulateWinding(state, start, delta);
		const auto horizontal = state.ReverseWinding ? NegateNativeFixed(delta.X16Dot16) : delta.X16Dot16;
		const auto vertical = state.ReverseWinding ? NegateNativeFixed(delta.Y16Dot16) : delta.Y16Dot16;
		CffFixedPoint result;
		const auto twice_horizontal = AddNativeFixed(horizontal, horizontal);
		const auto twice_vertical = AddNativeFixed(vertical, vertical);
		if (horizontal < 0) {
			const auto horizontal_magnitude = NegateNativeFixed(horizontal);
			const auto vertical_magnitude = vertical < 0 ? NegateNativeFixed(vertical) : vertical;
			if (horizontal_magnitude > AddNativeFixed(vertical_magnitude, vertical_magnitude)) {
				result.Y16Dot16 = AddNativeFixed(state.VerticalDarkening16Dot16, state.VerticalDarkening16Dot16);
				return result;
			}
			if (vertical_magnitude <= AddNativeFixed(horizontal_magnitude, horizontal_magnitude)) {
				return DiagonalOffset(state, vertical < 0 ? kNegativeDiagonalHorizontal : kPositiveDiagonalHorizontal, kHighDiagonalVertical);
			}

			result.X16Dot16 = vertical < 0 ? NegateNativeFixed(state.HorizontalDarkening16Dot16) : state.HorizontalDarkening16Dot16;
			result.Y16Dot16 = state.VerticalDarkening16Dot16;

			return result;
		}

		const auto vertical_magnitude = vertical < 0 ? NegateNativeFixed(vertical) : vertical;
		const auto twice_vertical_magnitude = vertical < 0 ? AddNativeFixed(vertical_magnitude, vertical_magnitude) : twice_vertical;
		if (horizontal > twice_vertical_magnitude) {
			return result;
		}
		if (vertical_magnitude <= twice_horizontal) {
			return DiagonalOffset(state, vertical < 0 ? kNegativeDiagonalHorizontal : kPositiveDiagonalHorizontal, kLowDiagonalVertical);
		}

		result.X16Dot16 = vertical < 0 ? NegateNativeFixed(state.HorizontalDarkening16Dot16) : state.HorizontalDarkening16Dot16;
		result.Y16Dot16 = state.VerticalDarkening16Dot16;

		return result;
	}

	CffFixedPoint TransformCffHintedPoint(CffHintMap& vertical_map, const CffHintPointTransform& transform, CffFixedPoint character_point) {
		const auto horizontal = AddNativeFixed(MultiplyNativeFixed(character_point.X16Dot16, transform.HorizontalCharacterScale16Dot16), MultiplyNativeFixed(character_point.Y16Dot16, transform.VerticalCharacterShearIntoHorizontal16Dot16));
		const auto vertical = EvaluateCffHintMap(vertical_map, character_point.Y16Dot16);

		CffFixedPoint result;
		result.X16Dot16 = AddNativeFixed(AddNativeFixed(MultiplyNativeFixed(horizontal, transform.OutputXFromHorizontal16Dot16), MultiplyNativeFixed(vertical, transform.OutputXFromVertical16Dot16)), transform.DeviceTranslation.X16Dot16);
		result.Y16Dot16 = AddNativeFixed(AddNativeFixed(MultiplyNativeFixed(horizontal, transform.OutputYFromHorizontal16Dot16), MultiplyNativeFixed(vertical, transform.OutputYFromVertical16Dot16)), transform.DeviceTranslation.Y16Dot16);

		return result;
	}

} // namespace sgre::text
