#include "../keyboard_sample.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using Mappings = sgre::input::KeyMappings;
using Sample = sgre::input::KeyboardSample;
using Reader = sgre::platform::MacPhysicalKeyReader;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}
	throw std::runtime_error("expected focused keyboard rejection: " + std::string(expected));
}

struct KeyState {
	std::array<bool, 128> Down{};
	std::size_t Reads = 0;
	std::size_t FailAt = 0;
};

static bool ReadPreparedKey(void* context, std::uint16_t code) {
	auto& state = *static_cast<KeyState*>(context);
	Require(code < state.Down.size(), "platform requested an unbounded prepared key code");
	++state.Reads;
	Require(state.Reads <= 256, "focused key sample exceeded its bounded table");
	if (state.FailAt != 0 && state.Reads == state.FailAt) {
		throw std::runtime_error("prepared reader failure");
	}

	return state.Down[code];
}

static Reader BorrowReader(KeyState& state) {
	Reader reader;
	reader.Context = &state;
	reader.Read = ReadPreparedKey;

	return reader;
}

struct ScriptBinding {
	std::string_view Source;
	std::string_view Target;
	std::uint16_t MacCode;
	std::uint8_t NativeCode;
	bool Virtual;
	std::uint32_t Logical;
	std::uint32_t Controller;
};

// Original keybind's 28 rows, then override's direct DIK_W/L3 call. Numeric host codes
// are independent SDK/Apple USB-to-ADB facts, not extracted from the runtime mapping table.
static constexpr std::array<ScriptBinding, 29> kScriptBindings{{
    {"DIK_NUMPADENTER", "A", 0x4c, 0x9c, false, 0x1, 0x1000},
    {"DIK_A", "A", 0x00, 0x1e, false, 0x1, 0x1000},
    {"DIK_S", "A", 0x01, 0x1f, false, 0x1, 0x1000},
    {"DIK_D", "A", 0x02, 0x20, false, 0x1, 0x1000},
    {"DIK_SPACE", "A", 0x31, 0x39, false, 0x1, 0x1000},
    {"VK_RETURN", "A", 0x24, 0x0d, true, 0x1, 0x1000},
    {"DIK_BACK", "B", 0x33, 0x0e, false, 0x2, 0x2000},
    {"DIK_X", "B", 0x07, 0x2d, false, 0x2, 0x2000},
    {"DIK_INSERT", "Y", 0x72, 0xd2, false, 0x800, 0x8000},
    {"DIK_DELETE", "X", 0x75, 0xd3, false, 0x400, 0x4000},
    {"DIK_1", "START", 0x12, 0x02, false, 0x8, 0x10},
    {"DIK_UP", "UP", 0x7e, 0xc8, false, 0x40, 0x1},
    {"DIK_LEFT", "LEFT", 0x7b, 0xcb, false, 0x20, 0x4},
    {"DIK_DOWN", "DOWN", 0x7d, 0xd0, false, 0x80, 0x2},
    {"DIK_RIGHT", "RIGHT", 0x7c, 0xcd, false, 0x10, 0x8},
    {"DIK_NUMPAD8", "UP", 0x5b, 0x48, false, 0x40, 0x1},
    {"DIK_NUMPAD4", "LEFT", 0x56, 0x4b, false, 0x20, 0x4},
    {"DIK_NUMPAD2", "DOWN", 0x54, 0x50, false, 0x80, 0x2},
    {"DIK_NUMPAD6", "RIGHT", 0x58, 0x4d, false, 0x10, 0x8},
    {"DIK_Q", "L1", 0x0c, 0x10, false, 0x200, 0x100},
    {"DIK_COMMA", "L1", 0x2b, 0x33, false, 0x200, 0x100},
    {"VK_CONTROL", "L1", 0x3b, 0x11, true, 0x200, 0x100},
    {"DIK_E", "R1", 0x0e, 0x12, false, 0x100, 0x200},
    {"DIK_PERIOD", "R1", 0x2f, 0x34, false, 0x100, 0x200},
    {"DIK_Z", "L2", 0x06, 0x2c, false, 0x10000, 0},
    {"DIK_C", "R2", 0x08, 0x2e, false, 0x20000, 0},
    {"DIK_LBRACKET", "R_UP", 0x21, 0x1a, false, 0x800000, 0},
    {"DIK_RBRACKET", "R_DOWN", 0x1e, 0x1b, false, 0x1000000, 0},
    {"DIK_W", "L3", 0x0d, 0x11, false, 0x40000, 0x40},
}};

