#include "sgre/resource/opentype_layout.hpp"
#include "sgre/text/recursive_decomposition.hpp"
#include "sgre/text/vector_font.hpp"

#include "vector_font_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("extension PairPos fixture expected failure");
}

static std::vector<std::uint8_t> Words(std::initializer_list<std::uint16_t> words) {
	std::vector<std::uint8_t> result;
	result.reserve(words.size() * 2);
	for (const auto word : words) {
		result.push_back(static_cast<std::uint8_t>(word >> 8));
		result.push_back(static_cast<std::uint8_t>(word));
	}

	return result;
}

static std::vector<std::uint8_t> PairTable(std::uint16_t format, bool device) {
	if (format == 2) {
		return Words({2, 18, 4, 0, 24, 24, 1, 1, 0xff38, 1, 1, 1, 1, 1, 2, 0, 0});
	}
	if (device) {
		return Words({1, 20, 0x14, 0, 1, 12, 1, 2, 0xff38, 0, 1, 1, 1});
	}
	return Words({1, 18, 4, 0, 1, 12, 1, 2, 0xff38, 1, 1, 1});
}

static std::vector<std::uint8_t> Positioning(std::uint16_t target_type, std::uint16_t flags, std::uint16_t pair_format, bool device, bool mixed) {
	auto scripts = Words({1, 0x6c61, 0x746e, 8, 4, 0, 0, 0xffff, 1, 0});
	auto features = Words({1, 0x6b65, 0x726e, 8, 0, 1, 0});
	auto target = target_type == 2 ? PairTable(pair_format, device) : Words({1, 8, 4, 0xff38, 1, 1, 1});
	const auto header_bytes = static_cast<std::uint16_t>(mixed ? 12 : 10);
	auto lookup = Words({9, flags, static_cast<std::uint16_t>(mixed ? 2 : 1), header_bytes});
	if (mixed) {
		vector_font_test::Append(lookup, Words({static_cast<std::uint16_t>(header_bytes + 8 + target.size())}));
	}
	vector_font_test::Append(lookup, Words({0}));
	vector_font_test::Append(lookup, Words({1, target_type, 0, 8}));
	vector_font_test::Append(lookup, target);
	if (mixed) {
		vector_font_test::Append(lookup, Words({1, 1, 0, 8, 1, 8, 4, 0xff38, 1, 1, 1}));
	}
	auto list = Words({1, 4});
	vector_font_test::Append(list, lookup);
	auto result = Words({1, 0, 10, static_cast<std::uint16_t>(10 + scripts.size()), static_cast<std::uint16_t>(10 + scripts.size() + features.size())});
	vector_font_test::Append(result, scripts);
	vector_font_test::Append(result, features);
	vector_font_test::Append(result, list);
	return result;
}

static std::shared_ptr<const std::vector<std::uint8_t>> Font(std::uint16_t target_type = 2, std::uint16_t flags = 16, std::uint16_t format = 1, bool device = false, bool mixed = false) {
	const auto original = vector_font_test::MakeFont();
	const sgre::resource::SfntFont reader(original);
	std::map<std::string, std::vector<std::uint8_t>> tables;
	for (const auto& table : reader.Tables()) {
		const auto bytes = reader.FindTable(table.Tag);
		Require(bytes.has_value(), "synthetic source table disappeared");
		tables[table.Tag] = std::vector<std::uint8_t>(bytes->begin(), bytes->end());
	}
	tables["GPOS"] = Positioning(target_type, flags, format, device, mixed);
	// Base glyphs1/2 and one real mark-filtering set containing glyph2.
	tables["GDEF"] = Words({1, 2, 14, 0, 0, 0, 24, 1, 1, 2, 1, 1, 1, 1, 0, 8, 1, 1, 2});
	auto output = std::make_shared<std::vector<std::uint8_t>>(12 + 16 * tables.size());
	vector_font_test::WriteDoubleWord(*output, 0, 0x4f54544f);
	vector_font_test::WriteWord(*output, 4, static_cast<std::uint16_t>(tables.size()));
	std::size_t index = 0;
	for (const auto& [tag, table] : tables) {
		while (output->size() % 4 != 0) {
			output->push_back(0);
		}
		const auto record = 12 + 16 * index++;
		std::copy(tag.begin(), tag.end(), output->begin() + static_cast<std::ptrdiff_t>(record));
		vector_font_test::WriteDoubleWord(*output, record + 8, static_cast<std::uint32_t>(output->size()));
		vector_font_test::WriteDoubleWord(*output, record + 12, static_cast<std::uint32_t>(table.size()));
		vector_font_test::Append(*output, table);
	}
	return output;
}

