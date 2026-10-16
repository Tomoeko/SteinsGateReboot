#include "sgre/text/cff_contour_junction.hpp"

#include <bit>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::int32_t kFixedUnit = 65536;
	static constexpr std::int32_t kFixedHalf = 32768;
	static constexpr std::int32_t kVectorReduction = 32;
	static constexpr std::int32_t kVectorReductionHalf = 16;

	static std::int32_t AddFixed(std::int32_t first, std::int32_t second) {
		return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(first) + std::bit_cast<std::uint32_t>(second));
	}

	static std::int32_t SubtractFixed(std::int32_t first, std::int32_t second) {
		return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(first) - std::bit_cast<std::uint32_t>(second));
	}

	static std::int32_t MultiplyFixed(std::int32_t first, std::int32_t second) {
		const auto product = std::int64_t(first) * second;
		const auto magnitude = static_cast<std::uint64_t>(product < 0 ? -product : product);
		const auto rounded_bits = static_cast<std::uint32_t>((magnitude + kFixedHalf) / kFixedUnit);

		return std::bit_cast<std::int32_t>(product < 0 ? 0U - rounded_bits : rounded_bits);
	}

	static std::int32_t DivideFixed(std::int32_t value, std::int32_t divisor) {
		// FT_DivFix sign-extends its 32-bit magnitude before unsigned division and retains low quotient bits.
		const auto numerator_bits = value < 0 ? 0U - std::bit_cast<std::uint32_t>(value) : std::bit_cast<std::uint32_t>(value);
		const auto denominator_bits = divisor < 0 ? 0U - std::bit_cast<std::uint32_t>(divisor) : std::bit_cast<std::uint32_t>(divisor);
		const auto numerator = static_cast<std::uint64_t>(std::int64_t(std::bit_cast<std::int32_t>(numerator_bits))) << 16;
		const auto denominator = static_cast<std::uint64_t>(std::int64_t(std::bit_cast<std::int32_t>(denominator_bits)));
		const auto rounded_bits = static_cast<std::uint32_t>((numerator + (denominator >> 1)) / denominator);

		return std::bit_cast<std::int32_t>((value < 0) != (divisor < 0) ? 0U - rounded_bits : rounded_bits);
	}

	static std::int32_t AbsoluteFixed(std::int32_t value) {
		return value < 0 ? SubtractFixed(0, value) : value;
	}

	static std::int32_t ReducedCoordinate(std::int32_t value) {
		return value >= 0 ? value / kVectorReduction : static_cast<std::int32_t>(-((-std::int64_t(value) + kVectorReduction - 1) / kVectorReduction));
	}

	static CffFixedPoint ReduceVector(CffFixedPoint start, CffFixedPoint end) {
		CffFixedPoint result;
		result.X16Dot16 = ReducedCoordinate(AddFixed(SubtractFixed(end.X16Dot16, start.X16Dot16), kVectorReductionHalf));
		result.Y16Dot16 = ReducedCoordinate(AddFixed(SubtractFixed(end.Y16Dot16, start.Y16Dot16), kVectorReductionHalf));

		return result;
	}

	static std::int32_t CrossProduct(CffFixedPoint first, CffFixedPoint second) {
		return SubtractFixed(MultiplyFixed(first.X16Dot16, second.Y16Dot16), MultiplyFixed(first.Y16Dot16, second.X16Dot16));
	}

	static void SnapAlignedAxis(CffFixedPoint& point, CffFixedPoint start, CffFixedPoint end, std::int32_t tolerance) {
		if (start.X16Dot16 == end.X16Dot16 && AbsoluteFixed(SubtractFixed(point.X16Dot16, start.X16Dot16)) < tolerance) {
			point.X16Dot16 = start.X16Dot16;
		}
		if (start.Y16Dot16 == end.Y16Dot16 && AbsoluteFixed(SubtractFixed(point.Y16Dot16, start.Y16Dot16)) < tolerance) {
			point.Y16Dot16 = start.Y16Dot16;
		}
	}

	CffContourJunction FindCffContourJunction(const CffContourJunctionSegments& segments, const CffContourJunctionLimits& limits) {
		if (limits.MaximumMidpointDistance16Dot16 < 0 || limits.AxisSnapTolerance16Dot16 < 0) {
			throw std::runtime_error("CFF contour junction limits must be nonnegative fixed distances");
		}

		const auto previous_delta = ReduceVector(segments.PreviousStart, segments.PreviousEnd);
		const auto next_delta = ReduceVector(segments.NextStart, segments.NextEnd);
		const auto determinant = CrossProduct(previous_delta, next_delta);
		CffContourJunction result;
		if (determinant == 0) {
			return result;
		}

		const auto between_starts = ReduceVector(segments.PreviousStart, segments.NextStart);
		const auto parameter = DivideFixed(CrossProduct(between_starts, next_delta), determinant);
		CffFixedPoint point;
		point.X16Dot16 = AddFixed(segments.PreviousStart.X16Dot16, MultiplyFixed(parameter, SubtractFixed(segments.PreviousEnd.X16Dot16, segments.PreviousStart.X16Dot16)));
		point.Y16Dot16 = AddFixed(segments.PreviousStart.Y16Dot16, MultiplyFixed(parameter, SubtractFixed(segments.PreviousEnd.Y16Dot16, segments.PreviousStart.Y16Dot16)));
		SnapAlignedAxis(point, segments.PreviousStart, segments.PreviousEnd, limits.AxisSnapTolerance16Dot16);
		SnapAlignedAxis(point, segments.NextStart, segments.NextEnd, limits.AxisSnapTolerance16Dot16);
		result.Candidate = point;

		const auto midpoint_x = AddFixed(segments.PreviousEnd.X16Dot16, segments.NextStart.X16Dot16) / 2;
		const auto midpoint_y = AddFixed(segments.PreviousEnd.Y16Dot16, segments.NextStart.Y16Dot16) / 2;
		result.Accepted = AbsoluteFixed(SubtractFixed(point.X16Dot16, midpoint_x)) <= limits.MaximumMidpointDistance16Dot16 && AbsoluteFixed(SubtractFixed(point.Y16Dot16, midpoint_y)) <= limits.MaximumMidpointDistance16Dot16;

		return result;
	}

} // namespace sgre::text
