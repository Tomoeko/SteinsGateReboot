#include "sgre/input/function_keys.hpp"

#include <stdexcept>

namespace sgre::input {

	std::uint32_t FunctionKeySnapshot::Pressed(std::uint32_t mask) const noexcept {
		return (Current ^ Previous) & Current & mask;
	}

	std::int32_t FunctionKeySnapshot::AltEnterPressed() const noexcept {
		return AltEnterCurrent && !AltEnterPrevious ? 1 : 0;
	}

	void FunctionKeyState::Advance(const KeyboardSample& actual_sample) {
		if (std::this_thread::get_id() != m_Thread) {
			throw std::runtime_error("function-key sampling requires its actual owner thread");
		}
		m_Text.Advance();
		m_State.Previous = m_State.Current;
		m_State.Current = 0;
		m_State.AltEnterPrevious = m_State.AltEnterCurrent;
		m_State.AltEnterCurrent = false;
		if (actual_sample.Available) {
			m_State.AltEnterCurrent = ((actual_sample.VirtualKeys[0x12] | actual_sample.VirtualKeys[0xa4] | actual_sample.VirtualKeys[0xa5]) & 0x80) != 0 && (actual_sample.VirtualKeys[0x0d] & 0x80) != 0;
			// Native048ef0 reads VK_F1..F24, then LBUTTON/RBUTTON/CANCEL/MBUTTON high bits.
			for (std::uint32_t index = 0; index != 24; ++index) {
				if ((actual_sample.VirtualKeys[0x70 + index] & 0x80) != 0) {
					m_State.Current |= std::uint32_t{1} << index;
				}
			}
			for (std::uint32_t index = 0; index != 4; ++index) {
				if ((actual_sample.VirtualKeys[index + 1] & 0x80) != 0) {
					m_State.Current |= std::uint32_t{1} << (index + 24);
				}
			}
		}
	}

	FunctionKeySnapshot FunctionKeyState::Snapshot() const {
		if (std::this_thread::get_id() != m_Thread) {
			throw std::runtime_error("function-key getter requires its actual owner thread");
		}

		return m_State;
	}

	void FunctionKeyState::QueueCharacter(std::uint16_t native_character) {
		m_Text.QueueCharacter(native_character);
	}

	void FunctionKeyState::QueueKeyDown(std::uint16_t native_virtual_key) {
		m_Text.QueueKeyDown(native_virtual_key);
	}

	void FunctionKeyState::SetExcludedKeys(const std::array<std::uint32_t, 3>& actual_special_keys) {
		m_Text.SetExcludedKeys(actual_special_keys);
	}

	KeyboardTextSnapshot FunctionKeyState::TextSnapshot() const {
		return m_Text.Snapshot();
	}

} // namespace sgre::input
