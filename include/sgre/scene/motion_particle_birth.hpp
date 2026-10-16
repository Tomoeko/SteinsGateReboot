#pragma once

#include "sgre/scene/motion_particle_frame_slots.hpp"
#include "sgre/scene/motion_random.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {

	struct MotionParticleBirthSource {
		MotionLinearTransform Transform;
		std::array<float, 3> Coordinate{};
		std::array<float, 3> Displacement{};
		float DeltaTicks = 0;
		resource::MotionPoint Origin;
		MotionLinearTransform PlayerRootTransform;
		std::uint32_t CoordinatePlane = 0;
		std::uint32_t Opacity = 255;
		// Native byte player+418, independently of the supplied-matrix byte+419.
		bool IndependentLayerInherit = false;
		bool FlipX = false;
		bool FlipY = false;
	};

	struct MotionParticleBirthState {
		std::array<float, 3> Coordinate{};
		std::array<float, 3> Velocity{};
		float AngleDegrees = 0;
		float Zoom = 1;
		std::uint32_t Opacity = 255;
		float Attenuation = 1;
		bool FlipX = false;
		bool FlipY = false;
	};

	// Called after the real target-index sample and manager-sampled child construction/selection.
	// Uses the actual containing player's stream; no RNG source or clock is manufactured.
	// This pure birth phase does not tick, compose or publish the new child.
	[[nodiscard]] MotionParticleBirthState EvaluateMotionParticleBirth(const MotionParticleDefinition& definition, const MotionParticleNumericState& numeric, const MotionParticleBirthSource& source, MotionPlayerRandom& random);

} // namespace sgre::scene
