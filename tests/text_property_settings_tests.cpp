#include "sgre/text/property_settings.hpp"

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

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("text property fixture expected failure");
}

static void TestRawPresenceAndIndependentTypedReads() {
	sgre::text::TextFormattingDefaults defaults;
	std::vector<std::string> events;
	sgre::text::TextPropertyReader reader;
	reader.ContainsRaw = [&](std::string_view name) {
		events.push_back("raw:" + std::string(name));

		return name == "fontsize";
	};
	reader.ReadInteger = [&](std::string_view name) {
		events.push_back("int:" + std::string(name));

		return 25;
	};
	reader.ReadNumber = [&](std::string_view name) {
		events.push_back("float:" + std::string(name));

		return 25.75f;
	};
	sgre::text::ApplyTextDefaultProperties(defaults, reader);
	Require(defaults.FontSize == 25 && defaults.LineSize == 25.75f && defaults.BigFontSize == 50 && defaults.SmallFontSize == 12.5f && defaults.RubySize == 25.0f / 3.0f, "fontsize derivation must retain integer normal size and independent late floating line size");
	Require(events == std::vector<std::string>{"raw:face", "raw:rubyface", "raw:bold", "raw:fontsize", "int:fontsize", "raw:bigfontsize", "raw:smallfontsize", "raw:rubysize", "raw:smallfontsize", "raw:bigfontsize", "raw:rubysize", "raw:rubyoffset", "raw:color", "raw:shadow", "raw:shadowcolor", "raw:shadowdiff", "raw:edge", "raw:edgecolor", "raw:edgesize", "raw:linespacing", "raw:pitch", "raw:linesize", "raw:fontsize", "float:fontsize", "raw:align", "raw:valign"}, "raw presence probes and general typed reads must preserve original order and repetitions");
}

static void TestExplicitSizesAndAppearance() {
	sgre::text::TextFormattingDefaults defaults;
	defaults.Italic = true;
	std::map<std::string, float> numbers{{"smallfontsize", 8.25f}, {"bigfontsize", 91.5f}, {"rubysize", 0}, {"rubyoffset", -7}, {"linespacing", -2.5f}, {"pitch", 1.75f}, {"linesize", 11.25f}};
	std::map<std::string, std::int32_t> integers{{"fontsize", 27}, {"color", -1}, {"shadowcolor", 0x12345678}, {"shadowdiff", -3}, {"edgecolor", 0x23456789}, {"edgesize", -4}, {"align", 9}, {"valign", -10}};
	std::map<std::string, bool> flags{{"bold", true}, {"shadow", false}, {"edge", true}};
	sgre::text::TextPropertyReader reader;
	reader.ContainsRaw = [&](std::string_view name) {
		const std::string key(name);

		return numbers.contains(key) || integers.contains(key) || flags.contains(key);
	};
	reader.ReadInteger = [&](std::string_view name) {
		return integers.at(std::string(name));
	};
	reader.ReadNumber = [&](std::string_view name) {
		return numbers.at(std::string(name));
	};
	reader.ReadBoolean = [&](std::string_view name) {
		return flags.at(std::string(name));
	};
	sgre::text::ApplyTextDefaultProperties(defaults, reader);
	Require(defaults.FontSize == 27 && defaults.SmallFontSize == 8.25f && defaults.BigFontSize == 91.5f && defaults.RubySize == 0 && defaults.RubyOffset == -7 && defaults.LineSize == 11.25f, "present explicit sizes must override derivation without skipping zero values");
	Require(defaults.Bold && defaults.Italic && !defaults.Shadow && defaults.Edge && defaults.Color == 0xffffffff && defaults.ShadowColor == 0x12345678 && defaults.ShadowOffset == -3 && defaults.EdgeColor == 0x23456789 && defaults.EdgeSize == -4 && defaults.LineSpacing == -2.5f && defaults.Pitch == 1.75f && defaults.Alignment == 9 && defaults.VerticalAlignment == -10, "default fields must use exact signed/color conversions and preserve the absent italic property");
}

