#include "sgre/resource/sfnt.hpp"

#include <algorithm>
#include <array>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "sfnt failure lost its context");

		return;
	}

	throw std::runtime_error("invalid sfnt input was accepted");
}

static void Write16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
	bytes.at(offset) = static_cast<std::uint8_t>(value >> 8);
	bytes.at(offset + 1) = static_cast<std::uint8_t>(value);
}

static void Write32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
	Write16(bytes, offset, static_cast<std::uint16_t>(value >> 16));
	Write16(bytes, offset + 2, static_cast<std::uint16_t>(value));
}

static std::uint32_t SumWords(std::span<const std::uint8_t> bytes) {
	std::uint32_t checksum = 0;
	for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
		std::uint32_t word = 0;
		for (std::size_t index = 0; index < 4; ++index) {
			word = (word << 8) | (offset + index < bytes.size() ? bytes[offset + index] : 0u);
		}

		checksum += word;
	}

	return checksum;
}

struct SfntFixture {
	std::vector<std::uint8_t> Bytes;
	std::map<std::string, std::size_t> Offsets;
	std::map<std::string, std::size_t> Records;
};

static std::vector<std::uint8_t> MakeCharacterMaps() {
	// Direct deltas map A/B/C to 1/2/3; a glyph array maps a to 4 and b to missing glyph 0.
	std::vector<std::uint8_t> segment(44);
	Write16(segment, 0, 4);
	Write16(segment, 2, 44);
	Write16(segment, 6, 6);
	Write16(segment, 8, 4);
	Write16(segment, 10, 1);
	Write16(segment, 12, 2);
	const std::array<std::uint16_t, 15> fields{0x43, 0x62, 0xffff, 0, 0x41, 0x61, 0xffff, 0xffc0, 3, 1, 0, 4, 0, 1, 0};
	for (std::size_t index = 0; index < fields.size(); ++index) {
		Write16(segment, 14 + index * 2, fields[index]);
	}

	std::vector<std::uint8_t> coverage(40);
	Write16(coverage, 0, 12);
	Write32(coverage, 4, 40);
	Write32(coverage, 12, 2);
	Write32(coverage, 16, 0x41);
	Write32(coverage, 20, 0x42);
	Write32(coverage, 24, 3);
	Write32(coverage, 28, 0x1f600);
	Write32(coverage, 32, 0x1f601);
	Write32(coverage, 36, 2);
	std::vector<std::uint8_t> variation(10);
	Write16(variation, 0, 14);
	Write32(variation, 2, 10);
	std::vector<std::uint8_t> maps(28);
	Write16(maps, 2, 3);
	Write16(maps, 4, 0);
	Write16(maps, 6, 3);
	Write32(maps, 8, 28);
	Write16(maps, 12, 0);
	Write16(maps, 14, 4);
	Write32(maps, 16, 72);
	Write16(maps, 20, 0);
	Write16(maps, 22, 5);
	Write32(maps, 24, 112);
	maps.insert(maps.end(), segment.begin(), segment.end());
	maps.insert(maps.end(), coverage.begin(), coverage.end());
	maps.insert(maps.end(), variation.begin(), variation.end());

	return maps;
}

