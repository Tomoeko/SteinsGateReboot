#include "sgre/scene/affine_trigonometry.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::uint32_t kQuarterPiFloatBits = 0x3f490fdb;
	static constexpr std::uint32_t kFastReductionBoundBits = 0x48f42400;
	static constexpr double kTwoOverPi = 0.63661977236758138;
	static constexpr double kHalfPiHigh = 1.5707963267341256;
	static constexpr double kHalfPiTail = 6.0771005065061922e-11;
	static constexpr double kHalfPiTailHigh = 6.077100506303966e-11;
	static constexpr double kHalfPiTailLow = 2.0222662487959506e-21;
	static constexpr double kOneSixth = 0.16666666666666666;

	struct ReducedAffineAngle {
		double Value = 0;
		std::uint32_t Quadrant = 0;
		std::uint32_t InitialExponent = 0;
	};

	static std::uint32_t DoubleExponent(double value) noexcept {
		return static_cast<std::uint32_t>((std::bit_cast<std::uint64_t>(value) >> 52) & 0x7ffu);
	}

	static ReducedAffineAngle ReduceAngle(double magnitude) {
		ReducedAffineAngle reduced;
		reduced.Quadrant = static_cast<std::uint32_t>(magnitude * kTwoOverPi + 0.5);
		const auto turns = static_cast<double>(reduced.Quadrant);
		const auto first = magnitude - kHalfPiHigh * turns;
		reduced.Value = first - kHalfPiTail * turns;
		reduced.InitialExponent = DoubleExponent(reduced.Value);
		if (DoubleExponent(magnitude) > reduced.InitialExponent + 15) {
			const auto tail = kHalfPiTailHigh * turns;
			const auto second = first - tail;
			const auto compensation = (first - second) - tail;
			reduced.Value = second - (kHalfPiTailLow * turns - compensation);
		}

		return reduced;
	}

	static double SinePolynomial(double value, bool multiply_cube_first) {
		const auto square = value * value;
		const auto fourth = square * square;
		const auto high_terms = (2.7557319223985893e-06 * square - 0.00019841269841269841) * fourth;
		const auto coefficient = (high_terms + 0.0083333333333333332 * square) - kOneSixth;
		if (multiply_cube_first) {
			return value + coefficient * (value * square);
		}

		return value + (coefficient * value) * square;
	}

	static double CosinePolynomial(double value) {
		const auto square = value * value;
		const auto fourth = square * square;
		const auto low_terms = -0.0013888888888888887 * square + 0.041666666666666664;
		const auto high_terms = (-2.7557319223985888e-07 * square + 2.4801587301587298e-05) * fourth;
		const auto leading = 1.0 - 0.5 * square;

		return leading + (high_terms + low_terms) * fourth;
	}

	static double ReducedSine(const ReducedAffineAngle& reduced, bool cosine_branch) {
		if (reduced.InitialExponent >= 0x3f2) {
			return SinePolynomial(reduced.Value, !cosine_branch);
		}
		if (reduced.InitialExponent > 0x3de) {
			const auto square = reduced.Value * reduced.Value;

			return reduced.Value - (kOneSixth * reduced.Value) * square;
		}

		return reduced.Value;
	}

	static double ReducedCosine(const ReducedAffineAngle& reduced) {
		if (reduced.InitialExponent >= 0x3f2) {
			return CosinePolynomial(reduced.Value);
		}
		if (reduced.InitialExponent > 0x3de) {
			return 1.0 - (reduced.Value * reduced.Value) * 0.5;
		}

		return 1;
	}

	static float SmallSine(float input, std::uint32_t magnitude_bits) {
		const auto value = static_cast<double>(input);
		if (magnitude_bits >= 0x3c000000) {
			return static_cast<float>(SinePolynomial(value, true));
		}
		if (magnitude_bits >= 0x39000000) {
			const auto third = (value * value) * value;

			return static_cast<float>(value - third * kOneSixth);
		}

		return input;
	}

	AffineSinCos PrepareAffineTrigonometry(float radians) {
		const auto bits = std::bit_cast<std::uint32_t>(radians);
		const auto magnitude_bits = bits & 0x7fffffffu;
		if (magnitude_bits >= kFastReductionBoundBits) {
			throw std::runtime_error("affine trigonometry large/nonfinite reduction parity is not recovered");
		}

		AffineSinCos result;
		if (magnitude_bits <= kQuarterPiFloatBits) {
			result.Sine = SmallSine(radians, magnitude_bits);
			result.Cosine = static_cast<float>(CosinePolynomial(static_cast<double>(radians)));

			return result;
		}

		const auto reduced = ReduceAngle(std::abs(static_cast<double>(radians)));
		const bool odd_quadrant = (reduced.Quadrant & 1u) != 0;
		auto sine = odd_quadrant ? ReducedCosine(reduced) : ReducedSine(reduced, false);
		auto cosine = odd_quadrant ? ReducedSine(reduced, true) : ReducedCosine(reduced);
		if (((reduced.Quadrant >> 1) & 1u) != (bits >> 31)) {
			sine = 0.0 - sine;
		}
		if (((reduced.Quadrant + 1) & 2u) != 0) {
			cosine = 0.0 - cosine;
		}
		result.Sine = static_cast<float>(sine);
		result.Cosine = static_cast<float>(cosine);

		return result;
	}

} // namespace sgre::scene
