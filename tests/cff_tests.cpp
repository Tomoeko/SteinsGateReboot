#include "sgre/resource/cff.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "CFF failure lost its context");

		return;
	}

	throw std::runtime_error("invalid CFF input was accepted");
}

static void WriteWord(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
	bytes.at(offset) = static_cast<std::uint8_t>(value >> 8);
	bytes.at(offset + 1) = static_cast<std::uint8_t>(value);
}

static void WriteDoubleWord(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
	WriteWord(bytes, offset, static_cast<std::uint16_t>(value >> 16));
	WriteWord(bytes, offset + 2, static_cast<std::uint16_t>(value));
}

static void Append(std::vector<std::uint8_t>& bytes, std::span<const std::uint8_t> appended) {
	bytes.insert(bytes.end(), appended.begin(), appended.end());
}

static void AppendInteger(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
	bytes.push_back(29);
	const auto offset = bytes.size();
	bytes.resize(offset + 4);
	WriteDoubleWord(bytes, offset, value);
}

struct IndexFixture {
	std::vector<std::uint8_t> Bytes;
	std::vector<std::size_t> ItemOffsets;
};

static IndexFixture MakeIndex(const std::vector<std::vector<std::uint8_t>>& items) {
	IndexFixture fixture;
	fixture.Bytes.resize(2);
	WriteWord(fixture.Bytes, 0, static_cast<std::uint16_t>(items.size()));
	if (items.empty()) {
		return fixture;
	}

	fixture.Bytes.push_back(1);
	std::size_t offset = 1;
	for (const auto& item : items) {
		fixture.Bytes.push_back(static_cast<std::uint8_t>(offset));
		offset += item.size();
	}
	Require(offset < 256, "synthetic INDEX exceeds its one-byte offset encoding");
	fixture.Bytes.push_back(static_cast<std::uint8_t>(offset));
	for (const auto& item : items) {
		fixture.ItemOffsets.push_back(fixture.Bytes.size());
		Append(fixture.Bytes, item);
	}

	return fixture;
}

struct CffFixture {
	std::vector<std::uint8_t> Bytes;
	std::size_t TopDictionary = 0;
	std::size_t FontSelection = 0;
	std::size_t FirstPrivate = 0;
	std::size_t FirstLocalIndex = 0;
	std::size_t GlyphIndex = 0;
};

