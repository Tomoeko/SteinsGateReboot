#include "sgre/input/mouse.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::input {

	static constexpr std::int32_t kOutsideCoordinate = 32768;

	static InputPoint ClientHalf(const ClientRectangle& client) {
		const auto width = static_cast<std::int64_t>(client.Right) - client.Left;
		const auto height = static_cast<std::int64_t>(client.Bottom) - client.Top;
		if (width <= 0 || height <= 0 || width > std::numeric_limits<std::int32_t>::max() || height > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("mouse client rectangle requires bounded positive dimensions");
		}

		return {static_cast<float>(static_cast<double>(width) * 0.5), static_cast<float>(static_cast<double>(height) * 0.5)};
	}

	static std::int32_t MouseInteger(float value) {
		if (!std::isfinite(value) || static_cast<double>(value) < static_cast<double>(std::numeric_limits<std::int32_t>::min()) || static_cast<double>(value) > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
			throw std::runtime_error("mouse coordinate exceeds its integer representation");
		}

		return static_cast<std::int32_t>(value);
	}

	static MousePixels QuantizeMousePosition(const MouseSample& sample) {
		const auto half = ClientHalf(sample.Client);
		if (!sample.TargetWindowMatches || sample.ClientX < 0 || sample.ClientY < 0 || sample.ClientX >= sample.Client.Right || sample.ClientY >= sample.Client.Bottom) {
			return {kOutsideCoordinate, kOutsideCoordinate};
		}

		const float x = (static_cast<float>(sample.ClientX) - half.X) / half.X;
		const float y = (static_cast<float>(sample.ClientY) - half.Y) / half.Y;

		return {MouseInteger(static_cast<float>(static_cast<double>(x) * 10000.0)), MouseInteger(static_cast<float>(static_cast<double>(y) * 10000.0))};
	}

	MousePixels PlanMousePosition(const ClientRectangle& client, InputPoint normalized_position) {
		const auto half = ClientHalf(client);
		const float x = half.X * normalized_position.X + half.X;
		const float y = half.Y * normalized_position.Y + half.Y;

		return {MouseInteger(x), MouseInteger(y)};
	}

	void MouseInput::Advance(const MouseSample& sample) {
		const auto position = QuantizeMousePosition(sample);
		if (m_Priming || m_PreviousPosition.X == kOutsideCoordinate) {
			m_Priming = false;
			m_PreviousButtons = sample.Buttons;
			m_PreviousPosition = position;
		}

		m_Buttons = sample.Buttons;
		m_Pressed = (m_PreviousButtons ^ m_Buttons) & m_Buttons;
		m_Released = (m_PreviousButtons ^ m_Buttons) & m_PreviousButtons;
		m_Movement.X = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(position.X) - std::bit_cast<std::uint32_t>(m_PreviousPosition.X));
		m_Movement.Y = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(position.Y) - std::bit_cast<std::uint32_t>(m_PreviousPosition.Y));
		m_Position = position;
		m_PreviousPosition = position;
		m_PreviousButtons = m_Buttons;
		m_Wheel = sample.WheelTicks;
		m_PreviousInside = m_Inside;
		m_Inside = sample.EnterTracking;
	}

	InputPoint MouseInput::Position() const {
		const auto x = static_cast<float>(m_Position.X);
		const auto y = static_cast<float>(m_Position.Y);
		if (x >= static_cast<float>(kOutsideCoordinate)) {
			return {x, y};
		}

		return {static_cast<float>(static_cast<double>(x) / 10000.0), static_cast<float>(static_cast<double>(y) / 10000.0)};
	}

	InputPoint MouseInput::Movement() const {
		return {static_cast<float>(m_Movement.X), static_cast<float>(m_Movement.Y)};
	}

	std::uint32_t MouseInput::Buttons() const {
		return m_Buttons;
	}

	std::uint32_t MouseInput::Pressed() const {
		return m_Pressed;
	}

	std::uint32_t MouseInput::Released() const {
		return m_Released;
	}

	std::int32_t MouseInput::WheelDirection() const {
		return m_Wheel > 0 ? 1 : m_Wheel < 0 ? -1
		                                     : 0;
	}

	bool MouseInput::Inside() const {
		return m_Inside;
	}

	bool MouseInput::Entered() const {
		return m_Inside && !m_PreviousInside;
	}

	bool MouseInput::Left() const {
		return !m_Inside && m_PreviousInside;
	}

} // namespace sgre::input
