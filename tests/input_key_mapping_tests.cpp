#include "sgre/input/key_mapping.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestNativeConversionVectors() {
	// These bounded samples retain original keyboard-loop outputs; unrelated key bytes are omitted.
	sgre::input::KeyMappings mappings;
	mappings.Clear();
	const std::array<std::array<std::string_view, 3>, 6> first{{{"vk_h", "L", "R_LEFT"}, {"vk_back", "R3", "LEFT"}, {"dik_g", "Z", "UP"}, {"dik_return", "Z", ""}, {"vk_sleep", "Y", "R_DOWN"}, {"dik_decimal", "BACK", "R_DOWN"}}};
	for (const auto& conversion : first) {
		Require(mappings.Add(conversion[0], conversion[1], conversion[2]), "known native key name was rejected");
	}

	sgre::input::KeyboardSample sample;
	sample.Available = true;
	sample.ScanKeys[28] = 0x80;
	sample.VirtualKeys[8] = 0x80;
	auto converted = mappings.Convert(sample);
	Require(converted.LogicalButtons == 0x88000 && converted.ControllerButtons == 4, "first original keyboard-loop vector differs");
	mappings.Clear();
	const std::array<std::array<std::string_view, 3>, 19> second{{{"vk_c", "A", "UP"}, {"dik_home", "R_DOWN", "L3"}, {"vk_back", "RIGHT", "A"}, {"vk_left", "C", "RIGHT"}, {"vk_b", "R1", "R_LEFT"}, {"dik_rmenu", "LEFT", "START"}, {"vk_accept", "R", "SELECT"}, {"dik_numlock", "Y", "A"}, {"dik_f6", "Z", "L2"}, {"vk_oem_period", "R", "R_RIGHT"}, {"vk_w", "RIGHT", "L1"}, {"vk_f5", "R1", "R1"}, {"dik_back", "Z", "RIGHT"}, {"vk_f", "Z", "R_RIGHT"}, {"dik_f4", "LEFT", "L1"}, {"vk_d", "R", "L2"}, {"dik_stop", "R", "X"}, {"dik_f14", "START", "L3"}, {"dik_noconvert", "RIGHT", ""}}};
	for (const auto& conversion : second) {
		Require(mappings.Add(conversion[0], conversion[1], conversion[2]), "known native custom key was rejected");
	}

	sample = {};
	sample.Available = true;
	for (const auto key : std::array<std::uint8_t, 4>{101, 123, 149, 199}) {
		sample.ScanKeys[key] = 0x80;
	}

	for (const auto key : std::array<std::uint8_t, 6>{8, 30, 37, 66, 67, 70}) {
		sample.VirtualKeys[key] = 0x80;
	}

	converted = mappings.Convert(sample);
	Require(converted.LogicalButtons == 0x100c119 && converted.ControllerButtons == 0x5069, "second original keyboard-loop vector differs");
}

static void TestDefaultsNamesAndAltReturn() {
	sgre::input::KeyMappings mappings;
	Require(mappings.ScanConversions().size() == 19 && mappings.VirtualConversions().size() == 3 && sgre::input::PhysicalKeyNames().size() == 312, "native key table extents differ");
	sgre::input::KeyboardSample sample;
	sample.Available = true;
	sample.ScanKeys[200] = 0x80;
	sample.VirtualKeys[13] = 0x80;
	auto converted = mappings.Convert(sample);
	Require(converted.LogicalButtons == 0x41 && converted.ControllerButtons == 0x1001, "default Up/Return mapping differs");
	sample.VirtualKeys[18] = 0x80;
	converted = mappings.Convert(sample);
	Require(converted.LogicalButtons == 0x40 && converted.ControllerButtons == 1, "Alt+Return was treated as game input");
	mappings.Clear();
	Require(mappings.Add("diK_return", "a", "A") && mappings.Add("DIK_RETURN", "b", "B"), "native case folding or duplicate append was rejected");
	sample = {};
	sample.Available = true;
	sample.ScanKeys[28] = 0x80;
	converted = mappings.Convert(sample);
	Require(converted.LogicalButtons == 3 && converted.ControllerButtons == 0x3000, "duplicate conversions did not accumulate");
	sample.ScanKeys[184] = 0x80;
	converted = mappings.Convert(sample);
	Require(converted.LogicalButtons == 0 && converted.ControllerButtons == 0, "right Alt did not suppress scan Return");
	sample.BackPulse = true;
	Require(mappings.Convert(sample).LogicalButtons == 0x100000, "native back pulse was lost");
	sample.Available = false;
	Require(mappings.Convert(sample).LogicalButtons == 0, "unavailable keyboard produced input");
	Require(!mappings.Add("unknown", "a", "a"), "unknown physical source was accepted");
	Require(mappings.Add("DIK_A", "unknown", ""), "known source with missing targets was rejected");
	Require(mappings.ScanConversions().back().LogicalButtons == 0 && mappings.ScanConversions().back().ControllerButtons == 0, "unknown target did not preserve zero");
	Require(mappings.Add(std::string_view("DIK_A\0ignored", 13), "X", "X"), "native zero-terminated source handling differs");

	for (const auto& key : sgre::input::PhysicalKeyNames()) {
		mappings.Clear();
		Require(mappings.Add(key.Name, "a", "a"), "a retained physical key name does not resolve");
		const auto entries = key.CodeSet == sgre::input::KeyboardCodeSet::Scan ? mappings.ScanConversions() : mappings.VirtualConversions();
		Require(entries.size() == 1 && entries[0].Source == key.Code, "physical compatibility code/set changed during lookup");
	}
}

int main() {
	try {
		TestNativeConversionVectors();
		TestDefaultsNamesAndAltReturn();
		std::cout << "native key mapping vectors, original names/defaults, duplicate bindings, and Alt+Return passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