static CffFixture MakeCff(bool cid, const std::vector<std::uint8_t>& extra_private = {}) {
	const auto private_size = static_cast<std::uint32_t>(18 + extra_private.size());
	CffFixture fixture;
	fixture.Bytes = {1, 0, 4, 4};
	Append(fixture.Bytes, MakeIndex({{u'T'}}).Bytes);
	std::vector<std::uint8_t> top;
	AppendInteger(top, 0);
	top.push_back(17);
	if (cid) {
		Append(top, std::array<std::uint8_t, 5>{139, 139, 139, 12, 30});
		AppendInteger(top, 0);
		Append(top, std::array<std::uint8_t, 2>{12, 36});
		AppendInteger(top, 0);
		Append(top, std::array<std::uint8_t, 2>{12, 37});
	} else {
		AppendInteger(top, private_size);
		AppendInteger(top, 0);
		top.push_back(18);
	}
	// Explicit decimal FontMatrix exercises nibble-real parsing independently of the default.
	Append(top, std::array<std::uint8_t, 4>{30, 0x0a, 0x00, 0x1f});
	Append(top, std::array<std::uint8_t, 2>{139, 139});
	Append(top, std::array<std::uint8_t, 4>{30, 0x0a, 0x00, 0x1f});
	Append(top, std::array<std::uint8_t, 4>{139, 139, 12, 7});
	const auto top_index = MakeIndex({top});
	fixture.TopDictionary = fixture.Bytes.size() + top_index.ItemOffsets[0];
	Append(fixture.Bytes, top_index.Bytes);
	Append(fixture.Bytes, MakeIndex({}).Bytes);
	Append(fixture.Bytes, MakeIndex({{11}}).Bytes);
	fixture.GlyphIndex = fixture.Bytes.size();
	WriteDoubleWord(fixture.Bytes, fixture.TopDictionary + 1, static_cast<std::uint32_t>(fixture.GlyphIndex));
	Append(fixture.Bytes, MakeIndex({{139, 139, 21, 14}, {14}, {140, 10, 14}}).Bytes);

	std::size_t first_dictionary = fixture.TopDictionary;
	std::size_t second_dictionary = 0;
	if (cid) {
		std::vector<std::uint8_t> dictionary;
		AppendInteger(dictionary, private_size);
		AppendInteger(dictionary, 0);
		dictionary.push_back(18);
		const auto array = MakeIndex({dictionary, dictionary});
		WriteDoubleWord(fixture.Bytes, fixture.TopDictionary + 12, static_cast<std::uint32_t>(fixture.Bytes.size()));
		first_dictionary = fixture.Bytes.size() + array.ItemOffsets[0];
		second_dictionary = fixture.Bytes.size() + array.ItemOffsets[1];
		Append(fixture.Bytes, array.Bytes);
		fixture.FontSelection = fixture.Bytes.size();
		WriteDoubleWord(fixture.Bytes, fixture.TopDictionary + 19, static_cast<std::uint32_t>(fixture.FontSelection));
		Append(fixture.Bytes, std::array<std::uint8_t, 11>{3, 0, 2, 0, 0, 0, 0, 2, 1, 0, 3});
	}
	fixture.FirstPrivate = fixture.Bytes.size();
	WriteDoubleWord(fixture.Bytes, first_dictionary + (cid ? 6 : 12), static_cast<std::uint32_t>(fixture.FirstPrivate));
	std::vector<std::uint8_t> private_dictionary;
	AppendInteger(private_dictionary, 500);
	private_dictionary.push_back(20);
	AppendInteger(private_dictionary, 100);
	private_dictionary.push_back(21);
	AppendInteger(private_dictionary, private_size);
	private_dictionary.push_back(19);
	Append(private_dictionary, extra_private);
	Append(fixture.Bytes, private_dictionary);
	fixture.FirstLocalIndex = fixture.Bytes.size();
	Append(fixture.Bytes, MakeIndex({{11}, {139, 139, 5, 11}}).Bytes);
	if (cid) {
		WriteDoubleWord(fixture.Bytes, second_dictionary + 6, static_cast<std::uint32_t>(fixture.Bytes.size()));
		private_dictionary.clear();
		AppendInteger(private_dictionary, 600);
		private_dictionary.push_back(20);
		AppendInteger(private_dictionary, 200);
		private_dictionary.push_back(21);
		AppendInteger(private_dictionary, private_size);
		private_dictionary.push_back(19);
		Append(private_dictionary, extra_private);
		Append(fixture.Bytes, private_dictionary);
		Append(fixture.Bytes, MakeIndex({}).Bytes);
	}

	return fixture;
}

