#include "sgre/input/device.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::input {

	static float UnsignedTicks(std::uint64_t ticks) {
		if (ticks <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
			return static_cast<float>(ticks);
		}

		// Preserve the native unsigned-to-float path, including its separate final addition.
		const auto half = static_cast<float>((ticks >> 1) | (ticks & 1));

		return half + half;
	}

	static void ValidateSample(const DeviceSample& sample, std::int32_t elapsed_ticks, const std::optional<InputExtent>& cursor_extent) {
		if (elapsed_ticks < 0) {
			throw std::runtime_error("input sampling requires a nonnegative clock step");
		}

		if (!std::isfinite(sample.LeftStick.X) || !std::isfinite(sample.LeftStick.Y) || !std::isfinite(sample.RightStick.X) || !std::isfinite(sample.RightStick.Y) || !std::isfinite(sample.LeftTrigger) || !std::isfinite(sample.RightTrigger)) {
			throw std::runtime_error("input sample contains a nonfinite axis");
		}

		if (cursor_extent && (!std::isfinite(cursor_extent->Width) || !std::isfinite(cursor_extent->Height) || cursor_extent->Width < 0 || cursor_extent->Height < 0)) {
			throw std::runtime_error("input cursor extent requires finite nonnegative dimensions");
		}
	}

	void InputDevice::Advance(const DeviceSample& sample, std::int32_t elapsed_ticks, std::optional<InputExtent> cursor_extent) {
		ValidateSample(sample, elapsed_ticks, cursor_extent);
		const float next_x = sample.LeftStick.X * 5.0f + m_Cursor.X;
		const float next_y = sample.LeftStick.Y * 5.0f + m_Cursor.Y;
		if (!std::isfinite(next_x) || !std::isfinite(next_y)) {
			throw std::runtime_error("input cursor accumulation exceeds finite coordinates");
		}

		m_PreviousButtons = m_Buttons;
		m_Buttons = sample.Buttons;
		m_PreviousPhysicalKeys = m_PhysicalKeys;
		m_PhysicalKeys = sample.PhysicalKeys;
		m_LeftStick = sample.LeftStick;
		m_RightStick = sample.RightStick;
		m_LeftTrigger = sample.LeftTrigger;
		m_RightTrigger = sample.RightTrigger;
		m_Connected = sample.Connected;
		m_Cursor.X = next_x;
		m_Cursor.Y = next_y;
		if (cursor_extent) {
			m_Cursor.X = std::clamp(next_x, -cursor_extent->Width * 0.5f, cursor_extent->Width * 0.5f);
			m_Cursor.Y = std::clamp(next_y, -cursor_extent->Height * 0.5f, cursor_extent->Height * 0.5f);
		}

		const auto step = static_cast<float>(elapsed_ticks);
		for (std::size_t index = 0; index < kPhysicalKeyCount; ++index) {
			auto& repeat = m_PhysicalRepeat[index];
			if (!repeat.Settings.Enabled) {
				continue;
			}

			if ((m_PhysicalKeys[index] & 0x80) == 0) {
				repeat.RemainingTicks = UnsignedTicks(repeat.Settings.InitialTicks);
				continue;
			}

			repeat.RemainingTicks = repeat.RemainingTicks - step;
			if (repeat.RemainingTicks <= 0) {
				repeat.RemainingTicks = std::max(UnsignedTicks(repeat.Settings.IntervalTicks) + repeat.RemainingTicks, 0.0f);
				m_PreviousPhysicalKeys[index] &= 0x7f;
			}
		}

		if (m_RepeatMask == 0) {
			return;
		}

		if ((m_Buttons & m_RepeatMask) == 0) {
			m_RepeatRemaining = UnsignedTicks(m_InitialTicks);
			return;
		}

		m_RepeatRemaining = m_RepeatRemaining - step;
		if (m_RepeatRemaining <= 0) {
			m_PreviousButtons &= ~m_RepeatMask;
			m_RepeatRemaining = std::max(UnsignedTicks(m_IntervalTicks) + m_RepeatRemaining, 0.0f);
		}
	}

	void InputDevice::SetButtonRepeat(std::uint32_t mask, std::uint64_t initial_ticks, std::uint64_t interval_ticks) {
		m_RepeatMask = mask;
		m_InitialTicks = initial_ticks;
		m_IntervalTicks = interval_ticks;
		m_RepeatRemaining = UnsignedTicks(initial_ticks);
	}

	void InputDevice::SetPhysicalRepeat(std::uint8_t key, const RepeatSettings& settings) {
		m_PhysicalRepeat[key].Settings = settings;
	}

	std::uint32_t InputDevice::Buttons() const {
		return m_Buttons;
	}

	std::uint32_t InputDevice::PreviousButtons() const {
		return m_PreviousButtons;
	}

	std::uint32_t InputDevice::Pressed(std::uint32_t mask) const {
		return (m_Buttons ^ m_PreviousButtons) & m_Buttons & mask;
	}

	std::uint32_t InputDevice::Released(std::uint32_t mask) const {
		return (m_PreviousButtons ^ m_Buttons) & m_PreviousButtons & mask;
	}

	bool InputDevice::ComboPressed(std::uint32_t mask) const {
		return Pressed(mask) != 0 && (m_Buttons & mask) == mask;
	}

	std::int32_t InputDevice::StickDirection() const {
		static constexpr std::array<std::int32_t, 16> kDirections{-1, 0, 4, -1, 6, 7, 5, -1, 2, 1, 3, -1, -1, -1, -1, -1};

		return kDirections[(m_Buttons >> 4) & 0xf];
	}

	bool InputDevice::Connected() const {
		return m_Connected;
	}

	void InputDevice::SetControllerType(ControllerType type) {
		m_ControllerType = type;
	}

	ControllerType InputDevice::Type() const {
		return m_ControllerType;
	}

	InputPoint InputDevice::LeftStick() const {
		return m_LeftStick;
	}

	InputPoint InputDevice::RightStick() const {
		return m_RightStick;
	}

	float InputDevice::LeftTrigger() const {
		return m_LeftTrigger;
	}

	float InputDevice::RightTrigger() const {
		return m_RightTrigger;
	}

	InputPoint InputDevice::Cursor() const {
		return m_Cursor;
	}

	const std::array<std::uint8_t, kPhysicalKeyCount>& InputDevice::PhysicalKeys() const {
		return m_PhysicalKeys;
	}

	const std::array<std::uint8_t, kPhysicalKeyCount>& InputDevice::PreviousPhysicalKeys() const {
		return m_PreviousPhysicalKeys;
	}

	float InputDevice::ButtonRepeatRemaining() const {
		return m_RepeatRemaining;
	}

	float InputDevice::PhysicalRepeatRemaining(std::uint8_t key) const {
		return m_PhysicalRepeat[key].RemainingTicks;
	}

} // namespace sgre::input
