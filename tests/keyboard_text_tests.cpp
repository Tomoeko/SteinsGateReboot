#include "sgre/input/function_keys.hpp"
#include "sgre/script/input_bindings.hpp"
#include "sgre/script/source_compiler.hpp"

#include "keyboard_text_native_vectors.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("missing expected keyboard text rejection");
}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, std::string_view name, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), vm.RootTable());
	return vm.Invoke(vm.GetSlot(vm.RootTable(), std::string(name)), arguments).Value;
}

static void TestOriginalWindowWords() {
	for (const auto& row : kNativeTextKeys) {
		sgre::input::FunctionKeyState state;
		state.SetExcludedKeys({row[1], row[2], row[3]});
		state.QueueKeyDown(static_cast<std::uint16_t>(row[4]));
		state.QueueKeyDown(static_cast<std::uint16_t>(row[0]));
		Require(state.TextSnapshot().Characters.empty() && state.TextSnapshot().DownKey == 0, "queued native key callback must not publish before VSync");
		state.Advance({});
		const auto sample = state.TextSnapshot();
		const auto expected_byte_count = row[6] == 256 ? 0u : 1u;
		Require(sample.DownKey == static_cast<std::int32_t>(row[5]) && sample.Characters.size() == expected_byte_count, "original key filter/publication mismatch for word " + std::to_string(row[0]));
		if (expected_byte_count != 0) {
			Require(static_cast<std::uint8_t>(sample.Characters[0]) == row[6], "original navigation key must append its low byte before special-key exclusion");
		}
		Require(state.TextSnapshot().Characters == sample.Characters && state.TextSnapshot().DownKey == sample.DownKey, "snapshot getters must not consume current text/key");
		state.Advance({});
		Require(state.TextSnapshot().Characters.empty() && state.TextSnapshot().DownKey == 0, "next empty VSync must publish empty text and zero last-key");
	}
	for (const auto& row : kNativeTextCharacters) {
		sgre::input::FunctionKeyState state;
		state.QueueCharacter(static_cast<std::uint16_t>(row[0]));
		state.Advance({});
		const auto sample = state.TextSnapshot();
		Require(sample.Characters.size() == 1 && static_cast<std::uint8_t>(sample.Characters[0]) == row[1] && sample.DownKey == 0, "native WM_CHAR callback must preserve exactly the low byte, independently of DownKey");
	}
}

static void TestSharedVSyncAndBoundedOwner() {
	sgre::input::FunctionKeyState state;
	Require(state.TextSnapshot().Characters.empty() && state.TextSnapshot().DownKey == 0, "native text globals are zero/empty before any VSync");
	state.QueueCharacter(0x41);
	state.QueueKeyDown(0x41);
	state.QueueCharacter(0x3042);
	state.QueueKeyDown(0x25);
	sgre::input::KeyboardSample sample;
	sample.Available = true;
	sample.VirtualKeys[0x70] = 0x80;
	state.Advance(sample);
	Require(state.TextSnapshot().Characters == "AB%" && state.TextSnapshot().DownKey == 0x25 && state.Snapshot().Pressed(1) == 1, "single actual VSync must publish event bytes and advance the existing F-key snapshot");
	state.QueueCharacter(0);
	state.QueueCharacter(0x42);
	state.QueueKeyDown(0x10);
	Require(state.TextSnapshot().Characters == "AB%" && state.TextSnapshot().DownKey == 0x25, "new window events must leave the last completed publication unchanged");
	state.Advance({});
	Require(state.TextSnapshot().Characters == std::string_view("\0B", 2) && state.TextSnapshot().DownKey == 0 && state.Snapshot().Previous == 1 && state.Snapshot().Current == 0, "unfocused VSync still publishes delivered events, including raw NUL, before clearing pending state");
	for (std::size_t index = 0; index != sgre::input::KeyboardTextState::kMaximumPendingBytes; ++index) {
		state.QueueCharacter(0x7f);
	}
	RequireFailure([&] {
		state.QueueCharacter(0x41);
	},
	    "pending byte limit");
	RequireFailure([&] {
		state.QueueKeyDown(0x25);
	},
	    "pending byte limit");
	state.Advance({});
	Require(state.TextSnapshot().Characters.size() == sgre::input::KeyboardTextState::kMaximumPendingBytes && std::all_of(state.TextSnapshot().Characters.begin(), state.TextSnapshot().Characters.end(), [](char value) {
		return value == 0x7f;
	}) && state.TextSnapshot().DownKey == 0,
	    "failed append must preserve the complete bounded pending publication and prior key state");
	state.Advance({});
	std::size_t rejections = 0;
	std::thread foreign([&] {
		for (const auto operation : {0, 1, 2, 3}) {
			try {
				if (operation == 0) {
					state.QueueCharacter(0x41);
				} else if (operation == 1) {
					state.QueueKeyDown(0x41);
				} else if (operation == 2) {
					state.Advance({});
				} else {
					static_cast<void>(state.TextSnapshot());
				}
			} catch (const std::runtime_error&) {
				++rejections;
			}
		}
	});
	foreign.join();
	Require(rejections == 4 && state.TextSnapshot().Characters.empty(), "window events/publication/readback must all reject a foreign thread without altering the owner");
}