static std::vector<std::uint8_t> MakeSfnt(const std::vector<std::uint8_t>& cff) {
	std::map<std::string, std::vector<std::uint8_t>> tables;
	tables["CFF "] = cff;
	tables["head"].resize(54);
	WriteDoubleWord(tables["head"], 0, 0x00010000);
	WriteDoubleWord(tables["head"], 12, 0x5f0f3cf5);
	WriteWord(tables["head"], 18, 1000);
	tables["hhea"].resize(36);
	WriteDoubleWord(tables["hhea"], 0, 0x00010000);
	WriteWord(tables["hhea"], 34, 1);
	tables["hmtx"] = {3, 232, 0, 0, 0, 0, 0, 0};
	tables["maxp"] = {0, 0, 0x50, 0, 0, 3};
	tables["cmap"].resize(40);
	WriteWord(tables["cmap"], 2, 1);
	WriteWord(tables["cmap"], 6, 3);
	WriteDoubleWord(tables["cmap"], 8, 12);
	WriteWord(tables["cmap"], 12, 12);
	WriteDoubleWord(tables["cmap"], 16, 28);
	WriteDoubleWord(tables["cmap"], 24, 1);
	WriteDoubleWord(tables["cmap"], 28, 0x41);
	WriteDoubleWord(tables["cmap"], 32, 0x42);
	WriteDoubleWord(tables["cmap"], 36, 1);
	std::vector<std::uint8_t> bytes(12 + tables.size() * 16);
	WriteDoubleWord(bytes, 0, 0x4f54544f);
	WriteWord(bytes, 4, static_cast<std::uint16_t>(tables.size()));
	std::size_t index = 0;
	for (const auto& [tag, table] : tables) {
		while (bytes.size() % 4 != 0) {
			bytes.push_back(0);
		}
		const auto record = 12 + index++ * 16;
		std::copy(tag.begin(), tag.end(), bytes.begin() + static_cast<std::ptrdiff_t>(record));
		WriteDoubleWord(bytes, record + 8, static_cast<std::uint32_t>(bytes.size()));
		WriteDoubleWord(bytes, record + 12, static_cast<std::uint32_t>(table.size()));
		Append(bytes, table);
	}

	return bytes;
}

static void TestCidAndBorrowedPrograms() {
	const auto fixture = MakeCff(true);
	const auto bytes = MakeSfnt(fixture.Bytes);
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::resource::CffFont font(sfnt);
	Require(font.IsCid() && font.FontDictionaries().size() == 2 && font.GlobalSubroutines().size() == 1, "CFF CID dictionary/subroutine counts differ");
	Require(font.FontMatrix() == std::array<double, 6>{0.001, 0, 0, 0.001, 0, 0}, "CFF decimal matrix differs");
	Require(font.GlyphFontDictionary(0) == 0 && font.GlyphFontDictionary(1) == 0 && font.GlyphFontDictionary(2) == 1, "CFF range-selected dictionary differs");
	Require(font.FontDictionaries()[0].DefaultWidth == 500 && font.FontDictionaries()[0].NominalWidth == 100 && font.FontDictionaries()[0].LocalSubroutines.size() == 2, "CFF first private dictionary differs");
	Require(font.FontDictionaries()[1].DefaultWidth == 600 && font.FontDictionaries()[1].NominalWidth == 200 && font.FontDictionaries()[1].LocalSubroutines.empty(), "CFF empty local subroutine index differs");
	const auto private_bytes = font.FontDictionaries()[0].PrivateDictionaryBytes;
	Require(private_bytes.size() == 18 && private_bytes.data() == sfnt.FindTable("CFF ")->data() + fixture.FirstPrivate && std::equal(private_bytes.begin(), private_bytes.end(), fixture.Bytes.begin() + static_cast<std::ptrdiff_t>(fixture.FirstPrivate)), "CFF original encoded private dictionary lost its bytes/resource owner");
	Require(font.FontDictionaries()[1].PrivateDictionaryBytes.data() != private_bytes.data(), "distinct CFF private dictionaries must preserve their original ranges");
	const auto program = font.GlyphProgram(0);
	Require(program.size() == 4 && program[2] == 21 && program.data() >= sfnt.FindTable("CFF ")->data() && program.data() < sfnt.FindTable("CFF ")->data() + sfnt.FindTable("CFF ")->size(), "CFF program view lost its resource owner");
	Require(font.GlobalSubroutines()[0].size() == 1 && font.GlobalSubroutines()[0][0] == 11, "CFF global program bytes changed");
	RequireFailure([&] {
		static_cast<void>(font.GlyphProgram(3));
	},
	    "font");
}

