#include "sgre/text/layout_lookup_accelerator.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void WriteBig(std::vector<std::uint8_t>& bytes, std::size_t field, std::uint32_t value) {
	bytes.at(field) = static_cast<std::uint8_t>(value >> 8);
	bytes.at(field + 1) = static_cast<std::uint8_t>(value);
}

static std::vector<std::uint8_t> SingleCoverage(bool positioning, std::uint32_t format, std::uint32_t first, std::uint32_t last) {
	std::vector<std::uint8_t> bytes(format == 1 ? 34 : 38);
	WriteBig(bytes, 0, 1);
	WriteBig(bytes, 8, 10);
	WriteBig(bytes, 10, 1);
	WriteBig(bytes, 12, 4);
	WriteBig(bytes, 14, 1);
	WriteBig(bytes, 18, 1);
	WriteBig(bytes, 20, 8);
	WriteBig(bytes, 22, 1);
	WriteBig(bytes, 24, 6);
	WriteBig(bytes, 26, positioning ? 0 : 1);
	WriteBig(bytes, 28, format);
	WriteBig(bytes, 30, 1);
	WriteBig(bytes, 32, first);
	if (format == 2) {
		WriteBig(bytes, 34, last);
		WriteBig(bytes, 36, 0xffff);
	}

	return bytes;
}

static void TestAdmittedPointAndRangeBits() {
	const auto point = SingleCoverage(false, 1, 0xffff, 0);
	const sgre::text::GlyphSubstitutionBlob substitution(point);
	const auto values = sgre::text::BuildSubstitutionLookupAccelerators(substitution);
	Require(values.size() == 1 && values[0].GlyphsShift4 == 0x80000000 && values[0].GlyphsShift0 == 0x80000000 && values[0].GlyphsShift9 == 0x80000000, "native65535 coverage sets high bit in three planes");
	for (const bool positioning : {false, true}) {
		const auto bytes = SingleCoverage(positioning, 2, 30, 33);
		const sgre::text::GlyphSubstitutionBlob sub(bytes);
		const sgre::text::GlyphPositioningBlob pos(bytes);
		const auto masks = positioning ? sgre::text::BuildPositioningLookupAccelerators(pos) : sgre::text::BuildSubstitutionLookupAccelerators(sub);
		Require(masks.size() == 1 && masks[0].GlyphsShift4 == 6 && masks[0].GlyphsShift0 == 0xc0000003 && masks[0].GlyphsShift9 == 1, "range masks preserve cyclic bucket wrap");
	}
	const auto inverted = SingleCoverage(false, 2, 2, 1);
	const sgre::text::GlyphSubstitutionBlob unsorted(inverted);
	const auto backwards = sgre::text::BuildSubstitutionLookupAccelerators(unsorted);
	Require(backwards[0].GlyphsShift4 == 1 && backwards[0].GlyphsShift0 == 0xffffffff && backwards[0].GlyphsShift9 == 1, "native admitted inverted coverage range saturates only differing shifted buckets");
}

