#include "sgre/text/glyph_definition_blob.hpp"
#include "sgre/text/recursive_decomposition.hpp"

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

static void WriteBig(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value, std::size_t count = 2) {
	for (std::size_t index = 0; index < count; ++index) {
		bytes.at(offset + count - index - 1) = static_cast<std::uint8_t>(value & 0xffU);
		value >>= 8;
	}
}

static std::vector<std::uint8_t> MakeDefinition(std::uint32_t profile) {
	Require(profile < 6, "unknown GDEF blob fixture profile");
	std::vector<std::uint8_t> bytes(40);
	WriteBig(bytes, 0, 0x10000, 4);
	WriteBig(bytes, 4, 12);
	WriteBig(bytes, 10, 26);
	WriteBig(bytes, 12, 1);
	WriteBig(bytes, 14, 4);
	WriteBig(bytes, 16, 4);
	for (std::uint32_t index = 0; index < 4; ++index) {
		WriteBig(bytes, 18 + index * 2, index + 1);
	}
	WriteBig(bytes, 26, 1);
	WriteBig(bytes, 28, 4);
	WriteBig(bytes, 30, 4);
	WriteBig(bytes, 32, 0xffff);
	WriteBig(bytes, 34, 0x1234);
	WriteBig(bytes, 36, 0xabcd);
	WriteBig(bytes, 38, 0);
	if (profile == 1) {
		WriteBig(bytes, 16, 0xffff);
	} else if (profile == 2) {
		WriteBig(bytes, 4, 0xffff);
	} else if (profile == 3) {
		WriteBig(bytes, 0, 0x20000, 4);
	} else if (profile == 4) {
		bytes.resize(0x1ba);
		WriteBig(bytes, 20, 3);
	} else if (profile == 5) {
		bytes.resize(0x382);
	}

	return bytes;
}

static void TestAdmissionAndOwnedRepair() {
	const auto bytes = MakeDefinition(0);
	const sgre::text::GlyphDefinitionBlob blob(bytes);
	Require(blob.Status() == sgre::text::GlyphDefinitionBlobStatus::Accepted && blob.Bytes().data() == bytes.data() && blob.Length() == 40, "clean GDEF remains borrowed without duplication");
	Require(blob.GlyphProperties(4) == 2 && blob.GlyphProperties(5) == 4 && blob.GlyphProperties(6) == 0xcd08 && blob.GlyphProperties(7) == 0 && blob.GlyphProperties(0x10006) == 0, "actual GDEF class and attachment narrowing");
	const auto malformed = MakeDefinition(1);
	const sgre::text::GlyphDefinitionBlob repaired(malformed);
	Require(repaired.Status() == sgre::text::GlyphDefinitionBlobStatus::Repaired && repaired.Length() == malformed.size() && repaired.Bytes().data() != malformed.data() && repaired.Bytes()[4] == 0 && repaired.Bytes()[5] == 0 && malformed[5] == 12 && !repaired.HasGlyphClasses(), "repair owns a duplicate and preserves borrowed bytes");
	const sgre::text::GlyphDefinitionBlob immutable(malformed, true);
	Require(immutable.Status() == sgre::text::GlyphDefinitionBlobStatus::Rejected && immutable.Bytes().empty(), "immutable input refuses an edit request");
}

static void TestFatalBoundsAndEmpty() {
	for (const auto profile : {2U, 3U}) {
		const auto bytes = MakeDefinition(profile);
		const sgre::text::GlyphDefinitionBlob blob(bytes);
		Require(blob.Status() == sgre::text::GlyphDefinitionBlobStatus::Rejected && blob.Length() == 0 && !blob.HasGlyphClasses() && blob.GlyphClass(6) == 0, "invalid major or out-of-range offset replaces data with empty blob");
	}
	const sgre::text::GlyphDefinitionBlob empty{std::span<const std::uint8_t>()};
	Require(empty.Status() == sgre::text::GlyphDefinitionBlobStatus::Empty && empty.Bytes().empty(), "absent GDEF is empty");
	bool rejected = false;
	try {
		const std::vector<std::uint8_t> oversized(2 * 1024 * 1024 + 1);
		const sgre::text::GlyphDefinitionBlob blob(oversized);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "GDEF admission byte limit");
}

