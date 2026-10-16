#pragma once

#include "sgre/resource/motion_mesh.hpp"

#include <cstddef>

namespace sgre::scene {
	struct EmoteCollisionEllipse {
		resource::MotionPoint Center;
		resource::MotionPoint Radius{1, 1};
		float Rotation = 0;
	};

	// Native2faca0 and2f8c60 consume a completed physical section, not a scalar particle radius.
	[[nodiscard]] bool ProjectEmoteEllipseHalfPlane(resource::MotionPoint& projected, const EmoteCollisionEllipse& particle, const resource::MotionPoint& first, const resource::MotionPoint& second);
	[[nodiscard]] bool ProjectEmoteEllipseContact(resource::MotionPoint& projected, const EmoteCollisionEllipse& particle, const resource::MotionPoint& relative_displacement, const EmoteCollisionEllipse& collider, bool interior, std::size_t& remaining_samples);
} // namespace sgre::scene