static void TestActualRootBindingsAndRetirement() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::input::KeyMappings mappings;
	sgre::input::FunctionKeyState actual_owner;
	auto bindings = std::make_unique<sgre::script::ScriptInputBindings>(vm, objects, mappings);
	RequireFailure([&] {
		static_cast<void>(Call(vm, "getWMChar"));
	},
	    "actual input snapshot provider");
	RequireFailure([&] {
		static_cast<void>(Call(vm, "getDownKey"));
	},
	    "actual input snapshot provider");
	bool reenter = false;
	bool fail = false;
	std::size_t reads = 0;
	bindings->ConfigureFunctionKeys([&] {
		return actual_owner.Snapshot();
	});
	bindings->ConfigureKeyboardText([&] {
		++reads;
		if (reenter) {
			static_cast<void>(Call(vm, "funcKeyPressed", {std::int32_t{-1}}));
		}
		if (fail) {
			throw std::runtime_error("actual event owner failed");
		}
		return actual_owner.TextSnapshot();
	});
	RequireFailure([&] {
		bindings->ConfigureKeyboardText([&] {
			return actual_owner.TextSnapshot();
		});
	},
	    "one actual immutable");
	Require(sgre::script::StringText(Call(vm, "getWMChar")).empty() && std::get<std::int32_t>(Call(vm, "getDownKey")) == 0, "bound roots expose genuine initialized state before the first hook, not an invented service");
	actual_owner.QueueCharacter(0x41);
	actual_owner.QueueCharacter(0);
	actual_owner.QueueCharacter(0x42);
	actual_owner.QueueKeyDown(0x5a);
	actual_owner.Advance({});
	Require(sgre::script::StringText(Call(vm, "getWMChar", {false, std::string("ignored")})) == "A", "native loose wrapper ignores extras and creates the string only through its first NUL");
	Require(actual_owner.TextSnapshot().Characters.size() == 3 && std::get<std::int32_t>(Call(vm, "getDownKey", {std::string("ignored")})) == 0x5a, "string observation must not discard native bytes or consume actual last key");
	const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
	const auto hub = vm.Invoke(bindings->InputHubClass(), root_receiver).Value;
	const auto hub_root = vm.Retain(hub);
	const auto text_program = sgre::script::CompileSquirrelSource(vm, "return getWMChar();", "text-with-real-hub-this");
	const auto key_program = sgre::script::CompileSquirrelSource(vm, "return getDownKey();", "key-with-real-hub-this");
	Require(sgre::script::StringText(vm.Invoke(vm.CreateClosure(text_program), std::array<sgre::script::SquirrelValue, 1>{hub}).Value) == "A" && std::get<std::int32_t>(vm.Invoke(vm.CreateClosure(key_program), std::array<sgre::script::SquirrelValue, 1>{hub}).Value) == 0x5a, "actual script root fallback must resolve both globals from an InputHub receiver without manufacturing members");
	reenter = true;
	RequireFailure([&] {
		static_cast<void>(Call(vm, "getDownKey"));
	},
	    "nonrecursive");
	reenter = false;
	fail = true;
	RequireFailure([&] {
		static_cast<void>(Call(vm, "getWMChar"));
	},
	    "actual event owner failed");
	fail = false;
	Require(sgre::script::StringText(Call(vm, "getWMChar")) == "A" && std::get<std::int32_t>(Call(vm, "getDownKey")) == 0x5a && reads >= 9, "exception paths must release the shared input callback fence without consuming publication");
	actual_owner.Advance({});
	Require(sgre::script::StringText(Call(vm, "getWMChar")).empty() && std::get<std::int32_t>(Call(vm, "getDownKey")) == 0, "only the next real shared VSync clears published text and last key");
	bindings.reset();
	static_cast<void>(vm.CollectGarbage());
	RequireFailure([&] {
		static_cast<void>(Call(vm, "getWMChar"));
	},
	    "live nonrecursive");
	RequireFailure([&] {
		static_cast<void>(Call(vm, "getDownKey"));
	},
	    "live nonrecursive");
}

int main() {
	try {
		TestOriginalWindowWords();
		TestSharedVSyncAndBoundedOwner();
		TestActualRootBindingsAndRetirement();
		std::cout << "Original keyboard event words, shared VSync publication and actual VM root lifetime PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
