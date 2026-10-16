#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/emote_jelly_surface_samples.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::scene {

	struct EmoteJellySurfaceSamplingDefinition {
		resource::MotionPoint Size{1, 1};
		resource::MotionPoint Origin{0.5f, 0.5f};
		// Actual tress direction after constructor TipTilt rotation, before any live physics update.
		resource::MotionPoint ProjectionDirection;
		std::uint8_t EncodedDegree = 0x33;
		float SampleDensity = 1;
		bool ReverseOrder = false;
	};

	// Native310740's constructor-only hull, positive quad cells and triangular sampling.
	// Borrows the actual initial physical N+1 ring cloud; the last16 points are the collapsed tip.
	// Returned bindings own their data and feed the real EmoteJellySurfaceSamples numerical owner.
	[[nodiscard]] std::vector<EmoteJellyRingSample> BuildEmoteJellySurfaceBindings(const EmoteJellySurfaceSamplingDefinition& definition, std::span<const resource::MotionPoint> initial_rings);

} // namespace sgre::scene
