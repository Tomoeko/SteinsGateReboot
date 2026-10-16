#include "sgre/text/recursive_decomposition.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::text {

	// Observation only: every phase/cursor/failure/tail condition comes from real public owner operations.
	struct UnicodeBufferPositionTestAccess {
		static std::span<const SubstitutionGlyph> PhysicalInput(const UnicodeNormalizationBuffer& buffer) {
			return buffer.m_Storage[buffer.m_InputStorage];
		}

		static std::span<const SubstitutionGlyph> PhysicalAuxiliary(const UnicodeNormalizationBuffer& buffer) {
			return buffer.m_Storage[1 - buffer.m_InputStorage];
		}
	};

} // namespace sgre::text

struct PositionAllocationProbe {
	static thread_local bool sm_Enabled;
	static thread_local std::uint32_t sm_FailureOrdinal;
	static thread_local std::uint32_t sm_Ordinal;
	static thread_local std::array<std::size_t, 4> sm_Requests;
};

thread_local bool PositionAllocationProbe::sm_Enabled = false;
thread_local std::uint32_t PositionAllocationProbe::sm_FailureOrdinal = 0;
thread_local std::uint32_t PositionAllocationProbe::sm_Ordinal = 0;
thread_local std::array<std::size_t, 4> PositionAllocationProbe::sm_Requests = {};

