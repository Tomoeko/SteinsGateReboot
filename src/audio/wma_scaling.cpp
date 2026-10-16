#include "sgre/audio/wma_scaling.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace sgre::audio {

	// Native Standard 0x1800977f0 stores exact float multipliers, with a power-of-two divisor for each four exponent steps.
	static constexpr std::array<float, 72> kExponentMultipliers = {
	    0x1.0000000000000p+0f,
	    0x1.bb5fac0000000p-1f,
	    0x1.7ff2220000000p-1f,
	    0x1.4c7bc00000000p-1f,
	    0x1.1feb340000000p+0f,
	    0x1.f2a79e0000000p-1f,
	    0x1.afd1360000000p-1f,
	    0x1.75f0340000000p-1f,
	    0x1.43d1360000000p+0f,
	    0x1.186a080000000p+0f,
	    0x1.e5a8460000000p-1f,
	    0x1.a48fda0000000p-1f,
	    0x1.6c310e0000000p+0f,
	    0x1.3b60800000000p+0f,
	    0x1.111aee0000000p+0f,
	    0x1.d8ffaa0000000p-1f,
	    0x1.99999a0000000p+0f,
	    0x1.62b2f00000000p+0f,
	    0x1.33281c0000000p+0f,
	    0x1.09fc9a0000000p+0f,
	    0x1.ccab860000000p+0f,
	    0x1.8eec7e0000000p+0f,
	    0x1.59742a0000000p+0f,
	    0x1.2b26900000000p+0f,
	    0x1.030dc40000000p+1f,
	    0x1.c0a9a40000000p+0f,
	    0x1.8486a00000000p+0f,
	    0x1.5073160000000p+0f,
	    0x1.235a720000000p+1f,
	    0x1.f89a660000000p+0f,
	    0x1.b4f7e20000000p+0f,
	    0x1.7a66220000000p+0f,
	    0x1.47ae140000000p+1f,
	    0x1.1bc25a0000000p+1f,
	    0x1.eb735e0000000p+0f,
	    0x1.a994280000000p+0f,
	    0x1.7089380000000p+1f,
	    0x1.3f23980000000p+1f,
	    0x1.145cee0000000p+1f,
	    0x1.dea41a0000000p+0f,
	    0x1.9e7c6e0000000p+1f,
	    0x1.66ee1e0000000p+1f,
	    0x1.36d2180000000p+1f,
	    0x1.0d28de0000000p+1f,
	    0x1.d22a500000000p+1f,
	    0x1.93aeb80000000p+1f,
	    0x1.5d931c0000000p+1f,
	    0x1.2eb81c0000000p+1f,
	    0x1.0624de0000000p+2f,
	    0x1.c603c40000000p+1f,
	    0x1.8929180000000p+1f,
	    0x1.5476860000000p+1f,
	    0x1.26d42c0000000p+2f,
	    0x1.fe9f5a0000000p+1f,
	    0x1.ba2e4c0000000p+1f,
	    0x1.7ee9ae0000000p+1f,
	    0x1.4b96be0000000p+2f,
	    0x1.1f24e40000000p+2f,
	    0x1.f150280000000p+1f,
	    0x1.aea7ca0000000p+1f,
	    0x1.74eea60000000p+2f,
	    0x1.42f22e0000000p+2f,
	    0x1.17a8e20000000p+2f,
	    0x1.e459c60000000p+1f,
	    0x1.a36e2e0000000p+2f,
	    0x1.6b36360000000p+2f,
	    0x1.3a87480000000p+2f,
	    0x1.105ed20000000p+2f,
	    0x1.d7b9e20000000p+2f,
	    0x1.987f7a0000000p+2f,
	    0x1.61bea20000000p+2f,
	    0x1.32548c0000000p+2f};

	float WmaGainScale(std::int32_t total_gain) {
		constexpr std::int32_t kMinimumGain = -32;
		constexpr std::int32_t kMaximumGain = 512;
		if (total_gain < kMinimumGain || total_gain > kMaximumGain) {
			throw std::runtime_error("WMA total gain exceeds the validated native floating-point domain");
		}

		const float exponent = static_cast<float>(total_gain) * 0.05f;
		const auto scale = static_cast<float>(std::pow(10.0, static_cast<double>(exponent)));
		if (!std::isfinite(scale) || !(scale > 0.0f)) {
			throw std::runtime_error("WMA total gain did not produce a finite positive scale");
		}

		return scale;
	}

	float ScaleWmaCoefficient(const WmaCoefficient& coefficient, float gain_scale, std::int32_t exponent_difference) {
		constexpr std::uint32_t kMaximumMagnitude = (1 << WmaCoefficientEscapeWidths::kMaxNativeFieldBits) - 1;
		if (coefficient.Kind != WmaCoefficientKind::Value || coefficient.Magnitude > kMaximumMagnitude || exponent_difference < 0 || !std::isfinite(gain_scale) || !(gain_scale > 0.0f)) {
			throw std::runtime_error("WMA coefficient scaling requires a bounded value and nonnegative exponent difference");
		}

		const auto difference = static_cast<std::uint32_t>(std::min(exponent_difference, 71));
		const float divisor = static_cast<float>(std::uint32_t{1} << (difference >> 2));
		const float inverse_exponent = kExponentMultipliers[difference] / divisor;
		const float scale = inverse_exponent * gain_scale;
		const float magnitude = static_cast<float>(coefficient.Magnitude) * scale;
		if (!std::isfinite(magnitude)) {
			throw std::runtime_error("WMA scaled coefficient exceeds finite floating-point storage");
		}

		return coefficient.IsNegative ? -magnitude : magnitude;
	}

} // namespace sgre::audio
