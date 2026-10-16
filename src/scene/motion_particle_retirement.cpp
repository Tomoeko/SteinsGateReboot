#include "sgre/scene/motion_particle_retirement.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static bool ParticleOverlapsScreen(resource::MotionPoint coordinate, const resource::MotionTimelineBounds& bounds, const MotionParticleScreenRectangle& screen) {
		const float right = coordinate.X + bounds.Right;
		const float left = coordinate.X + bounds.Left;
		const float bottom = coordinate.Y + bounds.Bottom;
		const float top = coordinate.Y + bounds.Top;
		for (const auto number : std::array<float, 8>{right, left, bottom, top, screen.Left, screen.Top, screen.Right, screen.Bottom}) {
			if (!std::isfinite(number)) {
				throw std::runtime_error("motion particle outside-screen test exceeds its finite retained-bounds domain");
			}
		}

		return screen.Left < right && left < screen.Right && screen.Top < bottom && top < screen.Bottom;
	}

	bool MotionParticleScreenState::ShouldRetire(const MotionParticleDefinition& definition, resource::MotionPoint coordinate, const resource::MotionTimelineBounds& bounds) {
		if (!definition.DeleteOutsideScreen) {
			return false;
		}
		if (!definition.Screen || !definition.ExpandedScreen) {
			throw std::runtime_error("motion particle outside-screen deletion requires its actual original and expanded screen rectangles");
		}

		if (EnteredScreen) {
			return !ParticleOverlapsScreen(coordinate, bounds, *definition.ExpandedScreen);
		}
		if (ParticleOverlapsScreen(coordinate, bounds, *definition.Screen)) {
			EnteredScreen = true;
		}

		return false;
	}

	bool MotionParticleCountExceedsMaximum(std::size_t count_after_push, std::int32_t maximum) noexcept {
		return count_after_push > static_cast<std::uint32_t>(maximum);
	}

} // namespace sgre::scene
