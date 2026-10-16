#pragma once

#include "sgre/resource/sfnt.hpp"
#include "sgre/text/direction_adjustment.hpp"
#include "sgre/text/recursive_decomposition.hpp"
#include "sgre/text/shaping_feature_map.hpp"

#include <span>

namespace sgre::text {

	struct NeutralGlyphPreparationContext {
		// OriginalDirection is saved before the earlier direction-adjustment/reversal phase.
		GlyphShapingDirection OriginalDirection = GlyphShapingDirection::LeftToRight;
		GlyphShapingDirection BufferDirection = GlyphShapingDirection::LeftToRight;
		GlyphClusterLevel ClusterLevel = GlyphClusterLevel::MonotoneGraphemes;
		// The original caller derives this from its actual layout service/descriptor decision.
		bool FallbackMarkPositioning = false;
	};

	// Actual neutral caller after Unicode/cluster/direction preparation and before GDEF/lookup application.
	// Features must be prepared from this same font; font/resource and feature metadata borrow for this call.
	[[nodiscard]] NormalizedGlyphContent PrepareNeutralGlyphIndices(UnicodeNormalizationBuffer& buffer, const resource::SfntFont& font, const VectorFontFeatureMap& features, const NeutralGlyphPreparationContext& context, std::span<const VectorFontFeatureRequest> requests = {});

} // namespace sgre::text
