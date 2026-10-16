#pragma once

#include <array>
#include <cstdint>

namespace sgre::scene {

	enum class EmoteClampKind : std::int32_t {
		Smooth = 0,
		Radial = 1
	};

	struct EmoteClampTransform {
		EmoteClampKind Kind = EmoteClampKind::Smooth;
		float Minimum = 0;
		float Maximum = 1;
	};

	// Native 2cd680 postprocesses a pair after ordinary variable publication.
	// This does not modify the Emote current-value map, and axis-only values are not radially clipped.
	[[nodiscard]] std::array<float, 2> ApplyEmoteClamp(const EmoteClampTransform& transform, float left_right, float up_down);

} // namespace sgre::scene
