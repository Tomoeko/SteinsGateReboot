#pragma once

#include "sgre/scene/motion_evaluator.hpp"
#include "sgre/scene/motion_particle_definition.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace sgre::scene {

	struct MotionParticleNumericState {
		// Actual constructor-owned +38..+58 words begin at zero, independently of decoded slot defaults.
		float FrequencyMin = 0;
		float FrequencyMax = 0;
		float VelocityMin = 0;
		float VelocityMax = 0;
		float AngleMin = 0;
		float AngleMax = 0;
		float ZoomMin = 0;
		float ZoomMax = 0;
		float Range = 0;

		bool operator==(const MotionParticleNumericState&) const = default;
	};

	class MotionParticleFrameSlots {
	public:
		// The exact immutable preparation encloses this physical-slot owner; decoded controls are copied values.
		MotionParticleFrameSlots(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared);
		MotionParticleFrameSlots(const MotionParticleFrameSlots& source, const PreparedMotionLayer& prepared);
		MotionParticleFrameSlots(const MotionParticleFrameSlots&) = delete;
		MotionParticleFrameSlots& operator=(const MotionParticleFrameSlots&) = delete;
		// Called by the actual containing cursor decoder, including hidden/absent-control physical-slot reuse.
		void PrepareFrame(std::size_t slot, std::size_t frame_index);
		[[nodiscard]] const MotionParticleFrameControl& Control(std::size_t slot) const;
		// Native50bc40 shares ordinary dirty/force/ratio suppression; particle values have no separate easing curve.
		[[nodiscard]] bool Evaluate(const resource::MotionResource& resource, float position, std::size_t current_slot, bool force, bool frame_dirty);
		[[nodiscard]] const MotionParticleNumericState& Numeric() const;
		[[nodiscard]] std::size_t EvaluatedFrameIndex() const;
		[[nodiscard]] float EvaluatedPosition() const;

	private:
		const PreparedMotionLayer& m_Prepared;
		std::vector<std::optional<MotionParticleFrameControl>> m_Updates;
		std::array<MotionParticleFrameControl, 2> m_Controls{};
		std::array<std::optional<std::size_t>, 2> m_FrameIndices{};
		MotionLayerEvaluationCache m_Evaluation;
		MotionParticleNumericState m_Numeric;
		std::size_t m_EvaluatedFrameIndex = 0;
		float m_EvaluatedPosition = 0;
		bool m_Published = false;
	};

} // namespace sgre::scene
