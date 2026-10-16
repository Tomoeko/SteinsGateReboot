#pragma once

#include "sgre/resource/opentype_layout.hpp"
#include "sgre/resource/sfnt.hpp"
#include "sgre/text/shaping_feature_map.hpp"

#include <span>
#include <string_view>

namespace sgre::text {

	// Native417c70 maps the recovered canonical en/ja profiles, or the actual absent-language branch.
	[[nodiscard]] std::string_view ResolveVectorFontLayoutLanguageTag(const VectorFontConfiguration& configuration);

	// Uses the actual font's GSUB/GPOS metadata and the recovered neutral-script language profile.
	// Produces feature masks only; it does not apply lookup payloads or publish glyph positions.
	[[nodiscard]] VectorFontFeatureMap PrepareVectorFontFeatureMap(const resource::SfntFont& font, const VectorFontConfiguration& configuration, std::span<const VectorFontFeatureRequest> requests = {});
	[[nodiscard]] VectorFontFeatureMap PrepareVectorFontFeatureMap(const resource::OpenTypeLayoutFont& substitutions, const resource::OpenTypeLayoutFont& positioning, const VectorFontConfiguration& configuration, std::span<const VectorFontFeatureRequest> requests);

} // namespace sgre::text
