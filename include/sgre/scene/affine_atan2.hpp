#pragma once

namespace sgre::scene {

	// Recovered native SSE2 finite-input profile; returns radians with signed axis conventions.
	[[nodiscard]] float PrepareAffineAngle(float y, float x);

} // namespace sgre::scene