static SfntFixture MakeFont(bool cff, bool vertical) {
	std::map<std::string, std::vector<std::uint8_t>> tables;
	tables["cmap"] = MakeCharacterMaps();
	tables["head"].resize(54);
	Write32(tables["head"], 0, 0x00010000);
	Write32(tables["head"], 12, 0x5f0f3cf5);
	Write16(tables["head"], 18, 1000);
	tables["hhea"].resize(36);
	Write32(tables["hhea"], 0, 0x00010000);
	Write16(tables["hhea"], 34, 2);
	tables["hmtx"] = {0x01, 0xf4, 0x00, 0x0a, 0x02, 0x58, 0xff, 0xec, 0x00, 0x1e, 0xff, 0xd8, 0x00, 0x32};
	tables["maxp"].resize(cff ? 6 : 32);
	Write32(tables["maxp"], 0, cff ? 0x00005000 : 0x00010000);
	Write16(tables["maxp"], 4, 5);
	if (cff) {
		tables["CFF "] = {1, 0, 4, 4};
	} else {
		tables["glyf"] = {};
		tables["loca"].resize(12);
	}

	if (vertical) {
		tables["vhea"].resize(36);
		Write32(tables["vhea"], 0, 0x00011000);
		Write16(tables["vhea"], 34, 3);
		tables["vmtx"] = {0x02, 0xbc, 0xff, 0xff, 0x03, 0x20, 0xff, 0xfe, 0x03, 0x84, 0xff, 0xfd, 0x00, 0x04, 0x00, 0x05};
	}

	SfntFixture fixture;
	fixture.Bytes.resize(12 + tables.size() * 16);
	Write32(fixture.Bytes, 0, cff ? 0x4f54544f : 0x00010000);
	Write16(fixture.Bytes, 4, static_cast<std::uint16_t>(tables.size()));
	std::size_t index = 0;
	for (const auto& [tag, table] : tables) {
		while (fixture.Bytes.size() % 4 != 0) {
			fixture.Bytes.push_back(0);
		}

		const auto record = 12 + index++ * 16;
		fixture.Records[tag] = record;
		fixture.Offsets[tag] = fixture.Bytes.size();
		std::copy(tag.begin(), tag.end(), fixture.Bytes.begin() + static_cast<std::ptrdiff_t>(record));
		Write32(fixture.Bytes, record + 4, SumWords(table));
		Write32(fixture.Bytes, record + 8, static_cast<std::uint32_t>(fixture.Bytes.size()));
		Write32(fixture.Bytes, record + 12, static_cast<std::uint32_t>(table.size()));
		fixture.Bytes.insert(fixture.Bytes.end(), table.begin(), table.end());
	}

	Write32(fixture.Bytes, fixture.Offsets["head"] + 8, 0xb1b0afba - SumWords(fixture.Bytes));

	return fixture;
}

static void TestMappingAndBorrowedTables() {
	const auto fixture = MakeFont(false, true);
	const sgre::resource::SfntFont font(fixture.Bytes);
	font.ValidateChecksums();
	Require(font.OutlineKind() == sgre::resource::SfntOutlineKind::TrueType && font.GlyphCount() == 5 && font.UnitsPerEm() == 1000, "sfnt identity fields differ");
	Require(font.SelectedCharacterMap() == 0 && font.CharacterMaps().size() == 3, "sfnt native character-map selection differs");
	Require(font.MapCharacter(0x41) == 1 && font.MapCharacter(0x42) == 2 && font.MapCharacter(0x43) == 3, "sfnt direct delta mapping differs");
	Require(font.MapCharacter(0x61) == 4 && font.MapCharacter(0x62) == 0, "sfnt glyph array delta or missing glyph differs");
	Require(font.MapCharacter(0x40) == 0 && font.MapCharacter(0xffff) == 0 && font.MapCharacter(0x1f600) == 0, "sfnt BMP map broadened its character coverage");
	Require(font.MapCharacter(0x41, 1) == 3 && font.MapCharacter(0x1f600, 1) == 2 && font.MapCharacter(0x1f601, 1) == 3 && font.MapCharacter(0x110000, 1) == 0, "sfnt explicit coverage map differs");
	const auto head = font.FindTable("head");
	Require(head && head->data() == fixture.Bytes.data() + fixture.Offsets.at("head"), "sfnt table view lost its resource owner");
	Require(!font.FindTable("xxxx"), "absent sfnt table was synthesized");
	RequireFailure([&] {
		static_cast<void>(font.MapCharacter(0x41, 2));
	},
	    "not implemented");
	RequireFailure([&] {
		static_cast<void>(font.MapCharacter(0x41, 3));
	},
	    "font owner");
}

static void TestMetricTailsAndMissingVertical() {
	const auto fixture = MakeFont(true, true);
	const sgre::resource::SfntFont font(fixture.Bytes);
	font.ValidateChecksums();
	Require(font.OutlineKind() == sgre::resource::SfntOutlineKind::Cff, "sfnt CFF identity was not preserved");
	const std::array<std::int16_t, 5> horizontal_bearings{10, -20, 30, -40, 50};
	const std::array<std::int16_t, 5> vertical_bearings{-1, -2, -3, 4, 5};
	for (std::uint32_t index = 0; index < 5; ++index) {
		const auto horizontal = font.HorizontalMetrics(index);
		const auto vertical = font.VerticalMetrics(index);
		Require(horizontal.Advance == (index == 0 ? 500 : 600) && horizontal.SideBearing == horizontal_bearings[index], "sfnt horizontal repeated advance or bearing tail differs");
		Require(vertical && vertical->Advance == (index == 0 ? 700 : index == 1 ? 800
		                                                                        : 900) &&
		        vertical->SideBearing == vertical_bearings[index],
		    "sfnt vertical repeated advance or bearing tail differs");
	}

	const auto without_vertical = MakeFont(true, false);
	const sgre::resource::SfntFont horizontal_font(without_vertical.Bytes);
	Require(!horizontal_font.VerticalMetrics(0), "missing vertical metrics gained an inferred fallback");
	RequireFailure([&] {
		static_cast<void>(font.HorizontalMetrics(5));
	},
	    "font owner");
	RequireFailure([&] {
		static_cast<void>(horizontal_font.VerticalMetrics(5));
	},
	    "font owner");
}

