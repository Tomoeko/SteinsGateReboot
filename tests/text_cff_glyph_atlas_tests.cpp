#include "sgre/text/cff_glyph_atlas.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

static void TestDirectAtlasBytesAndNativePlacement() {
	const auto bytes = MakeFont();
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::text::CffGlyphFont font(sfnt);
	sgre::text::GlyphAtlasPacking packing;
	packing.WidthPixels = 64;
	packing.HeightPixels = 64;
	std::vector<std::uint8_t> pixels(273 * 64 + 13, 0xa5);
	sgre::text::GlyphAtlasCpuSurface surface;
	surface.Pixels = pixels;
	surface.RowPitchBytes = 273;
	const auto glyph = sgre::text::RasterizeDefaultCffGlyphInAtlas(font, 0, 24, packing, surface);
	Require(glyph && glyph->Allocation.LeftPixels == 1 && glyph->Allocation.TopPixels == 1 && glyph->Allocation.WidthPixels == 24 && glyph->Allocation.HeightPixels == 24 && glyph->Allocation.RowPitchBytes == 273 && glyph->Allocation.DataOffsetBytes == 277, "default CFF atlas allocation differs");
	Require(glyph->LeftPixel == 0 && glyph->TopTranslationPixel == 24 && packing.PenXPixels == 26 && packing.PenYPixels == 0 && packing.RowHeightPixels == 26, "native six-word glyph record or packing state differs");
	Require(!surface.Locked && surface.UploadRequired, "default CFF atlas draw did not release its CPU map while retaining upload dirtiness");
	for (std::size_t offset = 0; offset < pixels.size(); ++offset) {
		const auto row = offset / 273;
		const auto column_bytes = offset % 273;
		auto expected = std::uint8_t(0xa5);
		if (row < 26 && column_bytes < 26 * 4) {
			expected = 0;
			const auto column = column_bytes / 4;
			if (row >= 1 && row <= 24 && column >= 1 && column <= 24) {
				expected = column <= 4 ? std::uint8_t(254) : std::uint8_t(255);
			}
		}
		Require(pixels[offset] == expected, "native rectangle atlas fill/border/row padding differs");
	}
	const auto first = pixels;
	const auto second = sgre::text::RasterizeDefaultCffGlyphInAtlas(font, 2, 24, packing, surface);
	Require(second && second->Allocation.LeftPixels == 27 && second->Allocation.TopPixels == 1 && packing.PenXPixels == 52 && !surface.Locked, "next glyph failed to append in the native atlas row");
	for (std::size_t row = 0; row < 26; ++row) {
		Require(std::equal(pixels.data() + row * 273, pixels.data() + row * 273 + 26 * 4, first.data() + row * 273), "next allocation changed the previous glyph or border");
	}
	const auto third = sgre::text::RasterizeDefaultCffGlyphInAtlas(font, 0, 24, packing, surface);
	Require(third && third->Allocation.LeftPixels == 1 && third->Allocation.TopPixels == 27 && packing.PenXPixels == 26 && packing.PenYPixels == 26 && packing.RowHeightPixels == 26, "native atlas row wrap differs");
}

static void TestEmptyUnavailableAndInvalidRequests() {
	const auto bytes = MakeFont();
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::text::CffGlyphFont font(sfnt);
	sgre::text::GlyphAtlasPacking packing;
	packing.WidthPixels = 64;
	packing.HeightPixels = 64;
	std::vector<std::uint8_t> pixels(64 * 64 * 4, 0xa5);
	sgre::text::GlyphAtlasCpuSurface surface;
	surface.Pixels = pixels;
	surface.RowPitchBytes = 64 * 4;
	const auto initial = pixels;
	Require(!sgre::text::RasterizeDefaultCffGlyphInAtlas(font, 1, 24, packing, surface), "empty glyph allocated a visible atlas rectangle");
	Require(pixels == initial && packing.PenXPixels == 0 && packing.PenYPixels == 0 && packing.RowHeightPixels == 0 && !surface.Locked && !surface.UploadRequired, "empty glyph changed its atlas owner");
	surface.Locked = true;
	Require(!sgre::text::RasterizeDefaultCffGlyphInAtlas(font, 0, 24, packing, surface), "already locked CPU map was borrowed for a glyph");
	Require(surface.Locked && pixels == initial, "failed mapping released another owner's lock or wrote pixels");
	surface.Locked = false;
	surface.Enabled = false;
	Require(!sgre::text::RasterizeDefaultCffGlyphInAtlas(font, 0, 24, packing, surface), "disabled atlas mapping was accepted");
	surface.Enabled = true;
	RequireFailure([&] {
		static_cast<void>(sgre::text::RasterizeDefaultCffGlyphInAtlas(font, 3, 24, packing, surface));
	},
	    "range");
	RequireFailure([&] {
		static_cast<void>(sgre::text::RasterizeDefaultCffGlyphInAtlas(font, 0, 2000, packing, surface));
	},
	    "bounded raster dimensions");
	Require(pixels == initial && !surface.Locked && !surface.UploadRequired, "rejected glyph preparation modified atlas storage");
}

static void TestImmutablePreparationAndMovedStorageRejection() {
	const auto bytes = MakeFont();
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::text::CffGlyphFont font(sfnt);
	auto raster = sgre::text::PreparedCffGlyphRaster::Prepare(font.PrepareGlyph(0, 24));
	Require(raster && raster->BitmapPlan().WidthPixels == 24 && raster->BitmapPlan().HeightPixels == 24, "actual CFF preparation did not return its bounded raster geometry");
	auto moved = std::move(*raster);
	sgre::text::GlyphAtlasPacking packing;
	packing.WidthPixels = 64;
	packing.HeightPixels = 64;
	std::vector<std::uint8_t> pixels(64 * 64 * 4, 0xa5);
	sgre::text::GlyphAtlasCpuSurface surface;
	surface.Pixels = pixels;
	surface.RowPitchBytes = 256;
	RequireFailure([&] {
		static_cast<void>(sgre::text::RasterizePreparedCffGlyphInAtlas(*raster, packing, surface));
	},
	    "storage no longer covers");
	Require(packing.PenXPixels == 0 && !surface.Locked && !surface.UploadRequired && std::all_of(pixels.begin(), pixels.end(), [](auto value) {
		return value == 0xa5;
	}),
	    "moved raster rejection must precede atlas mutation");
	Require(sgre::text::RasterizePreparedCffGlyphInAtlas(moved, packing, surface).has_value() && packing.PenXPixels == 26, "owned immutable raster failed after a legitimate move");
}

int main() {
	try {
		TestDirectAtlasBytesAndNativePlacement();
		TestEmptyUnavailableAndInvalidRequests();
		TestImmutablePreparationAndMovedStorageRejection();
		std::cout << "Text CFF glyph atlas tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Text CFF glyph atlas failure: " << error.what() << '\n';

		return 1;
	}
}
