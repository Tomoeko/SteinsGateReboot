#pragma once

#include "sgre/input/key_mapping.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace sgre::debug {
	// Explicit headless input only. No host events or function-key shortcuts are generated.
	enum class GameTestKey {
		Confirm,
		Cancel,
		Up,
		Down,
		Left,
		Right,
		Escape
	};

	class GameTestInput {
	public:
		[[nodiscard]] bool Queue(GameTestKey key);
		void BeginUpdate(bool hub_sampled);
		void EndUpdate() noexcept;
		[[nodiscard]] input::KeyboardSample Keyboard() const;

	private:
		static constexpr std::size_t kCapacity = 16;
		std::array<GameTestKey, kCapacity> m_Queue{};
		std::size_t m_First = 0;
		std::size_t m_Count = 0;
		std::optional<GameTestKey> m_Active;
		std::uint32_t m_PressUpdates = 0;
		std::uint32_t m_ReleaseUpdates = 0;
		bool m_Pressed = false;
	};
} // namespace sgre::debug