static void TestActualUnitExtensionPairs() {
	for (const auto format : {std::uint16_t{1}, std::uint16_t{2}}) {
		for (const auto flags : {std::uint16_t{0}, std::uint16_t{16}}) {
			const auto bytes = Font(2, flags, format);
			sgre::text::VectorFont font(bytes);
			font.SetSize(33);
			const sgre::resource::OpenTypeLayoutFont layout(font.Font(), sgre::resource::OpenTypeLayoutKind::Positioning);
			Require(layout.LookupInputCoversGlyph(0, 1) && layout.LookupRequiresFollowingGlyph(0), "covered direct extension PairPos formats1/2 require an actual following record");
			const auto size = font.MeasureCodeUnit(u'A');
			const auto advance = font.MetricServices().HorizontalAdvance(1);
			Require(size.Width == static_cast<float>(advance) / 64.0f && size.Height == 0 && font.InputBuffer().InputGlyphs().size() == 1 && font.InputBuffer().InputGlyphs()[0].GlyphIndex == 1, "one real unit must publish its own unchanged metric row without applying a fabricated second glyph");
			RequireFailure([&] {
				static_cast<void>(font.PrepareRun(u"AB"));
			},
			    "positioning");
		}
	}
}

static void TestIndependentScaleAndDeviceBoundaries() {
	for (const auto sign : {sgre::text::VectorFontMetricScaleSign::Nonnegative, sgre::text::VectorFontMetricScaleSign::Negative}) {
		sgre::text::VectorFont font(Font(2, 16, 1, true));
		font.SetSize(22);
		font.BindMetricServices(sign, sign);
		const auto size = font.MeasureCodeUnit(u'A');
		Require(size.Width == static_cast<float>(font.MetricServices().HorizontalAdvance(1)) / 64.0f && size.Height == 0, "missing-input proof must not depend on fresh zero HB scale provenance or evaluate device values");
		const sgre::resource::OpenTypeLayoutFont layout(font.Font(), sgre::resource::OpenTypeLayoutKind::Positioning);
		Require(!layout.CanApplyPairPositioning(0), "isolated exclusion must not admit filtered/extension/device pair execution");
		RequireFailure([&] {
			std::size_t work = 0;
			static_cast<void>(layout.MatchPairPositioning(0, 1, 2, work));
		},
		    "without device");
	}
}

static void TestOtherCoveredPositioningStillRejects() {
	for (const auto configuration : {std::array<std::uint16_t, 3>{1, 0, 0}, std::array<std::uint16_t, 3>{2, 1, 0}, std::array<std::uint16_t, 3>{2, 0, 1}}) {
		sgre::text::VectorFont font(Font(configuration[0], configuration[1], 1, false, configuration[2] != 0));
		font.SetSize(33);
		const sgre::resource::OpenTypeLayoutFont layout(font.Font(), sgre::resource::OpenTypeLayoutKind::Positioning);
		Require(!layout.LookupRequiresFollowingGlyph(0), "a single adjustment, unsupported flag or mixed subtable must retain actual positioning execution guard");
		RequireFailure([&] {
			static_cast<void>(font.MeasureCodeUnit(u'A'));
		},
		    "covered positioning");
	}
}

static void TestLookupBoundsAndOwnedLifetime() {
	auto bytes = Font();
	const std::weak_ptr<const std::vector<std::uint8_t>> weak = bytes;
	{
		sgre::text::VectorFont font(bytes);
		bytes.reset();
		font.SetSize(44);
		const sgre::resource::OpenTypeLayoutFont layout(font.Font(), sgre::resource::OpenTypeLayoutKind::Positioning);
		RequireFailure([&] {
			static_cast<void>(layout.LookupRequiresFollowingGlyph(1));
		},
		    "lookup index");
		Require(!weak.expired() && font.MeasureCodeUnit(u'A').Width > 0, "actual font owner must keep the borrowed extension/coverage bytes live");
	}
	Require(weak.expired(), "isolated-pair query must add no hidden font owner");
}

int main() {
	try {
		TestActualUnitExtensionPairs();
		TestIndependentScaleAndDeviceBoundaries();
		TestOtherCoveredPositioningStillRejects();
		TestLookupBoundsAndOwnedLifetime();
		std::cout << "VectorFont isolated extension PairPos tests passed (4 groups)\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
