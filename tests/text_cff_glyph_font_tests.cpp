#include "sgre/text/cff_glyph_font.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "CFF glyph font failure lost context");

		return;
	}

	throw std::runtime_error("invalid CFF glyph font input was accepted");
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

static void AppendDictionaryInteger(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
	bytes.push_back(29);
	const auto offset = bytes.size();
	bytes.resize(offset + 4);
	WriteDoubleWord(bytes, offset, value);
}

static std::vector<std::uint8_t> MakeIndex(const std::vector<std::vector<std::uint8_t>>& items) {
	std::vector<std::uint8_t> bytes(2);
	WriteWord(bytes, 0, static_cast<std::uint16_t>(items.size()));
	if (items.empty()) {
		return bytes;
	}

	bytes.push_back(1);
	std::size_t offset = 1;
	for (const auto& item : items) {
		Require(offset < 256, "fixture INDEX offset is too large");
		bytes.push_back(static_cast<std::uint8_t>(offset));
		offset += item.size();
	}

	Require(offset < 256, "fixture INDEX exceeds one-byte range");
	bytes.push_back(static_cast<std::uint8_t>(offset));
	for (const auto& item : items) {
		Append(bytes, item);
	}

	return bytes;
}

static std::vector<std::uint8_t> RectangleProgram(bool return_to_caller) {
	// Independent Type2 rectangle: move(0,0), four relative edges, explicit end/return.
	std::vector<std::uint8_t> bytes{139, 139, 21};
	const std::array<std::array<std::int16_t, 2>, 4> edges{{{1000, 0}, {0, 1000}, {-1000, 0}, {0, -1000}}};
	for (const auto& edge : edges) {
		for (const auto value : edge) {
			const auto bits = std::bit_cast<std::uint16_t>(value);
			bytes.push_back(28);
			bytes.push_back(static_cast<std::uint8_t>(bits >> 8));
			bytes.push_back(static_cast<std::uint8_t>(bits));
		}

		bytes.push_back(5);
	}

	bytes.push_back(return_to_caller ? 11 : 14);

	return bytes;
}

static std::vector<std::uint8_t> MakeCff() {
	std::vector<std::uint8_t> bytes{1, 0, 4, 4};
	Append(bytes, MakeIndex({{'T'}}));
	std::vector<std::uint8_t> dictionary;
	AppendDictionaryInteger(dictionary, 0);
	dictionary.push_back(17);
	AppendDictionaryInteger(dictionary, 18);
	AppendDictionaryInteger(dictionary, 0);
	dictionary.push_back(18);
	const auto top_offset = bytes.size() + 5;
	Append(bytes, MakeIndex({dictionary}));
	Append(bytes, MakeIndex({}));
	Append(bytes, MakeIndex({}));
	WriteDoubleWord(bytes, top_offset + 1, static_cast<std::uint32_t>(bytes.size()));
	Append(bytes, MakeIndex({RectangleProgram(false), {14}, {32, 10, 14}}));
	WriteDoubleWord(bytes, top_offset + 12, static_cast<std::uint32_t>(bytes.size()));
	std::vector<std::uint8_t> private_dictionary;
	AppendDictionaryInteger(private_dictionary, 500);
	private_dictionary.push_back(20);
	AppendDictionaryInteger(private_dictionary, 100);
	private_dictionary.push_back(21);
	AppendDictionaryInteger(private_dictionary, 18);
	private_dictionary.push_back(19);
	Append(bytes, private_dictionary);
	Append(bytes, MakeIndex({RectangleProgram(true)}));

	return bytes;
}

