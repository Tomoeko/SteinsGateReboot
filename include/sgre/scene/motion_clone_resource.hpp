#pragma once

#include "sgre/scene/motion_resources.hpp"

namespace sgre::scene {
	class MotionPlayerCommands;

	// Native504cf0 can override the manager while retaining the exact selected immutable resource atoms.
	// Portable project slots must resolve through the same document identity and model, never through a name reload.
	[[nodiscard]] MotionProjectId ResolveMotionCloneProject(const MotionResourceManager& destination, const MotionResourceManager& source, MotionProjectId source_project);
	void RequireMotionCloneSelection(const MotionPlayerCommands& destination, const MotionPlayerCommands& source);
} // namespace sgre::scene
