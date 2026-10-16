#include "sgre/text/recursive_decomposition.hpp"

#include "sgre/text/unicode_properties.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct BufferAllocationProbe {
	static thread_local bool sm_Enabled;
	static thread_local std::uint32_t sm_FailureOrdinal;
	static thread_local std::uint32_t sm_Ordinal;
	static thread_local std::array<std::size_t, 32> sm_Requests;
};

thread_local bool BufferAllocationProbe::sm_Enabled = false;
thread_local std::uint32_t BufferAllocationProbe::sm_FailureOrdinal = 0;
thread_local std::uint32_t BufferAllocationProbe::sm_Ordinal = 0;
thread_local std::array<std::size_t, 32> BufferAllocationProbe::sm_Requests = {};

// Isolated test executable allocation boundary; the portable runtime receives no allocator injection API.
void* operator new(std::size_t size) {
	if (BufferAllocationProbe::sm_Enabled) {
		const auto ordinal = ++BufferAllocationProbe::sm_Ordinal;
		if (ordinal <= BufferAllocationProbe::sm_Requests.size()) {
			BufferAllocationProbe::sm_Requests[ordinal - 1] = size;
		}
		if (ordinal == BufferAllocationProbe::sm_FailureOrdinal) {
			throw std::bad_alloc();
		}
	}
	if (auto* memory = std::malloc(size == 0 ? 1 : size)) {
		return memory;
	}

	throw std::bad_alloc();
}

void operator delete(void* memory) noexcept {
	std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
	std::free(memory);
}

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void BeginProbe(std::uint32_t failure) {
	BufferAllocationProbe::sm_FailureOrdinal = failure;
	BufferAllocationProbe::sm_Ordinal = 0;
	BufferAllocationProbe::sm_Requests = {};
	BufferAllocationProbe::sm_Enabled = true;
}

static void TestPairedGrowthAndPartialFailure() {
	for (std::uint32_t failure : {0U, 1U, 2U}) {
		sgre::text::UnicodeNormalizationBuffer buffer;
		Require(buffer.UsesAllocatorGrowth() && buffer.State().MaximumLength == 0x3fffffff && buffer.State().ResidentCapacity == 0, "actual general constructor starts with unallocated arrays and native limits");
		BeginProbe(failure);
		const auto grown = buffer.EnsureInputCapacity(1);
		BufferAllocationProbe::sm_Enabled = false;
		Require(BufferAllocationProbe::sm_Ordinal == 2 && BufferAllocationProbe::sm_Requests[0] == 640 && BufferAllocationProbe::sm_Requests[1] == 640, "both paired allocations run in original order");
		const auto state = buffer.State();
		const auto metadata = buffer.InputMetadata();
		Require(grown == (failure == 0) && state.Failed == (failure != 0) && state.ResidentCapacity == (failure == 0 ? 32 : 0), "only complete paired growth publishes native capacity");
		Require(metadata.InputAllocationSlots == (failure == 2 ? 0 : 32) && metadata.AuxiliaryAllocationSlots == (failure == 1 ? 0 : 32), "successful partial arrays remain owned for teardown");
		buffer.ClearInput();
		Require(!buffer.State().Failed && buffer.State().ResidentCapacity == state.ResidentCapacity, "clear resets sticky failure and preserves published capacity");
		Require(buffer.EnsureInputCapacity(32) && buffer.State().ResidentCapacity == 80, "equal-to-capacity request grows using the original strict comparison");
	}

	sgre::text::UnicodeNormalizationBuffer buffer;
	Require(buffer.EnsureInputCapacity(1), "initial general growth succeeds");
	buffer.SetInputGrowthLimits(0, 17);
	Require(buffer.EnsureInputCapacity(31) && !buffer.State().Failed, "resident request precedes maximum-length check");
	Require(!buffer.EnsureInputCapacity(32) && buffer.State().Failed && buffer.EnsureInputCapacity(0) && buffer.EnsureInputCapacity(1), "resident request precedes sticky failure as well");
	buffer.ClearInput();
	Require(buffer.InputMetadata().MaximumOperations == 17 && buffer.State().MaximumLength == 0, "clear preserves both original limits");
}

