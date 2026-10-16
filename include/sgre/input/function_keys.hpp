#pragma once

#include "sgre/input/key_mapping.hpp"
#include "sgre/input/keyboard_text.hpp"

#include <cstdint>
#include <thread>

namespace sgre::input {

	struct FunctionKeySnapshot {
		std::uint32_t Current = 0;
		std::uint32_t Previous = 0;
		bool AltEnterCurrent = false;
		bool AltEnterPrevious = false;
		[[nodiscard]] std::uint32_t Pressed(std::uint32_t mask) const noexcept;
		[[nodiscard]] std::int32_t AltEnterPressed() const noexcept;
	};

	class FunctionKeyState {
	public:
		FunctionKeyState() = default;
		FunctionKeyState(const FunctionKeyState&) = delete;
		FunctionKeyState& operator=(const FunctionKeyState&) = delete;
		FunctionKeyState(FunctionKeyState&&) = delete;
		FunctionKeyState& operator=(FunctionKeyState&&) = delete;
		// Advance once in the native post-wait/VSync input phase, including unfocused frames.
		// VirtualKeys use the recovered compatibility identifiers, never host hardware key codes.
		void Advance(const KeyboardSample& actual_sample);
		[[nodiscard]] FunctionKeySnapshot Snapshot() const;
		// Actual window events queue separately; the existing Advance publishes them once.
		void QueueCharacter(std::uint16_t native_character);
		void QueueKeyDown(std::uint16_t native_virtual_key);
		void SetExcludedKeys(const std::array<std::uint32_t, 3>& actual_special_keys);
		[[nodiscard]] KeyboardTextSnapshot TextSnapshot() const;

	private:
		const std::thread::id m_Thread = std::this_thread::get_id();
		FunctionKeySnapshot m_State;
		KeyboardTextState m_Text;
	};

} // namespace sgre::input
