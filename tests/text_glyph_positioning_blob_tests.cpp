#include "sgre/text/glyph_positioning_blob.hpp"

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

static std::vector<std::uint8_t> MakePositioning(std::uint32_t type, const std::vector<std::uint8_t>& subtable) {
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
	const auto bytes = MakePositioning(1, {0, 1, 0, 8, 0, 1, 0x7f, 0xff, 0, 1, 0, 1, 0, 5});
	const sgre::text::GlyphPositioningBlob clean(bytes);
	Require(clean.Status() == sgre::text::GlyphPositioningBlobStatus::Accepted && clean.Bytes().data() == bytes.data(), "clean GPOS borrows unchanged font bytes");
	auto damaged = bytes;
	WriteBig(damaged, 32, 0xffff);
	const sgre::text::GlyphPositioningBlob repaired(damaged);
	Require(repaired.Status() == sgre::text::GlyphPositioningBlobStatus::Repaired && repaired.Bytes().data() != damaged.data() && repaired.Bytes()[25] == 0 && damaged[25] == 8, "nested GPOS coverage repair owns duplicate without source changes");
	const sgre::text::GlyphPositioningBlob immutable(damaged, true);
	Require(immutable.Status() == sgre::text::GlyphPositioningBlobStatus::Rejected && immutable.Length() == 0, "immutable GPOS denies repairs");
	WriteBig(damaged, 8, 0xffff);
	const sgre::text::GlyphPositioningBlob fatal(damaged);
	Require(fatal.Status() == sgre::text::GlyphPositioningBlobStatus::Rejected, "root range failure produces empty GPOS");
	const sgre::text::GlyphPositioningBlob empty{std::span<const std::uint8_t>()};
	Require(empty.Status() == sgre::text::GlyphPositioningBlobStatus::Empty, "absent GPOS is empty");
}

static void TestNativeFormatsAndContextBounds() {
	for (const auto type : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 0xffffU}) {
		const auto unknown = MakePositioning(type, {0, 0x7f});
		const sgre::text::GlyphPositioningBlob blob(unknown);
		Require(blob.Status() == sgre::text::GlyphPositioningBlobStatus::Accepted, "unknown GPOS lookup/subtable format admission");
	}
	const auto context_bytes = MakePositioning(7, {0, 3, 0, 1, 0, 0, 0, 0});
	const sgre::text::GlyphPositioningBlob context(context_bytes);
	Require(context.Status() == sgre::text::GlyphPositioningBlobStatus::Accepted, "shared context format3 nonempty input");
	auto zero_input = context_bytes;
	WriteBig(zero_input, 24, 0);
	const sgre::text::GlyphPositioningBlob removed(zero_input);
	Require(removed.Status() == sgre::text::GlyphPositioningBlobStatus::Repaired && removed.Bytes()[13] == 0, "zero input removes whole positioning lookup");
}

static void TestExtensionAndSupportedWorkLimits() {
	const auto extension = MakePositioning(9, {0, 1, 0, 9, 0, 0, 0, 8, 0, 0x7f});
	const sgre::text::GlyphPositioningBlob accepted(extension);
	Require(accepted.Status() == sgre::text::GlyphPositioningBlobStatus::Accepted, "GPOS permits nested extension type9 unlike GSUB type7 gate");
	auto oversized_offset = extension;
	WriteBig(oversized_offset, 26, 0xffff);
	WriteBig(oversized_offset, 28, 0xffff);
	const sgre::text::GlyphPositioningBlob bounded(oversized_offset);
	Require(bounded.Status() == sgre::text::GlyphPositioningBlobStatus::Repaired && bounded.Bytes()[13] == 0, "unsigned extension offset outside remaining bytes removes lookup before addition");
	const auto device = MakePositioning(1, {0, 1, 0, 0, 0, 0x10, 0, 8, 0, 1, 0, 1, 0, 1});
	const sgre::text::GlyphPositioningBlob repaired(device);
	Require(repaired.Status() == sgre::text::GlyphPositioningBlobStatus::Repaired && repaired.Bytes()[29] == 0, "device range needs payload word and erases value offset");
	auto unknown_device = device;
	WriteBig(unknown_device, 34, 0x7fff);
	const sgre::text::GlyphPositioningBlob unknown(unknown_device);
	Require(unknown.Status() == sgre::text::GlyphPositioningBlobStatus::Accepted, "native unknown device format remains admitted");
	const auto reserved_format = MakePositioning(1, {0, 1, 0, 0, 0x80, 0, 0, 0});
	const sgre::text::GlyphPositioningBlob reserved(reserved_format);
	Require(reserved.Status() == sgre::text::GlyphPositioningBlobStatus::Accepted, "all16 format bits count words while only low byte selects devices");
	bool oversized_rejected = false;
	try {
		const std::vector<std::uint8_t> oversized(2 * 1024 * 1024 + 1);
		const sgre::text::GlyphPositioningBlob blob(oversized);
	} catch (const std::runtime_error&) {
		oversized_rejected = true;
	}
	Require(oversized_rejected, "supported GPOS admission byte bound");
}

static bool ReadWord(std::uint32_t& value, bool allow_end = false) {
	std::array<char, 4> bytes{};
	std::cin.read(bytes.data(), 4);
	if (allow_end && std::cin.eof() && std::cin.gcount() == 0) {
		return false;
	}
	Require(std::cin.gcount() == 4, "truncated GPOS proof word");
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
		Require(length <= 2 * 1024 * 1024, "oversized GPOS proof input");
		std::uint32_t immutable = 0;
		ReadWord(immutable);
		Require(immutable <= 1, "invalid GPOS proof immutability flag");
		std::vector<std::uint8_t> bytes(length);
		for (auto& value : bytes) {
			const auto character = std::cin.get();
			Require(character != std::char_traits<char>::eof(), "truncated GPOS proof bytes");
			value = static_cast<std::uint8_t>(character);
		}
		const sgre::text::GlyphPositioningBlob blob(bytes, immutable != 0);
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
		Require(argc == 1, "unsupported GPOS proof argument");
		TestAdmissionOwnershipAndBounds();
		TestNativeFormatsAndContextBounds();
		TestExtensionAndSupportedWorkLimits();
		std::cout << "PASS 3 GPOS blob groups\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
