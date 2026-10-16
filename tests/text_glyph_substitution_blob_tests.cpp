#include "sgre/text/glyph_substitution_blob.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
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

static void WriteBig(std::vector<std::uint8_t>& bytes, std::size_t field, std::uint32_t value) {
	bytes.at(field) = static_cast<std::uint8_t>(value >> 8);
	bytes.at(field + 1) = static_cast<std::uint8_t>(value);
}

static std::vector<std::uint8_t> MakeSubstitution(std::uint32_t type, const std::vector<std::uint8_t>& subtable) {
	std::vector<std::uint8_t> bytes(22 + subtable.size());
	WriteBig(bytes, 0, 1);
	WriteBig(bytes, 8, 10);
	WriteBig(bytes, 10, 1);
	WriteBig(bytes, 12, 4);
	WriteBig(bytes, 14, type);
	WriteBig(bytes, 18, 1);
	WriteBig(bytes, 20, 8);
	std::copy(subtable.begin(), subtable.end(), bytes.begin() + 22);

	return bytes;
}

static void TestAdmissionOwnershipAndBounds() {
	const auto bytes = MakeSubstitution(1, {0, 1, 0, 6, 0, 2, 0, 1, 0, 1, 0, 5});
	const sgre::text::GlyphSubstitutionBlob clean(bytes);
	Require(clean.Status() == sgre::text::GlyphSubstitutionBlobStatus::Accepted && clean.Bytes().data() == bytes.data() && clean.Length() == 34, "clean GSUB borrows unchanged immutable font bytes");
	auto damaged = bytes;
	WriteBig(damaged, 30, 0xffff);
	const sgre::text::GlyphSubstitutionBlob repaired(damaged);
	Require(repaired.Status() == sgre::text::GlyphSubstitutionBlobStatus::Repaired && repaired.Bytes().data() != damaged.data() && repaired.Bytes()[25] == 0 && damaged[25] == 6, "nested coverage repair owns a duplicate and preserves source");
	const sgre::text::GlyphSubstitutionBlob immutable(damaged, true);
	Require(immutable.Status() == sgre::text::GlyphSubstitutionBlobStatus::Rejected && immutable.Bytes().empty(), "native immutable input denies offset repair");
	WriteBig(damaged, 8, 0xffff);
	const sgre::text::GlyphSubstitutionBlob fatal(damaged);
	Require(fatal.Status() == sgre::text::GlyphSubstitutionBlobStatus::Rejected && fatal.Length() == 0, "root offset range failure replaces table with empty");
	const sgre::text::GlyphSubstitutionBlob empty{std::span<const std::uint8_t>()};
	Require(empty.Status() == sgre::text::GlyphSubstitutionBlobStatus::Empty, "absent GSUB remains empty");
}

static void TestNativeFormatsAndContextBounds() {
	for (const auto type : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 0xffffU}) {
		const auto unknown = MakeSubstitution(type, {0, 0x7f});
		const sgre::text::GlyphSubstitutionBlob blob(unknown);
		Require(blob.Status() == sgre::text::GlyphSubstitutionBlobStatus::Accepted, "unknown native lookup/subtable formats remain admitted");
	}
	const auto nonempty = MakeSubstitution(5, {0, 3, 0, 1, 0, 0, 0, 0});
	const sgre::text::GlyphSubstitutionBlob context(nonempty);
	Require(context.Status() == sgre::text::GlyphSubstitutionBlobStatus::Accepted, "format3 context accepts zero coverage offset with nonzero input count");
	auto no_input = nonempty;
	WriteBig(no_input, 24, 0);
	const sgre::text::GlyphSubstitutionBlob erased(no_input);
	Require(erased.Status() == sgre::text::GlyphSubstitutionBlobStatus::Repaired && erased.Bytes()[13] == 0, "zero input context removes lookup offset");
	auto null_subtable = nonempty;
	WriteBig(null_subtable, 20, 0);
	const sgre::text::GlyphSubstitutionBlob removed(null_subtable);
	Require(removed.Status() == sgre::text::GlyphSubstitutionBlobStatus::Repaired && removed.Bytes()[13] == 0, "null subtable invalidates whole lookup");
	const auto rule = MakeSubstitution(5, {0, 1, 0, 0, 0, 1, 0, 8, 0, 1, 0, 4, 0, 1, 0, 0});
	const sgre::text::GlyphSubstitutionBlob rule_erased(rule);
	Require(rule_erased.Status() == sgre::text::GlyphSubstitutionBlobStatus::Repaired && rule_erased.Bytes()[33] == 0, "native context rule requires extra first-glyph word");
}

