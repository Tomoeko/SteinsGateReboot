#pragma once

#include "sgre/resource/image.hpp"
#include "sgre/resource/psb.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::resource {

	struct BitmapGlyph {
		std::uint32_t Codepoint = 0;
		std::uint32_t SourceIndex = 0;
		float AtlasX = 0;
		float AtlasY = 0;
		float NominalWidth = 0;
		float NominalHeight = 0;
		float InkWidth = 0;
		float InkHeight = 0;
		float VerticalBearing = 0;
		// The native reader retains b and d; their layout roles remain unverified.
		float MetricB = 0;
		float MetricD = 0;
	};

	struct BitmapFont {
		std::string Label;
		float MinWidth = 0;
		float MinHeight = 0;
		float MaxWidth = 0;
		float MaxHeight = 0;
		std::vector<ImageTexture> Sources;
		// Exact original UTF-8 lookup keys, with transparent string_view lookup.
		std::map<std::string, BitmapGlyph, std::less<>> Glyphs;

		[[nodiscard]] const BitmapGlyph* FindGlyph(std::string_view utf8_key) const;
		// Native bitmap lookup converts one UTF-16 code unit at a time, including surrogate replacement.
		[[nodiscard]] const BitmapGlyph* FindGlyphCodeUnit(std::uint16_t code_unit) const;
	};

	struct GlyphSize {
		float Width = 0;
		float Height = 0;
	};

	struct GlyphQuad {
		float Left = 0;
		float Top = 0;
		float Right = 0;
		float Bottom = 0;
		float U0 = 0;
		float V0 = 0;
		float U1 = 0;
		float V1 = 0;
	};

	[[nodiscard]] BitmapFont ReadBitmapFont(const PsbDocument& document);
	[[nodiscard]] GlyphSize MeasureBitmapGlyph(const BitmapGlyph* glyph, float scale_x, float scale_y);
	[[nodiscard]] GlyphQuad EvaluateGlyphQuad(const BitmapGlyph& glyph, const ImageTexture& source, float draw_x, float draw_y, float scale_x, float scale_y);

} // namespace sgre::resource
