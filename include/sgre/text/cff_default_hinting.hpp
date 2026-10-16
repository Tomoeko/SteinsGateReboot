#pragma once

#include "sgre/resource/cff_driver_hints.hpp"
#include "sgre/text/cff_hinted_charstring.hpp"

#include <cstdint>

namespace sgre::text {

	// Uses the recovered CFF driver constructor defaults: hinting/scaling on, stem darkening off, no bold override.
	// The caller supplies coordinates already normalized by the font matrix and the matching units per em.
	[[nodiscard]] CffHintedCharstringConfiguration PrepareDefaultCffHintedConfiguration(const resource::CffDriverHintParameters& hints, std::int32_t pixel_size, std::uint16_t units_per_em);

} // namespace sgre::text