static void TestNonCidAndDirectSelection() {
	const auto non_cid = MakeCff(false);
	const auto bytes = MakeSfnt(non_cid.Bytes);
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::resource::CffFont font(sfnt);
	Require(!font.IsCid() && font.FontDictionaries().size() == 1 && font.GlyphFontDictionary(2) == 0 && font.FontDictionaries()[0].DefaultWidth == 500, "non-CID private dictionary differs");
	auto direct = MakeCff(true);
	const std::array<std::uint8_t, 4> selection{0, 0, 1, 0};
	std::copy(selection.begin(), selection.end(), direct.Bytes.begin() + static_cast<std::ptrdiff_t>(direct.FontSelection));
	const auto direct_bytes = MakeSfnt(direct.Bytes);
	const sgre::resource::SfntFont direct_sfnt(direct_bytes);
	const sgre::resource::CffFont direct_font(direct_sfnt);
	Require(direct_font.GlyphFontDictionary(0) == 0 && direct_font.GlyphFontDictionary(1) == 1 && direct_font.GlyphFontDictionary(2) == 0, "CFF direct FDSelect mapping differs");
}

static void AppendSignedInteger(std::vector<std::uint8_t>& bytes, std::int32_t value) {
	AppendInteger(bytes, std::bit_cast<std::uint32_t>(value));
}

static void AppendHintArray(std::vector<std::uint8_t>& bytes, std::span<const std::int32_t> deltas, std::uint16_t operation) {
	for (const auto delta : deltas) {
		AppendSignedInteger(bytes, delta);
	}
	if (operation > 255) {
		bytes.push_back(12);
	}
	bytes.push_back(static_cast<std::uint8_t>(operation));
}

static void TestPrivateHintDefaults() {
	const auto fixture = MakeCff(false);
	const auto bytes = MakeSfnt(fixture.Bytes);
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::resource::CffFont font(sfnt);
	const auto& hints = font.FontDictionaries()[0].Hinting;
	Require(hints.BlueValuesFontUnits.empty() && hints.HorizontalStemSnapFontUnits.empty() && !hints.StandardHorizontalStemWidthFontUnits && !hints.StandardVerticalStemWidthFontUnits, "CFF absent hint arrays/widths lost their default state");
	Require(hints.BlueScale == 0.039625 && hints.BlueShiftFontUnits == 7 && hints.BlueFuzzFontUnits == 1 && hints.ExpansionFactor == 0.06 && hints.LanguageGroup == 0 && hints.InitialRandomSeed == 0 && !hints.ForceBold, "CFF private hint scalar defaults differ");
}

static void TestPrivateHintArraysAndScalars() {
	std::vector<std::uint8_t> extra;
	AppendHintArray(extra, std::array<std::int32_t, 4>{-10, 10, 700, 10}, 6);
	AppendHintArray(extra, std::array<std::int32_t, 2>{-250, 10}, 7);
	AppendHintArray(extra, std::array<std::int32_t, 4>{-20, 21, 700, 10}, 8);
	AppendHintArray(extra, std::array<std::int32_t, 2>{-251, 10}, 9);
	AppendHintArray(extra, std::array<std::int32_t, 2>{50, 30}, 0x0c0c);
	AppendHintArray(extra, std::array<std::int32_t, 2>{60, 30}, 0x0c0d);
	Append(extra, std::array<std::uint8_t, 6>{30, 0x0a, 0x04, 0xff, 12, 9});
	Append(extra, std::array<std::uint8_t, 3>{147, 12, 10});
	Append(extra, std::array<std::uint8_t, 3>{141, 12, 11});
	AppendHintArray(extra, std::array<std::int32_t, 1>{100}, 10);
	AppendHintArray(extra, std::array<std::int32_t, 1>{120}, 11);
	Append(extra, std::array<std::uint8_t, 3>{140, 12, 14});
	Append(extra, std::array<std::uint8_t, 3>{140, 12, 17});
	Append(extra, std::array<std::uint8_t, 6>{30, 0x0a, 0x07, 0x5f, 12, 18});
	AppendHintArray(extra, std::array<std::int32_t, 1>{-5}, 0x0c13);
	const auto fixture = MakeCff(true, extra);
	const auto bytes = MakeSfnt(fixture.Bytes);
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::resource::CffFont font(sfnt);
	const auto& hints = font.FontDictionaries()[0].Hinting;
	Require(hints.BlueValuesFontUnits == std::vector<double>{-10, 0, 700, 710} && hints.OtherBluesFontUnits == std::vector<double>{-250, -240} && hints.FamilyBluesFontUnits == std::vector<double>{-20, 1, 701, 711} && hints.FamilyOtherBluesFontUnits == std::vector<double>{-251, -241}, "CFF private blue-array deltas did not become absolute values");
	Require(hints.HorizontalStemSnapFontUnits == std::vector<double>{50, 80} && hints.VerticalStemSnapFontUnits == std::vector<double>{60, 90}, "CFF private stem-snap deltas differ");
	Require(hints.BlueScale == 0.04 && hints.BlueShiftFontUnits == 8 && hints.BlueFuzzFontUnits == 2 && hints.StandardHorizontalStemWidthFontUnits == 100 && hints.StandardVerticalStemWidthFontUnits == 120 && hints.ForceBold && hints.LanguageGroup == 1 && hints.ExpansionFactor == 0.075 && hints.InitialRandomSeed == -5, "CFF private hint scalars differ");
	Require(font.FontDictionaries()[1].Hinting.BlueValuesFontUnits == hints.BlueValuesFontUnits && font.FontDictionaries()[0].LocalSubroutines.size() == 2 && font.FontDictionaries()[1].LocalSubroutines.empty(), "per-dictionary hinting changed local subroutine identity");
}

