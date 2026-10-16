#pragma once

#include "sgre/resource/motion.hpp"

#include <span>

namespace sgre::scene {
	struct VertexModulatorRange {
		float Left = 0;
		float Top = 0;
		float Right = 0;
		float Bottom = 0;
	};

	// The script base has no constructor. A Picture borrows the actual concrete model while retaining its wrapper.
	class VertexModulator {
	public:
		virtual ~VertexModulator() = default;
		VertexModulator(const VertexModulator&) = delete;
		VertexModulator& operator=(const VertexModulator&) = delete;
		void SetRange(float left, float top, float width, float height);
		[[nodiscard]] VertexModulatorRange Range() const noexcept;
		virtual void Advance(float actual_delta_ticks) = 0;
		virtual void Apply(std::span<resource::MotionPoint> positions) const = 0;
		[[nodiscard]] virtual resource::MotionPoint ApplyPoint(resource::MotionPoint point) const = 0;

	protected:
		VertexModulator() = default;

	private:
		VertexModulatorRange m_Range;
	};

	struct RasterVertexModulatorState {
		VertexModulatorRange Range;
		float CycleFrame = 360;
		float CycleLines = 128;
		float Amplitude = 32;
		float Phase = 0;
	};

	class RasterVertexModulator final : public VertexModulator {
	public:
		void SetCycleFrame(float value);
		void SetCycleLines(float value);
		void SetAmplitude(float value);
		[[nodiscard]] RasterVertexModulatorState State() const noexcept;
		void Advance(float actual_delta_ticks) override;
		void Apply(std::span<resource::MotionPoint> positions) const override;
		[[nodiscard]] resource::MotionPoint ApplyPoint(resource::MotionPoint point) const override;

	private:
		float Displacement(float y) const;
		float m_CycleFrame = 360;
		float m_CycleLines = 128;
		float m_Amplitude = 32;
		float m_Phase = 0;
	};

	struct DistortionVertexModulatorState {
		VertexModulatorRange Range;
		float CycleFrameX = 360;
		float CycleFrameY = 720;
		float CycleLinesX = 1024;
		float CycleLinesY = 1024;
		float AmplitudeX = 32;
		float AmplitudeY = 32;
		float PhaseX = 0;
		float PhaseY = 0;
	};

	class DistortionVertexModulator final : public VertexModulator {
	public:
		void SetCycleFrame(float value);
		void SetCycleFrameX(float value);
		void SetCycleFrameY(float value);
		void SetCycleLines(float value);
		void SetCycleLinesX(float value);
		void SetCycleLinesY(float value);
		void SetAmplitude(float value);
		void SetAmplitudeX(float value);
		void SetAmplitudeY(float value);
		[[nodiscard]] DistortionVertexModulatorState State() const noexcept;
		void Advance(float actual_delta_ticks) override;
		void Apply(std::span<resource::MotionPoint> positions) const override;
		[[nodiscard]] resource::MotionPoint ApplyPoint(resource::MotionPoint point) const override;

	private:
		resource::MotionPoint Modulate(resource::MotionPoint point, bool vector_operation) const;
		float m_CycleFrameX = 360;
		float m_CycleFrameY = 720;
		float m_CycleLinesX = 1024;
		float m_CycleLinesY = 1024;
		float m_AmplitudeX = 32;
		float m_AmplitudeY = 32;
		float m_PhaseX = 0;
		float m_PhaseY = 0;
	};
} // namespace sgre::scene
