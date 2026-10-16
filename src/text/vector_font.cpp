#include "sgre/text/vector_font.hpp"

#include "sgre/text/vector_font_metric_services.hpp"
#include "sgre/text/vector_font_normalization_font.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sgre::text {

	std::uint32_t VectorFontSizeWord(float size) {
		const auto number = static_cast<double>(size);
		// Native CVTTSS2SI uses a signed 64-bit destination, then stores its low word.
		if (!(number >= -9223372036854775808.0 && number < 9223372036854775808.0)) {
			return 0;
		}

		return static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(number)));
	}

	std::int32_t VectorFontRequestedHeight26Dot6(std::uint32_t size_word) {
		const auto height = std::uint64_t(size_word) * 64;
		if (height > std::uint64_t(std::numeric_limits<std::int32_t>::max())) {
			return std::numeric_limits<std::int32_t>::min();
		}

		return static_cast<std::int32_t>(height);
	}

	static std::string CanonicalVectorFontLanguage(std::string_view language) {
		std::string result;
		const auto count = std::min(std::size_t{63}, language.size());
		result.reserve(count);
		for (const auto character : language.substr(0, count)) {
			if (character >= 'A' && character <= 'Z') {
				result.push_back(static_cast<char>(character - 'A' + 'a'));
			} else if (character == '_' || character == '@') {
				result.push_back('-');
			} else if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-') {
				result.push_back(character);
			} else {
				break;
			}
		}

		return result;
	}

	VectorFontConfiguration ResolveVectorFontConfiguration(std::int32_t mode, std::string language) {
		if (mode < 0 || mode > 4 || language.size() > 65536 || language.find('\0') != std::string::npos) {
			throw std::runtime_error("VectorFont shaping mode or language is outside its recovered range");
		}

		VectorFontConfiguration result;
		result.HasLanguage = !language.empty();
		result.Language = CanonicalVectorFontLanguage(language);
		switch (mode) {
		case 1:
			result.Script = VectorFontScript::Cyrillic;
			break;
		case 2:
			result.Script = VectorFontScript::Arabic;
			result.Direction = VectorFontDirection::RightToLeft;
			break;
		case 3:
			result.Script = VectorFontScript::Han;
			break;
		case 4:
			result.Script = VectorFontScript::Han;
			result.Direction = VectorFontDirection::TopToBottom;
			break;
		default:
			break;
		}

		return result;
	}

	static std::shared_ptr<const std::vector<std::uint8_t>> ValidateFontBytes(std::shared_ptr<const std::vector<std::uint8_t>> bytes) {
		if (!bytes || bytes->empty() || bytes->size() > 64 * 1024 * 1024) {
			throw std::runtime_error("VectorFont raw bytes are absent or exceed the bounded font owner");
		}

		return bytes;
	}

	VectorFont::VectorFont(std::shared_ptr<const std::vector<std::uint8_t>> bytes, std::int32_t mode, std::string language)
	    : m_Bytes(ValidateFontBytes(std::move(bytes))), m_Font(*m_Bytes), m_Configuration(ResolveVectorFontConfiguration(mode, std::move(language))) {
		if (m_Font.OutlineKind() == resource::SfntOutlineKind::Cff) {
			m_Cff = std::make_unique<CffGlyphFont>(m_Font);
		}

		m_NormalizationFont = std::make_unique<VectorFontNormalizationFont>(m_Bytes, m_Font);
		m_InputBuffer = std::make_unique<UnicodeNormalizationBuffer>();
	}

	VectorFont::~VectorFont() = default;

	VectorFontNormalizationFont& VectorFont::NormalizationFont() {
		return *m_NormalizationFont;
	}

	void VectorFont::BindDefaultCffMetricServices() {
		BindMetricServices(VectorFontMetricScaleSign::Nonnegative, VectorFontMetricScaleSign::Nonnegative);
		m_DefaultCffLayoutScale = true;
	}

	void VectorFont::BindMetricServices(VectorFontMetricScaleSign actual_initial_hb_x_sign, VectorFontMetricScaleSign actual_initial_hb_y_sign) {
		if (m_MetricServices) {
			throw std::runtime_error("VectorFont actual metric userdata has already been bound");
		}

		auto services = std::make_unique<VectorFontMetricServices>(*this, actual_initial_hb_x_sign, actual_initial_hb_y_sign);
		m_NormalizationFont->BindMetricServices(*services);
		m_MetricServices = std::move(services);
	}

	const VectorFontMetricServices& VectorFont::MetricServices() const {
		if (!m_MetricServices) {
			throw std::runtime_error("VectorFont metric services require actual applied-domain admission");
		}

		return *m_MetricServices;
	}

	const resource::SfntFont& VectorFont::Font() const {
		return m_Font;
	}

	UnicodeNormalizationBuffer& VectorFont::InputBuffer() {
		return *m_InputBuffer;
	}

	const VectorFontConfiguration& VectorFont::Configuration() const {
		return m_Configuration;
	}

	const VectorFontSizeState& VectorFont::SizeState() const {
		return m_Size;
	}

	float VectorFont::Size() const {
		return static_cast<float>(m_Size.Word);
	}

	void VectorFont::SetSize(float size) {
		const auto word = VectorFontSizeWord(size);
		if (word == m_Size.Word) {
			return;
		}

		m_Size.Word = word;
		m_Size.RequestedHeight26Dot6 = VectorFontRequestedHeight26Dot6(word);
	}

	std::uint16_t VectorFont::MapCharacter(std::uint32_t character) const {
		return m_Font.MapCharacter(character);
	}

	CffPreparedGlyph VectorFont::PrepareGlyph(std::uint32_t glyph_index) const {
		if (!m_Cff) {
			throw std::runtime_error("VectorFont TrueType outline preparation remains unrecovered");
		}
		if (!m_Size.RequestedHeight26Dot6) {
			throw std::runtime_error("VectorFont glyph preparation requires an applied character size");
		}

		// FT_Set_Char_Size clamps both requested dimensions to at least one pixel.
		const auto height = std::max(std::int32_t{64}, *m_Size.RequestedHeight26Dot6);

		return m_Cff->PrepareGlyph(glyph_index, height / 64);
	}

} // namespace sgre::text
