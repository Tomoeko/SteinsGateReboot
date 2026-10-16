#include "sgre/scene/emote_point_prediction.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {
	static resource::MotionPoint TransformPredictionPoint(const resource::MotionPoint& point, const resource::MotionMeshMatrix& matrix) {
		resource::MotionPoint result;
		result.X = (matrix.XX * point.X + matrix.XY * point.Y) + matrix.TranslationX;
		result.Y = (matrix.YY * point.Y + matrix.YX * point.X) + matrix.TranslationY;
		if (!std::isfinite(result.X) || !std::isfinite(result.Y)) {
			throw std::runtime_error("Emote point prediction requires finite actual transformed points");
		}
		return result;
	}

	EmotePointPrediction::EmotePointPrediction(std::uint32_t degree, std::size_t point_count) : m_Degree(degree), m_Count(point_count) {
		if (degree > 3 || point_count > m_Samples[0].size()) {
			throw std::runtime_error("Emote point prediction requires native degree0..3 and at most three actual fixes");
		}
	}

	float EmotePointPrediction::Predict(std::size_t point, bool y_axis) const {
		const auto coordinate = [&](std::size_t sample) {
			return y_axis ? m_Samples[sample][point].Y : m_Samples[sample][point].X;
		};
		auto degree = m_Degree;
		while (degree != 0) {
			bool available = true;
			for (std::size_t index = 0; index <= degree; ++index) {
				available = available && m_Seconds[index] != 0;
			}
			if (available) {
				break;
			}
			--degree;
		}
		const auto current = coordinate(0);
		if (degree == 0) {
			return current;
		}
		const auto velocity = (current - coordinate(1)) / m_Seconds[0];
		if (degree == 1) {
			return velocity * m_Seconds[0] + current;
		}
		const auto preceding_velocity = (coordinate(1) - coordinate(2)) / m_Seconds[1];
		const auto velocity_difference = velocity - preceding_velocity;
		if (degree == 2) {
			return (velocity_difference * m_Seconds[0] + velocity) * m_Seconds[0] + current;
		}
		const auto earlier_velocity = (coordinate(2) - coordinate(3)) / m_Seconds[2];
		const auto preceding_difference = preceding_velocity - earlier_velocity;
		const auto predicted_difference = (velocity_difference - preceding_difference) * m_Seconds[0] + velocity_difference;
		return (predicted_difference * m_Seconds[0] + velocity) * m_Seconds[0] + current;
	}

	void EmotePointPrediction::Advance(float elapsed_ticks, bool reset, const resource::MotionMeshMatrix& normalization, std::span<resource::MotionPoint> actual_points) {
		if (!m_Valid || actual_points.size() != m_Count) {
			throw std::runtime_error("Emote point prediction requires its valid actual point count");
		}
		if (reset) {
			m_Seconds.fill(0);
			return;
		}
		if (m_Count == 0) {
			return;
		}
		if (!std::isfinite(elapsed_ticks)) {
			throw std::runtime_error("Emote point prediction requires actual finite elapsed ticks");
		}
		m_Valid = false;
		for (std::size_t sample = 3; sample != 0; --sample) {
			m_Seconds[sample] = m_Seconds[sample - 1];
			m_Samples[sample] = m_Samples[sample - 1];
		}
		m_Seconds[0] = elapsed_ticks / 60.0f;
		for (std::size_t point = 0; point != m_Count; ++point) {
			m_Samples[0][point] = TransformPredictionPoint(actual_points[point], normalization);
		}
		const auto determinant = normalization.XX * normalization.YY - normalization.YX * normalization.XY;
		if (!std::isfinite(determinant) || determinant == 0) {
			throw std::runtime_error("Emote point prediction requires an invertible actual posture matrix");
		}
		const auto inverse_determinant = 1.0f / determinant;
		resource::MotionMeshMatrix inverse;
		inverse.XX = normalization.YY * inverse_determinant;
		inverse.XY = -normalization.XY * inverse_determinant;
		inverse.TranslationX = (normalization.TranslationY * normalization.XY - normalization.YY * normalization.TranslationX) * inverse_determinant;
		inverse.YX = -normalization.YX * inverse_determinant;
		inverse.YY = normalization.XX * inverse_determinant;
		inverse.TranslationY = (normalization.YX * normalization.TranslationX - normalization.XX * normalization.TranslationY) * inverse_determinant;
		for (std::size_t point = 0; point != m_Count; ++point) {
			actual_points[point] = TransformPredictionPoint({Predict(point, false), Predict(point, true)}, inverse);
		}
		m_Valid = true;
	}
} // namespace sgre::scene
