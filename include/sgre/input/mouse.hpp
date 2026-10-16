#pragma once

#include "sgre/input/device.hpp"

#include <cstdint>

namespace sgre::input {

	struct ClientRectangle {
		std::int32_t Left = 0;
		std::int32_t Top = 0;
		std::int32_t Right = 0;
		std::int32_t Bottom = 0;
	};

	struct MouseSample {
		std::int32_t ClientX = 0;
		std::int32_t ClientY = 0;
		ClientRectangle Client;
		std::uint32_t Buttons = 0;
		std::int32_t WheelTicks = 0;
		bool TargetWindowMatches = false;
		// Original mouse-enter tracking is separate from WindowFromPoint/rectangle validity.
		bool EnterTracking = false;
	};

	struct MousePixels {
		std::int32_t X = 0;
		std::int32_t Y = 0;
	};

	// Native 0x1404f4670 derives an absolute cursor request directly from client dimensions.
	// It does not modify the sampled mouse state. Applying the request belongs to the platform.
	[[nodiscard]] MousePixels PlanMousePosition(const ClientRectangle& client, InputPoint normalized_position);

	class MouseInput {
	public:
		void Advance(const MouseSample& sample);
		[[nodiscard]] InputPoint Position() const;
		// Movement is in the original integer normalized-coordinate units, not Position's float units.
		[[nodiscard]] InputPoint Movement() const;
		[[nodiscard]] std::uint32_t Buttons() const;
		[[nodiscard]] std::uint32_t Pressed() const;
		[[nodiscard]] std::uint32_t Released() const;
		[[nodiscard]] std::int32_t WheelDirection() const;
		[[nodiscard]] bool Inside() const;
		[[nodiscard]] bool Entered() const;
		[[nodiscard]] bool Left() const;

	private:
		MousePixels m_Position;
		MousePixels m_PreviousPosition;
		MousePixels m_Movement;
		std::uint32_t m_Buttons = 0;
		std::uint32_t m_PreviousButtons = 0;
		std::uint32_t m_Pressed = 0;
		std::uint32_t m_Released = 0;
		std::int32_t m_Wheel = 0;
		bool m_Priming = true;
		bool m_Inside = false;
		bool m_PreviousInside = false;
	};

} // namespace sgre::input
