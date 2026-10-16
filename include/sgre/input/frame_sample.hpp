#pragma once

#include "sgre/input/key_mapping.hpp"

#include <array>
#include <optional>

namespace sgre::input {

	struct HubFrameSample {
		std::array<DeviceSample, kInputDeviceCount> Devices;
		std::optional<InputExtent> CursorExtent;
	};

	// Recovered keyboard-only4f2da0 domain. The concrete platform must establish absent physical controllers.
	// Converted masks are the actual same sample's KeyMappings result, not already resolved MInput buttons.
	[[nodiscard]] HubFrameSample ResolveKeyboardOnlyFrame(const KeyboardSample& actual_keyboard, const ConvertedKeyboard& actual_conversion);

} // namespace sgre::input