static void TestPrivateHintBounds() {
	const auto reject = [](const std::vector<std::uint8_t>& extra, std::string_view context) {
		const auto fixture = MakeCff(false, extra);
		const auto bytes = MakeSfnt(fixture.Bytes);
		RequireFailure([&] {
			const sgre::resource::SfntFont sfnt(bytes);
			const sgre::resource::CffFont font(sfnt);
			static_cast<void>(font);
		},
		    context);
	};
	std::vector<std::uint8_t> odd;
	AppendHintArray(odd, std::array<std::int32_t, 1>{0}, 6);
	reject(odd, "unpaired");
	std::vector<std::uint8_t> excessive;
	AppendHintArray(excessive, std::array<std::int32_t, 16>{}, 6);
	reject(excessive, "excessive");
	std::vector<std::uint8_t> stems;
	AppendHintArray(stems, std::array<std::int32_t, 13>{}, 0x0c0c);
	reject(stems, "excessive");
	reject({139, 140, 12, 9}, "operand count");
	reject({141, 12, 14}, "ForceBold");
	reject({30, 0x0a, 0x5f, 12, 17}, "fractional");
}

static void TestMalformedInput() {
	const auto reject = [](CffFixture fixture, std::size_t offset, std::uint8_t value, std::string_view context) {
		fixture.Bytes.at(offset) = value;
		const auto bytes = MakeSfnt(fixture.Bytes);
		RequireFailure([&] {
			const sgre::resource::SfntFont sfnt(bytes);
			const sgre::resource::CffFont font(sfnt);
			static_cast<void>(font);
		},
		    context);
	};
	const auto fixture = MakeCff(true);
	reject(fixture, 0, 2, "header");
	reject(fixture, 7, 0, "first offset");
	reject(fixture, fixture.GlyphIndex + 2, 5, "offset size");
	reject(fixture, fixture.FontSelection, 4, "FDSelect format");
	reject(fixture, fixture.FontSelection + 5, 2, "dictionary exceeds");
	reject(fixture, fixture.FontSelection + 10, 2, "glyph range");
	reject(fixture, fixture.FirstLocalIndex + 2, 0, "offset size");
	reject(fixture, fixture.TopDictionary + 26, 0xd0, "reserved nibble");
}

int main() {
	try {
		TestCidAndBorrowedPrograms();
		TestNonCidAndDirectSelection();
		TestMalformedInput();
		TestPrivateHintDefaults();
		TestPrivateHintArraysAndScalars();
		TestPrivateHintBounds();
		std::cout << "CFF tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "CFF test failure: " << error.what() << '\n';

		return 1;
	}
}
