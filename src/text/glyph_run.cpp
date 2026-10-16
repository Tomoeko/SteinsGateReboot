#include "sgre/text/glyph_run.hpp"

#include <cstddef>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumRunGlyphs = 1024 * 1024;
	static constexpr float kFixedPositionScale = 1.0f / 64.0f;

	TextCursor PrepareTextGlyphRun(std::span<const ShapedTextGlyph> shaped_glyphs, std::span<TextRunGlyph> output) {
		if (shaped_glyphs.size() > kMaximumRunGlyphs) {
			throw std::runtime_error("text glyph run exceeds the glyph-count limit");
		}

		if (output.size() < shaped_glyphs.size()) {
			throw std::runtime_error("text glyph run output is shorter than its shaped input");
		}

		TextCursor advance;
		for (std::size_t index = 0; index < shaped_glyphs.size(); ++index) {
			const auto& shaped = shaped_glyphs[index];
			auto& glyph = output[index];
			glyph.GlyphIndex = shaped.GlyphIndex;
			glyph.Position.X = static_cast<float>(shaped.XOffset26Dot6) * kFixedPositionScale + advance.X;
			glyph.Position.Y = static_cast<float>(shaped.YOffset26Dot6) * kFixedPositionScale + advance.Y;
			advance.X += static_cast<float>(shaped.XAdvance26Dot6) * kFixedPositionScale;
			advance.Y += static_cast<float>(shaped.YAdvance26Dot6) * kFixedPositionScale;
		}

		return advance;
	}

} // namespace sgre::text