static void TestExtensionAndSupportedWorkLimits() {
	const auto extension = MakeSubstitution(7, {0, 1, 0, 1, 0, 0, 0, 8, 0, 1, 0, 0, 0, 0});
	const sgre::text::GlyphSubstitutionBlob accepted(extension);
	Require(accepted.Status() == sgre::text::GlyphSubstitutionBlobStatus::Accepted, "extension follows original unsigned relative target");
	auto oversized_offset = extension;
	WriteBig(oversized_offset, 26, 0xffff);
	WriteBig(oversized_offset, 28, 0xffff);
	const sgre::text::GlyphSubstitutionBlob bounded(oversized_offset);
	Require(bounded.Status() == sgre::text::GlyphSubstitutionBlobStatus::Repaired && bounded.Bytes()[13] == 0, "unsigned extension offset outside remaining bytes removes lookup before addition");
	auto nested = extension;
	WriteBig(nested, 24, 7);
	const sgre::text::GlyphSubstitutionBlob rejected_nested(nested);
	Require(rejected_nested.Status() == sgre::text::GlyphSubstitutionBlobStatus::Repaired && rejected_nested.Bytes()[13] == 0, "lookup extension type7 is rejected after nested validation");
	bool oversized_rejected = false;
	try {
		const std::vector<std::uint8_t> oversized(2 * 1024 * 1024 + 1);
		const sgre::text::GlyphSubstitutionBlob blob(oversized);
	} catch (const std::runtime_error&) {
		oversized_rejected = true;
	}
	Require(oversized_rejected, "supported GSUB admission byte limit is explicit");
	std::vector<std::uint8_t> repeated(12 + 40 + 8 + 6 + 0xffff * 2);
	WriteBig(repeated, 0, 1);
	WriteBig(repeated, 8, 10);
	WriteBig(repeated, 10, 20);
	for (std::size_t index = 0; index < 20; ++index) {
		WriteBig(repeated, 12 + index * 2, 42);
	}
	WriteBig(repeated, 52, 5);
	WriteBig(repeated, 56, 1);
	WriteBig(repeated, 58, 8);
	WriteBig(repeated, 60, 3);
	WriteBig(repeated, 62, 0xffff);
	bool work_rejected = false;
	try {
		const sgre::text::GlyphSubstitutionBlob blob(repeated);
	} catch (const std::runtime_error&) {
		work_rejected = true;
	}
	Require(work_rejected, "repeated valid offsets cannot exceed supported validation work");
}

static bool ReadWord(std::uint32_t& value, bool allow_end = false) {
	std::array<char, 4> bytes{};
	std::cin.read(bytes.data(), 4);
	if (allow_end && std::cin.eof() && std::cin.gcount() == 0) {
		return false;
	}
	Require(std::cin.gcount() == 4, "truncated GSUB proof word");
	value = 0;
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		value |= std::uint32_t(static_cast<std::uint8_t>(bytes[index])) << (index * 8);
	}

	return true;
}

static void WriteWord(std::uint32_t value) {
	for (std::size_t index = 0; index < 4; ++index) {
		std::cout.put(static_cast<char>(value & 0xffU));
		value >>= 8;
	}
}

static void RunNativeProtocol() {
	std::uint32_t length = 0;
	while (ReadWord(length, true)) {
		Require(length <= 2 * 1024 * 1024, "oversized GSUB proof input");
		std::uint32_t immutable = 0;
		ReadWord(immutable);
		Require(immutable <= 1, "invalid GSUB proof immutability flag");
		std::vector<std::uint8_t> bytes(length);
		for (auto& value : bytes) {
			const auto character = std::cin.get();
			Require(character != std::char_traits<char>::eof(), "truncated GSUB proof bytes");
			value = static_cast<std::uint8_t>(character);
		}
		const sgre::text::GlyphSubstitutionBlob blob(bytes, immutable != 0);
		WriteWord(static_cast<std::uint32_t>(blob.Status()));
		WriteWord(blob.Length());
		for (const auto value : blob.Bytes()) {
			std::cout.put(static_cast<char>(value));
		}
		std::cout.flush();
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			RunNativeProtocol();

			return 0;
		}
		Require(argc == 1, "unsupported GSUB proof argument");
		TestAdmissionOwnershipAndBounds();
		TestNativeFormatsAndContextBounds();
		TestExtensionAndSupportedWorkLimits();
		std::cout << "PASS 3 GSUB blob groups\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