static void TestFirstMatchingMapOrder() {
	auto fixture = MakeFont(false, false);
	const auto cmap = fixture.Offsets.at("cmap");
	Write16(fixture.Bytes, cmap + 4, 3);
	Write16(fixture.Bytes, cmap + 6, 1);
	Write16(fixture.Bytes, cmap + 12, 0);
	Write16(fixture.Bytes, cmap + 14, 3);
	const sgre::resource::SfntFont font(fixture.Bytes);
	Require(font.SelectedCharacterMap() == 0 && font.CharacterMaps()[0].PlatformId == 3 && font.MapCharacter(0x41) == 1, "sfnt reordered first matching native character map");
}

static void TestMalformedDirectoryAndMetrics() {
	const auto original = MakeFont(false, true);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(std::span(original.Bytes).first(11)));
	},
	    "table directory");
	auto fixture = original;
	Write32(fixture.Bytes, 0, 0x74746366);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "signature");
	fixture = original;
	Write16(fixture.Bytes, 4, 513);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "count");
	fixture = original;
	Write32(fixture.Bytes, fixture.Records.at("head") + 8, 0xffffffff);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "head");
	fixture = original;
	std::copy_n(fixture.Bytes.begin() + 12, 4, fixture.Bytes.begin() + 28);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "repeats");
	fixture = original;
	Write16(fixture.Bytes, fixture.Offsets.at("head") + 18, 0);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "units per em");
	fixture = original;
	Write16(fixture.Bytes, fixture.Offsets.at("hhea") + 34, 6);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "metric count");
	fixture = original;
	Write32(fixture.Bytes, fixture.Records.at("hmtx") + 12, 8);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "hmtx");
	fixture = original;
	fixture.Bytes[fixture.Records.at("vmtx")] = 'x';
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "paired");
}

static void TestMalformedMapsAndChecksums() {
	const auto original = MakeFont(false, false);
	auto fixture = original;
	const auto cmap = fixture.Offsets.at("cmap");
	Write32(fixture.Bytes, cmap + 8, 0xffffffff);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "cmap");
	fixture = original;
	Write16(fixture.Bytes, cmap + 28 + 6, 5);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "segment count");
	fixture = original;
	Write16(fixture.Bytes, cmap + 28 + 24, 0x40);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "overlap");
	fixture = original;
	Write16(fixture.Bytes, cmap + 28 + 36, 0xfffe);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "glyph array");
	fixture = original;
	Write16(fixture.Bytes, cmap + 28 + 28, 0);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "mapped glyph");
	fixture = original;
	Write32(fixture.Bytes, cmap + 72 + 36, 4);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "mapped glyph");
	fixture = original;
	Write16(fixture.Bytes, cmap + 6, 7);
	RequireFailure([&] {
		static_cast<void>(sgre::resource::SfntFont(fixture.Bytes));
	},
	    "native-preferred");
	fixture = original;
	fixture.Bytes[fixture.Offsets.at("head") + 24] ^= 1;
	const sgre::resource::SfntFont changed_font(fixture.Bytes);
	RequireFailure([&] {
		changed_font.ValidateChecksums();
	},
	    "head");
	fixture = original;
	fixture.Bytes[fixture.Offsets.at("head") + 8] ^= 1;
	const sgre::resource::SfntFont changed_adjustment(fixture.Bytes);
	RequireFailure([&] {
		changed_adjustment.ValidateChecksums();
	},
	    "whole-font");
}

int main() {
	try {
		TestMappingAndBorrowedTables();
		TestMetricTailsAndMissingVertical();
		TestFirstMatchingMapOrder();
		TestMalformedDirectoryAndMetrics();
		TestMalformedMapsAndChecksums();
		std::cout << "Sfnt reader tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Sfnt reader test failure: " << error.what() << '\n';

		return 1;
	}
}
