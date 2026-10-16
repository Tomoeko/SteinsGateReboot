#pragma once

#include "sgre/resource/motion_mesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::scene {
	class EmotePointPrediction {
	public:
		// Native2ffd10/300ce0 owns four actual point samples and four elapsed-second slots.
		EmotePointPrediction(std::uint32_t degree, std::size_t point_count);
		// Reset is actual UID reactivation; it clears timing and leaves this frame's world points unchanged.
		// Normalization is the actual control posture inverse used before extrapolation, not driver WorldToPhysics.
		void Advance(float elapsed_ticks, bool reset, const resource::MotionMeshMatrix& normalization, std::span<resource::MotionPoint> actual_points);

	private:
		[[nodiscard]] float Predict(std::size_t point, bool y_axis) const;
		std::array<std::array<resource::MotionPoint, 3>, 4> m_Samples{};
		std::array<float, 4> m_Seconds{};
		std::uint32_t m_Degree = 0;
		std::size_t m_Count = 0;
		bool m_Valid = true;
	};
} // namespace sgre::scene
