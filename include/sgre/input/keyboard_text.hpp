#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <thread>

namespace sgre::input {

	struct KeyboardTextSnapshot {
		// Borrows the actual owner until its next Advance. Bytes retain native NULs.
		std::string_view Characters;
		std::int32_t DownKey = 0;
	};

	class KeyboardTextState {
	public:
		static constexpr std::size_t kMaximumPendingBytes = 4096;
		KeyboardTextState() = default;
		KeyboardTextState(const KeyboardTextState&) = delete;
		KeyboardTextState& operator=(const KeyboardTextState&) = delete;
		// The native callbacks receive 16-bit window words; Character retains only their low byte.
		void QueueCharacter(std::uint16_t native_character);
		void QueueKeyDown(std::uint16_t native_virtual_key);
		void SetExcludedKeys(const std::array<std::uint32_t, 3>& actual_special_keys);
		// Called by the existing FunctionKeyState VSync, before its focus-gated keyboard sample.
		void Advance();
		[[nodiscard]] KeyboardTextSnapshot Snapshot() const;

	private:
		void RequireThread() const;
		const std::thread::id m_Thread = std::this_thread::get_id();
		std::array<char, kMaximumPendingBytes> m_Pending{};
		std::array<char, kMaximumPendingBytes> m_Published{};
		std::size_t m_PendingCount = 0;
		std::size_t m_PublishedCount = 0;
		std::int32_t m_PendingDownKey = 0;
		std::int32_t m_PublishedDownKey = 0;
		std::array<std::uint32_t, 3> m_ExcludedKeys{0, 0, 0x1b};
	};

} // namespace sgre::input
