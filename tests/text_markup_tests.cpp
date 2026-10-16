#include "sgre/text/encoding.hpp"
#include "sgre/text/markup.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static std::string Ascii(std::span<const std::uint16_t> units) {
	std::string text;
	for (const auto unit : units) {
		Require(unit < 128, "non-ASCII test payload");
		text.push_back(static_cast<char>(unit));
	}

	return text;
}

static void TestDelimitedDirectives() {
	const auto input = sgre::text::DecodeTextCodeUnits("A#123abc;$name;&icon;[face]Z");
	sgre::text::TextMarkupReader reader(input);
	const auto first = reader.Next();
	Require(first && first->Kind == sgre::text::TextMarkupKind::CodeUnit && first->CodeUnit == u'A' && first->SourceLength == 1, "literal markup prefix was changed");
	const std::array<sgre::text::TextMarkupKind, 4> kinds{sgre::text::TextMarkupKind::ColorDirective, sgre::text::TextMarkupKind::ExpansionDirective, sgre::text::TextMarkupKind::ObjectDirective, sgre::text::TextMarkupKind::StyleDirective};
	const std::array<std::string_view, 4> payloads{"123abc", "name", "icon", "face"};
	std::size_t next = 1;
	for (std::size_t index = 0; index < kinds.size(); ++index) {
		const auto token = reader.Next();
		Require(token && token->Kind == kinds[index] && Ascii(token->Payload) == payloads[index] && token->HasTerminator && token->SourceOffset == next, "delimited markup boundary differs from original grammar");
		Require(token->Payload.data() >= input.data() && token->Payload.data() < input.data() + input.size(), "markup payload lost its input ownership");
		next += token->SourceLength;
	}

	const auto last = reader.Next();
	Require(last && last->CodeUnit == u'Z' && last->SourceOffset == next && !reader.Next(), "markup source ranges lost the final unit");
}

static void TestPercentOperandBoundaries() {
	struct Fixture {
		std::string_view Input;
		std::uint16_t Directive = 0;
		std::string_view Payload;
		std::size_t Consumed = 0;
		bool Terminated = false;
		bool Variable = false;
	};
	const std::array<Fixture, 19> fixtures = {{
	    {"%125;Z", 0, "125", 5, true, false},
	    {"%125X", 0, "125", 4, false, false},
	    {"%b1;", u'b', "1", 3, false, false},
	    {"%i0Z", u'i', "0", 3, false, false},
	    {"%k;Z", u'k', ";", 3, false, false},
	    {"%s#ff0011;Z", u's', "#ff0011", 10, true, false},
	    {"%e1;Z", u'e', "1", 3, false, false},
	    {"%d125X", u'd', "125", 5, false, false},
	    {"%a-12;", u'a', "", 2, false, false},
	    {"%p--12;Z", u'p', "--12", 7, true, false},
	    {"%n3-4X", u'n', "3-4", 5, false, false},
	    {"%fFace;Z", u'f', "Face", 7, true, false},
	    {"%lLink;Z", u'l', "Link", 7, true, false},
	    {"%w25;Z", u'w', "25", 5, true, false},
	    {"%t$Wait;Z", u't', "Wait", 8, true, true},
	    {"%D$Time;Z", u'D', "Time", 8, true, true},
	    {"%DZ", u'D', "", 2, false, false},
	    {"%Qabc;Z", u'Q', "abc", 6, true, false},
	    {"%Qabc", u'Q', "abc", 5, false, false},
	}};
	for (const auto& fixture : fixtures) {
		const auto input = sgre::text::DecodeTextCodeUnits(fixture.Input);
		sgre::text::TextMarkupReader reader(input);
		const auto token = reader.Next();
		Require(token && token->Kind == sgre::text::TextMarkupKind::PercentDirective && token->DirectiveCode == fixture.Directive && Ascii(token->Payload) == fixture.Payload && token->SourceLength == fixture.Consumed && reader.Position() == fixture.Consumed && token->HasTerminator == fixture.Terminated && token->VariablePayload == fixture.Variable, "percent directive consumed the wrong native operand range");
	}

	for (const auto directive : {'B', 'C', 'L', 'R', 'S', 'r', ';'}) {
		const std::string source = std::string("%") + directive + 'X';
		const auto input = sgre::text::DecodeTextCodeUnits(source);
		sgre::text::TextMarkupReader reader(input);
		const auto token = reader.Next();
		Require(token && token->Payload.empty() && token->SourceLength == 2 && reader.Next()->CodeUnit == u'X', "immediate percent control swallowed a following unit");
	}
}

