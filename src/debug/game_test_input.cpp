#include "sgre/debug/game_test_input.hpp"

#include <stdexcept>

namespace sgre::debug {
	static std::array<std::uint8_t, 2> KeyCodes(GameTestKey key) {
		switch (key) {
		case GameTestKey::Confirm:
			return {0x1c, 0x0d};
		case GameTestKey::Cancel:
			return {0x2d, 0x02};
		case GameTestKey::Up:
			return {0xc8, 0x26};
		case GameTestKey::Down:
			return {0xd0, 0x28};
		case GameTestKey::Left:
			return {0xcb, 0x25};
		case GameTestKey::Right:
			return {0xcd, 0x27};
		case GameTestKey::Escape:
			return {0x01, 0x1b};
		}
		throw std::runtime_error("headless test input requires a supported key");
	}

	bool GameTestInput::Queue(GameTestKey key) {
		static_cast<void>(KeyCodes(key));
		if (m_Count == kCapacity) {
			return false;
		}
		m_Queue[(m_First + m_Count) % kCapacity] = key;
		++m_Count;

		return true;
	}

	void GameTestInput::BeginUpdate(bool hub_sampled) {
		// InputHub samples in VSync after Update; keep the latch through that completed update.
		m_Pressed = m_PressUpdates != 0;
		if (m_PressUpdates != 0 || m_ReleaseUpdates != 0 || m_Count == 0 || !hub_sampled) {
			return;
		}
		m_Active = m_Queue[m_First];
		m_First = (m_First + 1) % kCapacity;
		--m_Count;
		m_PressUpdates = 4;
		m_Pressed = true;
	}

	void GameTestInput::EndUpdate() noexcept {
		if (m_PressUpdates != 0) {
			--m_PressUpdates;
			if (m_PressUpdates == 0) {
				m_ReleaseUpdates = 1;
			}
		} else if (m_ReleaseUpdates != 0) {
			--m_ReleaseUpdates;
		}
	}

	input::KeyboardSample GameTestInput::Keyboard() const {
		input::KeyboardSample result;
		result.Available = true;
		if (m_Pressed && m_Active) {
			const auto codes = KeyCodes(*m_Active);
			result.ScanKeys[codes[0]] = 0x80;
			result.VirtualKeys[codes[1]] = 0x80;
		}

		return result;
	}
} // namespace sgre::debug
