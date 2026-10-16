#include "sgre/input/hub.hpp"

#include <algorithm>
#include <stdexcept>

namespace sgre::input {

	static std::size_t DeviceIndex(std::int32_t index) {
		if (index < 0) {
			throw std::runtime_error("input device index is negative");
		}

		return std::min(static_cast<std::size_t>(index), kInputDeviceCount - 1);
	}

	std::size_t InputHub::DeviceCount() const {
		return m_Devices.size();
	}

	InputDevice& InputHub::DeviceAt(std::int32_t index) {
		return m_Devices[DeviceIndex(index)];
	}

	const InputDevice& InputHub::DeviceAt(std::int32_t index) const {
		return m_Devices[DeviceIndex(index)];
	}

	void InputHub::Advance(std::span<const DeviceSample, kInputDeviceCount> samples, std::int32_t elapsed_ticks, std::optional<InputExtent> cursor_extent) {
		for (std::size_t index = 0; index < m_Devices.size(); ++index) {
			// Each native hub primes its first device twice on its first sample, suppressing edges there.
			if (m_PrimeFirstDevice) {
				m_Devices[index].Advance(samples[index], elapsed_ticks, cursor_extent);
			}

			m_Devices[index].Advance(samples[index], elapsed_ticks, cursor_extent);
			m_PrimeFirstDevice = false;
		}
	}

	void InputHub::SetTransferLeftAnalogToDigital(bool enabled) {
		m_TransferLeftAnalog = enabled;
	}

	void InputHub::SetTransferRightAnalogToDigital(bool enabled) {
		m_TransferRightAnalog = enabled;
	}

	bool InputHub::TransferLeftAnalogToDigital() const {
		return m_TransferLeftAnalog;
	}

	bool InputHub::TransferRightAnalogToDigital() const {
		return m_TransferRightAnalog;
	}

} // namespace sgre::input
