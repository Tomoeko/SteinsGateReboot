#include "sgre/scene/emote_coordinate_transition.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace sgre::scene {
	static void RequireFiniteEmoteCoordinate(const std::array<float, 2>& coordinate) {
		if (!std::isfinite(coordinate[0]) || !std::isfinite(coordinate[1])) {
			throw std::runtime_error("Emote coordinate requires finite components within the recovered property domain");
		}
	}

	EmoteCoordinateTransition::EmoteCoordinateTransition(EmoteCoordinateMode coordinate_mode, float units_per_meter) : m_Mode(coordinate_mode), m_UnitsPerMeter(units_per_meter) {
		if ((coordinate_mode != EmoteCoordinateMode::Pixels && coordinate_mode != EmoteCoordinateMode::Meters) || !std::isfinite(units_per_meter) || !(units_per_meter > 0) || !std::isfinite(1.0f / units_per_meter)) {
			throw std::runtime_error("Emote coordinate requires a recovered mode and finite positive unit scale");
		}
	}

	void EmoteCoordinateTransition::Set(const std::array<float, 2>& coordinate, float duration, float easing) {
		RequireFiniteEmoteCoordinate(coordinate);
		if (!std::isfinite(duration) || !std::isfinite(easing)) {
			throw std::runtime_error("Emote coordinate requires finite duration and easing");
		}

		auto target = coordinate;
		if (m_Mode == EmoteCoordinateMode::Meters) {
			target[0] *= m_UnitsPerMeter;
			target[1] *= m_UnitsPerMeter;
		}
		RequireFiniteEmoteCoordinate(target);

		auto next = m_Components;
		for (std::size_t index = 0; index != next.size(); ++index) {
			next[index].Set(target[index], duration, easing);
		}
		m_Components = next;
	}

	std::array<float, 2> EmoteCoordinateTransition::Current() const {
		std::array<float, 2> coordinate{m_Components[0].Current(), m_Components[1].Current()};
		if (m_Mode == EmoteCoordinateMode::Meters) {
			coordinate[0] /= m_UnitsPerMeter;
			coordinate[1] /= m_UnitsPerMeter;
		}
		RequireFiniteEmoteCoordinate(coordinate);

		return coordinate;
	}

	std::array<float, 2> EmoteCoordinateTransition::Advance(float elapsed_ticks) {
		if (!std::isfinite(elapsed_ticks) || elapsed_ticks < 0) {
			throw std::runtime_error("Emote coordinate requires finite nonnegative elapsed ticks");
		}

		auto next = m_Components;
		std::array<float, 2> coordinate{};
		for (std::size_t index = 0; index != next.size(); ++index) {
			coordinate[index] = next[index].Advance(elapsed_ticks);
		}
		RequireFiniteEmoteCoordinate(coordinate);
		m_Components = next;

		return coordinate;
	}

	std::array<float, 2> PrepareEmoteDefaultCoordinatePublication(const std::array<float, 2>& coordinate) {
		RequireFiniteEmoteCoordinate(coordinate);
		// Native2CC510 adds the transformed default origin; this clears a retained negative zero.
		return {0.0f + coordinate[0], 0.0f + coordinate[1]};
	}
} // namespace sgre::scene
