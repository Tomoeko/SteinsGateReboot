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

int main() {
	try {
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
