#pragma once

#include "sgre/scene/emote_tress_model.hpp"

#include <array>
#include <span>
#include <vector>

namespace sgre::scene {
	// Native String30aa03/306ee0/30e450: natural cubic spline over actual tress points.
	// Actual tress outlives this borrowed sampler; construction owns all solve/output storage.
	class EmoteTressSpline {
	public:
		explicit EmoteTressSpline(EmoteTressModel& actual_tress);
		EmoteTressSpline(const EmoteTressSpline&) = delete;
		EmoteTressSpline& operator=(const EmoteTressSpline&) = delete;
		// String output preparation captures actual physical points after its update/reset phase.
		void Prepare();
		[[nodiscard]] std::array<float, 2> Sample(float position) const;
		[[nodiscard]] std::span<const float> Knots() const noexcept;

	private:
		friend class EmoteStringPhysics;
		void PrepareImpl(bool unordered_comparison);
		[[nodiscard]] std::array<float, 2> SampleImpl(float position, bool unordered_comparison) const;
		EmoteTressModel& m_Tress;
		std::vector<float> m_Knots;
		std::vector<float> m_Intervals;
		std::vector<float> m_Diagonal;
		std::vector<std::array<float, 2>> m_Points;
		std::vector<std::array<float, 2>> m_RightHandSide;
		std::vector<std::array<float, 2>> m_SecondDerivative;
		bool m_Prepared = false;
		bool m_UnorderedStringComparison = false;
	};
} // namespace sgre::scene
