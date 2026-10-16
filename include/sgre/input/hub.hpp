#pragma once

#include "sgre/input/device.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace sgre::input {

	class InputHub {
	public:
		[[nodiscard]] std::size_t DeviceCount() const;
		// Native upper indices clamp to the last device. Negative indices are rejected safely.
		[[nodiscard]] InputDevice& DeviceAt(std::int32_t index);
		[[nodiscard]] const InputDevice& DeviceAt(std::int32_t index) const;
		void Advance(std::span<const DeviceSample, kInputDeviceCount> samples, std::int32_t elapsed_ticks, std::optional<InputExtent> cursor_extent = {});
		void SetTransferLeftAnalogToDigital(bool enabled);
		void SetTransferRightAnalogToDigital(bool enabled);
		[[nodiscard]] bool TransferLeftAnalogToDigital() const;
		[[nodiscard]] bool TransferRightAnalogToDigital() const;

	private:
		std::array<InputDevice, kInputDeviceCount> m_Devices;
		bool m_TransferLeftAnalog = false;
		bool m_TransferRightAnalog = false;
		bool m_PrimeFirstDevice = true;
	};

} // namespace sgre::input
