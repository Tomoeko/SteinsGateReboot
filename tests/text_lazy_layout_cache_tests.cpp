#include "sgre/text/blob_font_layout_cache.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(!std::is_default_constructible_v<sgre::text::BlobFontLayoutCache::ConstructionKey>);
static_assert(!std::is_constructible_v<sgre::text::BlobFontLayoutCache, sgre::text::BlobFontFace&>);

// Standalone proof boundary only: runtime code receives ordinary standard allocation failures.
static bool g_RecordAllocations = false;
static std::size_t g_AllocationCount = 0;
static std::size_t g_FailureOrdinal = 0;
static std::array<std::size_t, 64> g_AllocationSizes = {};

void* operator new(std::size_t size) {
	if (g_RecordAllocations) {
		if (g_AllocationCount >= g_AllocationSizes.size()) {
			throw std::bad_alloc();
		}
		g_AllocationSizes[g_AllocationCount++] = size;
		if (g_AllocationCount == g_FailureOrdinal) {
			throw std::bad_alloc();
		}
	}
	if (void* pointer = std::malloc(size == 0 ? 1 : size)) {
		return pointer;
	}

	throw std::bad_alloc();
}

void operator delete(void* pointer) noexcept {
	std::free(pointer);
}

void* operator new[](std::size_t size) {
	return ::operator new(size);
}

void operator delete[](void* pointer) noexcept {
	::operator delete(pointer);
}

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void WriteBig(std::vector<std::uint8_t>& bytes, std::size_t field, std::uint32_t value, std::size_t width = 2) {
	for (std::size_t index = 0; index < width; ++index) {
		bytes.at(field + width - index - 1) = static_cast<std::uint8_t>(value & 0xffU);
		value >>= 8;
	}
}

static std::vector<std::uint8_t> PointLayout() {
	std::vector<std::uint8_t> bytes(34);
	const std::array<std::uint16_t, 17> words = {1, 0, 0, 0, 10, 1, 4, 1, 0, 1, 8, 1, 6, 0, 1, 1, 5};
	for (std::size_t index = 0; index < words.size(); ++index) {
		WriteBig(bytes, index * 2, words[index]);
	}

	return bytes;
}

static std::vector<std::uint8_t> MakeFont(std::span<const std::uint8_t> definition, std::span<const std::uint8_t> substitution, std::span<const std::uint8_t> positioning) {
	const std::array<std::uint32_t, 3> tags = {0x47444546, 0x47535542, 0x47504f53};
	const std::array<std::span<const std::uint8_t>, 3> tables = {definition, substitution, positioning};
	std::vector<std::uint8_t> bytes(60);
	WriteBig(bytes, 0, 0x10000, 4);
	WriteBig(bytes, 4, 3);
	for (std::size_t index = 0; index < tables.size(); ++index) {
		WriteBig(bytes, 12 + index * 16, tags[index], 4);
		WriteBig(bytes, 20 + index * 16, static_cast<std::uint32_t>(bytes.size()), 4);
		WriteBig(bytes, 24 + index * 16, static_cast<std::uint32_t>(tables[index].size()), 4);
		bytes.insert(bytes.end(), tables[index].begin(), tables[index].end());
	}

	return bytes;
}

static bool PrepareWithFailure(sgre::text::BlobFontFace& face, std::size_t failure) {
	g_AllocationCount = 0;
	g_FailureOrdinal = failure;
	g_RecordAllocations = true;
	try {
		const bool result = face.PrepareLayout();
		g_RecordAllocations = false;

		return result;
	} catch (...) {
		g_RecordAllocations = false;
		throw;
	}
}

static void TestLazyPublicationAndLifetime() {
	const auto point = PointLayout();
	auto pin = std::make_shared<const std::vector<std::uint8_t>>(MakeFont({}, point, point));
	const std::weak_ptr<const std::vector<std::uint8_t>> observer = pin;
	auto face = std::make_unique<sgre::text::BlobFontFace>(pin);
	pin.reset();
	Require(face->LayoutState() == sgre::text::FontFaceLayoutState::Unprepared && face->LayoutCache() == nullptr && !face->ParentIsImmutable(), "constructor does not query tables or publish a cache");
	auto whole = face->ReferenceTable(0);
	Require(PrepareWithFailure(*face, 0), "actual pinned face admits all three table queries");
	const auto allocations = g_AllocationCount;
	const auto* cache = face->LayoutCache();
	Require(cache != nullptr && cache->SubstitutionLookups().size() == 1 && cache->PositioningLookups().size() == 1 && whole.IsImmutable(), "completed lookup arrays and shared parent freeze precede ready publication");
	Require(cache->SubstitutionLookups()[0].GlyphsShift0 == 32 && cache->PositioningLookups()[0].GlyphsShift0 == 32, "native first-input coverage masks are installed into owned records");
	Require(face->HasPositioning() && !face->HasGlyphClasses() && PrepareWithFailure(*face, 1) && g_AllocationCount == 0 && face->LayoutCache() == cache && allocations == 3, "ready fast path never reconstructs or allocates");
	face.reset();
	Require(!observer.expired() && whole.Length() == 128, "an external parent-reference blob survives cache and face destruction");
	whole = {};
	Require(observer.expired(), "cache table pins are retired by face destruction");
}

