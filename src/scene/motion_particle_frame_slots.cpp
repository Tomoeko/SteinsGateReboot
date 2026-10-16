#include "sgre/scene/motion_particle_frame_slots.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaximumParticleSlotFrames = 65536;

	static MotionParticleNumericState CopyParticleNumeric(const MotionParticleFrameControl& control) {
		MotionParticleNumericState numeric;
		numeric.FrequencyMin = control.FrequencyMin;
		numeric.FrequencyMax = control.FrequencyMax;
		numeric.VelocityMin = control.VelocityMin;
		numeric.VelocityMax = control.VelocityMax;
		numeric.AngleMin = control.AngleMin;
		numeric.AngleMax = control.AngleMax;
		numeric.ZoomMin = control.ZoomMin;
		numeric.ZoomMax = control.ZoomMax;
		numeric.Range = control.Range;

		return numeric;
	}

	static float BlendParticleNumber(float current, float following, float ratio) {
		// Native50c327..50c449 multiplies both endpoints even when their values compare equal.
		const auto current_part = (1.0f - ratio) * current;
		const auto following_part = ratio * following;

		return current_part + following_part;
	}

	static MotionParticleNumericState BlendParticleNumeric(const MotionParticleFrameControl& current, const MotionParticleFrameControl& following, float ratio) {
		MotionParticleNumericState numeric;
		numeric.FrequencyMin = BlendParticleNumber(current.FrequencyMin, following.FrequencyMin, ratio);
		numeric.FrequencyMax = BlendParticleNumber(current.FrequencyMax, following.FrequencyMax, ratio);
		numeric.VelocityMin = BlendParticleNumber(current.VelocityMin, following.VelocityMin, ratio);
		numeric.VelocityMax = BlendParticleNumber(current.VelocityMax, following.VelocityMax, ratio);
		numeric.AngleMin = BlendParticleNumber(current.AngleMin, following.AngleMin, ratio);
		numeric.AngleMax = BlendParticleNumber(current.AngleMax, following.AngleMax, ratio);
		numeric.ZoomMin = BlendParticleNumber(current.ZoomMin, following.ZoomMin, ratio);
		numeric.ZoomMax = BlendParticleNumber(current.ZoomMax, following.ZoomMax, ratio);
		numeric.Range = BlendParticleNumber(current.Range, following.Range, ratio);

		return numeric;
	}

	static void RequireFiniteParticleNumeric(const MotionParticleNumericState& numeric) {
		for (const auto number : {numeric.FrequencyMin, numeric.FrequencyMax, numeric.VelocityMin, numeric.VelocityMax, numeric.AngleMin, numeric.AngleMax, numeric.ZoomMin, numeric.ZoomMax, numeric.Range}) {
			if (!std::isfinite(number)) {
				throw std::runtime_error("motion particle numeric output exceeds its finite recovered domain");
			}
		}
	}

	MotionParticleFrameSlots::MotionParticleFrameSlots(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared)
	    : m_Prepared(prepared) {
		if (layer.Type != 4 || prepared.Type != 4 || layer.Label != prepared.Label || layer.Coordinate != prepared.CoordinatePlane || layer.Frames.size() != prepared.Frames.size() || layer.Frames.size() < 2 || layer.Frames.size() > kMaximumParticleSlotFrames || layer.Frames.front().Time != 0) {
			throw std::runtime_error("motion particle slots require their actual bounded matching kind4 preparation");
		}

		m_Updates.reserve(layer.Frames.size());
		float previous = 0;
		for (std::size_t index = 0; index < layer.Frames.size(); ++index) {
			const auto& raw = layer.Frames[index];
			const auto& frame = prepared.Frames[index];
			if (!std::isfinite(raw.Time) || raw.Time < previous || raw.Time != frame.Time || (raw.Type != 0 && raw.Type != 2 && raw.Type != 3) || frame.Hidden != (raw.Type == 0) || frame.Tween != (raw.Type == 3)) {
				throw std::runtime_error("motion particle slots require actual sorted static/hidden/tween frames; type1 remains unrecovered");
			}

			if (frame.Tween && index + 1 < prepared.Frames.size() && !prepared.Frames[index + 1].Hidden && prepared.Frames[index + 1].Time <= frame.Time) {
				throw std::runtime_error("motion particle tween requires a positive adjacent duration");
			}

			auto update = PrepareMotionParticleFrameControl(layer, raw);
			if (!frame.Hidden && update.has_value() != ((frame.Attributes.Mask & 0x100000u) != 0)) {
				throw std::runtime_error("motion particle raw control mask differs from its actual preparation");
			}

			m_Updates.push_back(update);
			previous = raw.Time;
		}
	}

	MotionParticleFrameSlots::MotionParticleFrameSlots(const MotionParticleFrameSlots& source, const PreparedMotionLayer& prepared)
	    : m_Prepared(prepared), m_Updates(source.m_Updates), m_Controls(source.m_Controls), m_FrameIndices(source.m_FrameIndices), m_Evaluation(source.m_Evaluation), m_Numeric(source.m_Numeric), m_EvaluatedFrameIndex(source.m_EvaluatedFrameIndex), m_EvaluatedPosition(source.m_EvaluatedPosition), m_Published(source.m_Published) {
		if (&prepared != &source.m_Prepared) {
			throw std::runtime_error("motion particle slots clone requires the exact retained immutable preparation");
		}
	}

	void MotionParticleFrameSlots::PrepareFrame(std::size_t slot, std::size_t frame_index) {
		if (slot >= m_Controls.size() || frame_index >= m_Updates.size()) {
			throw std::runtime_error("motion particle decoded slot/frame exceeds its actual preparation");
		}

		ApplyMotionParticleFrameControl(m_Updates[frame_index], m_Controls[slot]);
		m_FrameIndices[slot] = frame_index;
		m_Published = false;
	}

	const MotionParticleFrameControl& MotionParticleFrameSlots::Control(std::size_t slot) const {
		if (slot >= m_Controls.size() || !m_FrameIndices[slot]) {
			throw std::runtime_error("motion particle control requires its actual decoded physical slot");
		}

		return m_Controls[slot];
	}

	bool MotionParticleFrameSlots::Evaluate(const resource::MotionResource& resource, float position, std::size_t current_slot, bool force, bool frame_dirty) {
		m_Published = false;
		if (current_slot >= m_Controls.size() || !m_FrameIndices[current_slot] || !m_FrameIndices[1 - current_slot] || *m_FrameIndices[current_slot] + 1 != *m_FrameIndices[1 - current_slot]) {
			throw std::runtime_error("motion particle numeric output requires its actual adjacent decoded physical slots");
		}

		const auto index = *m_FrameIndices[current_slot];
		auto evaluation = m_Evaluation;
		auto numeric = m_Numeric;
		const auto changed = EvaluateCachedMotionLayer(m_Prepared, resource, position, index, force, frame_dirty, evaluation);
		const auto& current = m_Prepared.Frames[index];
		if (!current.Hidden && changed) {
			const auto& control = m_Controls[current_slot];
			if (current.Tween && !m_Prepared.Frames[index + 1].Hidden && std::abs(evaluation.Ratio) >= std::numeric_limits<float>::epsilon()) {
				numeric = BlendParticleNumeric(control, m_Controls[1 - current_slot], evaluation.Ratio);
			} else {
				numeric = CopyParticleNumeric(control);
			}

			RequireFiniteParticleNumeric(numeric);
		}

		m_Evaluation = std::move(evaluation);
		m_Numeric = numeric;
		m_EvaluatedFrameIndex = index;
		m_EvaluatedPosition = position;
		m_Published = true;

		return changed;
	}

	const MotionParticleNumericState& MotionParticleFrameSlots::Numeric() const {
		if (!m_Published) {
			throw std::runtime_error("motion particle numeric output requires its actual current evaluation");
		}

		return m_Numeric;
	}

	std::size_t MotionParticleFrameSlots::EvaluatedFrameIndex() const {
		static_cast<void>(Numeric());

		return m_EvaluatedFrameIndex;
	}

	float MotionParticleFrameSlots::EvaluatedPosition() const {
		static_cast<void>(Numeric());

		return m_EvaluatedPosition;
	}

} // namespace sgre::scene