static void TestUtf16ClustersAndContexts() {
	sgre::text::UnicodeNormalizationBuffer buffer;
	const std::u16string text = {u'A', 0xd83d, 0xde00, u'B', 0xd800, u'C', 0xdc00, u'D', u'E', u'F', u'G', u'H'};
	buffer.SetInputDirection(sgre::text::GlyphShapingDirection::RightToLeft);
	buffer.SetInputScript(0x6c61746e);
	buffer.AddUtf16(text, 3, 4);
	const auto glyphs = buffer.InputGlyphs();
	Require(glyphs.size() == 4 && glyphs[0].GlyphIndex == u'B' && glyphs[1].GlyphIndex == 0xfffd && glyphs[2].GlyphIndex == u'C' && glyphs[3].GlyphIndex == 0xfffd, "malformed surrogate ingestion follows the actual replacement policy");
	for (std::size_t index = 0; index < glyphs.size(); ++index) {
		Require(glyphs[index].Cluster == index + 3 && glyphs[index].Mask == 0 && glyphs[index].GlyphProperties == 0 && glyphs[index].PrivateState == 0 && glyphs[index].CharacterState == 0, "new records preserve UTF16 unit clusters and clear remaining native words");
	}
	const auto metadata = buffer.InputMetadata();
	Require(metadata.PrecedingContextCount == 2 && metadata.PrecedingContext[0] == 0x1f600 && metadata.PrecedingContext[1] == u'A', "preceding context stores nearest decoded scalars first");
	Require(metadata.FollowingContextCount == 5 && metadata.FollowingContext[0] == u'D' && metadata.FollowingContext[4] == u'H', "following context stores at most five forward scalars");
	buffer.InitializeInputUnicodeProperties();
	Require((buffer.InputGlyphs()[0].CharacterState & 0x1fU) == static_cast<std::uint32_t>(sgre::text::UnicodeGeneralCategory::UppercaseLetter), "actual default Unicode preprocessing consumes ingested owned records");
	buffer.ClearInput();
	Require(buffer.InputGlyphs().empty() && buffer.State().ResidentCapacity == 32 && buffer.InputMetadata().DirectionWord == 0 && buffer.InputMetadata().ScriptTag == 0 && buffer.InputMetadata().PrecedingContextCount == 0, "engine clear resets content and segment properties while retaining allocations");
	buffer.AddUtf16(text, 1, 2);
	Require(buffer.InputGlyphs().size() == 1 && buffer.InputGlyphs()[0].GlyphIndex == 0x1f600 && buffer.InputGlyphs()[0].Cluster == 1, "paired UTF16 input consumes two units under the original cluster");
	buffer.ResetInput();
	Require(buffer.InputMetadata().Content == sgre::text::UnicodeBufferContent::Unset && buffer.InputMetadata().ReplacementCodepoint == 0xfffd, "actual reset retains the engine-default replacement policy");
}

static void TestResidentCompatibilityAndBounds() {
	std::array<sgre::text::SubstitutionGlyph, 1> records;
	records[0].GlyphIndex = 0x41;
	records[0].Mask = 17;
	sgre::text::UnicodeNormalizationBuffer resident(records, 0);
	Require(!resident.UsesAllocatorGrowth() && !resident.EnsureInputCapacity(1) && resident.State().Failed && resident.InputGlyphs()[0].Mask == 17, "resident owner keeps its original capacity failure policy");
	sgre::text::UnicodeNormalizationBuffer buffer;
	bool rejected = false;
	try {
		buffer.AddUtf16(u"A", 1, 1);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && buffer.InputGlyphs().empty(), "invalid subrange is rejected before publication");
	rejected = false;
	try {
		buffer.SetInputDirection(static_cast<sgre::text::GlyphShapingDirection>(0));
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "unavailable direction is rejected at its supported input boundary");
	Require(!buffer.EnsureInputCapacity(0x0ccccccc) && buffer.State().Failed && buffer.InputMetadata().InputAllocationSlots == 0, "native arithmetic hard limit fails without allocation");
}

static bool ReadWord(std::uint32_t& value, bool allow_end = false) {
	std::array<char, 4> bytes;
	std::cin.read(bytes.data(), 4);
	if (allow_end && std::cin.gcount() == 0 && std::cin.eof()) {
		return false;
	}
	if (std::cin.gcount() != 4) {
		throw std::runtime_error("Unicode buffer message has a truncated word");
	}
	value = 0;
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		value |= std::uint32_t(static_cast<unsigned char>(bytes[index])) << (index * 8);
	}

	return true;
}

static void WriteWord(std::uint32_t value) {
	for (std::uint32_t index = 0; index < 4; ++index) {
		std::cout.put(std::bit_cast<char>(static_cast<std::uint8_t>(value & 0xffU)));
		value >>= 8;
	}
}

static void WriteGlyph(const sgre::text::SubstitutionGlyph& glyph) {
	WriteWord(glyph.GlyphIndex);
	WriteWord(glyph.Mask);
	WriteWord(glyph.Cluster);
	WriteWord(std::uint32_t(glyph.GlyphProperties) | (std::uint32_t(glyph.PrivateState) << 16));
	WriteWord(glyph.CharacterState);
}