static Mappings ScriptMappings() {
	Mappings mappings;
	mappings.Clear();
	for (const auto& binding : kScriptBindings) {
		Require(mappings.Add(binding.Source, binding.Target, binding.Target), "original source key no longer resolves");
	}

	return mappings;
}

static void TestAllScriptBindingsFocused() {
	const auto mappings = ScriptMappings();
	Require(mappings.ScanConversions().size() == 27 && mappings.VirtualConversions().size() == 2, "original script source-key extents changed");
	for (const auto& binding : kScriptBindings) {
		KeyState state;
		state.Down[binding.MacCode] = true;
		Sample sample;
		sample.Available = true;
		sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, false, BorrowReader(state));
		Require(state.Reads != 0, "focused helper never called the actual reader seam");
		Require((binding.Virtual ? sample.VirtualKeys[binding.NativeCode] : sample.ScanKeys[binding.NativeCode]) == 0x80, "original source key projected to the wrong compatibility byte");

		Mappings isolated;
		isolated.Clear();
		Require(isolated.Add(binding.Source, binding.Target, binding.Target), "isolated original binding failed");
		const auto buttons = isolated.Convert(sample);
		Require(buttons.LogicalButtons == binding.Logical && buttons.ControllerButtons == binding.Controller, "independent original source-to-button vector differs");

		state = {};
		sample = {};
		sample.Available = true;
		sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, false, BorrowReader(state));
		const auto released = mappings.Convert(sample);
		Require(released.LogicalButtons == 0 && released.ControllerButtons == 0, "released prepared keys retained a game button");
	}
}

static void TestInsertDeleteKeypadAndControls() {
	const auto mappings = ScriptMappings();
	KeyState state;
	state.Down[0x72] = true;
	Sample sample;
	sample.Available = true;
	sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, false, BorrowReader(state));
	Require(sample.ScanKeys[0xd2] == 0x80 && sample.VirtualKeys[0x2d] == 0x80 && sample.ScanKeys[0x52] == 0 && sample.ScanKeys[0xd3] == 0 && sample.VirtualKeys[0x2e] == 0, "dedicated Insert was conflated with keypad0 or forward Delete");
	Require(mappings.Convert(sample).LogicalButtons == 0x800, "actual script Insert/Y action changed");

	state = {};
	state.Down[0x52] = true;
	sample = {};
	sample.Available = true;
	sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, false, BorrowReader(state));
	Require(sample.ScanKeys[0x52] == 0x80 && sample.VirtualKeys[0x60] == 0x80 && sample.ScanKeys[0xd2] == 0 && sample.VirtualKeys[0x2d] == 0, "keypad0 became dedicated Insert");

	state = {};
	state.Down[0x3e] = true;
	sample = {};
	sample.Available = true;
	sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, false, BorrowReader(state));
	Require(sample.ScanKeys[0x9d] == 0x80 && sample.VirtualKeys[0xa3] == 0x80 && sample.VirtualKeys[0x11] == 0x80 && sample.VirtualKeys[0xa2] == 0, "right Control did not aggregate independently");
	Require(mappings.Convert(sample).LogicalButtons == 0x200, "script generic Control/L1 lost right Control");

	Mappings return_mapping;
	return_mapping.Clear();
	Require(return_mapping.Add("DIK_RETURN", "A", "A") && return_mapping.Add("VK_RETURN", "B", "B"), "Return fixture mapping failed");
	state = {};
	state.Down[0x24] = true;
	state.Down[0x3a] = true;
	sample = {};
	sample.Available = true;
	sgre::platform::SampleMacPhysicalKeyboard(sample, return_mapping, false, BorrowReader(state));
	Require(sample.ScanKeys[0x1c] == 0x80 && sample.VirtualKeys[0x0d] == 0x80, "Return physical state was discarded");
	Require(return_mapping.Convert(sample).LogicalButtons == 0, "original Alt+Return suppression changed");
}