static void TestStickyAllocationFailures() {
	const auto point = PointLayout();
	for (std::size_t failure = 1; failure <= 3; ++failure) {
		sgre::text::BlobFontFace face(std::make_shared<const std::vector<std::uint8_t>>(MakeFont({}, point, point)));
		Require(!PrepareWithFailure(face, failure), "cache object and either nonzero lookup array failure deny publication");
		Require(face.LayoutState() == sgre::text::FontFaceLayoutState::Failed && face.LayoutCache() == nullptr, "failed candidate is retired before exposing a sticky failure");
		Require(g_AllocationCount == (failure == 1 ? 1 : 3), "both lookup arrays are attempted before checking failure");
		Require(face.ParentIsImmutable() == (failure != 1), "cache object failure precedes table callbacks");
		Require(!PrepareWithFailure(face, 0) && !face.HasPositioning() && !face.HasGlyphClasses() && g_AllocationCount == 0, "failed state remains sticky across all preparatory queries");
	}
	const std::array<std::uint8_t, 10> repair = {0, 1, 0, 0, 0, 0, 0, 0, 0, 10};
	for (std::size_t failure : {std::size_t(2), std::size_t(3)}) {
		sgre::text::BlobFontFace face(std::make_shared<const std::vector<std::uint8_t>>(MakeFont({}, repair, repair)));
		Require(PrepareWithFailure(face, failure), "a denied repair copy empties one table and permits complete cache admission");
		Require((face.LayoutCache()->Substitution().Length() == 0) == (failure == 2), "failed substitution repair is replaced with the empty table");
		Require((face.LayoutCache()->Positioning().Length() == 0) == (failure == 3), "failed positioning repair is replaced with the empty table");
		Require(failure != 2 || face.LayoutCache()->Substitution().Status() == sgre::text::GlyphSubstitutionBlobStatus::Rejected, "allocation-denied substitution retains rejected admission status");
		Require(failure != 3 || face.LayoutCache()->Positioning().Status() == sgre::text::GlyphPositioningBlobStatus::Rejected, "allocation-denied positioning retains rejected admission status");
	}
}

static void TestSlotCountAndReset() {
	const auto point = PointLayout();
	const sgre::text::GlyphSubstitutionBlob substitution(point);
	std::array<sgre::text::LayoutLookupAccelerator, 1> values;
	values[0].GlyphsShift0 = 0xffffffff;
	values[0].GlyphsShift4 = 0xffffffff;
	values[0].GlyphsShift9 = 0xffffffff;
	sgre::text::InitializeSubstitutionLookupAccelerators(substitution, values);
	Require(values[0].GlyphsShift0 == 32 && values[0].GlyphsShift4 == 1 && values[0].GlyphsShift9 == 1, "fill resets all existing record words");
	bool rejected = false;
	try {
		sgre::text::InitializeSubstitutionLookupAccelerators(substitution, {});
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "fill rejects a span whose length differs from the admitted lookup count");
}

static void TestSuppressionAndSupportedLimit() {
	std::vector<std::uint8_t> definition(0x382);
	WriteBig(definition, 0, 0x10000, 4);
	WriteBig(definition, 4, 12);
	WriteBig(definition, 12, 1);
	WriteBig(definition, 14, 5);
	WriteBig(definition, 16, 1);
	WriteBig(definition, 18, 3);
	std::vector<std::uint8_t> substitution(0x310a);
	std::vector<std::uint8_t> positioning(0xb586);
	WriteBig(substitution, 0, 0x10000, 4);
	WriteBig(positioning, 0, 0x10000, 4);
	sgre::text::BlobFontFace suppressed(std::make_shared<const std::vector<std::uint8_t>>(MakeFont(definition, substitution, positioning)));
	Require(suppressed.PrepareLayout() && suppressed.HasPositioning() && !suppressed.HasGlyphClasses(), "actual admitted lengths suppress the effective GDEF view before entry");
	Require(suppressed.LayoutCache()->Definition().IsSuppressed() && suppressed.LayoutCache()->Definition().Length() == 0x382 && suppressed.LayoutCache()->Definition().Bytes()[18] == 0 && suppressed.LayoutCache()->Definition().Bytes()[19] == 3, "suppression retains the owned admitted blob and its class data");

	std::vector<std::uint8_t> excessive(2 * 1024 * 1024 + 1);
	WriteBig(excessive, 0, 0x10000, 4);
	sgre::text::BlobFontFace bounded(std::make_shared<const std::vector<std::uint8_t>>(MakeFont({}, excessive, {})));
	bool rejected = false;
	try {
		static_cast<void>(bounded.PrepareLayout());
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && bounded.LayoutState() == sgre::text::FontFaceLayoutState::Unprepared && bounded.LayoutCache() == nullptr, "supported table byte-limit exception does not manufacture native cache failure or success");
}

