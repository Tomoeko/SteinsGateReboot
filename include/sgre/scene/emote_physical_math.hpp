#pragma once

namespace sgre::scene {

	// Native SSE2 finite profile. Unordered zero-time comparison keeps its existing NaN path.
	[[nodiscard]] float PrepareEmotePhysicalSine(float radians);
	[[nodiscard]] float PrepareEmotePhysicalCosine(float radians);
	[[nodiscard]] float PrepareEmotePhysicalAngle(float y, float x);
	[[nodiscard]] float PrepareEmotePhysicalArcCosine(float value);

} // namespace sgre::scene