static void TestAdmissionAndEmptyLookupSlots() {
	auto bytes = SingleCoverage(false, 1, 5, 0);
	WriteBig(bytes, 30, 0xffff);
	const sgre::text::GlyphSubstitutionBlob repaired(bytes);
	const auto masked = sgre::text::BuildSubstitutionLookupAccelerators(repaired);
	Require(repaired.Status() == sgre::text::GlyphSubstitutionBlobStatus::Repaired && masked.size() == 1 && masked[0].GlyphsShift0 == 0, "accelerator consumes repaired coverage offset");
	WriteBig(bytes, 20, 0);
	const sgre::text::GlyphSubstitutionBlob erased(bytes);
	const auto empty_slot = sgre::text::BuildSubstitutionLookupAccelerators(erased);
	Require(empty_slot.size() == 1 && empty_slot[0].GlyphsShift4 == 0 && empty_slot[0].GlyphsShift0 == 0 && empty_slot[0].GlyphsShift9 == 0, "erased lookup offset retains original zero accelerator slot");
	WriteBig(bytes, 8, 0xffff);
	const sgre::text::GlyphSubstitutionBlob rejected(bytes);
	Require(sgre::text::BuildSubstitutionLookupAccelerators(rejected).empty(), "rejected table creates no accelerator slots");
	const sgre::text::GlyphPositioningBlob absent{std::span<const std::uint8_t>()};
	Require(sgre::text::BuildPositioningLookupAccelerators(absent).empty(), "missing positioning table has no slots");
	std::vector<std::uint8_t> repeated(70 + 0xffff * 2);
	WriteBig(repeated, 0, 1);
	WriteBig(repeated, 8, 10);
	WriteBig(repeated, 10, 20);
	for (std::size_t index = 0; index < 20; ++index) {
		WriteBig(repeated, 12 + index * 2, 42);
	}
	WriteBig(repeated, 52, 1);
	WriteBig(repeated, 56, 1);
	WriteBig(repeated, 58, 8);
	WriteBig(repeated, 60, 1);
	WriteBig(repeated, 62, 6);
	WriteBig(repeated, 66, 1);
	WriteBig(repeated, 68, 0xffff);
	const sgre::text::GlyphSubstitutionBlob repeated_coverage(repeated);
	Require(repeated_coverage.Status() == sgre::text::GlyphSubstitutionBlobStatus::Accepted, "count-only coverage admission is independent of accelerator work");
	bool work_rejected = false;
	try {
		const auto unsupported = sgre::text::BuildSubstitutionLookupAccelerators(repeated_coverage);
	} catch (const std::runtime_error&) {
		work_rejected = true;
	}
	Require(work_rejected, "repeated coverage references obey explicit accelerator work bound");
}

static bool ReadWord(std::uint32_t& value, bool allow_end = false) {
	std::array<char, 4> bytes{};
	std::cin.read(bytes.data(), 4);
	if (allow_end && std::cin.eof() && std::cin.gcount() == 0) {
		return false;
	}
	Require(std::cin.gcount() == 4, "truncated accelerator proof word");
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

static void WriteAccelerators(std::uint32_t status, const std::vector<sgre::text::LayoutLookupAccelerator>& accelerators) {
	WriteWord(status);
	WriteWord(static_cast<std::uint32_t>(accelerators.size()));
	for (const auto& accelerator : accelerators) {
		WriteWord(accelerator.GlyphsShift4);
		WriteWord(accelerator.GlyphsShift0);
		WriteWord(accelerator.GlyphsShift9);
	}
	std::cout.flush();
}

static void RunNativeProtocol() {
	std::uint32_t kind = 0;
	while (ReadWord(kind, true)) {
		std::uint32_t length = 0;
		std::uint32_t immutable = 0;
		ReadWord(length);
		ReadWord(immutable);
		Require(kind <= 1 && immutable <= 1 && length <= 2 * 1024 * 1024, "invalid accelerator proof domain");
		std::vector<std::uint8_t> bytes(length);
		for (auto& value : bytes) {
			const auto character = std::cin.get();
			Require(character != std::char_traits<char>::eof(), "truncated accelerator table input");
			value = static_cast<std::uint8_t>(character);
		}
		if (kind == 0) {
			const sgre::text::GlyphSubstitutionBlob blob(bytes, immutable != 0);
			WriteAccelerators(static_cast<std::uint32_t>(blob.Status()), sgre::text::BuildSubstitutionLookupAccelerators(blob));
		} else {
			const sgre::text::GlyphPositioningBlob blob(bytes, immutable != 0);
			WriteAccelerators(static_cast<std::uint32_t>(blob.Status()), sgre::text::BuildPositioningLookupAccelerators(blob));
		}
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			RunNativeProtocol();

			return 0;
		}
		Require(argc == 1, "unsupported accelerator proof argument");
		TestAdmittedPointAndRangeBits();
		TestAdmissionAndEmptyLookupSlots();
		std::cout << "PASS 2 layout accelerator groups\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