static bool ReadWord(std::uint32_t& value, bool allow_end = false) {
	std::array<char, 4> bytes{};
	std::cin.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	if (allow_end && std::cin.gcount() == 0 && std::cin.eof()) {
		return false;
	}
	if (std::cin.gcount() != 4) {
		throw std::runtime_error("native layout cache message has a truncated word");
	}
	value = 0;
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << (index * 8);
	}

	return true;
}

static void WriteWord(std::uint32_t value) {
	for (std::uint32_t index = 0; index < 4; ++index) {
		std::cout.put(static_cast<char>(value & 0xffU));
		value >>= 8;
	}
}

static void WriteSample(std::span<const std::uint8_t> bytes) {
	for (const auto byte : bytes) {
		std::cout.put(static_cast<char>(byte));
	}
}

static void NativeMessages() {
	std::uint32_t length = 0;
	while (ReadWord(length, true)) {
		std::uint32_t index = 0;
		std::uint32_t failure = 0;
		ReadWord(index);
		ReadWord(failure);
		if (length > 64 * 1024 * 1024 || failure > 64) {
			throw std::runtime_error("native layout cache message exceeds its input limit");
		}
		std::vector<std::uint8_t> bytes(length);
		std::array<char, 4096> block{};
		std::size_t offset = 0;
		while (offset < bytes.size()) {
			const auto count = std::min(block.size(), bytes.size() - offset);
			std::cin.read(block.data(), static_cast<std::streamsize>(count));
			if (std::cin.gcount() != static_cast<std::streamsize>(count)) {
				throw std::runtime_error("native layout cache message has truncated bytes");
			}

			for (std::size_t index = 0; index < count; ++index) {
				bytes[offset + index] = static_cast<std::uint8_t>(static_cast<unsigned char>(block[index]));
			}

			offset += count;
		}
		auto pin = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
		const std::weak_ptr<const std::vector<std::uint8_t>> observer = pin;
		auto face = std::make_unique<sgre::text::BlobFontFace>(pin, index);
		pin.reset();
		auto whole = face->ReferenceTable(0);
		WriteWord(static_cast<std::uint32_t>(face->Status()));
		WriteWord(face->ParentIsImmutable());
		WriteWord(static_cast<std::uint32_t>(face->LayoutState()));
		const bool ready = PrepareWithFailure(*face, failure);
		const auto allocation_count = g_AllocationCount;
		const auto sizes = g_AllocationSizes;
		WriteWord(ready);
		WriteWord(static_cast<std::uint32_t>(face->LayoutState()));
		WriteWord(face->ParentIsImmutable());
		WriteWord(face->HasPositioning());
		WriteWord(face->HasGlyphClasses());
		WriteWord(PrepareWithFailure(*face, 1));
		WriteWord(g_AllocationCount == 0);
		if (ready) {
			const auto& cache = *face->LayoutCache();
			for (const auto admitted : {cache.Definition().Bytes(), cache.Substitution().Bytes(), cache.Positioning().Bytes()}) {
				WriteWord(static_cast<std::uint32_t>(admitted.size()));
				WriteSample(admitted);
			}
			WriteWord(cache.Definition().IsSuppressed());
			for (const auto lookups : {cache.SubstitutionLookups(), cache.PositioningLookups()}) {
				WriteWord(static_cast<std::uint32_t>(lookups.size()));
				for (const auto& lookup : lookups) {
					WriteWord(lookup.GlyphsShift4);
					WriteWord(lookup.GlyphsShift0);
					WriteWord(lookup.GlyphsShift9);
				}
			}
		}
		face.reset();
		WriteWord(observer.expired());
		whole = {};
		WriteWord(observer.expired());
		WriteWord(static_cast<std::uint32_t>(allocation_count));
		for (std::size_t allocation = 0; allocation < allocation_count; ++allocation) {
			WriteWord(static_cast<std::uint32_t>(sizes[allocation]));
		}
		std::cout.flush();
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--native") {
			NativeMessages();

			return 0;
		}
		if (argc != 1) {
			throw std::runtime_error("unknown lazy layout cache proof argument");
		}
		TestLazyPublicationAndLifetime();
		TestStickyAllocationFailures();
		TestSlotCountAndReset();
		TestSuppressionAndSupportedLimit();
		std::cout << "PASS 4 lazy layout cache groups\n";

		return 0;
	} catch (const std::exception& error) {
		g_RecordAllocations = false;
		std::cerr << "text_lazy_layout_cache_tests: " << error.what() << '\n';

		return 1;
	}
}
