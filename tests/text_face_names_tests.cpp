#include "sgre/text/face_names.hpp"

#include "sgre/text/property_settings.hpp"

#include <array>
#include <iostream>
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

	throw std::runtime_error("text face-name fixture expected failure");
}

static void TestNativeDefaultsAndEncounterOrder() {
	sgre::text::TextFaceNames names;
	Require(names.Count() == 2 && names.CodeUnitCount() == 10 && names.Utf8Name(0) == "normal" && names.Utf8Name(1) == "ruby", "native constructor must intern normal first and ruby second");
	Require(names.Intern("z") == 2 && names.Intern("a") == 3 && names.Intern("z") == 2 && names.Intern("normal") == 0 && names.Intern("NORMAL") == 4, "intern IDs must follow first encounter rather than sorted order or case folding");
	Require(names.Count() == 5 && names.Utf8Name(2) == "z" && names.Utf8Name(3) == "a", "duplicate name lookup must neither grow the owner nor change prior index names");
}

static void TestCStringBoundaryAndBorrowedStability() {
	sgre::text::TextFaceNames names;
	const auto index = names.Intern(std::string_view("A\0bad\xff", 6));
	const auto view = names.CodeUnits(index);
	const auto* original_pointer = view.data();
	for (std::uint32_t value = 0; value < 1000; ++value) {
		(void)names.Intern(std::to_string(value));
	}

	const std::array<std::uint16_t, 4> units{0x41, 0, 0xd800, 0x42};
	Require(names.InternCodeUnits(units) == index && names.Utf8Name(index) == "A" && view.size() == 1 && view[0] == 0x41 && view.data() == original_pointer, "first NUL must exclude later invalid units and borrowing must survive later interning");
	Require(names.Intern("") == names.InternCodeUnits({}), "empty names must be genuine interned keys");
}

static void TestUnicodeUnitsAndInvalidSurrogateBoundary() {
	sgre::text::TextFaceNames names;
	const auto index = names.Intern("\xe3\x81\x82\xf0\x9f\x98\x80");
	const auto units = names.CodeUnits(index);
	Require(units.size() == 3 && units[0] == 0x3042 && units[1] == 0xd83d && units[2] == 0xde00 && names.Utf8Name(index) == "\xe3\x81\x82\xf0\x9f\x98\x80", "valid supplementary names must retain UTF-16 units and round-trip UTF-8 exactly");
	const std::array<std::uint16_t, 1> high{0xd800};
	const std::array<std::uint16_t, 1> low{0xdc00};
	const auto high_index = names.InternCodeUnits(high);
	const auto low_index = names.InternCodeUnits(low);
	Require(high_index != low_index && names.CodeUnits(high_index)[0] == 0xd800 && names.CodeUnits(low_index)[0] == 0xdc00, "internal UTF-16 keys must preserve distinct unpaired surrogate units");
	RequireFailure([&] {
		(void)names.Utf8Name(high_index);
	},
	    "unpaired UTF-16 high surrogate");
	RequireFailure([&] {
		(void)names.Utf8Name(low_index);
	},
	    "unpaired UTF-16 low surrogate");
}

static void TestOwnerLimitsAndUnsafeNativeIndex() {
	RequireFailure([] {
		sgre::text::TextFaceNames names(1, 10);
	},
	    "native normal/ruby defaults");
	sgre::text::TextFaceNames names(3, 11);
	Require(names.Intern("X") == 2 && names.Intern("X") == 2 && names.Intern("normal") == 0, "duplicates must remain usable after name and total-unit capacities are filled");
	RequireFailure([&] {
		(void)names.Intern("Y");
	},
	    "name/code-unit limit");
	Require(names.Count() == 3 && names.CodeUnitCount() == 11 && names.CodeUnits(3).empty() && names.Utf8Name(0x7fffffff).empty(), "failed insertion must preserve owner state; positive out-of-range lookup uses native empty fallback");
	RequireFailure([&] {
		(void)names.CodeUnits(0xffffffff);
	},
	    "negative native text face index");
	RequireFailure([&] {
		(void)names.Intern(std::string_view("\xff", 1));
	},
	    "invalid leading byte");
	Require(names.Count() == 3, "rejected malformed UTF-8 must not manufacture a native face ID");
}

static void TestRealOwnerPropertyIntegration() {
	sgre::text::TextFaceNames names;
	sgre::text::TextFormattingDefaults defaults;
	sgre::text::TextPropertyReader reader;
	reader.ContainsRaw = [](std::string_view name) {
		return name == "face" || name == "rubyface";
	};
	reader.ReadString = [](std::string_view name) {
		return name == "face" ? "dialog" : "ruby";
	};
	reader.InternFace = [&](std::span<const std::uint16_t> units) {
		return names.InternCodeUnits(units);
	};
	sgre::text::ApplyTextDefaultProperties(defaults, reader);
	Require(defaults.NormalFaceIndex == 2 && defaults.RubyFaceIndex == 1 && names.Utf8Name(defaults.NormalFaceIndex) == "dialog" && names.Count() == 3, "default property application must use the actual private face owner and preserve its stable default ruby ID");
}

int main() {
	try {
		TestNativeDefaultsAndEncounterOrder();
		TestCStringBoundaryAndBorrowedStability();
		TestUnicodeUnitsAndInvalidSurrogateBoundary();
		TestOwnerLimitsAndUnsafeNativeIndex();
		TestRealOwnerPropertyIntegration();
		std::cout << "Five independent native text face-name owner groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
