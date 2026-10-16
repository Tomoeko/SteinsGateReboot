#pragma once

#include "sgre/resource/font.hpp"
#include "sgre/resource/sfnt.hpp"
#include "sgre/text/cff_glyph_font.hpp"
#include "sgre/text/glyph_run.hpp"
#include "sgre/text/vector_font_metric_services.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::text {

	enum class VectorFontScript {
		Latin,
		Cyrillic,
		Arabic,
		Han
	};

	enum class VectorFontDirection {
		LeftToRight = 4,
		RightToLeft = 5,
		TopToBottom = 6
	};

	struct VectorFontConfiguration {
		VectorFontScript Script = VectorFontScript::Latin;
		VectorFontDirection Direction = VectorFontDirection::LeftToRight;
		std::string Language = "en";
		bool HasLanguage = true;
	};

	struct VectorFontSizeState {
		std::uint32_t Word = 0xffffffff;
		std::optional<std::int32_t> RequestedHeight26Dot6;
	};

	[[nodiscard]] std::uint32_t VectorFontSizeWord(float size);
	[[nodiscard]] std::int32_t VectorFontRequestedHeight26Dot6(std::uint32_t size_word);
	[[nodiscard]] VectorFontConfiguration ResolveVectorFontConfiguration(std::int32_t mode, std::string language);

	class VectorFontNormalizationFont;
	class UnicodeNormalizationBuffer;

	class VectorFont {
	public:
		// Owns the raw resource pin. SFNT and CFF readers borrow these immutable bytes.
		VectorFont(std::shared_ptr<const std::vector<std::uint8_t>> bytes, std::int32_t mode = 0, std::string language = "en");
		~VectorFont();
		VectorFont(const VectorFont&) = delete;
		VectorFont& operator=(const VectorFont&) = delete;
		VectorFont(VectorFont&&) = delete;
		VectorFont& operator=(VectorFont&&) = delete;

		[[nodiscard]] const resource::SfntFont& Font() const;
		[[nodiscard]] VectorFontNormalizationFont& NormalizationFont();
		// Default fresh CFF construction proves nonnegative HB signs; current positive-size admission is still required.
		void BindDefaultCffMetricServices();
		// One-time applied-domain binding requires actual initialized HB scale signs; no initial FT size is guessed.
		void BindMetricServices(VectorFontMetricScaleSign actual_initial_hb_x_sign, VectorFontMetricScaleSign actual_initial_hb_y_sign);
		[[nodiscard]] const VectorFontMetricServices& MetricServices() const;
		// Borrows for this immovable font's lifetime; callers serialize buffer mutations.
		[[nodiscard]] UnicodeNormalizationBuffer& InputBuffer();
		[[nodiscard]] const VectorFontConfiguration& Configuration() const;
		[[nodiscard]] const VectorFontSizeState& SizeState() const;
		[[nodiscard]] float Size() const;
		void SetSize(float size);
		// Native one-unit consumer for the admitted neutral single-base-glyph lookup domain.
		[[nodiscard]] resource::GlyphSize MeasureCodeUnit(std::uint16_t code_unit);
		// One actual UTF-16 buffer through neutral normalization, admitted GSUB/GPOS and owned run publication.
		[[nodiscard]] OwnedTextGlyphRun PrepareRun(std::u16string_view text);
		[[nodiscard]] std::uint16_t MapCharacter(std::uint32_t character) const;
		// CPU preparation for the verified default CFF driver; does not perform shaping or GPU work.
		[[nodiscard]] CffPreparedGlyph PrepareGlyph(std::uint32_t glyph_index) const;

	private:
		friend class VectorFontMetricServices;
		std::shared_ptr<const std::vector<std::uint8_t>> m_Bytes;
		resource::SfntFont m_Font;
		VectorFontConfiguration m_Configuration;
		VectorFontSizeState m_Size;
		bool m_DefaultCffLayoutScale = false;
		std::unique_ptr<CffGlyphFont> m_Cff;
		std::unique_ptr<VectorFontMetricServices> m_MetricServices;
		std::unique_ptr<VectorFontNormalizationFont> m_NormalizationFont;
		std::unique_ptr<UnicodeNormalizationBuffer> m_InputBuffer;
	};

} // namespace sgre::text
