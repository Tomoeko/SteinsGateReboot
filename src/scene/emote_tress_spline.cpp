#include "sgre/scene/emote_tress_spline.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {
	EmoteTressSpline::EmoteTressSpline(EmoteTressModel& actual_tress)
	    : m_Tress(actual_tress) {
		const auto count = actual_tress.PointCount();
		if (count < 2 || count > 8192) {
			throw std::runtime_error("Emote String spline requires bounded nontrivial actual tress geometry");
		}
		m_Knots.resize(count);
		m_Intervals.resize(count - 1);
		m_Diagonal.resize(count);
		m_Points.resize(count);
		m_RightHandSide.resize(count);
		m_SecondDerivative.resize(count);
		actual_tress.WriteSegmentLengths(m_Intervals);
		float total_length = 0;
		for (std::size_t index = 0; index != count; ++index) {
			total_length += index == 0 ? 0 : m_Intervals[index - 1];
			m_Knots[index] = total_length;
		}
		if (!std::isfinite(total_length) || !(total_length > 0)) {
			throw std::runtime_error("Emote String spline requires finite positive actual rest length");
		}
		for (auto& knot : m_Knots) {
			knot /= total_length;
		}
		for (std::size_t index = 0; index != count - 1; ++index) {
			m_Intervals[index] = m_Knots[index + 1] - m_Knots[index];
			if (!(m_Intervals[index] > 0)) {
				throw std::runtime_error("Emote String spline requires distinct actual normalized segment knots");
			}
		}
		for (std::size_t index = 1; index < count - 1; ++index) {
			const auto span = m_Knots[index + 1] - m_Knots[index - 1];
			m_Diagonal[index] = span + span;
		}
		for (std::size_t index = 1; index < count - 2; ++index) {
			const auto square = m_Intervals[index] * m_Intervals[index];
			m_Diagonal[index + 1] -= square / m_Diagonal[index];
		}
	}

	void EmoteTressSpline::Prepare() {
		PrepareImpl(false);
	}

	void EmoteTressSpline::PrepareImpl(bool unordered_comparison) {
		m_Prepared = false;
		m_UnorderedStringComparison = unordered_comparison;
		m_Tress.WritePointsImpl(m_Points, unordered_comparison);
		const auto count = m_Points.size();
		for (std::size_t index = 1; index < count - 1; ++index) {
			for (std::size_t axis = 0; axis != 2; ++axis) {
				const auto forward = m_Points[index + 1][axis] - m_Points[index][axis];
				const auto backward = m_Points[index][axis] - m_Points[index - 1][axis];
				m_RightHandSide[index][axis] = (forward / m_Intervals[index] - backward / m_Intervals[index - 1]) * 6.0f;
			}
		}
		for (std::size_t index = 1; index < count - 2; ++index) {
			for (std::size_t axis = 0; axis != 2; ++axis) {
				const auto correction = (m_Intervals[index] * m_RightHandSide[index][axis]) / m_Diagonal[index];
				m_RightHandSide[index + 1][axis] -= correction;
			}
		}
		for (std::size_t index = count - 2; index != 0; --index) {
			for (std::size_t axis = 0; axis != 2; ++axis) {
				m_SecondDerivative[index][axis] = (m_RightHandSide[index][axis] - m_Intervals[index] * m_SecondDerivative[index + 1][axis]) / m_Diagonal[index];
				if (!unordered_comparison && !std::isfinite(m_SecondDerivative[index][axis])) {
					throw std::runtime_error("Emote String spline physical solve exceeds its finite output domain");
				}
			}
		}
		m_Prepared = true;
	}

	std::array<float, 2> EmoteTressSpline::Sample(float position) const {
		return SampleImpl(position, false);
	}

	std::array<float, 2> EmoteTressSpline::SampleImpl(float position, bool unordered_comparison) const {
		if (!m_Prepared || !std::isfinite(position) || (m_UnorderedStringComparison && !unordered_comparison)) {
			throw std::runtime_error("Emote String spline requires finite position and actual preceding physical preparation");
		}
		if (position <= m_Knots.front()) {
			return m_Points.front();
		}
		if (!(position < m_Knots.back())) {
			return m_Points.back();
		}
		std::size_t interval = 0;
		while (position > m_Knots[interval + 1]) {
			++interval;
		}
		const auto length = m_Knots[interval + 1] - m_Knots[interval];
		const auto right = (position - m_Knots[interval]) / length;
		const auto left = 1.0f - right;
		const auto left_cubic = (left * left) * left - left;
		const auto right_cubic = (right * right) * right - right;
		std::array<float, 2> point;
		const auto x = right_cubic * m_SecondDerivative[interval + 1][0] + left_cubic * m_SecondDerivative[interval][0];
		const auto y = left_cubic * m_SecondDerivative[interval][1] + right_cubic * m_SecondDerivative[interval + 1][1];
		point[0] = ((x * length) * length) / 6.0f + right * m_Points[interval + 1][0] + left * m_Points[interval][0];
		point[1] = ((y * length) * length) / 6.0f + right * m_Points[interval + 1][1] + left * m_Points[interval][1];
		if (!unordered_comparison && (!std::isfinite(point[0]) || !std::isfinite(point[1]))) {
			throw std::runtime_error("Emote String spline sampling exceeds its finite output domain");
		}
		return point;
	}

	std::span<const float> EmoteTressSpline::Knots() const noexcept {
		return m_Knots;
	}
} // namespace sgre::scene