static std::vector<std::uint8_t> MakeFont(std::uint16_t units = 1000, bool vertical = true) {
	std::map<std::string, std::vector<std::uint8_t>> tables;
	tables["CFF "] = MakeCff();
	tables["head"].resize(54);
	WriteDoubleWord(tables["head"], 0, 0x00010000);
	WriteDoubleWord(tables["head"], 12, 0x5f0f3cf5);
	WriteWord(tables["head"], 18, units);
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
	if (vertical) {
		tables["vhea"].resize(36);
		WriteDoubleWord(tables["vhea"], 0, 0x00011000);
		WriteWord(tables["vhea"], 34, 1);
		tables["vmtx"] = {3, 232, 0, 100, 0, 100, 0, 100};
	}

	std::vector<std::uint8_t> bytes(12 + tables.size() * 16);
	WriteDoubleWord(bytes, 0, 0x4f54544f);
	WriteWord(bytes, 4, static_cast<std::uint16_t>(tables.size()));
	std::size_t index = 0;
	for (const auto& [tag, table] : tables) {
		while (bytes.size() % 4 != 0) {
			bytes.push_back(0);
		}

		const auto record = 12 + index++ * 16;
		std::copy(tag.begin(), tag.end(), bytes.data() + record);
		WriteDoubleWord(bytes, record + 8, static_cast<std::uint32_t>(bytes.size()));
		WriteDoubleWord(bytes, record + 12, static_cast<std::uint32_t>(table.size()));
		Append(bytes, table);
	}

	return bytes;
}

static void TestActualTableProgramsAndOwnedResults() {
	const auto bytes = MakeFont();
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::text::CffGlyphFont font(sfnt);
	const auto result = font.PrepareGlyph(0, 24);
	Require(result.GlyphIndex == 0 && result.PixelSize == 24 && result.Outline.Points.size() == 4 && result.Outline.ContourEnds == std::vector<std::uint16_t>{3}, "complete CFF glyph identities or native closure differ");
	const std::array<std::array<std::int32_t, 2>, 4> expected{{{0, 0}, {1536, 0}, {1536, 1536}, {0, 1536}}};
	for (std::size_t index = 0; index < expected.size(); ++index) {
		Require(result.Outline.Points[index].X26Dot6 == expected[index][0] && result.Outline.Points[index].Y26Dot6 == expected[index][1] && result.Outline.Points[index].Kind == sgre::resource::CffPointKind::OnCurve, "complete rectangle hinted points differ");
	}

	Require(result.Metrics.Width26Dot6 == 1536 && result.Metrics.Height26Dot6 == 1536 && result.Metrics.HorizontalAdvance26Dot6 == 1536 && result.Metrics.VerticalBearingY26Dot6 == 64, "complete glyph did not retain actual hmtx/vmtx behavior");
	const auto empty = font.PrepareGlyph(1, 12);
	Require(empty.Outline.Points.empty() && empty.Outline.ContourEnds.empty() && empty.Metrics.HorizontalAdvance26Dot6 == 768 && empty.Metrics.OutlineFlags == 0x104, "empty original program lost its table advance or small-size flags");
	const auto subroutine = font.PrepareGlyph(2, 24);
	Require(subroutine.Outline.Points.size() == result.Outline.Points.size() && subroutine.Metrics.Width26Dot6 == result.Metrics.Width26Dot6, "complete glyph did not select its original local program");
	Require(result.Outline.Points[1].X26Dot6 == 1536 && result.PixelSize == 24, "preparing another glyph changed owned previous geometry");
}

static void TestScopeAndRequestErrors() {
	const auto bytes = MakeFont();
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::text::CffGlyphFont font(sfnt);
	for (const auto size : {0, -1, 2001}) {
		RequireFailure([&] {
			static_cast<void>(font.PrepareGlyph(0, size));
		},
		    "range");
	}

	RequireFailure([&] {
		static_cast<void>(font.PrepareGlyph(3, 24));
	},
	    "range");
	const auto unsupported_bytes = MakeFont(2048);
	const sgre::resource::SfntFont unsupported(unsupported_bytes);
	RequireFailure([&] {
		static_cast<void>(sgre::text::CffGlyphFont(unsupported));
	},
	    "normalized");
	const auto no_vertical_bytes = MakeFont(1000, false);
	const sgre::resource::SfntFont no_vertical(no_vertical_bytes);
	const sgre::text::CffGlyphFont no_vertical_font(no_vertical);
	RequireFailure([&] {
		static_cast<void>(no_vertical_font.PrepareGlyph(1, 24));
	},
	    "CFF glyph 1 at pixel size 24: CFF glyph font requires original vertical metrics");
}

int main() {
	try {
		TestActualTableProgramsAndOwnedResults();
		TestScopeAndRequestErrors();
		std::cout << "Text CFF glyph font tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text CFF glyph font test failure: " << error.what() << '\n';

		return 1;
	}
}
