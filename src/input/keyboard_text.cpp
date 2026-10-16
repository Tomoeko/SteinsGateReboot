#include "sgre/input/keyboard_text.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace sgre::input {

	void KeyboardTextState::RequireThread() const {
		if (std::this_thread::get_id() != m_Thread) {
			throw std::runtime_error("keyboard text events and publication require their actual owner thread");
		}
	}

	void KeyboardTextState::QueueCharacter(std::uint16_t native_character) {
		RequireThread();
		if (m_PendingCount == m_Pending.size()) {
			throw std::runtime_error("keyboard text pending byte limit exceeded");
		}
		m_Pending[m_PendingCount++] = std::bit_cast<char>(static_cast<std::uint8_t>(native_character));
	}

	void KeyboardTextState::QueueKeyDown(std::uint16_t native_virtual_key) {
		RequireThread();
		const auto key = static_cast<std::uint32_t>(native_virtual_key);
		if (key == 0x25 || key == 0x27 || key == 0x2e) {
			QueueCharacter(native_virtual_key);
		}
		if (key <= 7 || (key >= 0x10 && key <= 0x12) || (key >= 0xa0 && key <= 0xa5)) {
			return;
		}
		if ((key >= 0x5b && key <= 0x87 && ((std::uint64_t{0x1fffffe00003} >> (key - 0x5b)) & 1) != 0) || key == 0xdc || std::find(m_ExcludedKeys.begin(), m_ExcludedKeys.end(), key) != m_ExcludedKeys.end()) {
			return;
		}
		m_PendingDownKey = static_cast<std::int32_t>(key);
	}

	void KeyboardTextState::SetExcludedKeys(const std::array<std::uint32_t, 3>& actual_special_keys) {
		RequireThread();
		m_ExcludedKeys = actual_special_keys;
	}

	void KeyboardTextState::Advance() {
		RequireThread();
		std::copy_n(m_Pending.begin(), m_PendingCount, m_Published.begin());
		m_PublishedCount = m_PendingCount;
		m_PendingCount = 0;
		m_PublishedDownKey = m_PendingDownKey;
		m_PendingDownKey = 0;
	}

	KeyboardTextSnapshot KeyboardTextState::Snapshot() const {
		RequireThread();
		return {std::string_view(m_Published.data(), m_PublishedCount), m_PublishedDownKey};
	}

} // namespace sgre::input