static void WriteSnapshot(const sgre::text::UnicodeNormalizationBuffer& buffer, std::uint32_t result) {
	const auto state = buffer.State();
	const auto metadata = buffer.InputMetadata();
	WriteWord(result);
	WriteWord(state.InputCursor);
	WriteWord(state.InputCount);
	WriteWord(state.OutputCount);
	WriteWord(state.ResidentCapacity);
	WriteWord(state.MaximumLength);
	WriteWord(static_cast<std::uint32_t>(state.ScratchFlags));
	WriteWord(state.OutputActive);
	WriteWord(state.Failed);
	WriteWord(state.OutputAliasesInput);
	WriteWord(static_cast<std::uint32_t>(metadata.Content));
	WriteWord(metadata.TextFlags);
	WriteWord(metadata.ReplacementCodepoint);
	WriteWord(metadata.MaximumOperations);
	WriteWord(metadata.DirectionWord);
	WriteWord(metadata.ScriptTag);
	WriteWord(metadata.InputAllocationSlots);
	WriteWord(metadata.AuxiliaryAllocationSlots);
	WriteWord(metadata.PrecedingContextCount);
	WriteWord(metadata.FollowingContextCount);
	WriteWord(static_cast<std::uint32_t>(metadata.ClusterLevel));
	for (const auto word : metadata.PrecedingContext) {
		WriteWord(word);
	}
	for (const auto word : metadata.FollowingContext) {
		WriteWord(word);
	}
	for (const auto& glyph : buffer.InputGlyphs()) {
		WriteGlyph(glyph);
	}
	for (const auto& glyph : buffer.OutputGlyphs()) {
		WriteGlyph(glyph);
	}
	WriteWord(BufferAllocationProbe::sm_Ordinal);
	for (std::uint32_t index = 0; index < BufferAllocationProbe::sm_Ordinal; ++index) {
		WriteWord(static_cast<std::uint32_t>(BufferAllocationProbe::sm_Requests.at(index)));
	}
	std::cout.flush();
}

static void NativeMessages() {
	std::uint32_t count = 0;
	while (ReadWord(count, true)) {
		if (count > 4096) {
			throw std::runtime_error("Unicode buffer operation message exceeds its input limit");
		}
		sgre::text::UnicodeNormalizationBuffer buffer;
		for (std::uint32_t index = 0; index < count; ++index) {
			std::array<std::uint32_t, 4> fields;
			for (auto& value : fields) {
				ReadWord(value);
			}
			std::u16string text;
			if (fields[0] == 1) {
				if (fields[1] > 65536) {
					throw std::runtime_error("Unicode buffer text message exceeds its input limit");
				}
				text.resize(fields[1]);
				for (auto& unit : text) {
					std::uint32_t value = 0;
					ReadWord(value);
					if (value > 0xffff) {
						throw std::runtime_error("Unicode buffer message has an invalid UTF16 unit");
					}
					unit = static_cast<char16_t>(value);
				}
			}
			std::uint32_t failure = 0;
			ReadWord(failure);
			BeginProbe(failure);
			std::uint32_t result = 1;
			switch (fields[0]) {
			case 0:
				result = buffer.EnsureInputCapacity(fields[1]);
				break;
			case 1:
				buffer.AddUtf16(text, fields[2], fields[3]);
				result = !buffer.State().Failed;
				break;
			case 2:
				buffer.ClearInput();
				break;
			case 3:
				buffer.ResetInput();
				break;
			case 4:
				buffer.SetInputGrowthLimits(fields[1], fields[2]);
				break;
			case 5:
				buffer.SetInputDirection(static_cast<sgre::text::GlyphShapingDirection>(fields[1]));
				break;
			case 6:
				buffer.SetInputScript(fields[1]);
				break;
			case 7:
				buffer.BeginOutput();
				break;
			case 8:
				buffer.CopyCurrentInput();
				break;
			case 9:
				result = buffer.PublishOutput();
				break;
			case 10:
				buffer.InitializeInputUnicodeProperties();
				break;
			case 11:
				buffer.InitializeInputClusters(static_cast<sgre::text::GlyphClusterLevel>(fields[1]));
				break;
			case 12:
				result = static_cast<std::uint32_t>(buffer.AdjustInputDirection());
				break;
			default:
				throw std::runtime_error("Unicode buffer message has an invalid operation");
			}
			BufferAllocationProbe::sm_Enabled = false;
			WriteSnapshot(buffer, result);
		}
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			NativeMessages();

			return 0;
		}
		if (argc != 1) {
			throw std::runtime_error("unknown Unicode buffer proof argument");
		}
		TestPairedGrowthAndPartialFailure();
		TestUtf16ClustersAndContexts();
		TestResidentCompatibilityAndBounds();
		std::cout << "PASS 3 Unicode general-buffer groups\n";

		return 0;
	} catch (const std::exception& error) {
		BufferAllocationProbe::sm_Enabled = false;
		std::cerr << "text_unicode_buffer_input_tests: " << error.what() << '\n';

		return 1;
	}
}
