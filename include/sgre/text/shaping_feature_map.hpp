#pragma once

#include "sgre/text/shaping_features.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sgre::text {

	struct VectorFontFeatureTable {
		// Indexed by the actual layout table's FeatureList, including tags outside the selected language.
		std::vector<std::string> FeatureTags;
		std::vector<std::uint16_t> SelectedFeatures;
		std::optional<std::uint16_t> RequiredFeature;
	};

	struct VectorFontFeatureMask {
		std::string Tag;
		std::optional<std::uint16_t> SubstitutionFeature;
		std::optional<std::uint16_t> PositioningFeature;
		std::uint32_t SubstitutionStage = 0;
		std::uint32_t PositioningStage = 0;
		std::uint32_t Shift = 0;
		std::uint32_t Mask = 0;
		std::uint32_t OneMask = 0;
		// Original compiled lookup-policy word; bit zero records a retained missing-feature fallback.
		std::uint32_t LookupFlags = 0;
	};

	struct VectorFontFeatureMap {
		std::uint32_t GlobalMask = 2;
		std::uint32_t RequiredSubstitutionStage = 0;
		std::uint32_t RequiredPositioningStage = 0;
		std::vector<VectorFontFeatureMask> Features;
	};

	// Compiles neutral collected records through the original mask-allocation boundary.
	// Tables are checked prepared metadata; lookup schedules and glyph mutation are separate operations.
	[[nodiscard]] VectorFontFeatureMap CompileVectorFontFeatureMasks(std::span<const VectorFontCollectedFeature> features, const VectorFontFeatureTable& substitutions, const VectorFontFeatureTable& positioning);

} // namespace sgre::text
