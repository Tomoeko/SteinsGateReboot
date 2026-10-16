#include "sgre/scene/emote_easing.hpp"

#include "sgre/scene/affine_trigonometry.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {

	static constexpr float kPi = 3.1415927410125732421875f;
	static constexpr float kBackSmall = 1.701580047607421875f;
	static constexpr float kBackLarge = 2.701580047607421875f;

	static float PolynomialIn(float ratio, std::uint32_t degree) {
		auto result = ratio;
		for (std::uint32_t index = 1; index < degree; ++index) {
			result *= ratio;
		}

		return result;
	}

	static float PolynomialEasing(float ratio, std::uint32_t degree, std::uint32_t mode) {
		const auto exponent = static_cast<float>(degree);
		if (mode == 0) {
			return PolynomialIn(ratio, degree);
		}
		if (mode == 1) {
			return 1.0f - std::pow(1.0f - ratio, exponent);
		}
		if (mode == 2) {
			if (ratio < .5f) {
				const auto factor = static_cast<float>(std::uint32_t{1} << (degree - 1));
				auto result = ratio * factor;
				for (std::uint32_t index = 1; index < degree; ++index) {
					result *= ratio;
				}

				return result;
			}

			return 1.0f - std::pow(2.0f - (ratio + ratio), exponent) * .5f;
		}
		if (degree == 2) {
			if (ratio < .5f) {
				const auto remaining = 1.0f - (ratio + ratio);
				return (1.0f - remaining * remaining) * .5f;
			}

			const auto shifted = ratio - .5f;
			return (shifted + shifted) * shifted + .5f;
		}
		if (ratio < .5f) {
			return (1.0f - std::pow(1.0f - (ratio + ratio), exponent)) * .5f;
		}

		return (std::pow((ratio + ratio) - 1.0f, exponent) + 1.0f) * .5f;
	}

	static float SineEasing(float ratio, std::uint32_t mode) {
		if (mode == 0) {
			return 1.0f - PrepareAffineTrigonometry((ratio * kPi) * .5f).Cosine;
		}
		if (mode == 1) {
			return PrepareAffineTrigonometry((ratio * kPi) * .5f).Sine;
		}
		if (mode == 2) {
			return -(PrepareAffineTrigonometry(ratio * kPi).Cosine - 1.0f) * .5f;
		}
		if (ratio < .5f) {
			return PrepareAffineTrigonometry(((ratio + ratio) * kPi) * .5f).Sine * .5f;
		}

		return (1.0f - PrepareAffineTrigonometry((((ratio + ratio) - 1.0f) * kPi) * .5f).Cosine) * .5f + .5f;
	}

	static float ExponentialEasing(float ratio, std::uint32_t mode) {
		if (mode == 0) {
			return std::pow(2.0f, ratio * 10.0f - 10.0f);
		}
		if (mode == 1) {
			return 1.0f - std::pow(2.0f, ratio * -10.0f);
		}
		if (mode == 2) {
			if (ratio < .5f) {
				return std::pow(2.0f, ratio * 20.0f - 10.0f) * .5f;
			}

			return (2.0f - std::pow(2.0f, 10.0f - ratio * 20.0f)) * .5f;
		}
		if (ratio < .5f) {
			return (1.0f - std::pow(2.0f, ratio * -20.0f)) * .5f;
		}

		// The original right half is discontinuous at .5; it is not the symmetric generic curve.
		return std::pow(2.0f, (ratio - .5f) * 20.0f - 10.0f) * .5f + .5f;
	}

	static float CircularEasing(float ratio, std::uint32_t mode) {
		if (mode == 0) {
			return 1.0f - std::sqrt(1.0f - std::pow(ratio, 2.0f));
		}
		if (mode == 1) {
			return std::sqrt(1.0f - std::pow(ratio - 1.0f, 2.0f));
		}
		if (mode == 2) {
			if (ratio < .5f) {
				return (1.0f - std::sqrt(1.0f - std::pow(ratio + ratio, 2.0f))) * .5f;
			}

			return (std::sqrt(1.0f - std::pow(2.0f - (ratio + ratio), 2.0f)) + 1.0f) * .5f;
		}
		if (ratio < .5f) {
			return std::sqrt(1.0f - std::pow(1.0f - (ratio + ratio), 2.0f)) * .5f;
		}

		return (1.0f - std::sqrt(1.0f - std::pow((ratio + ratio) - 1.0f, 2.0f))) * .5f + .5f;
	}

	static float BackEasing(float ratio, std::uint32_t mode) {
		if (mode == 0) {
			return ((ratio * kBackLarge) * ratio) * ratio - (ratio * kBackSmall) * ratio;
		}
		if (mode == 1) {
			const auto shifted = ratio - 1.0f;
			const auto cubic = std::pow(shifted, 3.0f) * kBackLarge + 1.0f;
			return cubic + std::pow(shifted, 2.0f) * kBackSmall;
		}
		if (mode == 2) {
			if (ratio < .5f) {
				return (std::pow(ratio + ratio, 2.0f) * (ratio * 7.189818859100341796875f - 2.5949094295501708984375f)) * .5f;
			}

			const auto shifted = (ratio + ratio) - 2.0f;
			return (std::pow(shifted, 2.0f) * (shifted * 3.5949094295501708984375f + 2.5949094295501708984375f) + 2.0f) * .5f;
		}

		const auto shifted = (ratio + ratio) - 1.0f;
		const auto square = std::pow(shifted, 2.0f) * kBackSmall;
		const auto cubic = std::pow(shifted, 3.0f) * kBackLarge;
		if (ratio < .5f) {
			return (cubic + 1.0f + square) * .5f;
		}

		return (cubic - square) * .5f + .5f;
	}

	static float ElasticEasing(float ratio, std::uint32_t mode) {
		if (mode == 0) {
			const auto position = ratio * 10.0f;
			const auto amplitude = -std::pow(2.0f, position - 10.0f);
			const auto sine = PrepareAffineTrigonometry((position - 10.75f) * 2.094395160675048828125f).Sine;
			return amplitude * sine;
		}
		if (mode == 1) {
			const auto sine = PrepareAffineTrigonometry((ratio * 10.0f - .75f) * 2.094395160675048828125f).Sine;
			const auto amplitude = std::pow(2.0f, ratio * -10.0f);
			return sine * amplitude + 1.0f;
		}
		if (mode == 2) {
			const auto position = ratio * 20.0f;
			const auto sine = PrepareAffineTrigonometry((position - 11.125f) * 1.39626348018646240234375f).Sine;
			if (ratio < .5f) {
				return -(std::pow(2.0f, position - 10.0f) * sine) * .5f;
			}

			return std::pow(2.0f, 10.0f - position) * sine * .5f + 1.0f;
		}
		if (ratio < .5f) {
			const auto sine = PrepareAffineTrigonometry(((ratio + ratio) * 10.0f - .75f) * 2.094395160675048828125f).Sine;
			const auto amplitude = std::pow(2.0f, ratio * -20.0f);
			return sine * amplitude * .5f + .5f;
		}

		const auto position = ((ratio + ratio) - 1.0f) * 10.0f;
		const auto sine = PrepareAffineTrigonometry((position - 10.75f) * 2.094395160675048828125f).Sine;
		const auto amplitude = std::pow(2.0f, position - 10.0f);
		return .5f - sine * amplitude * .5f;
	}

	static float BounceOut(float ratio) {
		if (ratio < .36363637447357177734375f) {
			return (ratio * 7.5625f) * ratio;
		}
		if (ratio < .7272727489471435546875f) {
			const auto shifted = ratio - .545454561710357666015625f;
			return (shifted * 7.5625f) * shifted + .75f;
		}
		// The third comparison promotes its operand to double in the executable.
		if (static_cast<double>(ratio) < .9090909090909091) {
			const auto shifted = ratio - .818181812763214111328125f;
			return (shifted * 7.5625f) * shifted + .9375f;
		}

		const auto shifted = ratio - .954545438289642333984375f;
		return (shifted * 7.5625f) * shifted + .984375f;
	}

	static float BounceEasing(float ratio, std::uint32_t mode) {
		if (mode == 0) {
			return 1.0f - BounceOut(1.0f - ratio);
		}
		if (mode == 1) {
			return BounceOut(ratio);
		}
		if (mode == 2) {
			if (ratio < .5f) {
				return (1.0f - BounceOut(1.0f - (ratio + ratio))) * .5f;
			}

			return (BounceOut((ratio + ratio) - 1.0f) + 1.0f) * .5f;
		}
		if (ratio < .5f) {
			return BounceOut(ratio + ratio) * .5f;
		}

		return (1.0f - BounceOut((1.0f - ratio) + (1.0f - ratio))) * .5f + .5f;
	}

	float PrepareEmoteEasing(float easing, float ratio) {
		if (!std::isfinite(easing) || !std::isfinite(ratio)) {
			throw std::runtime_error("Emote easing requires finite control and ratio");
		}
		if (!(ratio > 0)) {
			return 0;
		}
		if (!(ratio < 1)) {
			return 1;
		}
		if (easing == 0) {
			return ratio;
		}
		if (easing < 100) {
			if (easing > 0) {
				return std::pow(ratio, easing + 1.0f);
			}

			return std::pow(ratio, 1.0f / (1.0f - easing));
		}
		if (easing < 101 || easing >= 141) {
			return ratio;
		}

		const auto index = static_cast<std::uint32_t>(easing) - 101;
		const auto mode = index % 4;
		if (index < 16) {
			return PolynomialEasing(ratio, index / 4 + 2, mode);
		}
		switch (index / 4) {
		case 4:
			return SineEasing(ratio, mode);
		case 5:
			return ExponentialEasing(ratio, mode);
		case 6:
			return CircularEasing(ratio, mode);
		case 7:
			return BackEasing(ratio, mode);
		case 8:
			return ElasticEasing(ratio, mode);
		case 9:
			return BounceEasing(ratio, mode);
		default:
			throw std::runtime_error("Emote easing dispatch exceeds its proven named table");
		}
	}

} // namespace sgre::scene