static void TestEscapeDirectivesAndLiteralMode() {
	const auto input = sgre::text::DecodeTextCodeUnits("\\n\\t\\i\\k\\r\\w\\x\\Q\\\\\n");
	sgre::text::TextMarkupReader reader(input);
	const auto newline = reader.Next();
	Require(newline && newline->Kind == sgre::text::TextMarkupKind::LineBreak && newline->SourceLength == 2, "escaped newline did not select line finalization");
	const auto tab = reader.Next();
	Require(tab && tab->Kind == sgre::text::TextMarkupKind::EscapedCodeUnit && tab->CodeUnit == u'\t', "escaped tab did not retain a measured tab unit");
	for (const auto directive : {'i', 'k', 'r', 'w', 'x'}) {
		const auto token = reader.Next();
		Require(token && token->Kind == sgre::text::TextMarkupKind::EscapeDirective && token->DirectiveCode == static_cast<std::uint16_t>(directive), "escape control was silently converted to a glyph");
	}

	Require(reader.Next()->CodeUnit == u'Q' && reader.Next()->CodeUnit == u'\\' && reader.Next()->Kind == sgre::text::TextMarkupKind::LineBreak && !reader.Next(), "literal escape handling changed source order");
	const auto raw_input = sgre::text::DecodeTextCodeUnits("#x;\\n\n");
	sgre::text::TextMarkupReader raw(raw_input, false);
	for (std::size_t index = 0; index < raw_input.size(); ++index) {
		const auto token = raw.Next();
		Require(token && token->CodeUnit == raw_input[index] && token->SourceLength == 1 && token->Kind == (raw_input[index] == u'\n' ? sgre::text::TextMarkupKind::LineBreak : sgre::text::TextMarkupKind::CodeUnit), "plain native rendering still interpreted markup");
	}
}

static void TestMissingOperandsAndInputBoundary() {
	for (const auto source : {"#", "$name", "&", "[name", "%", "\\"}) {
		const auto input = sgre::text::DecodeTextCodeUnits(source);
		sgre::text::TextMarkupReader reader(input);
		const auto token = reader.Next();
		Require(token && token->SourceLength == input.size() && !token->HasTerminator && !reader.Next(), "unterminated directive read beyond its source");
	}

	sgre::text::TextMarkupReader empty(std::span<const std::uint16_t>{});
	Require(!empty.Next() && empty.Position() == 0, "empty markup input produced a token");
	const std::array<std::uint16_t, 6> units{0xd83d, 0xde00, 0, u'#', u';', u'A'};
	sgre::text::TextMarkupReader reader(units);
	Require(reader.Next()->CodeUnit == 0xd83d && reader.Next()->CodeUnit == 0xde00 && !reader.Next(), "native code-unit/NUL boundary was normalized to scalars");
	const std::vector<std::uint16_t> excessive(1'048'577, u'A');
	try {
		sgre::text::TextMarkupReader too_large(excessive);
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("text markup") != std::string_view::npos, "markup limit lost error context");

		return;
	}

	throw std::runtime_error("excessive markup input was accepted");
}

int main() {
	try {
		TestDelimitedDirectives();
		TestPercentOperandBoundaries();
		TestEscapeDirectivesAndLiteralMode();
		TestMissingOperandsAndInputBoundary();
		std::cout << "Text markup tests passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
