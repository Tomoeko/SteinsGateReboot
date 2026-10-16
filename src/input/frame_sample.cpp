#include "sgre/input/frame_sample.hpp"

#include <array>
#include <cstdint>

namespace sgre::input {

	HubFrameSample ResolveKeyboardOnlyFrame(const KeyboardSample& actual_keyboard, const ConvertedKeyboard& actual_conversion) {
		HubFrameSample frame;
		if (!actual_keyboard.Available) {
			return frame;
		}
		// Physical scan state belongs to every actual device, including disconnected XInput slots.
		for (auto& device : frame.Devices) {
			device.PhysicalKeys = actual_keyboard.ScanKeys;
			if (((device.PhysicalKeys[0x38] | device.PhysicalKeys[0xb8]) & 0x80) != 0) {
				device.PhysicalKeys[0x1c] = 0;
			}
		}
		auto& first = frame.Devices[0];
		first.Connected = true;
		// Keyboard is native designated device0. Logical and XInput target masks have distinct roles.
		static constexpr std::array<std::array<std::uint32_t, 2>, 14> controller_to_input{{{0x1, 0x40}, {0x2, 0x80}, {0x4, 0x20}, {0x8, 0x10},
		    {0x10, 0x8}, {0x20, 0x4}, {0x40, 0x40000}, {0x80, 0x80000},
		    {0x100, 0x200}, {0x200, 0x100}, {0x1000, 0x1}, {0x2000, 0x2},
		    {0x4000, 0x400}, {0x8000, 0x800}}};
		for (const auto& conversion : controller_to_input) {
			if ((actual_conversion.ControllerButtons & conversion[0]) != 0) {
				first.Buttons |= conversion[1];
			}
		}
		first.Buttons |= actual_conversion.LogicalButtons & 0x1f00000;
		if ((actual_conversion.LogicalButtons & 0x10000) != 0) {
			first.Buttons |= 0x10000;
			first.LeftTrigger = 30.0f / 255.0f;
		}
		if ((actual_conversion.LogicalButtons & 0x20000) != 0) {
			first.Buttons |= 0x20000;
			first.RightTrigger = 30.0f / 255.0f;
		}

		return frame;
	}

} // namespace sgre::input