static void TestSanitizedSuppression() {
	const auto bytes = MakeDefinition(5);
	sgre::text::GlyphDefinitionBlob blob(bytes);
	blob.ApplyLayoutSuppression(0x310a, 0xb586);
	Require(blob.IsSuppressed() && !blob.HasGlyphClasses() && blob.GlyphProperties(4) == 0 && blob.Length() == 0x382 && blob.Bytes().data() == bytes.data(), "unconditional suppression keeps immutable owned blob data");
	const auto conditional = MakeDefinition(4);
	sgre::text::GlyphDefinitionBlob marked(conditional);
	marked.ApplyLayoutSuppression(0xb3a, 0xa436);
	Require(marked.IsSuppressed() && marked.Length() == 0x1ba, "conditional class5 mark suppression");
	marked.ApplyLayoutSuppression(0xb3a, 0xa435);
	Require(!marked.IsSuppressed(), "suppression uses exact sanitized length triple");
}

static void TestLiveRecordEntry() {
	const auto bytes = MakeDefinition(0);
	const sgre::text::GlyphDefinitionBlob definition(bytes);
	std::array<sgre::text::SubstitutionGlyph, 3> input;
	for (std::uint32_t index = 0; index < input.size(); ++index) {
		input[index].GlyphIndex = index + 4;
		input[index].Mask = 0xaabbccdd;
		input[index].Cluster = 0x80000000 + index;
		input[index].GlyphProperties = 0x1234;
		input[index].PrivateState = 0xffff;
		input[index].CharacterState = 0xaabbccdd;
	}
	sgre::text::UnicodeNormalizationBuffer buffer(input, 3);
	buffer.InitializeGlyphDefinitionProperties(definition, sgre::text::NormalizedGlyphContent::GlyphIndices);
	const auto output = buffer.InputGlyphs();
	Require(output[0].GlyphProperties == 2 && output[1].GlyphProperties == 4 && output[2].GlyphProperties == 0xcd08, "late GDEF entry publishes admitted class properties");
	for (std::size_t index = 0; index < input.size(); ++index) {
		Require(output[index].PrivateState == 0 && output[index].GlyphIndex == input[index].GlyphIndex && output[index].Mask == input[index].Mask && output[index].Cluster == input[index].Cluster && output[index].CharacterState == input[index].CharacterState, "GDEF entry clears two private bytes and preserves other record words");
	}
	Require(buffer.State().InputCount == 3 && !buffer.State().OutputActive && !buffer.State().Failed, "GDEF entry preserves buffer state");
	bool rejected = false;
	try {
		buffer.InitializeGlyphDefinitionProperties(definition, static_cast<sgre::text::NormalizedGlyphContent>(1));
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && buffer.InputGlyphs()[2].GlyphProperties == 0xcd08, "GDEF entry requires final caller mapping before mutation");
}

static void TestEmptyAndSuppressedEntryContinuation() {
	std::array<sgre::text::SubstitutionGlyph, 3> input;
	for (std::uint32_t index = 0; index < input.size(); ++index) {
		input[index].GlyphIndex = 4 + index;
		input[index].Mask = 0xfdaabbcc;
		input[index].Cluster = 0xffffffff - index;
		input[index].GlyphProperties = 0xffff;
		input[index].PrivateState = 0x1234;
		const std::array<std::uint32_t, 3> states = {12, 0x2c, 10};
		input[index].CharacterState = 0xaabb0000 | states[index];
	}
	const sgre::text::GlyphDefinitionBlob empty{std::span<const std::uint8_t>()};
	const auto bytes = MakeDefinition(5);
	sgre::text::GlyphDefinitionBlob suppressed(bytes);
	suppressed.ApplyLayoutSuppression(0x310a, 0xb586);
	for (const auto* definition : {&empty, static_cast<const sgre::text::GlyphDefinitionBlob*>(&suppressed)}) {
		sgre::text::UnicodeNormalizationBuffer buffer(input, 3);
		buffer.InitializeGlyphDefinitionProperties(*definition, sgre::text::NormalizedGlyphContent::GlyphIndices);
		Require(buffer.InputGlyphs()[0].GlyphProperties == 8 && buffer.InputGlyphs()[1].GlyphProperties == 2 && buffer.InputGlyphs()[2].GlyphProperties == 2, "effective-empty GDEF continuation uses nonignored Mn as mark and others as base");
		for (std::size_t index = 0; index < input.size(); ++index) {
			Require(buffer.InputGlyphs()[index].PrivateState == 0 && buffer.InputGlyphs()[index].Mask == input[index].Mask && buffer.InputGlyphs()[index].Cluster == input[index].Cluster && buffer.InputGlyphs()[index].CharacterState == input[index].CharacterState, "effective-empty continuation preserves masks/clusters/character state");
		}
	}
	auto unknown_bytes = MakeDefinition(0);
	WriteBig(unknown_bytes, 12, 99);
	const sgre::text::GlyphDefinitionBlob unknown(unknown_bytes);
	sgre::text::UnicodeNormalizationBuffer buffer(input, 3);
	buffer.InitializeGlyphDefinitionProperties(unknown, sgre::text::NormalizedGlyphContent::GlyphIndices);
	Require(unknown.HasGlyphClasses() && buffer.InputGlyphs()[0].GlyphProperties == 0, "unknown ClassDef format with nonzero offset suppresses Unicode fallback just like native presence query");
}

