#pragma once

#include "sgre/input/key_mapping.hpp"
#include "sgre/input/mouse.hpp"

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
		[[nodiscard]] bool QueueClick(std::int32_t x, std::int32_t y);
		void BeginUpdate(bool hub_sampled);
		void EndUpdate() noexcept;
		[[nodiscard]] input::KeyboardSample Keyboard() const;
		// Caller supplies the actual headless client rectangle; no host pointer is read or moved.
		[[nodiscard]] std::optional<input::MouseSample> Mouse() const;

	private:
		static constexpr std::size_t kCapacity = 16;
		struct Action {
			std::optional<GameTestKey> Key;
			std::optional<input::MousePixels> Click;
		};
		enum class Phase {
			Idle,
			Tracking,
			Wake,
			Hover,
			Press,
			Release
		};
		[[nodiscard]] bool QueueAction(Action action);
		std::array<Action, kCapacity> m_Queue{};
		std::size_t m_First = 0;
		std::size_t m_Count = 0;
		std::optional<Action> m_Active;
		std::optional<input::MousePixels> m_Mouse;
		Phase m_Phase = Phase::Idle;
		std::uint32_t m_Remaining = 0;
		bool m_Pressed = false;
	};
} // namespace sgre::debug
