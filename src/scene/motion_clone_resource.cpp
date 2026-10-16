#include "sgre/scene/motion_clone_resource.hpp"

#include "sgre/scene/motion_player_commands.hpp"

#include <optional>
#include <stdexcept>
#include <vector>

namespace sgre::scene {
	static constexpr std::size_t kMaximumCloneProjects = 65536;

	MotionProjectId ResolveMotionCloneProject(const MotionResourceManager& destination, const MotionResourceManager& source, MotionProjectId source_project) {
		if (!source.Contains(source_project)) {
			throw std::runtime_error("motion clone requires its actual live source resource registration");
		}
		if (&destination == &source) {
			return source_project;
		}
		const auto identity = source.Identity(source_project);
		const auto& resource = source.Resource(source_project);
		if (destination.Count() > kMaximumCloneProjects) {
			throw std::runtime_error("motion clone destination resource aliases exceed the bounded project inventory");
		}
		std::vector<MotionProjectId> projects(destination.Count());
		if (destination.WriteRegistrationOrder(projects) != projects.size()) {
			throw std::runtime_error("motion clone destination resource inventory changed during receiving");
		}
		for (const auto project : projects) {
			if (destination.Identity(project) == identity) {
				if (&destination.Resource(project) != &resource) {
					throw std::runtime_error("motion clone destination identity does not alias the actual immutable source resource");
				}
				return project;
			}
		}
		throw std::runtime_error("motion clone requires the actual destination alias of its immutable source resource");
	}

	void RequireMotionCloneSelection(const MotionPlayerCommands& destination, const MotionPlayerCommands& source) {
		const auto& old = source.SelectedProject();
		const auto& copied = destination.SelectedProject();
		if (source.SelectionSerial() != destination.SelectionSerial() || source.CachedTimeline() != destination.CachedTimeline() || old.has_value() != copied.has_value()) {
			throw std::runtime_error("motion clone requires its exact copied command selection receipt");
		}
		if (old && (copied->Project != ResolveMotionCloneProject(destination.Manager(), source.Manager(), old->Project) || copied->Identity != old->Identity || copied->Character != old->Character || copied->Timeline != old->Timeline)) {
			throw std::runtime_error("motion clone selected atoms differ from the actual destination resource alias");
		}
	}
} // namespace sgre::scene
