#pragma once

#include "sgre/resource/opentype_layout.hpp"
#include "sgre/resource/sfnt.hpp"
#include "sgre/text/shaping_feature_map.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	struct VectorFontScheduledLookup {
		std::uint16_t Index = 0;
		// The original two policy bits; glyph filtering is a separate recovered operation.
		std::uint16_t Options = 0;
		std::uint32_t Mask = 0;
	};

	struct VectorFontLookupSchedule {
		std::vector<VectorFontScheduledLookup> Lookups;
		// Cumulative lookup endpoints for the neutral profile's null-callback pauses.
		std::vector<std::size_t> StageEnds;
	};

	struct VectorFontLookupPlan {
		VectorFontFeatureMap FeatureMap;
		VectorFontLookupSchedule Substitutions;
		VectorFontLookupSchedule Positioning;
	};

	// Prepares the actual neutral/en feature masks and complete GSUB/GPOS lookup schedule.
	// Returned values own their metadata; glyph buffers, lookup payloads and positions remain untouched.
	[[nodiscard]] VectorFontLookupPlan PrepareVectorFontLookupPlan(const resource::SfntFont& font, const VectorFontConfiguration& configuration, std::span<const VectorFontFeatureRequest> requests = {});
	[[nodiscard]] VectorFontLookupPlan PrepareVectorFontLookupPlan(const resource::OpenTypeLayoutFont& substitutions, const resource::OpenTypeLayoutFont& positioning, const VectorFontConfiguration& configuration, std::span<const VectorFontFeatureRequest> requests);

} // namespace sgre::text
