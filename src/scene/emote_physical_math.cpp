#include "sgre/scene/emote_physical_math.hpp"

#include "sgre/scene/affine_atan2.hpp"
#include "sgre/scene/affine_trigonometry.hpp"

#include <bit>
#include <cmath>
#include <cstdint>

namespace sgre::scene {

	float PrepareEmotePhysicalSine(float radians) {
		if (!std::isfinite(radians)) {
			return std::sin(radians);
		}

		return PrepareAffineTrigonometry(radians).Sine;
	}

	float PrepareEmotePhysicalCosine(float radians) {
		if (!std::isfinite(radians)) {
			return std::cos(radians);
		}

		return PrepareAffineTrigonometry(radians).Cosine;
	}

	float PrepareEmotePhysicalAngle(float y, float x) {
		if (!std::isfinite(y) || !std::isfinite(x)) {
			return std::atan2(y, x);
		}

		return PrepareAffineAngle(y, x);
	}

	float PrepareEmotePhysicalArcCosine(float value) {
		if (!(value >= -1 && value <= 1)) {
			return std::acos(value);
		}

		// Native58c9d0: float rational approximation, with explicit double final reductions.
		const auto bits = std::bit_cast<std::uint32_t>(value);
		const auto exponent = (bits >> 23) & 0xffu;
		if (exponent < 0x65) {
			return std::bit_cast<float>(std::uint32_t{0x3fc90fdb});
		}
		if (exponent >= 0x7f) {
			return value == 1 ? 0.0f : std::bit_cast<float>(std::uint32_t{0x40490fdb});
		}

		const auto magnitude = std::abs(value);
		const float square = exponent < 0x7e ? magnitude * magnitude : (1.0f - magnitude) * 0.5f;
		const float root = exponent < 0x7e ? magnitude : std::sqrt(square);
		float numerator = -0.013381929136812687f - square * 0.003961374517530203f;
		const float denominator = 1.1049696207046509f - square * 0.8364112973213196f;
		numerator = numerator * square;
		numerator = numerator - 0.05652986839413643f;
		numerator = numerator * square;
		numerator = numerator + 0.18416160345077515f;
		numerator = numerator * square;
		const float correction = numerator / denominator;
		if (exponent < 0x7e) {
			const float scaled = correction * value;
			const double compensated = static_cast<double>(value) - (6.123233995736766e-17 - static_cast<double>(scaled));

			return static_cast<float>(1.5707963267948966 - compensated);
		}
		if ((bits >> 31) != 0) {
			const float scaled = root * correction;
			const double compensated = (static_cast<double>(scaled) - 6.123233995736766e-17) + static_cast<double>(root);

			return static_cast<float>(3.141592653589793 - (compensated + compensated));
		}

		const float scaled = (root + root) * correction;
		const float high = std::bit_cast<float>(std::bit_cast<std::uint32_t>(root) & 0xffff0000u);
		const float low = (square - high * high) / (high + root);

		return (scaled + (low + low)) + (high + high);
	}

} // namespace sgre::scene
