#include "sgre/text/unicode_properties.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireProperties(std::uint32_t codepoint, sgre::text::UnicodeGeneralCategory category, std::uint8_t combining_class) {
	const auto value = sgre::text::QueryUnicodeProperties(codepoint);
	Require(value.Category == category && value.CombiningClass == combining_class, "recovered Unicode category/combining pair differs");
}

static void TestBasicCategories() {
	using Category = sgre::text::UnicodeGeneralCategory;
	RequireProperties(0, Category::Control, 0);
	RequireProperties(0x20, Category::SpaceSeparator, 0);
	RequireProperties(0x24, Category::CurrencySymbol, 0);
	RequireProperties(0x28, Category::OpenPunctuation, 0);
	RequireProperties(0x29, Category::ClosePunctuation, 0);
	RequireProperties(0x30, Category::DecimalNumber, 0);
	RequireProperties(0x41, Category::UppercaseLetter, 0);
	RequireProperties(0x61, Category::LowercaseLetter, 0);
	RequireProperties(0x3042, Category::OtherLetter, 0);
	RequireProperties(0x4e00, Category::OtherLetter, 0);
	RequireProperties(0x2b0, Category::ModifierLetter, 0);
	RequireProperties(0x1c5, Category::TitlecaseLetter, 0);
	RequireProperties(0x903, Category::SpacingMark, 0);
	RequireProperties(0x20dd, Category::EnclosingMark, 0);
	RequireProperties(0x2160, Category::LetterNumber, 0);
	RequireProperties(0xb2, Category::OtherNumber, 0);
	RequireProperties(0x5f, Category::ConnectorPunctuation, 0);
	RequireProperties(0x2d, Category::DashPunctuation, 0);
	RequireProperties(0xbb, Category::FinalPunctuation, 0);
	RequireProperties(0xab, Category::InitialPunctuation, 0);
	RequireProperties(0x21, Category::OtherPunctuation, 0);
	RequireProperties(0x5e, Category::ModifierSymbol, 0);
	RequireProperties(0x2b, Category::MathSymbol, 0);
	RequireProperties(0xa9, Category::OtherSymbol, 0);
	RequireProperties(0x2028, Category::LineSeparator, 0);
	RequireProperties(0x2029, Category::ParagraphSeparator, 0);
}

static void TestCanonicalCombiningAndSpecialRanges() {
	using Category = sgre::text::UnicodeGeneralCategory;
	RequireProperties(0x301, Category::NonSpacingMark, 230);
	RequireProperties(0x327, Category::NonSpacingMark, 202);
	RequireProperties(0x315, Category::NonSpacingMark, 232);
	RequireProperties(0x345, Category::NonSpacingMark, 240);
	RequireProperties(0x334, Category::NonSpacingMark, 1);
	RequireProperties(0x316, Category::NonSpacingMark, 220);
	RequireProperties(0x5b0, Category::NonSpacingMark, 10);
	RequireProperties(0x3099, Category::NonSpacingMark, 8);
	RequireProperties(0x94d, Category::NonSpacingMark, 9);
	RequireProperties(0x35d, Category::NonSpacingMark, 234);
	RequireProperties(0x34f, Category::NonSpacingMark, 0);
	RequireProperties(0x200d, Category::Format, 0);
	RequireProperties(0xfeff, Category::Format, 0);
	RequireProperties(0x1f3fb, Category::ModifierSymbol, 0);
	RequireProperties(0xd800, Category::Surrogate, 0);
	RequireProperties(0xdfff, Category::Surrogate, 0);
	RequireProperties(0xe000, Category::PrivateUse, 0);
	RequireProperties(0xf0000, Category::PrivateUse, 0);
	RequireProperties(0x10fffd, Category::PrivateUse, 0);
	RequireProperties(0x10ffff, Category::Unassigned, 0);
}

static void TestRangeBoundaries() {
	using Category = sgre::text::UnicodeGeneralCategory;
	RequireProperties(0x1f, Category::Control, 0);
	RequireProperties(0x20, Category::SpaceSeparator, 0);
	RequireProperties(0x2ff, Category::ModifierSymbol, 0);
	RequireProperties(0x300, Category::NonSpacingMark, 230);
	RequireProperties(0x36f, Category::NonSpacingMark, 230);
	RequireProperties(0x370, Category::UppercaseLetter, 0);
	RequireProperties(0x378, Category::Unassigned, 0);
	RequireProperties(0xd7ff, Category::Unassigned, 0);
	RequireProperties(0xd800, Category::Surrogate, 0);
	RequireProperties(0xdfff, Category::Surrogate, 0);
	RequireProperties(0xe000, Category::PrivateUse, 0);
	RequireProperties(0xf8ff, Category::PrivateUse, 0);
	RequireProperties(0xf900, Category::OtherLetter, 0);
	RequireProperties(0xffff, Category::Unassigned, 0);
	RequireProperties(0x10000, Category::OtherLetter, 0);
	RequireProperties(0x10fffd, Category::PrivateUse, 0);
	RequireProperties(0x10fffe, Category::Unassigned, 0);
}

static void TestNativeOutOfRangeRecord() {
	using Category = sgre::text::UnicodeGeneralCategory;
	RequireProperties(0x110000, Category::Unassigned, 0);
	RequireProperties(0x80000000, Category::Unassigned, 0);
	RequireProperties(0xffffffff, Category::Unassigned, 0);
	RequireProperties(0x80000041, Category::Unassigned, 0);
}

static void WriteProperties(std::uint32_t first, std::uint32_t count) {
	for (std::uint32_t offset = 0; offset < count; ++offset) {
		const auto codepoint = first + offset;
		const auto value = sgre::text::QueryUnicodeProperties(codepoint);
		const std::array<std::uint8_t, 2> bytes{static_cast<std::uint8_t>(value.Category), value.CombiningClass};
		std::cout.write(reinterpret_cast<const char*>(bytes.data()), 2);
	}
}

static std::uint32_t ReadNumber(std::string_view text) {
	std::uint32_t value = 0;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
	Require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "invalid decimal Unicode query range");

	return value;
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--all") {
			WriteProperties(0, 0x110000);

			return 0;
		}

		if (argc == 4 && std::string_view(argv[1]) == "--range") {
			const auto first = ReadNumber(argv[2]);
			const auto count = ReadNumber(argv[3]);
			Require(count <= 4096 && (count == 0 || count - 1 <= 0xffffffff - first), "Unicode query range exceeds bounded output or wraps");
			WriteProperties(first, count);

			return 0;
		}

		Require(argc == 1, "unsupported Unicode fixture arguments");
		TestBasicCategories();
		TestCanonicalCombiningAndSpecialRanges();
		TestRangeBoundaries();
		TestNativeOutOfRangeRecord();
		std::cout << "Unicode category/combining properties: four groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