static std::uint32_t ReadWord() {
	std::uint32_t result = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto value = std::cin.get();
		Require(value != std::char_traits<char>::eof(), "truncated GDEF blob proof word");
		result |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(value)) << shift;
	}

	return result;
}

static void WriteWord(std::uint32_t value) {
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		std::cout.put(static_cast<char>((value >> shift) & 0xffU));
	}
}

static void RunNative() {
	while (std::cin.peek() != std::char_traits<char>::eof()) {
		const auto length = ReadWord();
		const auto immutable = ReadWord();
		const auto substitution = ReadWord();
		const auto positioning = ReadWord();
		const auto count = ReadWord();
		Require(length <= 2 * 1024 * 1024 && immutable <= 1 && count <= 65536, "GDEF blob proof exceeds bounds");
		std::vector<std::uint8_t> bytes(length);
		for (auto& byte : bytes) {
			const auto value = std::cin.get();
			Require(value != std::char_traits<char>::eof(), "truncated GDEF blob proof bytes");
			byte = static_cast<std::uint8_t>(value);
		}
		sgre::text::GlyphDefinitionBlob blob(bytes, immutable != 0);
		blob.ApplyLayoutSuppression(substitution, positioning);
		WriteWord(static_cast<std::uint32_t>(blob.Status()));
		WriteWord(blob.Length());
		WriteWord(static_cast<std::uint32_t>(blob.IsSuppressed()));
		WriteWord(static_cast<std::uint32_t>(blob.HasGlyphClasses()));
		for (const auto byte : blob.Bytes()) {
			std::cout.put(static_cast<char>(byte));
		}
		std::vector<sgre::text::SubstitutionGlyph> records(count);
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto glyph = ReadWord();
			WriteWord(blob.GlyphClass(glyph));
			WriteWord(blob.GlyphProperties(glyph));
			records[index].GlyphIndex = glyph;
			records[index].Mask = 0x12345679 + index;
			records[index].Cluster = 0xffffffff - index;
			records[index].GlyphProperties = 0x1234;
			records[index].PrivateState = 0xabcd;
			records[index].CharacterState = 0xaabbcc00U | (index % 4 == 0 ? 12U : index % 4 == 1 ? 11U
			                                                      : index % 4 == 2               ? 10U
			                                                                                     : 5U) |
			    ((index & 4U) != 0 ? 0x20U : 0U);
		}
		sgre::text::UnicodeNormalizationBuffer buffer(records, count);
		buffer.BeginOutput();
		if (count != 0) {
			buffer.CopyCurrentInput();
		}
		buffer.InitializeGlyphDefinitionProperties(blob, sgre::text::NormalizedGlyphContent::GlyphIndices);
		for (const auto& glyph : buffer.InputGlyphs()) {
			WriteWord(glyph.GlyphIndex);
			WriteWord(glyph.Mask);
			WriteWord(glyph.Cluster);
			WriteWord(static_cast<std::uint32_t>(glyph.GlyphProperties) | (static_cast<std::uint32_t>(glyph.PrivateState) << 16));
			WriteWord(glyph.CharacterState);
		}
		std::cout.flush();
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			RunNative();
		} else if (argc == 3 && std::string_view(argv[1]) == "--fixture") {
			const auto bytes = MakeDefinition(static_cast<std::uint32_t>(std::stoul(argv[2])));
			for (const auto byte : bytes) {
				std::cout.put(static_cast<char>(byte));
			}
		} else if (argc == 1) {
			TestAdmissionAndOwnedRepair();
			TestFatalBoundsAndEmpty();
			TestSanitizedSuppression();
			TestLiveRecordEntry();
			TestEmptyAndSuppressedEntryContinuation();
			std::cout << "PASS 5 GDEF blob groups\n";
		} else {
			throw std::runtime_error("unknown GDEF blob proof arguments");
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
