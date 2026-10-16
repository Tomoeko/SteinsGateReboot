#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sgre::text {

	enum class TextMarkupKind {
		CodeUnit,
		EscapedCodeUnit,
		LineBreak,
		ColorDirective,
		ExpansionDirective,
		ObjectDirective,
		StyleDirective,
		PercentDirective,
		EscapeDirective,
		IncompleteDirective
	};

	struct TextMarkupToken {
		TextMarkupKind Kind = TextMarkupKind::CodeUnit;
		std::uint16_t CodeUnit = 0;
		// Percent directives retain the native operator; zero identifies a leading font-size percentage.
		std::uint16_t DirectiveCode = 0;
		std::size_t SourceOffset = 0;
		std::size_t SourceLength = 0;
		std::span<const std::uint16_t> Payload;
		bool HasTerminator = false;
		bool VariablePayload = false;
	};

	// The prepared input owner must outlive this reader and every returned payload view.
	// The original render boundary stops at the first NUL unit. No glyph/style callbacks run here.
	class TextMarkupReader {
	public:
		explicit TextMarkupReader(std::span<const std::uint16_t> code_units, bool interpret_markup = true);
		[[nodiscard]] std::optional<TextMarkupToken> Next();
		[[nodiscard]] std::size_t Position() const noexcept;

	private:
		void ReadDelimitedPayload(TextMarkupToken& token, std::uint16_t delimiter);
		void ReadNumericPayload(TextMarkupToken& token, bool allow_minus, std::size_t begin);
		void ReadPercentDirective(TextMarkupToken& token);
		void ReadEscapeDirective(TextMarkupToken& token);

		std::span<const std::uint16_t> m_CodeUnits;
		std::size_t m_Position = 0;
		bool m_InterpretMarkup = true;
	};

} // namespace sgre::text