static void TestPreservedSnapshotAndUnsupportedDomains() {
	KeyState state;
	Sample sample;
	sample.Available = true;
	sample.BackPulse = true;
	sample.VirtualKeys[0x70] = 0x80;
	sample.VirtualKeys[2] = 0x80;
	Mappings mappings;
	sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, false, BorrowReader(state));
	Require(sample.Available && sample.BackPulse && sample.VirtualKeys[0x70] == 0x80 && sample.VirtualKeys[2] == 0x80, "focus/function/mouse snapshot was replaced");

	for (const auto source : {"DIK_CONVERT", "DIK_AT", "VK_A", "VK_OEM_1"}) {
		mappings.Clear();
		Require(mappings.Add(source, "A", "A"), "unsupported-domain fixture source is not a known original key");
		state = {};
		const auto before = sample;
		RequireError([&] {
			sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, false, BorrowReader(state));
		},
		    std::string_view(source).starts_with("DIK_") ? "physical-key compatibility" : "layout-specific virtual-key");
		Require(state.Reads == 0 && sample.ScanKeys == before.ScanKeys && sample.VirtualKeys == before.VirtualKeys, "unrepresentable mapping read or mutated key state before rejection");
	}

	mappings.Clear();
	Require(mappings.Add("VK_CANCEL", "A", "A"), "Break fixture source failed");
	state = {};
	RequireError([&] {
		sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, false, BorrowReader(state));
	},
	    "layout-specific virtual-key");
	sample.VirtualKeys[3] = 0x80;
	sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, true, BorrowReader(state));
	Require(sample.VirtualKeys[3] == 0x80, "actual learned Break snapshot was replaced");

	sample = {};
	state = {};
	sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, false, BorrowReader(state));
	Require(state.Reads == 0 && !sample.Available && mappings.Convert(sample).LogicalButtons == 0, "unfocused helper sampled or published keyboard input");
}

static void TestReaderFailureAndSynchronousBorrow() {
	const auto mappings = ScriptMappings();
	Sample sample;
	sample.Available = true;
	Reader missing;
	RequireError([&] {
		sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, false, missing);
	},
	    "physical key-state reader");
	const auto before = sample;
	KeyState state;
	state.Down[0x35] = true;
	state.FailAt = 3;
	RequireError([&] {
		sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, false, BorrowReader(state));
	},
	    "prepared reader failure");
	Require(sample.ScanKeys == before.ScanKeys && sample.VirtualKeys == before.VirtualKeys, "failed synchronous sample partially published keys");

	{
		KeyState local;
		local.Down[0x72] = true;
		sgre::platform::SampleMacPhysicalKeyboard(sample, mappings, false, BorrowReader(local));
	}
	Require(sample.ScanKeys[0xd2] == 0x80 && mappings.Convert(sample).LogicalButtons == 0x800, "completed sample retained a reader borrow");
}

int main() {
	try {
		// No NSApplication, window, hardware query, input posting, focus or activation is performed.
		TestAllScriptBindingsFocused();
		TestInsertDeleteKeypadAndControls();
		TestPreservedSnapshotAndUnsupportedDomains();
		TestReaderFailureAndSynchronousBorrow();
		std::cout << "29 supplied-script bindings, independent focused vectors, controls, unsupported domains and synchronous reader ownership passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
