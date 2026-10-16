#include "sgre/text/cff_glyph_font.hpp"

#include "sgre/resource/cff_outline.hpp"
#include "sgre/text/cff_default_hinting.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace sgre::text {

	CffGlyphFont::CffGlyphFont(const resource::SfntFont& font) : m_Sfnt(font), m_Font(font) {
		if (m_Sfnt.UnitsPerEm() != 1000 || m_Font.FontMatrix() != std::array<double, 6>{0.001, 0, 0, 0.001, 0, 0}) {
			throw std::runtime_error("CFF glyph font requires the recovered normalized 1000-unit standard matrix");
		}

		m_Hints.reserve(m_Font.FontDictionaries().size());
		for (const auto& dictionary : m_Font.FontDictionaries()) {
			if (m_Font.IsCid() && dictionary.FontMatrix) {
				throw std::runtime_error("CFF glyph font subfont matrix overrides remain unsupported");
			}

			m_Hints.push_back(resource::ReadCffDriverHintParameters(dictionary.PrivateDictionaryBytes));
		}
	}

	CffPreparedGlyph CffGlyphFont::PrepareGlyph(std::uint32_t glyph_index, std::int32_t pixel_size) const {
		if (glyph_index >= m_Sfnt.GlyphCount() || pixel_size <= 0 || pixel_size > 2000) {
			throw std::runtime_error("CFF glyph font index or pixel size is outside the recovered range");
		}

		try {
			const auto vertical = m_Sfnt.VerticalMetrics(glyph_index);
			if (!vertical) {
				throw std::runtime_error("CFF glyph font requires original vertical metrics");
			}

			const auto horizontal = m_Sfnt.HorizontalMetrics(glyph_index);
			const auto dictionary_index = m_Font.GlyphFontDictionary(glyph_index);
			const auto& dictionary = m_Font.FontDictionaries()[dictionary_index];
			const auto configuration = PrepareDefaultCffHintedConfiguration(m_Hints[dictionary_index], pixel_size, m_Sfnt.UnitsPerEm());
			CffCharstringPrograms programs;
			programs.Glyph = m_Font.GlyphProgram(glyph_index);
			programs.LocalSubroutines = dictionary.LocalSubroutines;
			programs.GlobalSubroutines = m_Font.GlobalSubroutines();
			const auto hinted = ExecuteCffHintedCharstring(programs, configuration);

			CffPreparedGlyph result;
			result.GlyphIndex = static_cast<std::uint16_t>(glyph_index);
			result.PixelSize = static_cast<std::uint16_t>(pixel_size);
			result.Outline = BuildCffContourOutline(hinted.Callbacks);
			CffGlyphFontMetrics font_metrics;
			font_metrics.HorizontalAdvance = horizontal.Advance;
			font_metrics.VerticalAdvance = vertical->Advance;
			font_metrics.VerticalSideBearing = vertical->SideBearing;
			const auto scale = PrepareVectorFontScale(pixel_size, m_Sfnt.UnitsPerEm());
			result.Metrics = MeasureCffHintedGlyph(result.Outline, scale, scale, font_metrics, result.PixelSize);

			return result;
		} catch (const std::runtime_error& error) {
			throw std::runtime_error("CFF glyph " + std::to_string(glyph_index) + " at pixel size " + std::to_string(pixel_size) + ": " + error.what());
		}
	}

	CffUnhintedGlyph CffGlyphFont::PrepareUnhintedGlyph(std::uint32_t glyph_index, std::int32_t pixel_size) const {
		if (glyph_index >= m_Sfnt.GlyphCount() || pixel_size <= 0 || pixel_size > 2000) {
			throw std::runtime_error("CFF unhinted glyph index or pixel size is outside the recovered range");
		}

		try {
			const auto vertical = m_Sfnt.VerticalMetrics(glyph_index);
			if (!vertical) {
				throw std::runtime_error("CFF unhinted glyph requires original vertical metrics");
			}

			const auto horizontal = m_Sfnt.HorizontalMetrics(glyph_index);
			const auto design = resource::PrepareCffDesignOutline(resource::ReadCffGlyphOutline(m_Font, glyph_index));
			const auto scale = PrepareVectorFontScale(pixel_size, m_Sfnt.UnitsPerEm());
			CffGlyphFontMetrics font_metrics;
			font_metrics.HorizontalAdvance = horizontal.Advance;
			font_metrics.VerticalAdvance = vertical->Advance;
			font_metrics.VerticalSideBearing = vertical->SideBearing;
			CffUnhintedGlyph result;
			result.GlyphIndex = static_cast<std::uint16_t>(glyph_index);
			result.PixelSize = static_cast<std::uint16_t>(pixel_size);
			result.Outline = ScaleCffGlyphOutline26Dot6(design, scale, scale, font_metrics);

			return result;
		} catch (const std::runtime_error& error) {
			throw std::runtime_error("CFF unhinted glyph " + std::to_string(glyph_index) + " at pixel size " + std::to_string(pixel_size) + ": " + error.what());
		}
	}

} // namespace sgre::text
