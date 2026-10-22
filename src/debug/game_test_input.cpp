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
		Action action;
		action.Key = key;

		return QueueAction(action);
	}

	bool GameTestInput::QueueClick(std::int32_t x, std::int32_t y) {
		static constexpr std::int32_t kMaximumCoordinate = 16384;
		if (x < 0 || y < 0 || x >= kMaximumCoordinate || y >= kMaximumCoordinate) {
			return false;
		}
		Action action;
		action.Click = input::MousePixels{x, y};

		return QueueAction(action);
	}

	bool GameTestInput::QueueAction(Action action) {
		if (m_Count == kCapacity) {
			return false;
		}
		m_Queue[(m_First + m_Count) % kCapacity] = action;
		++m_Count;

		return true;
	}

	void GameTestInput::BeginUpdate(bool hub_sampled) {
		// InputHub samples in VSync after Update; keep the latch through that completed update.
		m_Pressed = m_Phase == Phase::Press;
		if (m_Phase == Phase::Idle) {
			if (m_Count == 0 || !hub_sampled) {
				return;
			}
			m_Active = m_Queue[m_First];
			m_First = (m_First + 1) % kCapacity;
			--m_Count;
			if (m_Active->Click) {
				m_Phase = Phase::Tracking;
				m_Remaining = 1;
			} else {
				m_Phase = Phase::Press;
				m_Remaining = 4;
				m_Pressed = true;
			}
		}
		if (m_Active && m_Active->Click) {
			m_Mouse = *m_Active->Click;
			// Original panels consume one movement to enter mouse mode. Two successive
			// movements after tracking give them a separate focus update before the press.
			const auto distance = m_Phase == Phase::Tracking ? 2 : m_Phase == Phase::Wake ? 1
			                                                                              : 0;
			m_Mouse->X += m_Mouse->X >= 2 ? -distance : distance;
		}
	}

	void GameTestInput::EndUpdate() noexcept {
		if (m_Remaining == 0 || --m_Remaining != 0) {
			return;
		}
		if (m_Phase == Phase::Tracking) {
			m_Phase = Phase::Wake;
			m_Remaining = 1;
		} else if (m_Phase == Phase::Wake) {
			m_Phase = Phase::Hover;
			m_Remaining = 1;
		} else if (m_Phase == Phase::Hover) {
			m_Phase = Phase::Press;
			m_Remaining = 4;
		} else if (m_Phase == Phase::Press) {
			m_Phase = Phase::Release;
			m_Remaining = 1;
		} else {
			m_Phase = Phase::Idle;
		}
	}

	input::KeyboardSample GameTestInput::Keyboard() const {
		input::KeyboardSample result;
		result.Available = true;
		if (m_Pressed && m_Active && m_Active->Key) {
			const auto codes = KeyCodes(*m_Active->Key);
			result.ScanKeys[codes[0]] = 0x80;
			result.VirtualKeys[codes[1]] = 0x80;
		}

		return result;
	}

	std::optional<input::MouseSample> GameTestInput::Mouse() const {
		if (!m_Mouse) {
			return {};
		}
		input::MouseSample result;
		result.ClientX = m_Mouse->X;
		result.ClientY = m_Mouse->Y;
		result.TargetWindowMatches = true;
		result.EnterTracking = true;
		result.Buttons = m_Pressed && m_Active && m_Active->Click ? 1 : 0;

		return result;
	}
} // namespace sgre::debug
