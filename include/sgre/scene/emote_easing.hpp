#pragma once

namespace sgre::scene {

	// Native 53A800 finite scalar profile, including named codes 101..140 and unclamped overshoot.
	[[nodiscard]] float PrepareEmoteEasing(float easing, float ratio);

} // namespace sgre::scene
