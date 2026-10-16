#include "sgre/text/indicator_text.hpp"

#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace sgre::text {

	static constexpr std::size_t kMaxIndicatorCodeUnits = 65536;

	static bool IndicatorAsciiSpace(char character) {
		return character == ' ' || character == '\t' || character == '\n' || character == '\r' || character == '\f' || character == '\v';
	}

	static std::string_view IndicatorNumericPrefix(std::string_view text) {
		text = text.substr(0, text.find('\0'));
		while (!text.empty() && IndicatorAsciiSpace(text.front())) {
			text.remove_prefix(1);
		}

		return text;
	}

	static std::uint32_t ParseIndicatorColor(std::string_view text) {
		text = IndicatorNumericPrefix(text);
		bool negative = false;
		if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
			negative = text.front() == '-';
			text.remove_prefix(1);
		}

		if (text.starts_with("0x") || text.starts_with("0X")) {
			text.remove_prefix(2);
		}

		std::uint32_t value = 0;
		bool overflow = false;
		for (const auto character : text) {
			const auto digit = character >= '0' && character <= '9' ? static_cast<std::uint32_t>(character - '0') : (character >= 'a' && character <= 'f' ? static_cast<std::uint32_t>(character - 'a' + 10) : (character >= 'A' && character <= 'F' ? static_cast<std::uint32_t>(character - 'A' + 10) : 16u));
			if (digit >= 16) {
				break;
			}

			overflow = overflow || value > (std::numeric_limits<std::uint32_t>::max() - digit) / 16;
			value = value * 16 + digit;
		}

		return overflow ? std::numeric_limits<std::uint32_t>::max() : (negative ? 0u - value : value);
	}

	static bool IndicatorAsciiPrefix(std::string_view text, std::string_view prefix) {
		if (text.size() < prefix.size()) {
			return false;
		}

		for (std::size_t index = 0; index < prefix.size(); ++index) {
			const auto character = text[index];
			const auto lowercase = character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
			if (lowercase != prefix[index]) {
				return false;
			}
		}

		return true;
	}

	static float ParseIndicatorScale(std::string_view text) {
		text = IndicatorNumericPrefix(text);
		std::size_t position = !text.empty() && (text.front() == '+' || text.front() == '-') ? 1 : 0;
		if (text.substr(position).starts_with("0x") || text.substr(position).starts_with("0X")) {
			throw std::runtime_error("Indicator hexadecimal scale conversion has not been recovered");
		}

		const auto numeric = text.substr(position);
		if (IndicatorAsciiPrefix(numeric, "inf") || IndicatorAsciiPrefix(numeric, "nan")) {
			throw std::runtime_error("Indicator nonfinite scale directives are outside the finite layout contract");
		}

		std::size_t digits = 0;
		while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
			++position;
			++digits;
		}

		if (position < text.size() && text[position] == '.') {
			++position;
			while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
				++position;
				++digits;
			}
		}

		if (digits == 0) {
			return 0;
		}

		if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
			auto exponent_end = position + 1;
			if (exponent_end < text.size() && (text[exponent_end] == '+' || text[exponent_end] == '-')) {
				++exponent_end;
			}

			const auto exponent_begin = exponent_end;
			while (exponent_end < text.size() && text[exponent_end] >= '0' && text[exponent_end] <= '9') {
				++exponent_end;
			}

			if (exponent_end != exponent_begin) {
				position = exponent_end;
			}
		}

		double value = 0;
		std::istringstream input{std::string(text.substr(0, position))};
		input.imbue(std::locale::classic());
		const bool converted = static_cast<bool>(input >> value);
		const auto scale = static_cast<float>(value);
		if (!converted || !std::isfinite(scale)) {
			throw std::runtime_error("Indicator scale directive exceeds the supported finite decimal range");
		}

		return scale;
	}

	static bool IndicatorCombiningUnit(std::uint16_t unit) {
		if (unit == 0x3099 || unit == 0x309a) {
			return true;
		}

		return unit >= 0x0e31 && unit <= 0x0e4e && ((0x3fc003fdu >> (unit - 0x0e31)) & 1u) != 0;
	}

	static IndicatorTextElement ReadIndicatorCluster(std::span<const std::uint16_t> code_units, std::size_t& position, std::uint32_t color, float scale) {
		IndicatorTextElement element;
		element.Color = color;
		element.Scale = scale;
		const auto unit = code_units[position++];
		element.CodeUnits.push_back(unit);
		if (unit >= 0xd800 && unit <= 0xdfff) {
			if (position == code_units.size()) {
				throw std::runtime_error("Indicator text ends with an unpaired native surrogate unit");
			}

			element.CodeUnits.push_back(code_units[position++]);
		}

		if (position < code_units.size() && IndicatorCombiningUnit(code_units[position])) {
			element.CodeUnits.push_back(code_units[position++]);
		}

		return element;
	}

	static void ApplyIndicatorDirective(std::span<const std::string> parameters, std::uint32_t default_color, std::uint32_t& color, float& scale) {
		if (parameters.empty() || (parameters.front() != "color" && parameters.front() != "scale")) {
			return;
		}

		if (parameters.size() < 2) {
			throw std::runtime_error("Indicator color/scale directive lacks its required value");
		}

		if (parameters.front() == "color") {
			color = parameters[1] == "clear" ? default_color : ParseIndicatorColor(parameters[1]);
		} else {
			scale = ParseIndicatorScale(parameters[1]);
		}
	}

	std::vector<IndicatorTextElement> PrepareIndicatorText(std::span<const std::uint16_t> code_units, std::uint32_t default_color, bool recognize_tags) {
		if (code_units.size() > kMaxIndicatorCodeUnits) {
			throw std::runtime_error("Indicator text exceeds the bounded UTF-16 element budget");
		}

		std::vector<IndicatorTextElement> elements;
		elements.reserve(code_units.size());
		std::uint32_t color = default_color;
		float scale = 1;
		std::size_t position = 0;
		bool after_hash = false;
		while (position < code_units.size()) {
			if (!recognize_tags || (!after_hash && code_units[position] != '#')) {
				elements.push_back(ReadIndicatorCluster(code_units, position, color, scale));
				continue;
			}

			if (!after_hash) {
				after_hash = true;
				++position;
				continue;
			}

			if (code_units[position] != '{') {
				IndicatorTextElement escaped;
				escaped.Color = color;
				escaped.Scale = scale;
				escaped.CodeUnits.push_back(code_units[position++]);
				elements.push_back(std::move(escaped));
				// Native state 1 persists across literal units until an opening directive brace.
				continue;
			}

			++position;
			std::vector<std::string> parameters;
			std::string parameter;
			bool terminated = false;
			while (position < code_units.size()) {
				const auto unit = code_units[position++];
				if (unit == ',' || unit == '}') {
					parameters.push_back(std::move(parameter));
					parameter.clear();
					if (unit == '}') {
						terminated = true;
						break;
					}
				} else {
					parameter.push_back(static_cast<char>(unit & 0xffu));
				}
			}

			if (terminated) {
				after_hash = false;
				ApplyIndicatorDirective(parameters, default_color, color, scale);
			}
		}

		return elements;
	}

	bool IndicatorTextElement::IsLineBreak() const noexcept {
		return CodeUnits.size() == 1 && CodeUnits.front() == 0x0a;
	}

	IndicatorTextMeasurement::IndicatorTextMeasurement(resource::GlyphSize spacing) : m_Spacing(spacing) {
		if (!std::isfinite(spacing.Width) || !std::isfinite(spacing.Height)) {
			throw std::runtime_error("Indicator measurement requires finite spacing");
		}
	}

	void IndicatorTextMeasurement::Append(const IndicatorMeasuredElement& element) {
		if (m_Elements == kMaxIndicatorCodeUnits) {
			throw std::runtime_error("Indicator measurement exceeds its bounded element budget");
		}

		if (element.LineBreak) {
			const auto width = m_LineElements == 0 ? m_LineWidth : m_LineWidth + static_cast<float>(m_LineElements - 1) * m_Spacing.Width;
			const auto height = m_CompletedLines.Height + m_LineHeight;
			if (!std::isfinite(width) || !std::isfinite(height)) {
				throw std::runtime_error("Indicator completed line overflows finite logical coordinates");
			}

			m_CompletedLines.Width = width > m_CompletedLines.Width ? width : m_CompletedLines.Width;
			m_CompletedLines.Height = height;
			m_LineWidth = 0;
			m_LineHeight = 0;
			m_LineElements = 0;
			++m_LineBreaks;
			++m_Elements;
			return;
		}

		if (!std::isfinite(element.Scale) || !std::isfinite(element.Metrics.Width) || !std::isfinite(element.Metrics.Height)) {
			throw std::runtime_error("Indicator glyph measurement requires finite metrics and scale");
		}

		const auto height = element.Scale * element.Metrics.Height;
		const auto width = m_LineWidth + element.Scale * element.Metrics.Width;
		if (!std::isfinite(width) || !std::isfinite(height)) {
			throw std::runtime_error("Indicator line measurement overflows finite logical coordinates");
		}

		m_LineWidth = width;
		m_LineHeight = height > m_LineHeight ? height : m_LineHeight;
		++m_LineElements;
		++m_Elements;
	}

	resource::GlyphSize IndicatorTextMeasurement::Finish() const {
		const auto width = m_LineElements == 0 ? m_LineWidth : m_LineWidth + static_cast<float>(m_LineElements - 1) * m_Spacing.Width;
		resource::GlyphSize measured;
		measured.Width = width > m_CompletedLines.Width ? width : m_CompletedLines.Width;
		measured.Height = m_CompletedLines.Height + m_LineHeight;
		measured.Height = m_Spacing.Height * static_cast<float>(m_LineBreaks) + measured.Height;
		if (!std::isfinite(measured.Width) || !std::isfinite(measured.Height)) {
			throw std::runtime_error("Indicator measured extents overflow finite logical coordinates");
		}

		return measured;
	}

	resource::GlyphSize MeasureIndicatorText(std::span<const IndicatorMeasuredElement> elements, resource::GlyphSize spacing) {
		if (elements.size() > kMaxIndicatorCodeUnits) {
			throw std::runtime_error("Indicator measurement exceeds its bounded element budget");
		}

		IndicatorTextMeasurement measurement(spacing);
		for (const auto& element : elements) {
			measurement.Append(element);
		}

		return measurement.Finish();
	}

} // namespace sgre::text