// Dedicated fixture executable boundary; no allocator injection reaches the runtime interface.
void* operator new(std::size_t size) {
	if (PositionAllocationProbe::sm_Enabled) {
		const auto ordinal = ++PositionAllocationProbe::sm_Ordinal;
		if (ordinal <= PositionAllocationProbe::sm_Requests.size()) {
			PositionAllocationProbe::sm_Requests[ordinal - 1] = size;
		}
		if (ordinal == PositionAllocationProbe::sm_FailureOrdinal) {
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

class PositionAllocationWindow {
public:
	explicit PositionAllocationWindow(std::uint32_t failure = 0) {
		PositionAllocationProbe::sm_FailureOrdinal = failure;
		PositionAllocationProbe::sm_Ordinal = 0;
		PositionAllocationProbe::sm_Requests = {};
		PositionAllocationProbe::sm_Enabled = true;
	}
	~PositionAllocationWindow() {
		PositionAllocationProbe::sm_Enabled = false;
	}
	PositionAllocationWindow(const PositionAllocationWindow&) = delete;
	PositionAllocationWindow& operator=(const PositionAllocationWindow&) = delete;
};

using Buffer = sgre::text::UnicodeNormalizationBuffer;
using Position = sgre::text::GlyphPositionRow;
using Observer = sgre::text::UnicodeBufferPositionTestAccess;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("position fixture expected checked storage rejection");
}

static std::vector<sgre::text::SubstitutionGlyph> Snapshot(std::span<const sgre::text::SubstitutionGlyph> rows) {
	return {rows.begin(), rows.end()};
}

static void TestLazyRowsAndNoAllocation() {
	for (std::uint32_t count : {1U, 31U, 32U, 33U}) {
		Buffer buffer;
		const std::u16string text(count, u'A');
		buffer.AddUtf16(text, 0, count);
		const auto glyphs = Snapshot(buffer.InputGlyphs());
		const auto before = buffer.State();
		const auto* input = Observer::PhysicalInput(buffer).data();
		const auto* auxiliary = Observer::PhysicalAuxiliary(buffer).data();
		Position first;
		{
			PositionAllocationWindow window;
			first = buffer.PositionRow(0);
			buffer.SetPositionRow(0, first);
			buffer.ClearPositionRows();
		}
		const auto after = buffer.State();
		Require(PositionAllocationProbe::sm_Ordinal == 0 && first == Position{}, "lazy value read/write/clear must allocate nothing");
		Require(after.PositionsActive && !after.OutputActive && after.OutputAliasesInput && after.OutputCount == 0, "position transition must use the actual output/auxiliary phase");
		Require(after.InputCount == before.InputCount && after.InputCursor == before.InputCursor && after.Failed == before.Failed && after.ResidentCapacity == before.ResidentCapacity && after.MaximumLength == before.MaximumLength, "position initialization must retain the original live/count/cursor/failure/capacity state");
		Require(Snapshot(buffer.InputGlyphs()) == glyphs && Observer::PhysicalInput(buffer).data() == input && Observer::PhysicalAuxiliary(buffer).data() == auxiliary, "position clear must preserve glyph infos and both actual allocations");
		Require(buffer.InputMetadata().Content == sgre::text::UnicodeBufferContent::Unicode, "position storage must not masquerade as successful glyph-content publication");
	}
}

static void TestAllFiveWordsAndRepeatedAccess() {
	Buffer buffer;
	buffer.AddUtf16(u"AB", 0, 2);
	RequireFailure([&]() {
		buffer.SetPositionRow(0, {});
	},
	    "active positions");
	buffer.ClearPositionRows();
	const Position row{std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max(), -64, std::bit_cast<std::int32_t>(0x89abcdefU), 0xa1b2c3d4U};
	buffer.SetPositionRow(0, row);
	const auto& raw = Observer::PhysicalAuxiliary(buffer)[0];
	Require(raw.GlyphIndex == 0x80000000U && raw.Mask == 0x7fffffffU && raw.Cluster == 0xffffffc0U && raw.GlyphProperties == 0xcdefU && raw.PrivateState == 0x89abU && raw.CharacterState == 0xa1b2c3d4U, "five logical DWORDs must retain signed bits, explicit16-bit fields and the entire private word");
	Require(buffer.PositionRow(0) == row && buffer.PositionRow(0) == row && buffer.PositionRow(1) == Position{}, "repeated lazy reads must retain computed rows rather than clear them again");
	auto copied = buffer.PositionRow(0);
	copied.PrivateWord = 0;
	Require(copied.PrivateWord == 0 && buffer.PositionRow(0).PrivateWord == 0xa1b2c3d4U, "a returned typed value must not alias or mutate its buffer owner");
	buffer.ClearPositionRows();
	Require(buffer.PositionRow(0) == Position{} && buffer.PositionRow(1) == Position{}, "explicit clear must zero all five words even when positions were active");
}

static void TestRealTailCursorAndStickyFailure() {
	Buffer buffer;
	buffer.AddUtf16(u"ABCDE", 0, 5);
	buffer.ClearPositionRows();
	for (std::uint32_t index = 0; index < 5; ++index) {
		buffer.SetPositionRow(index, Position{static_cast<std::int32_t>(index + 1), -7, 11, -13, 0xa1b20000U + index});
	}
	const auto earlier = Snapshot(Observer::PhysicalAuxiliary(buffer));
	buffer.ClearInput();
	buffer.AddUtf16(u"AB", 0, 2);
	buffer.BeginOutput();
	buffer.CopyCurrentInput();
	buffer.SetInputGrowthLimits(0, 17);
	Require(!buffer.EnsureInputCapacity(32), "real maximum-length failure must set the sticky failure before clearing positions");
	const auto before = buffer.State();
	Require(before.InputCursor == 1 && before.OutputCount == 1 && before.OutputActive && !before.PositionsActive && before.Failed, "cursor and failure fixture prerequisites must come from actual public operations");
	const auto glyphs = Snapshot(buffer.InputGlyphs());
	const auto metadata = buffer.InputMetadata();
	buffer.ClearPositionRows();
	const auto after = buffer.State();
	const auto physical = Observer::PhysicalAuxiliary(buffer);
	Require(after.InputCursor == 1 && after.InputCount == 2 && after.Failed && after.PositionsActive && !after.OutputActive && after.OutputCount == 0 && after.ResidentCapacity == before.ResidentCapacity && after.ScratchFlags == before.ScratchFlags, "clear must preserve cursor/sticky failure/count/scratch and only retire output");
	Require(Snapshot(buffer.InputGlyphs()) == glyphs && buffer.InputMetadata().InputAllocationSlots == metadata.InputAllocationSlots && buffer.InputMetadata().AuxiliaryAllocationSlots == metadata.AuxiliaryAllocationSlots && buffer.InputMetadata().MaximumOperations == 17, "actual input records, physical allocations and configured limits must survive clear");
	Require(physical[0] == sgre::text::SubstitutionGlyph{} && physical[1] == sgre::text::SubstitutionGlyph{} && std::equal(physical.begin() + 2, physical.end(), earlier.begin() + 2), "shorter real live prefix clears without changing the previously written tail");
	buffer.SetPositionRow(0, Position{1, 2, 3, 4, 5});
	buffer.BeginOutput();
	Require(!buffer.State().PositionsActive && buffer.State().OutputActive && buffer.State().Failed, "real output restart must retire positions while retaining sticky failure");
	Require(buffer.PositionRow(0) == Position{} && buffer.State().InputCursor == 1 && buffer.State().Failed, "lazy access after output restart clears once and retains cursor/error");
}

static void TestPairedGrowthWithLivePositions() {
	for (std::uint32_t failure : {0U, 1U, 2U}) {
		Buffer buffer;
		buffer.AddUtf16(u"AB", 0, 2);
		buffer.ClearPositionRows();
		const Position row{-17, 29, -31, 43, 0xfedcba98U};
		buffer.SetPositionRow(0, row);
		const auto glyphs = Snapshot(buffer.InputGlyphs());
		bool grown = false;
		{
			PositionAllocationWindow window(failure);
			grown = buffer.EnsureInputCapacity(32);
		}
		const auto metadata = buffer.InputMetadata();
		Require(PositionAllocationProbe::sm_Ordinal == 2 && PositionAllocationProbe::sm_Requests[0] == 80 * sizeof(sgre::text::SubstitutionGlyph) && PositionAllocationProbe::sm_Requests[1] == PositionAllocationProbe::sm_Requests[0], "both original paired growth requests must run auxiliary first, including partial failure");
		Require(grown == (failure == 0) && buffer.State().Failed == (failure != 0) && buffer.State().ResidentCapacity == (failure == 0 ? 80U : 32U), "complete growth alone publishes capacity while partial adoption retains failure");
		Require(metadata.AuxiliaryAllocationSlots == (failure == 1 ? 32U : 80U) && metadata.InputAllocationSlots == (failure == 2 ? 32U : 80U), "each successfully reallocated original array must remain adopted on partial failure");
		Require(buffer.State().PositionsActive && buffer.PositionRow(0) == row && Snapshot(buffer.InputGlyphs()) == glyphs, "growth must retain actual live glyph and position words without another clear");
		buffer.ClearPositionRows();
		Require(buffer.PositionRow(0) == Position{} && buffer.State().Failed == (failure != 0), "position clear must also preserve sticky partial-allocation failure");
	}
}

static void TestEmptyCheckedIndicesAndClearReset() {
	Buffer buffer;
	RequireFailure([&]() {
		static_cast<void>(buffer.PositionRow(0));
	},
	    "live owned rows");
	Require(!buffer.State().PositionsActive, "invalid value index must reject before a lazy transition");
	{
		PositionAllocationWindow window;
		buffer.ClearPositionRows();
	}
	Require(buffer.State().PositionsActive && PositionAllocationProbe::sm_Ordinal == 0 && buffer.InputMetadata().AuxiliaryAllocationSlots == 0, "empty explicit clear marks storage phase without allocating an array");
	buffer.ClearInput();
	Require(!buffer.State().PositionsActive && buffer.State().InputCount == 0, "clear must retire logical position availability");
	buffer.AddUtf16(u"AB", 0, 2);
	buffer.ClearPositionRows();
	buffer.SetPositionRow(0, Position{3, 5, 7, 11, 13});
	const auto physical = Snapshot(Observer::PhysicalAuxiliary(buffer));
	RequireFailure([&]() {
		buffer.SetPositionRow(2, {});
	},
	    "live owned rows");
	RequireFailure([&]() {
		static_cast<void>(buffer.PositionRow(std::numeric_limits<std::uint32_t>::max()));
	},
	    "live owned rows");
	Require(Snapshot(Observer::PhysicalAuxiliary(buffer)) == physical, "invalid checked read/write must preserve real position bytes");
	Require(buffer.PublishOutput() && buffer.State().InputCount == 0 && buffer.State().OutputCount == 2 && buffer.State().PositionsActive, "original publication changes output flag/counts but retains the separate positions byte");
	buffer.ResetInput();
	Require(!buffer.State().PositionsActive && !buffer.State().Failed && buffer.InputMetadata().Content == sgre::text::UnicodeBufferContent::Unset && Snapshot(Observer::PhysicalAuxiliary(buffer)) == physical, "reset must retire position availability while preserving both physical arrays");
}

static void TestUnindexedEmptyInitialization() {
	Buffer buffer;
	RequireFailure([&]() {
		static_cast<void>(buffer.PositionRow(0));
	},
	    "live owned rows");
	Require(!buffer.State().PositionsActive, "empty indexed read still rejects before position initialization");
	{
		PositionAllocationWindow window;
		buffer.EnsurePositionRows();
		buffer.EnsurePositionRows();
	}
	Require(buffer.State().PositionsActive && !buffer.State().OutputActive && buffer.State().OutputAliasesInput && PositionAllocationProbe::sm_Ordinal == 0, "unindexed access initializes the actual empty owner without allocation");
	Require(Observer::PhysicalInput(buffer).empty() && Observer::PhysicalAuxiliary(buffer).empty(), "empty initialization must not manufacture either allocation");
	RequireFailure([&]() {
		static_cast<void>(buffer.PositionRow(0));
	},
	    "live owned rows");
	Require(buffer.State().PositionsActive, "checked empty index rejection retains prior explicit initialization");
	buffer.BeginOutput();
	Require(!buffer.State().PositionsActive && buffer.State().OutputActive, "actual output restart retires empty position availability");
	buffer.EnsurePositionRows();
	Require(buffer.State().PositionsActive && !buffer.State().OutputActive && buffer.State().InputCount == 0, "unindexed access restores the genuine empty position phase");
}

static void TestUnindexedRepeatAndRestartPreserveKnownTail() {
	Buffer buffer;
	buffer.AddUtf16(u"ABCDE", 0, 5);
	buffer.EnsurePositionRows();
	for (std::uint32_t index = 0; index < 5; ++index) {
		buffer.SetPositionRow(index, Position{static_cast<std::int32_t>(index + 1), -7, 11, -13, 0xa1b20000U + index});
	}
	const auto earlier = Snapshot(Observer::PhysicalAuxiliary(buffer));
	buffer.ClearInput();
	buffer.AddUtf16(u"AB", 0, 2);
	buffer.BeginOutput();
	buffer.CopyCurrentInput();
	buffer.SetInputGrowthLimits(0, 17);
	Require(!buffer.EnsureInputCapacity(32), "real maximum failure supplies the unindexed sticky-error fixture");
	buffer.EnsurePositionRows();
	const Position row{1, -2, 3, -4, 0xfedcba98U};
	buffer.SetPositionRow(0, row);
	const auto initialized = Snapshot(Observer::PhysicalAuxiliary(buffer));
	const auto glyphs = Snapshot(buffer.InputGlyphs());
	const auto before = buffer.State();
	{
		PositionAllocationWindow window;
		buffer.EnsurePositionRows();
	}
	Require(PositionAllocationProbe::sm_Ordinal == 0 && buffer.PositionRow(0) == row && Snapshot(Observer::PhysicalAuxiliary(buffer)) == initialized, "repeated unindexed access preserves all words rather than reinitializing storage");
	Require(buffer.State().InputCursor == 1 && buffer.State().Failed && buffer.State().ResidentCapacity == before.ResidentCapacity, "repeat preserves actual cursor, failure and adopted capacity");
	buffer.BeginOutput();
	buffer.EnsurePositionRows();
	const auto physical = Observer::PhysicalAuxiliary(buffer);
	Require(buffer.PositionRow(0) == Position{} && buffer.PositionRow(1) == Position{} && std::equal(physical.begin() + 2, physical.end(), earlier.begin() + 2), "output restart then unindexed access reclears only the shorter live prefix");
	Require(buffer.State().PositionsActive && !buffer.State().OutputActive && buffer.State().InputCursor == 1 && buffer.State().Failed && buffer.State().MaximumLength == 0 && buffer.InputMetadata().MaximumOperations == 17 && Snapshot(buffer.InputGlyphs()) == glyphs, "restart initialization retains actual input, limits, cursor and failure");
}

int main() {
	try {
		TestLazyRowsAndNoAllocation();
		TestAllFiveWordsAndRepeatedAccess();
		TestRealTailCursorAndStickyFailure();
		TestPairedGrowthWithLivePositions();
		TestEmptyCheckedIndicesAndClearReset();
		TestUnindexedEmptyInitialization();
		TestUnindexedRepeatAndRestartPreserveKnownTail();
		std::cout << "Unicode buffer position storage fixtures passed7groups\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
