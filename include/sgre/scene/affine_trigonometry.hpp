#pragma once

namespace sgre::scene {

	struct AffineSinCos {
		float Sine = 0;
		float Cosine = 1;
	};

	// Recovered SSE2 float profile. The separate native FMA profile and large reduction are unresolved.
	[[nodiscard]] AffineSinCos PrepareAffineTrigonometry(float radians);

} // namespace sgre::scene
