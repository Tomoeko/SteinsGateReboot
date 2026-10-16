#pragma once

#include "sgre/audio/wma_coefficient.hpp"

#include <cstdint>

namespace sgre::audio {

	// Bounded native Standard gain domain. The single-precision exponent precedes the double-precision power.
	[[nodiscard]] float WmaGainScale(std::int32_t total_gain);
	[[nodiscard]] float ScaleWmaCoefficient(const WmaCoefficient& coefficient, float gain_scale, std::int32_t exponent_difference);

} // namespace sgre::audio
