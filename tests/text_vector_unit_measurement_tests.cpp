#include "sgre/resource/opentype_layout.hpp"
#include "sgre/script/text_font_info.hpp"
#include "sgre/text/recursive_decomposition.hpp"
#include "sgre/text/vector_font.hpp"

#include "vector_font_fixture.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}
static void RequireFailure(const std::function<void()>& action, std::string_view expected) {
	try {
		action();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("unit measurement fixture expected rejection");
}
static void Word(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
	bytes.push_back(static_cast<std::uint8_t>(value >> 8));
	bytes.push_back(static_cast<std::uint8_t>(value));
}
static std::vector<std::uint8_t> Words(std::initializer_list<std::uint16_t> words) {
	std::vector<std::uint8_t> bytes;
	for (const auto word : words) {
		Word(bytes, word);
	}
	return bytes;
}
static std::vector<std::uint8_t> Layout(bool positioning, std::uint16_t covered, bool ligature = false) {
	// Independent ScriptList(latn/default) -> Feature(liga/kern) -> lookup0.
	auto bytes = Words({1, 0, 10, 30, 44, 1});
	bytes.insert(bytes.end(), {'l', 'a', 't', 'n'});
	const auto script = Words({8, 4, 0, 0, 0xffff, 1, 0});
	bytes.insert(bytes.end(), script.begin(), script.end());
	Word(bytes, 1);
	const std::string_view tag = positioning ? "kern" : "liga";
	bytes.insert(bytes.end(), tag.begin(), tag.end());
	const auto feature = Words({8, 0, 1, 0, 1, 4});
	bytes.insert(bytes.end(), feature.begin(), feature.end());
	Word(bytes, positioning ? 2 : ligature ? 4
	                                       : 1);
	const auto lookup = Words({0, 1, 8});
	bytes.insert(bytes.end(), lookup.begin(), lookup.end());
	if (positioning) {
		// Empty pair set with a real input coverage; covered execution remains gated.
		const auto table = Words({1, 12, 0, 0, 1, 18, 1, 1, covered, 0});
		bytes.insert(bytes.end(), table.begin(), table.end());
	} else if (ligature) {
		// One A+B -> glyph2 ligature, genuinely impossible without second input.
		const auto table = Words({1, 8, 1, 14, 1, 1, covered, 1, 4, 2, 2, 2});
		bytes.insert(bytes.end(), table.begin(), table.end());
	} else {
		const auto table = Words({2, 8, 1, 2, 1, 1, covered});
		bytes.insert(bytes.end(), table.begin(), table.end());
	}
	return bytes;
}
static std::vector<std::uint8_t> Font(bool substitution = false, bool positioning = false, bool ligature = false, std::span<const std::uint8_t> substitution_table = {}) {
	const auto original = vector_font_test::MakeFont();
	const sgre::resource::SfntFont reader(original);
	std::map<std::string, std::vector<std::uint8_t>> tables;
	for (const auto& item : reader.Tables()) {
		const auto bytes = *reader.FindTable(item.Tag);
		tables[item.Tag] = std::vector<std::uint8_t>(bytes.begin(), bytes.end());
	}
	vector_font_test::WriteWord(tables["hhea"], 34, 3);
	tables["hmtx"] = Words({500, 0, 700, 0, 1000, 0});
	if (substitution) {
		tables["GSUB"] = substitution_table.empty() ? Layout(false, 1, ligature) : std::vector<std::uint8_t>(substitution_table.begin(), substitution_table.end());
	}
	if (positioning) {
		tables["GPOS"] = Layout(true, 1);
	}
	std::vector<std::uint8_t> result(12 + tables.size() * 16);
	vector_font_test::WriteDoubleWord(result, 0, 0x4f54544f);
	vector_font_test::WriteWord(result, 4, static_cast<std::uint16_t>(tables.size()));
	std::size_t index = 0;
	for (const auto& [tag, bytes] : tables) {
		while (result.size() % 4 != 0) {
			result.push_back(0);
		}
		const auto record = 12 + index++ * 16;
		std::copy(tag.begin(), tag.end(), result.begin() + static_cast<std::ptrdiff_t>(record));
		vector_font_test::WriteDoubleWord(result, record + 8, static_cast<std::uint32_t>(result.size()));
		vector_font_test::WriteDoubleWord(result, record + 12, static_cast<std::uint32_t>(bytes.size()));
		result.insert(result.end(), bytes.begin(), bytes.end());
	}
	return result;
}
static std::vector<std::uint8_t> IsolatedLookup(std::uint16_t type, std::span<const std::vector<std::uint8_t>> subtables, std::uint16_t flags = 0) {
	// Real latn/default liga scheduling; the lookup owns all declared independent subtables.
	auto bytes = Words({1, 0, 10, 30, 44, 1});
	bytes.insert(bytes.end(), {'l', 'a', 't', 'n'});
	const auto prefix = Words({8, 4, 0, 0, 0xffff, 1, 0, 1});
	bytes.insert(bytes.end(), prefix.begin(), prefix.end());
	bytes.insert(bytes.end(), {'l', 'i', 'g', 'a'});
	const auto feature = Words({8, 0, 1, 0, 1, 4});
	bytes.insert(bytes.end(), feature.begin(), feature.end());
	Word(bytes, type);
	Word(bytes, flags);
	Word(bytes, static_cast<std::uint16_t>(subtables.size()));
	std::size_t offset = 6 + subtables.size() * 2;
	for (const auto& subtable : subtables) {
		Word(bytes, static_cast<std::uint16_t>(offset));
		offset += subtable.size();
	}
	for (const auto& subtable : subtables) {
		bytes.insert(bytes.end(), subtable.begin(), subtable.end());
	}
	return bytes;
}

static std::vector<std::uint8_t> ChainSubtable(std::uint16_t backtrack, std::uint16_t input, std::uint16_t lookahead) {
	auto bytes = Words({3});
	std::vector<std::size_t> coverage_fields;
	for (const auto count : {backtrack, input, lookahead}) {
		Word(bytes, count);
		for (std::uint16_t i = 0; i < count; ++i) {
			coverage_fields.push_back(bytes.size());
			Word(bytes, 0);
		}
	}
	// A real nested action stays unsafe if all required records can exist.
	Word(bytes, 1);
	Word(bytes, 0);
	Word(bytes, 0);
	const auto coverage = static_cast<std::uint16_t>(bytes.size());
	for (const auto field : coverage_fields) {
		vector_font_test::WriteWord(bytes, field, coverage);
	}
	const auto covered = Words({1, 1, 1});
	bytes.insert(bytes.end(), covered.begin(), covered.end());
	return bytes;
}

static void TestIsolatedLookupEligibility() {
	const auto multi = Words({1, 8, 1, 14, 1, 1, 1, 1, 4, 2, 2, 2});
	auto single = multi;
	vector_font_test::WriteWord(single, 20, 1);
	for (const auto& counts : {std::array<std::uint16_t, 3>{1, 1, 0}, {0, 1, 1}, {0, 2, 0}, {1, 1, 1}}) {
		const std::array<std::vector<std::uint8_t>, 1> subtables{ChainSubtable(counts[0], counts[1], counts[2])};
		const auto gsub = IsolatedLookup(6, subtables);
		const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(Font(true, false, false, gsub));
		const sgre::resource::SfntFont sfnt(*bytes);
		const sgre::resource::OpenTypeLayoutFont layout(sfnt);
		Require(layout.LookupInputCoversGlyph(0, 1) && !layout.LookupMayApplyToIsolatedGlyph(0, 1), "first-input coverage remains true while actual missing neighboring records prove a format3 chain cannot execute");
		sgre::text::VectorFont font(bytes);
		font.SetSize(24);
		Require(font.MeasureCodeUnit(u'A').Width == 1075.0f / 64 && font.InputBuffer().InputGlyphs().front().GlyphIndex == 1, "impossible isolated chain preserves the real original glyph and metrics");
	}
	for (const auto& subtable : {single, ChainSubtable(0, 1, 0)}) {
		const auto type = static_cast<std::uint16_t>(subtable.front() == 0 && subtable[1] == 3 ? 6 : 4);
		const std::array<std::vector<std::uint8_t>, 1> subtables{subtable};
		const auto gsub = IsolatedLookup(type, subtables);
		sgre::text::VectorFont font(std::make_shared<const std::vector<std::uint8_t>>(Font(true, false, false, gsub)));
		font.SetSize(24);
		RequireFailure([&] {
			static_cast<void>(font.MeasureCodeUnit(u'A'));
		},
		    "covered substitution");
	}
	const auto distinct_sets = Words({1, 10, 2, 18, 28, 1, 2, 1, 2, 1, 4, 2, 2, 2, 1, 4, 2, 1});
	const std::array<std::vector<std::uint8_t>, 1> distinct{distinct_sets};
	const auto distinct_gsub = IsolatedLookup(4, distinct);
	const auto distinct_font = Font(true, false, false, distinct_gsub);
	const sgre::resource::SfntFont distinct_sfnt(distinct_font);
	const sgre::resource::OpenTypeLayoutFont distinct_layout(distinct_sfnt);
	Require(!distinct_layout.LookupMayApplyToIsolatedGlyph(0, 1) && distinct_layout.LookupMayApplyToIsolatedGlyph(0, 2), "isolated proof selects the covered glyph's actual ligature set rather than another glyph's rule count");

	const std::array<std::vector<std::uint8_t>, 2> mixed{multi, single};
	const auto mixed_gsub = IsolatedLookup(4, mixed);
	sgre::text::VectorFont possible(std::make_shared<const std::vector<std::uint8_t>>(Font(true, false, false, mixed_gsub)));
	possible.SetSize(24);
	RequireFailure([&] {
		static_cast<void>(possible.MeasureCodeUnit(u'A'));
	},
	    "covered substitution");
	const std::array<std::vector<std::uint8_t>, 1> multi_only{multi};
	const auto filtered = IsolatedLookup(4, multi_only, 2);
	sgre::text::VectorFont flags(std::make_shared<const std::vector<std::uint8_t>>(Font(true, false, false, filtered)));
	flags.SetSize(24);
	RequireFailure([&] {
		static_cast<void>(flags.MeasureCodeUnit(u'A'));
	},
	    "covered substitution");
	auto extension = Words({1, 4, 0, 8});
	extension.insert(extension.end(), multi.begin(), multi.end());
	const std::array<std::vector<std::uint8_t>, 1> extended{extension};
	const auto extended_gsub = IsolatedLookup(7, extended);
	sgre::text::VectorFont wrapped(std::make_shared<const std::vector<std::uint8_t>>(Font(true, false, false, extended_gsub)));
	wrapped.SetSize(24);
	Require(wrapped.MeasureCodeUnit(u'A').Width == 1075.0f / 64, "actual extension target type/count supplies the same isolated impossibility proof");
	auto truncated = multi;
	truncated.resize(22);
	const std::array<std::vector<std::uint8_t>, 1> malformed{truncated};
	const auto malformed_gsub = IsolatedLookup(4, malformed);
	const auto malformed_font = Font(true, false, false, malformed_gsub);
	const sgre::resource::SfntFont sfnt(malformed_font);
	const sgre::resource::OpenTypeLayoutFont layout(sfnt);
	RequireFailure([&] {
		static_cast<void>(layout.LookupMayApplyToIsolatedGlyph(0, 1));
	},
	    "range exceeds");
}

static std::vector<std::uint8_t> ClassChainSubtable(std::span<const std::vector<std::array<std::uint16_t, 3>>> sets, std::array<std::uint16_t, 2> input_classes = {1, 0}, bool ranges = false) {
	auto bytes = Words({2, 0, 0, 0, 0, static_cast<std::uint16_t>(sets.size())});
	bytes.resize(12 + sets.size() * 2);
	const auto definition = static_cast<std::uint16_t>(bytes.size());
	const auto classes = ranges ? Words({2, 2, 1, 1, input_classes[0], 2, 2, input_classes[1]}) : Words({1, 1, 2, input_classes[0], input_classes[1]});
	bytes.insert(bytes.end(), classes.begin(), classes.end());
	for (const auto field : {4U, 6U, 8U}) {
		vector_font_test::WriteWord(bytes, field, definition);
	}
	vector_font_test::WriteWord(bytes, 2, static_cast<std::uint16_t>(bytes.size()));
	const auto coverage = Words({1, 2, 1, 2});
	bytes.insert(bytes.end(), coverage.begin(), coverage.end());
	for (std::size_t index = 0; index < sets.size(); ++index) {
		if (sets[index].empty()) {
			continue;
		}
		const auto set = bytes.size();
		vector_font_test::WriteWord(bytes, 12 + index * 2, static_cast<std::uint16_t>(set));
		Word(bytes, static_cast<std::uint16_t>(sets[index].size()));
		bytes.resize(bytes.size() + sets[index].size() * 2);
		for (std::size_t rule = 0; rule < sets[index].size(); ++rule) {
			vector_font_test::WriteWord(bytes, set + 2 + rule * 2, static_cast<std::uint16_t>(bytes.size() - set));
			const auto& counts = sets[index][rule];
			Word(bytes, counts[0]);
			for (std::uint16_t count = 0; count < counts[0]; ++count) {
				Word(bytes, 1);
			}
			Word(bytes, counts[1]);
			for (std::uint16_t count = 1; count < counts[1]; ++count) {
				Word(bytes, 1);
			}
			Word(bytes, counts[2]);
			for (std::uint16_t count = 0; count < counts[2]; ++count) {
				Word(bytes, 1);
			}
			// Keep a genuine nested action; missing context alone must prevent its execution.
			Word(bytes, 1);
			Word(bytes, 0);
			Word(bytes, 0);
		}
	}
	return bytes;
}

static void TestIsolatedClassChainSelection() {
	const std::vector<std::vector<std::array<std::uint16_t, 3>>> sets{{{0, 1, 0}}, {{1, 1, 0}, {0, 1, 1}, {0, 2, 0}, {1, 1, 1}}};
	for (const auto ranges : {false, true}) {
		const std::array<std::vector<std::uint8_t>, 1> subtables{ClassChainSubtable(sets, {1, 0}, ranges)};
		const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(Font(true, false, false, IsolatedLookup(6, subtables)));
		const sgre::resource::SfntFont sfnt(*bytes);
		const sgre::resource::OpenTypeLayoutFont layout(sfnt);
		Require(layout.LookupInputCoversGlyph(0, 1) && !layout.LookupMayApplyToIsolatedGlyph(0, 1), "class1 selects only rules requiring missing neighbors despite coverage index0 and possible class0");
		Require(layout.LookupMayApplyToIsolatedGlyph(0, 2), "class0 is selected for the other covered glyph and retains its actual possible action");
		sgre::text::VectorFont font(bytes);
		font.SetSize(24);
		Require(font.MeasureCodeUnit(u'A').Width == 1075.0f / 64 && font.InputBuffer().InputGlyphs().front().GlyphIndex == 1, "class chain eligibility preserves the genuine unit glyph and advance");
		RequireFailure([&] {
			static_cast<void>(font.MeasureCodeUnit(u'B'));
		},
		    "covered substitution");
	}
	for (const auto input_class : {0U, 3U}) {
		const std::vector<std::vector<std::array<std::uint16_t, 3>>> sparse{{}, {{0, 1, 0}}};
		const std::array<std::vector<std::uint8_t>, 1> subtables{ClassChainSubtable(sparse, {static_cast<std::uint16_t>(input_class), 1})};
		const auto bytes = Font(true, false, false, IsolatedLookup(6, subtables));
		const sgre::resource::SfntFont sfnt(bytes);
		const sgre::resource::OpenTypeLayoutFont layout(sfnt);
		Require(!layout.LookupMayApplyToIsolatedGlyph(0, 1), "null or out-of-array actual class set has no native rule to invoke");
	}
}

static void TestIsolatedClassChainConservativeGates() {
	const std::vector<std::vector<std::array<std::uint16_t, 3>>> impossible{{}, {{1, 1, 0}}};
	const std::vector<std::vector<std::array<std::uint16_t, 3>>> possible{{}, {{1, 1, 0}, {0, 1, 0}}};
	const auto blocked = ClassChainSubtable(impossible);
	const auto admitted_action = ClassChainSubtable(possible);
	for (const auto filtered : {false, true}) {
		const std::array<std::vector<std::uint8_t>, 2> subtables{blocked, filtered ? blocked : admitted_action};
		sgre::text::VectorFont font(std::make_shared<const std::vector<std::uint8_t>>(Font(true, false, false, IsolatedLookup(6, subtables, filtered ? 2 : 0))));
		font.SetSize(24);
		RequireFailure([&] {
			static_cast<void>(font.MeasureCodeUnit(u'A'));
		},
		    "covered substitution");
	}
	auto extension = Words({1, 6, 0, 8});
	extension.insert(extension.end(), blocked.begin(), blocked.end());
	const std::array<std::vector<std::uint8_t>, 1> subtables{extension};
	sgre::text::VectorFont font(std::make_shared<const std::vector<std::uint8_t>>(Font(true, false, false, IsolatedLookup(7, subtables))));
	font.SetSize(24);
	Require(font.MeasureCodeUnit(u'A').Width == 1075.0f / 64, "checked extension reaches the actual class-chain neighbor proof");
	auto class_zero = ClassChainSubtable(possible);
	vector_font_test::WriteWord(class_zero, 6, 0);
	const std::array<std::vector<std::uint8_t>, 1> no_definition{class_zero};
	const auto bytes = Font(true, false, false, IsolatedLookup(6, no_definition));
	const sgre::resource::SfntFont sfnt(bytes);
	const sgre::resource::OpenTypeLayoutFont layout(sfnt);
	Require(!layout.LookupMayApplyToIsolatedGlyph(0, 1), "native null input class definition selects class0, whose set is empty");
}

static void TestIsolatedClassChainMalformedBounds() {
	const std::vector<std::vector<std::array<std::uint16_t, 3>>> sets{{}, {{1, 1, 0}}};
	const auto valid = ClassChainSubtable(sets);
	for (const auto kind : {0, 1, 2, 3}) {
		auto table = valid;
		std::string_view expected;
		if (kind == 0) {
			table.resize(table.size() - 1);
			expected = "range exceeds";
		} else if (kind == 1) {
			const auto set = std::size_t(table[14]) * 256 + table[15];
			const auto rule = set + std::size_t(table[set + 2]) * 256 + table[set + 3];
			vector_font_test::WriteWord(table, rule + 4, 0);
			expected = "actual leading input";
		} else if (kind == 2) {
			vector_font_test::WriteWord(table, 14, 0xffff);
			expected = "offset";
		} else {
			const auto definition = std::size_t(table[6]) * 256 + table[7];
			vector_font_test::WriteWord(table, definition + 2, 3);
			expected = "class range";
		}
		const std::array<std::vector<std::uint8_t>, 1> subtables{table};
		const auto bytes = Font(true, false, false, IsolatedLookup(6, subtables));
		const sgre::resource::SfntFont sfnt(bytes);
		const sgre::resource::OpenTypeLayoutFont layout(sfnt);
		RequireFailure([&] {
			static_cast<void>(layout.LookupMayApplyToIsolatedGlyph(0, 1));
		},
		    expected);
	}
}

static void TestExtensionSingleUnitMeasurement() {
	auto extension = Words({1, 1, 0, 8});
	const auto single = Words({2, 8, 1, 2, 1, 1, 1});
	extension.insert(extension.end(), single.begin(), single.end());
	const std::array<std::vector<std::uint8_t>, 1> subtables{extension};
	const auto gsub = IsolatedLookup(7, subtables);
	sgre::text::VectorFont font(std::make_shared<const std::vector<std::uint8_t>>(Font(true, false, false, gsub)));
	font.SetSize(24);
	Require(font.MeasureCodeUnit(u'A').Width == 24 && font.InputBuffer().InputGlyphs().front().GlyphIndex == 2, "real extension-to-single application must occur before selecting the actual replacement glyph advance");
	auto invalid = extension;
	vector_font_test::WriteDoubleWord(invalid, 4, 0);
	const std::array<std::vector<std::uint8_t>, 1> malformed{invalid};
	RequireFailure([&] {
		const auto bytes = Font(true, false, false, IsolatedLookup(7, malformed));
		const sgre::resource::SfntFont sfnt(bytes);
		const sgre::resource::OpenTypeLayoutFont layout(sfnt);
		static_cast<void>(layout);
	},
	    "offset");
}

static std::uint32_t ReadNativeWord() {
	std::array<std::uint8_t, 4> bytes{};
	std::cin.read(reinterpret_cast<char*>(bytes.data()), 4);
	Require(bool(std::cin), "native unit input word extent");
	return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}
static void TestActualOwnedBufferAndLookupBoundaries() {
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(Font());
	sgre::text::VectorFont font(bytes);
	font.SetSize(24);
	const auto missing = font.MeasureCodeUnit(0x3000);
	Require(missing.Width == 12 && missing.Height == 0 && font.InputBuffer().InputGlyphs().front().Mask == 2, "missing U+3000 must publish the native global mask and measure actual glyph0, not a nominal em constant");
	const auto first_capacity = font.InputBuffer().State().ResidentCapacity;
	const auto a = font.MeasureCodeUnit(u'A');
	Require(a.Width == 1075.0f / 64 && a.Height == 0 && font.InputBuffer().InputGlyphs().front().GlyphIndex == 1 && font.InputBuffer().PositionRow(0).XAdvance26Dot6 == 1075, "one-unit consumer must publish actual mapped glyph and signed advance rows");
	font.SetSize(12);
	Require(font.MeasureCodeUnit(u'B').Width == 12 && font.InputBuffer().State().ResidentCapacity == first_capacity, "size changes must use the same actual buffer allocations and current live font metrics");
	RequireFailure([&] {
		static_cast<void>(font.MeasureCodeUnit(0x301));
	},
	    "mark/control");
	RequireFailure([&] {
		static_cast<void>(font.MeasureCodeUnit(0x200d));
	},
	    "mark/control");
	sgre::text::VectorFont substituted(std::make_shared<const std::vector<std::uint8_t>>(Font(true)));
	substituted.SetSize(24);
	Require(substituted.MeasureCodeUnit(u'A').Width == 24 && substituted.InputBuffer().InputGlyphs().front().GlyphIndex == 2, "actual liga single replacement must precede advance selection");
	sgre::text::VectorFont covered(std::make_shared<const std::vector<std::uint8_t>>(Font(false, true)));
	covered.SetSize(24);
	Require(covered.MeasureCodeUnit(u'B').Width == 24, "exact uncovered pair input must admit actual base advance");
	Require(covered.MeasureCodeUnit(u'A').Width == 1075.0f / 64 && covered.InputBuffer().PositionRow(0).XOffset26Dot6 == 0, "covered native pair lookup has no second actual input and preserves the sole glyph advance");
	Require(covered.MeasureCodeUnit(u'B').Width == 24, "rejected positioning must leave the real buffer reusable on the next clear");
	sgre::text::VectorFont contextual(std::make_shared<const std::vector<std::uint8_t>>(Font(true, false, true)));
	contextual.SetSize(24);
	Require(contextual.MeasureCodeUnit(u'B').Width == 24, "uncovered native ligature lookup must not block unrelated input");
	Require(contextual.MeasureCodeUnit(u'A').Width == 1075.0f / 64 && contextual.InputBuffer().InputGlyphs().front().GlyphIndex == 1, "a covered two-component ligature cannot execute in this isolated one-record buffer");
	sgre::text::VectorFont vertical(bytes, 4);
	vertical.SetSize(24);
	RequireFailure([&] {
		static_cast<void>(vertical.MeasureCodeUnit(u'A'));
	},
	    "left-to-right");
}
static sgre::script::SquirrelValue Construct(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& type, std::vector<sgre::script::SquirrelValue> arguments) {
	arguments.insert(arguments.begin(), vm.RootTable());
	return vm.Invoke(type, arguments).Value;
}
static void TestRealFontInfoSharedSource() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	const auto bytes = std::make_shared<const std::vector<std::uint8_t>>(Font());
	sgre::script::ScriptVectorFontBindings vectors(
	    vm, objects, [bytes](auto, auto) {
		    return [bytes]() -> std::optional<sgre::script::ScriptResourceAsset> {
			    sgre::script::ScriptResourceAsset result;
			    result.Kind = sgre::script::ScriptResourceDataKind::Binary;
			    result.Backing = bytes;
			    return result;
		    };
	    },
	    [](auto, auto) {
	    });
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("vector unit measurement must not upload GPU textures");
	},
	    [](auto) {
	    });
	sgre::scene::BitmapFontAssetCatalog catalog(cache);
	const auto source = Construct(vm, vectors.VectorFontClass(), {std::string("actual.otf")});
	const std::array<sgre::script::SquirrelValue, 1> receiver{source};
	static_cast<void>(vm.Invoke(vm.GetSlot(source, std::string("getLoading")), receiver));
	sgre::script::ScriptTextFontInfo first(vm, objects, psb, vectors, catalog, cache, source, {});
	sgre::script::ScriptTextFontInfo second(vm, objects, psb, vectors, catalog, cache, source, {});
	first.SetSize(24);
	second.SetSize(12);
	Require(first.MeasureCodeUnit(u'B').Width == 24 && second.MeasureCodeUnit(u'B').Width == 12 && first.MeasureCodeUnit(u'B').Width == 24, "actual FontInfo private size must be reapplied to shared VectorFont before each measurement");
	Require(vectors.Provider(source).SizeState().Word == 24 && cache.Count() == 0, "unit measurement must affect actual shared core size without texture allocation");
	vectors.ReleaseInstance(source);
	RequireFailure([&] {
		static_cast<void>(first.MeasureCodeUnit(u'B'));
	},
	    "retired during");
	second.Close();
	first.Close();
	vm.Close();
}
int main(int argc, char** argv) {
	try {
		if (argc == 2 && (std::string_view(argv[1]) == "--native" || std::string_view(argv[1]) == "--native-ja")) {
			// Stream one full real font plus native expected rows, no resource duplicates retained.
			const auto length = ReadNativeWord();
			Require(std::cin && length > 0 && length <= 64 * 1024 * 1024, "native unit font extent");
			auto bytes = std::make_shared<std::vector<std::uint8_t>>(length);
			std::cin.read(reinterpret_cast<char*>(bytes->data()), length);
			sgre::text::VectorFont font(bytes, 0, std::string_view(argv[1]) == "--native-ja" ? "ja" : "en");
			const auto count = ReadNativeWord();
			Require(std::cin && count <= 4096, "native unit request count");
			for (std::uint32_t i = 0; i < count; ++i) {
				const std::array<std::uint32_t, 4> request{ReadNativeWord(), ReadNativeWord(), ReadNativeWord(), ReadNativeWord()};
				Require(bool(std::cin) && request[1] <= 65535, "native unit request extent");
				font.SetSize(static_cast<float>(request[0]));
				const auto actual = font.MeasureCodeUnit(static_cast<std::uint16_t>(request[1]));
				Require(std::bit_cast<std::uint32_t>(actual.Width) == request[2] && std::bit_cast<std::uint32_t>(actual.Height) == request[3], "whole native unit measurement differs");
				const auto glyphs = font.InputBuffer().InputGlyphs();
				Require(glyphs.size() == 1, "native unit glyph count differs");
				const auto& glyph = glyphs.front();
				const std::array<std::uint32_t, 5> actual_glyph{glyph.GlyphIndex, glyph.Mask, glyph.Cluster, std::uint32_t(glyph.GlyphProperties) | (std::uint32_t(glyph.PrivateState) << 16), glyph.CharacterState};
				const auto position = font.InputBuffer().PositionRow(0);
				const std::array<std::uint32_t, 5> actual_position{std::bit_cast<std::uint32_t>(position.XAdvance26Dot6), std::bit_cast<std::uint32_t>(position.YAdvance26Dot6), std::bit_cast<std::uint32_t>(position.XOffset26Dot6), std::bit_cast<std::uint32_t>(position.YOffset26Dot6), position.PrivateWord};
				for (std::size_t word = 0; word < actual_glyph.size(); ++word) {
					const auto expected = ReadNativeWord();
					if (actual_glyph[word] != expected) {
						throw std::runtime_error("native unit glyph word " + std::to_string(word) + " expected " + std::to_string(expected) + " actual " + std::to_string(actual_glyph[word]) + " at size " + std::to_string(request[0]));
					}
				}
				for (std::size_t word = 0; word < actual_position.size(); ++word) {
					const auto expected = ReadNativeWord();
					if (actual_position[word] != expected) {
						throw std::runtime_error("native unit position word " + std::to_string(word) + " expected " + std::to_string(expected) + " actual " + std::to_string(actual_position[word]) + " at size " + std::to_string(request[0]));
					}
				}
			}
			std::cout << "whole native vector unit rows passed\n";
		} else {
			TestActualOwnedBufferAndLookupBoundaries();
			TestIsolatedLookupEligibility();
			TestIsolatedClassChainSelection();
			TestIsolatedClassChainConservativeGates();
			TestIsolatedClassChainMalformedBounds();
			TestExtensionSingleUnitMeasurement();
			TestRealFontInfoSharedSource();
			std::cout << "actual neutral vector unit measurement and FontInfo lifetime passed\n";
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