static void TestUtf8BoundaryAndRealFaceInterning() {
	sgre::text::TextFormattingDefaults defaults;
	std::vector<std::vector<std::uint16_t>> names;
	sgre::text::TextPropertyReader reader;
	reader.ContainsRaw = [](std::string_view name) {
		return name == "face" || name == "rubyface";
	};
	reader.ReadString = [](std::string_view name) {
		return name == "face" ? std::string("A\xf0\x9f\x98\x80\0bad\xff", 10) : std::string();
	};
	reader.InternFace = [&](std::span<const std::uint16_t> units) {
		names.emplace_back(units.begin(), units.end());

		return static_cast<std::uint32_t>(40 + names.size());
	};
	sgre::text::ApplyTextDefaultProperties(defaults, reader);
	Require(names == std::vector<std::vector<std::uint16_t>>{{0x41, 0xd83d, 0xde00}, {}} && defaults.NormalFaceIndex == 41 && defaults.RubyFaceIndex == 42, "face assignment must use its real returned intern ID, preserve UTF-16 surrogate units and stop before invalid bytes beyond NUL");
}

static void TestOptionAliasesAndImmediateWordBreak() {
	sgre::text::TextFormattingState formatting;
	sgre::text::TextProcessingOptions options;
	options.Following = {0x21};
	std::vector<std::string> events;
	sgre::text::TextPropertyReader reader;
	reader.ContainsRaw = [&](std::string_view name) {
		events.push_back("raw:" + std::string(name));

		return name == "following" || name == "end" || name == "vertical" || name == "kinsoku_max" || name == "word_break" || name == "ignore_over" || name == "ignore_overy" || name == "ignore_overx" || name == "texture_size";
	};
	reader.ReadString = [](std::string_view name) {
		return name == "end" ? "XYZ" : "";
	};
	reader.ReadBoolean = [&](std::string_view name) {
		events.push_back("bool:" + std::string(name));

		return name == "vertical" || name == "ignore_over" || name == "ignore_overx";
	};
	reader.ReadInteger = [](std::string_view name) {
		return name == "texture_size" ? -17 : -3;
	};
	sgre::text::ApplyTextOptionProperties(formatting, options, reader);
	Require(formatting.Vertical && !formatting.Defaults.WordBreak && !formatting.Current.WordBreak && options.Following.empty() && options.End == std::vector<std::uint16_t>{0x58, 0x59, 0x5a} && options.MaximumProhibitionCount == -3 && options.RequestedTextureSize == -17, "option setters must preserve empty strings, signed stored requests and immediate default/current word-break writes");
	Require(options.IgnoreHorizontalOverflow && !options.IgnoreVerticalOverflow, "ignore_overy must override the earlier ignore_over alias without changing horizontal overflow");
	Require(events[events.size() - 1] == "raw:texture_size", "requested texture size must be the final property and must not allocate an atlas");
}

static void TestMissingServicesAndPartialState() {
	sgre::text::TextFormattingDefaults defaults;
	RequireFailure([&] {
		sgre::text::ApplyTextDefaultProperties(defaults, {});
	},
	    "raw presence reader");
	sgre::text::TextPropertyReader reader;
	reader.ContainsRaw = [](std::string_view name) {
		return name == "bold" || name == "fontsize";
	};
	reader.ReadBoolean = [](std::string_view) {
		return true;
	};
	RequireFailure([&] {
		sgre::text::ApplyTextDefaultProperties(defaults, reader);
	},
	    "fontsize requires the integer reader");
	Require(defaults.Bold && defaults.FontSize == 24, "earlier native field writes must remain visible when a later required service rejects");
	reader.ContainsRaw = [](std::string_view name) {
		return name == "face";
	};
	reader.ReadString = [](std::string_view) {
		return "actual";
	};
	RequireFailure([&] {
		sgre::text::ApplyTextDefaultProperties(defaults, reader);
	},
	    "actual face-name owner");
	Require(defaults.NormalFaceIndex == 0, "missing face owner must not fabricate an intern identity");
}

int main() {
	try {
		TestRawPresenceAndIndependentTypedReads();
		TestExplicitSizesAndAppearance();
		TestUtf8BoundaryAndRealFaceInterning();
		TestOptionAliasesAndImmediateWordBreak();
		TestMissingServicesAndPartialState();
		std::cout << "Five independent text default/option property application groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
