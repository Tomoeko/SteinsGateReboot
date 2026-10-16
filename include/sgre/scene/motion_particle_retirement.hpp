#pragma once

#include "sgre/scene/motion_particle_definition.hpp"

#include <cstddef>
#include <cstdint>

namespace sgre::scene {

	struct MotionParticleScreenState {
		// Native child+196 starts false and becomes sticky after strict overlap with the original screen.
		bool EnteredScreen = false;
		// Call after playing=false pruning and before the child's ordinary update, using retained raw root overrides/bounds.
		[[nodiscard]] bool ShouldRetire(const MotionParticleDefinition& definition, resource::MotionPoint coordinate, const resource::MotionTimelineBounds& bounds);
	};

	// Native compares count after EACH push with the zero-extended uint32 maximum, then erases only the oldest entry.
	[[nodiscard]] bool MotionParticleCountExceedsMaximum(std::size_t count_after_push, std::int32_t maximum) noexcept;

} // namespace sgre::scene
