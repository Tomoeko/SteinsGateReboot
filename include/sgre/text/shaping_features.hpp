#pragma once

#include "sgre/text/vector_font.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sgre::text {

	enum class VectorFontFeatureFlags : std::uint32_t {
		PerCluster = 0,
		Global = 1,
		GlobalWithFallback = 3,
		GlobalWithTableSearch = 17
	};

	struct VectorFontFeatureRequest {
		std::string Tag;
		std::uint32_t Value = 1;
		std::uint32_t StartCluster = 0;
		std::uint32_t EndCluster = 0xffffffff;
	};

	struct VectorFontCollectedFeature {
		std::string Tag;
		std::uint32_t Order = 0;
		std::uint32_t Value = 0;
		VectorFontFeatureFlags Flags = VectorFontFeatureFlags::PerCluster;
		std::uint32_t GlobalValue = 0;
		std::uint32_t SubstitutionStage = 0;
		std::uint32_t PositioningStage = 0;
	};

	// Collects the native neutral-shaper records for Latin, Cyrillic and Han.
	// Preserves duplicates and user order; lookup compilation and glyph application are separate.
	// The caller limit is at most 65,536 records; feature tags contain exactly four ASCII characters.
	[[nodiscard]] std::vector<VectorFontCollectedFeature> CollectVectorFontFeatures(const VectorFontConfiguration& configuration, std::span<const VectorFontFeatureRequest> requests = {}, std::size_t maximum_features = 4096);

} // namespace sgre::text
