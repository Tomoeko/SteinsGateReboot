#include "sgre/debug/game_test_input.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestMouseEdgesAndKeyboardIsolation() {
	sgre::debug::GameTestInput input;
	Require(!input.Mouse() && !input.QueueClick(-1, 12) && !input.QueueClick(12, 16384), "headless click must reject invalid bounded client coordinates without enabling a pointer");
	Require(input.QueueClick(48, 701) && input.Queue(sgre::debug::GameTestKey::Confirm), "headless mixed input queue rejected its bounded actions");
	sgre::input::MouseInput mouse;
	for (std::size_t frame = 0; frame != 6; ++frame) {
		input.BeginUpdate(true);
		input.EndUpdate();
		auto sample = input.Mouse();
		Require(sample && sample->ClientX == 48 && sample->ClientY == 701, "headless pointer lost its exact logical client position");
		sample->Client = {0, 0, 1280, 720};
		mouse.Advance(*sample);
		Require(mouse.Inside() && mouse.Buttons() == (frame >= 1 && frame <= 4 ? 1 : 0), "headless mouse hover, hold or release changed");
		Require(mouse.Pressed() == (frame == 1 ? 1 : 0) && mouse.Released() == (frame == 5 ? 1 : 0), "headless click must prime actual MouseInput before one press and one release edge");
		Require(input.Keyboard().ScanKeys[0x1c] == 0, "headless click leaked a keyboard confirm or skipped its release interval");
	}
	input.BeginUpdate(true);
	input.EndUpdate();
	Require(input.Keyboard().ScanKeys[0x1c] == 0x80 && input.Mouse()->Buttons == 0, "following keyboard action must leave the headless mouse released at its retained position");
}

int main() {
	try {
		TestMouseEdgesAndKeyboardIsolation();
		using sgre::debug::GameTestKey;
		const std::array<GameTestKey, 7> keys{GameTestKey::Confirm, GameTestKey::Cancel, GameTestKey::Up, GameTestKey::Down, GameTestKey::Left, GameTestKey::Right, GameTestKey::Escape};
		const std::array<std::size_t, 7> scans{0x1c, 0x2d, 0xc8, 0xd0, 0xcb, 0xcd, 1};
		const std::array<std::size_t, 7> virtuals{0x0d, 2, 0x26, 0x28, 0x25, 0x27, 0x1b};
		sgre::debug::GameTestInput input;
		for (const auto key : keys) {
			Require(input.Queue(key), "headless action queue rejected its bounded input");
		}
		input.BeginUpdate(false);
		input.EndUpdate();
		Require(input.Keyboard().Available && input.Keyboard().ScanKeys[0x1c] == 0, "queued key cannot start before actual InputHub sampling");
		for (std::size_t action = 0; action != keys.size(); ++action) {
			for (std::size_t update = 0; update != 5; ++update) {
				input.BeginUpdate(update != 1);
				input.EndUpdate();
				const auto sample = input.Keyboard();
				for (std::size_t code = 0; code != sample.ScanKeys.size(); ++code) {
					Require(sample.ScanKeys[code] == (update < 4 && code == scans[action] ? 0x80 : 0), "scan key pulse/release/order changed");
					Require(sample.VirtualKeys[code] == (update < 4 && code == virtuals[action] ? 0x80 : 0), "virtual key pulse/release/order changed");
				}
			}
		}
		input.BeginUpdate(true);
		input.EndUpdate();
		Require(input.Keyboard().ScanKeys[1] == 0, "completed queue retained a pressed key");
		for (std::size_t index = 0; index != 16; ++index) {
			Require(input.Queue(GameTestKey::Confirm), "headless action ring failed to reuse consumed slots");
		}
		Require(!input.Queue(GameTestKey::Up), "headless action queue exceeded its bound");
		std::cout << "Headless test input: exact key frames, hub admission, release intervals, order and capacity PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
