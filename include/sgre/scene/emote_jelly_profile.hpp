#pragma once

#include <cstdint>
#include <vector>

namespace sgre::scene {
	struct EmoteJellyProfileDefinition {
		float Radius = 1;
		float HeightRate = 1;
		float Density = 1;
		std::uint32_t ParticleCount = 2;
	};

	struct EmoteJellyProfile {
		float Length = 0;
		float Mass = 0;
		std::vector<float> MassWeights;
		std::vector<float> Radii;
	};

	// Native310740's spherical-cap cross-sections consumed by actual323ed0.
	// The containing tress normal/origin, surface triangulation and mesh fitting are separate producers.
	[[nodiscard]] EmoteJellyProfile BuildEmoteJellyProfile(const EmoteJellyProfileDefinition& definition);
} // namespace sgre::scene
