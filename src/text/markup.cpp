#include "sgre/text/markup.hpp"

#include <algorithm>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumTextUnits = 1'048'576;

	static bool IsDecimalDigit(std::uint16_t code_unit) {
		return code_unit >= u'0' && code_unit <= u'9';
	}

	TextMarkupReader::TextMarkupReader(std::span<const std::uint16_t> code_units, bool interpret_markup) : m_InterpretMarkup(interpret_markup) {
		if (code_units.size() > kMaximumTextUnits) {
			throw std::runtime_error("text markup input exceeds recovery limit");
		}

		if (code_units.empty()) {
			return;
		}

		const auto end = std::find(code_units.begin(), code_units.end(), std::uint16_t{0});
		m_CodeUnits = code_units.first(static_cast<std::size_t>(end - code_units.begin()));
	}

	std::size_t TextMarkupReader::Position() const noexcept {
		return m_Position;
	}

	void TextMarkupReader::ReadDelimitedPayload(TextMarkupToken& token, std::uint16_t delimiter) {
		const auto begin = m_Position;
		while (m_Position < m_CodeUnits.size() && m_CodeUnits[m_Position] != delimiter) {
			++m_Position;
		}

		token.Payload = m_CodeUnits.subspan(begin, m_Position - begin);
		if (m_Position < m_CodeUnits.size()) {
			++m_Position;
			token.HasTerminator = true;
		}
	}

	void TextMarkupReader::ReadNumericPayload(TextMarkupToken& token, bool allow_minus, std::size_t begin) {
		while (m_Position < m_CodeUnits.size()) {
			const auto code_unit = m_CodeUnits[m_Position];
			if (!IsDecimalDigit(code_unit) && !(allow_minus && code_unit == u'-')) {
				break;
			}

			++m_Position;
		}

		token.Payload = m_CodeUnits.subspan(begin, m_Position - begin);
		if (m_Position < m_CodeUnits.size() && m_CodeUnits[m_Position] == u';') {
			++m_Position;
			token.HasTerminator = true;
		}
	}

	static bool IsImmediatePercentDirective(std::uint16_t directive) {
		return directive == u';' || directive == u'B' || directive == u'C' || directive == u'L' || directive == u'R' || directive == u'S' || directive == u'r';
	}

	void TextMarkupReader::ReadPercentDirective(TextMarkupToken& token) {
		token.Kind = TextMarkupKind::PercentDirective;
		if (m_Position == m_CodeUnits.size()) {
			token.Kind = TextMarkupKind::IncompleteDirective;

			return;
		}

		const auto directive = m_CodeUnits[m_Position++];
		if (IsDecimalDigit(directive)) {
			// A leading decimal digit is itself the first font-size percentage digit.
			ReadNumericPayload(token, false, m_Position - 1);

			return;
		}

		token.DirectiveCode = directive;
		if (IsImmediatePercentDirective(directive)) {
			token.HasTerminator = directive == u';';

			return;
		}

		if (directive == u'b' || directive == u'i' || directive == u'k' || directive == u'e' || directive == u's') {
			if (m_Position == m_CodeUnits.size()) {
				return;
			}

			const auto operand_begin = m_Position++;
			if ((directive == u'e' || directive == u's') && m_CodeUnits[operand_begin] == u'#') {
				ReadDelimitedPayload(token, u';');
				// Retain the hex discriminator as part of this directive's operand.
				token.Payload = m_CodeUnits.subspan(operand_begin, token.Payload.size() + 1);
			} else {
				token.Payload = m_CodeUnits.subspan(operand_begin, 1);
			}

			return;
		}

		if (directive == u'a' || directive == u'd' || directive == u'p' || directive == u'n') {
			ReadNumericPayload(token, directive == u'p' || directive == u'n', m_Position);

			return;
		}

		if (directive == u'w' || directive == u't' || directive == u'D') {
			if (m_Position < m_CodeUnits.size() && m_CodeUnits[m_Position] == u'$') {
				++m_Position;
				token.VariablePayload = true;
				ReadDelimitedPayload(token, u';');
			} else {
				ReadNumericPayload(token, false, m_Position);
			}

			return;
		}

		// Font/link names and unrecognized controls all consume through the next semicolon.
		ReadDelimitedPayload(token, u';');
	}

	void TextMarkupReader::ReadEscapeDirective(TextMarkupToken& token) {
		if (m_Position == m_CodeUnits.size()) {
			token.Kind = TextMarkupKind::IncompleteDirective;

			return;
		}

		const auto code_unit = m_CodeUnits[m_Position++];
		token.Kind = TextMarkupKind::EscapedCodeUnit;
		token.CodeUnit = code_unit;
		if (code_unit == u'n') {
			token.Kind = TextMarkupKind::LineBreak;
			token.CodeUnit = u'\n';
		} else if (code_unit == u't') {
			token.CodeUnit = u'\t';
		} else if (code_unit == u'i' || code_unit == u'k' || code_unit == u'r' || code_unit == u'w' || code_unit == u'x') {
			token.Kind = TextMarkupKind::EscapeDirective;
			token.DirectiveCode = code_unit;
			token.CodeUnit = 0;
		}
	}

	std::optional<TextMarkupToken> TextMarkupReader::Next() {
		if (m_Position == m_CodeUnits.size()) {
			return std::nullopt;
		}

		TextMarkupToken token;
		token.SourceOffset = m_Position;
		token.CodeUnit = m_CodeUnits[m_Position++];
		if (token.CodeUnit == u'\n') {
			token.Kind = TextMarkupKind::LineBreak;
		} else if (m_InterpretMarkup) {
			switch (token.CodeUnit) {
			case u'#':
				token.Kind = TextMarkupKind::ColorDirective;
				ReadDelimitedPayload(token, u';');
				break;
			case u'$':
				token.Kind = TextMarkupKind::ExpansionDirective;
				ReadDelimitedPayload(token, u';');
				break;
			case u'&':
				token.Kind = TextMarkupKind::ObjectDirective;
				ReadDelimitedPayload(token, u';');
				break;
			case u'[':
				token.Kind = TextMarkupKind::StyleDirective;
				ReadDelimitedPayload(token, u']');
				break;
			case u'%':
				ReadPercentDirective(token);
				break;
			case u'\\':
				ReadEscapeDirective(token);
				break;
			default:
				break;
			}
		}

		token.SourceLength = m_Position - token.SourceOffset;

		return token;
	}

} // namespace sgre::text
