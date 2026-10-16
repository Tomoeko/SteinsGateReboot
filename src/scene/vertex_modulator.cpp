#include "sgre/scene/vertex_modulator.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::scene {
	static constexpr float kPi = 3.1415927410125732421875f;
	static constexpr float kTwoPi = 6.283185482025146484375f;

	static float RequireFiniteModulator(float value) {
		if (!std::isfinite(value)) {
			throw std::runtime_error("vertex modulator requires finite native numeric inputs/results");
		}

		return value;
	}

	static float RequireModulatorPeriod(float value) {
		if (value == 0) {
			throw std::runtime_error("vertex modulator zero-period unordered arithmetic is not recovered");
		}

		return value;
	}

	static void RequireModulatorPoint(resource::MotionPoint point) {
		static_cast<void>(RequireFiniteModulator(point.X));
		static_cast<void>(RequireFiniteModulator(point.Y));
	}

	static void RequireModulatorPoints(std::span<resource::MotionPoint> points) {
		if (points.size() > 1'000'000) {
			throw std::runtime_error("vertex modulator positions exceed bounded native vector receiving work");
		}
		for (const auto point : points) {
			RequireModulatorPoint(point);
		}
	}

	static float ClampModulatorCoordinate(float value, float lower, float upper) {
		// This literal native selection permits inverted ranges and preserves the selected endpoint's bits.
		const auto candidate = upper <= value ? upper : value;

		return candidate > lower ? candidate : lower;
	}

	static float ModulatorAngle(float coordinate, float origin, float period, float phase) {
		const float distance = RequireFiniteModulator(coordinate - origin);
		const float half_angle = RequireFiniteModulator(distance * kPi);
		const float angle = RequireFiniteModulator((half_angle + half_angle) / RequireModulatorPeriod(period) + phase);

		return angle;
	}

	void VertexModulator::SetRange(float left, float top, float width, float height) {
		VertexModulatorRange range;
		range.Left = RequireFiniteModulator(left);
		range.Top = RequireFiniteModulator(top);
		range.Right = RequireFiniteModulator(left + RequireFiniteModulator(width));
		range.Bottom = RequireFiniteModulator(top + RequireFiniteModulator(height));
		m_Range = range;
	}

	VertexModulatorRange VertexModulator::Range() const noexcept {
		return m_Range;
	}

	void RasterVertexModulator::SetCycleFrame(float value) {
		m_CycleFrame = RequireFiniteModulator(value);
	}

	void RasterVertexModulator::SetCycleLines(float value) {
		m_CycleLines = RequireFiniteModulator(value);
	}

	void RasterVertexModulator::SetAmplitude(float value) {
		m_Amplitude = RequireFiniteModulator(value);
	}

	RasterVertexModulatorState RasterVertexModulator::State() const noexcept {
		RasterVertexModulatorState state;
		state.Range = Range();
		state.CycleFrame = m_CycleFrame;
		state.CycleLines = m_CycleLines;
		state.Amplitude = m_Amplitude;
		state.Phase = m_Phase;

		return state;
	}

	void RasterVertexModulator::Advance(float actual_delta_ticks) {
		static_cast<void>(RequireFiniteModulator(actual_delta_ticks));
		const float travel = RequireFiniteModulator(actual_delta_ticks * kTwoPi);
		const float angle = RequireFiniteModulator(travel / RequireModulatorPeriod(m_CycleFrame) + m_Phase);
		m_Phase = RequireFiniteModulator(std::fmod(angle, kTwoPi));
	}

	float RasterVertexModulator::Displacement(float y) const {
		const auto angle = ModulatorAngle(y, Range().Top, m_CycleLines, m_Phase);

		return RequireFiniteModulator(std::sin(angle) * m_Amplitude);
	}

	resource::MotionPoint RasterVertexModulator::ApplyPoint(resource::MotionPoint point) const {
		RequireModulatorPoint(point);
		const auto range = Range();
		if (point.X == range.Left || point.X == range.Right) {
			return point;
		}

		point.X = ClampModulatorCoordinate(RequireFiniteModulator(point.X + Displacement(point.Y)), range.Left, range.Right);

		return point;
	}

	void RasterVertexModulator::Apply(std::span<resource::MotionPoint> positions) const {
		RequireModulatorPoints(positions);
		const auto range = Range();
		float previous_y = std::numeric_limits<float>::max();
		float displacement = 0;
		for (auto& point : positions) {
			if (point.X == range.Left || point.X == range.Right) {
				continue;
			}
			if (point.Y != previous_y) {
				previous_y = point.Y;
				displacement = Displacement(point.Y);
			}

			point.X = ClampModulatorCoordinate(RequireFiniteModulator(point.X + displacement), range.Left, range.Right);
		}
	}

	void DistortionVertexModulator::SetCycleFrame(float value) {
		m_CycleFrameX = RequireFiniteModulator(value);
		m_CycleFrameY = value;
	}

	void DistortionVertexModulator::SetCycleFrameX(float value) {
		m_CycleFrameX = RequireFiniteModulator(value);
	}

	void DistortionVertexModulator::SetCycleFrameY(float value) {
		m_CycleFrameY = RequireFiniteModulator(value);
	}

	void DistortionVertexModulator::SetCycleLines(float value) {
		m_CycleLinesX = RequireFiniteModulator(value);
		m_CycleLinesY = value;
	}

	void DistortionVertexModulator::SetCycleLinesX(float value) {
		m_CycleLinesX = RequireFiniteModulator(value);
	}

	void DistortionVertexModulator::SetCycleLinesY(float value) {
		m_CycleLinesY = RequireFiniteModulator(value);
	}

	void DistortionVertexModulator::SetAmplitude(float value) {
		m_AmplitudeX = RequireFiniteModulator(value);
		m_AmplitudeY = value;
	}

	void DistortionVertexModulator::SetAmplitudeX(float value) {
		m_AmplitudeX = RequireFiniteModulator(value);
	}

	void DistortionVertexModulator::SetAmplitudeY(float value) {
		m_AmplitudeY = RequireFiniteModulator(value);
	}

	DistortionVertexModulatorState DistortionVertexModulator::State() const noexcept {
		DistortionVertexModulatorState state;
		state.Range = Range();
		state.CycleFrameX = m_CycleFrameX;
		state.CycleFrameY = m_CycleFrameY;
		state.CycleLinesX = m_CycleLinesX;
		state.CycleLinesY = m_CycleLinesY;
		state.AmplitudeX = m_AmplitudeX;
		state.AmplitudeY = m_AmplitudeY;
		state.PhaseX = m_PhaseX;
		state.PhaseY = m_PhaseY;

		return state;
	}

	void DistortionVertexModulator::Advance(float actual_delta_ticks) {
		static_cast<void>(RequireFiniteModulator(actual_delta_ticks));
		// Native532fd0 divides before multiplying, unlike Raster533040.
		const float angle_x = RequireFiniteModulator(actual_delta_ticks / RequireModulatorPeriod(m_CycleFrameX) * kTwoPi + m_PhaseX);
		const float angle_y = RequireFiniteModulator(actual_delta_ticks / RequireModulatorPeriod(m_CycleFrameY) * kTwoPi + m_PhaseY);
		m_PhaseX = RequireFiniteModulator(std::fmod(angle_x, kTwoPi));
		m_PhaseY = RequireFiniteModulator(std::fmod(angle_y, kTwoPi));
	}

	resource::MotionPoint DistortionVertexModulator::Modulate(resource::MotionPoint point, bool vector_operation) const {
		RequireModulatorPoint(point);
		const auto range = Range();
		if (point.X == range.Left || point.X == range.Right || point.Y == range.Top || point.Y == range.Bottom) {
			return point;
		}

		const auto angle_y = ModulatorAngle(point.X, range.Left, m_CycleLinesY, m_PhaseY);
		const auto angle_x = ModulatorAngle(point.Y, range.Top, m_CycleLinesX, m_PhaseX);
		// Native vector532ab0 uses cosf for Y; point532930 uses sinf for both axes.
		const float vertical_wave = vector_operation ? std::cos(angle_y) : std::sin(angle_y);
		const float y = RequireFiniteModulator(vertical_wave * m_AmplitudeY + point.Y);
		const float x = RequireFiniteModulator(std::sin(angle_x) * m_AmplitudeX + point.X);
		point.X = ClampModulatorCoordinate(x, range.Left, range.Right);
		point.Y = ClampModulatorCoordinate(y, range.Top, range.Bottom);

		return point;
	}

	void DistortionVertexModulator::Apply(std::span<resource::MotionPoint> positions) const {
		RequireModulatorPoints(positions);
		for (auto& point : positions) {
			point = Modulate(point, true);
		}
	}

	resource::MotionPoint DistortionVertexModulator::ApplyPoint(resource::MotionPoint point) const {
		return Modulate(point, false);
	}
} // namespace sgre::scene
