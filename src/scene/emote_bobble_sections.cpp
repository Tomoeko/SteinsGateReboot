#include "sgre/scene/emote_bobble_sections.hpp"

#include "sgre/scene/emote_physical_math.hpp"

#include "sgre/scene/emote_bobble_model.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::scene {
	static std::array<float, 4> MultiplySectionQuaternions(const std::array<float, 4>& left, const std::array<float, 4>& right) {
		// Native3039c0's separate products and left-to-right reductions.
		return {((left[3] * right[0] - left[2] * right[1]) + left[1] * right[2]) + left[0] * right[3], ((right[0] * left[2] + left[3] * right[1]) - right[2] * left[0]) + left[1] * right[3], ((right[1] * left[0] - right[0] * left[1]) + left[3] * right[2]) + left[2] * right[3], ((-left[0] * right[0] - right[1] * left[1]) - left[2] * right[2]) + left[3] * right[3]};
	}

	static std::array<float, 3> RotateSectionVector(const std::array<float, 4>& rotation, const std::array<float, 3>& vector) {
		const auto product = MultiplySectionQuaternions(rotation, {vector[0], vector[1], vector[2], 0});
		const std::array<float, 4> conjugate{-rotation[0], -rotation[1], -rotation[2], rotation[3]};
		// Native3037e0 and inline3265d7 use this operand order for the second product.
		return {((conjugate[0] * product[3] - conjugate[1] * product[2]) + conjugate[2] * product[1]) + conjugate[3] * product[0], ((conjugate[1] * product[3] + conjugate[0] * product[2]) - conjugate[2] * product[0]) + conjugate[3] * product[1], ((conjugate[1] * product[0] - conjugate[0] * product[1]) + conjugate[2] * product[3]) + conjugate[3] * product[2]};
	}

	static float SectionSquaredLength(const std::array<float, 3>& vector) {
		return (vector[1] * vector[1] + vector[0] * vector[0]) + vector[2] * vector[2];
	}

	static std::array<float, 4> SectionDirectionRotation(const std::array<float, 3>& previous, const std::array<float, 3>& current, float inheritance) {
		std::array<float, 4> result;
		const float cosine_plus_one = ((current[0] * previous[0] + current[1] * previous[1]) + current[2] * previous[2]) + 1.0f;
		if (cosine_plus_one > std::numeric_limits<float>::epsilon()) {
			result = {current[2] * previous[1] - current[1] * previous[2], current[0] * previous[2] - current[2] * previous[0], current[1] * previous[0] - current[0] * previous[1], cosine_plus_one};
			const float norm = std::sqrt(((result[1] * result[1] + result[0] * result[0]) + result[2] * result[2]) + result[3] * result[3]);
			const float inverse = 1.0f / norm;
			for (auto& value : result) {
				value = inverse * value;
			}
		} else {
			const float axis_dot = (previous[1] * 0.0f + previous[0] * 0.0f) + previous[2];
			const bool parallel_z = !(std::abs(axis_dot) - 1.0f < -std::numeric_limits<float>::epsilon());
			const float axis_y = parallel_z ? 1.0f : 0.0f;
			const float axis_z = parallel_z ? 0.0f : 1.0f;
			const float projection = (previous[1] * axis_y + previous[0] * 0.0f) + axis_z * previous[2];
			const std::array<float, 3> perpendicular{0.0f - projection * previous[0], axis_y - projection * previous[1], axis_z - projection * previous[2]};
			const float inverse = 1.0f / std::sqrt(SectionSquaredLength(perpendicular));
			result = {inverse * perpendicular[0], inverse * perpendicular[1], inverse * perpendicular[2], 0};
		}
		if (inheritance < 1.0f) {
			const float identity_dot = ((result[1] * 0.0f + result[0] * 0.0f) + result[2] * 0.0f) + result[3];
			if (identity_dot - 1.0f < -std::numeric_limits<float>::epsilon()) {
				const float angle = PrepareEmotePhysicalArcCosine(identity_dot);
				const float sine = PrepareEmotePhysicalSine(angle * inheritance);
				const float complement_sine = PrepareEmotePhysicalSine((1.0f - inheritance) * angle);
				const float inverse_sine = 1.0f / PrepareEmotePhysicalSine(angle);
				result = {(sine * result[0] + complement_sine * 0.0f) * inverse_sine, (sine * result[1] + complement_sine * 0.0f) * inverse_sine, (sine * result[2] + complement_sine * 0.0f) * inverse_sine, (sine * result[3] + complement_sine) * inverse_sine};
			} else {
				result = {0, 0, 0, 1};
			}
		}

		return result;
	}

	EmoteBobbleSections::EmoteBobbleSections(const EmoteBobbleModel& model, const EmoteBobbleSectionDefinition& definition)
	    : m_Model(model), m_Definition(definition), m_Sections(model.Particles().size()) {
		if (definition.Radii.size() != model.Particles().size() || !std::isfinite(definition.OrientationInheritance) || !std::isfinite(definition.RadiusInheritance)) {
			throw std::runtime_error("Emote bobble sections require the exact actual radius count and finite inheritance");
		}
		for (std::size_t axis = 0; axis != 3; ++axis) {
			if (!std::isfinite(definition.Normal[axis]) || !std::isfinite(definition.Tangent[axis])) {
				throw std::runtime_error("Emote bobble sections require finite actual normal/tangent vectors");
			}
		}
		for (const auto radius : definition.Radii) {
			if (radius < 0 || !std::isfinite(radius)) {
				throw std::runtime_error("Emote bobble sections require finite nonnegative actual radii");
			}
		}
	}

	void EmoteBobbleSections::Refresh() {
		m_Model.RequireIdle();
		if (!m_Valid || !m_Model.m_Initialized) {
			throw std::runtime_error("Emote bobble sections require their valid initialized actual numerical owner");
		}
		try {
			const float half_angle = m_Model.m_PostureRotation * 0.5f;
			const float sine = PrepareEmotePhysicalSine(half_angle);
			const std::array<float, 4> posture{sine * 0.0f, sine * 0.0f, sine, PrepareEmotePhysicalCosine(half_angle)};
			auto normal = RotateSectionVector(posture, m_Definition.Normal);
			auto tangent = RotateSectionVector(posture, m_Definition.Tangent);
			auto previous_direction = m_Model.m_Direction;
			for (std::size_t index = 0; index != m_Sections.size(); ++index) {
				auto& output = m_Sections[index];
				const auto& particle = m_Model.m_Particles[index];
				output.Center = {particle.Position[0], particle.Position[1]};
				output.LengthScale = 1;
				if (index != 0) {
					std::array<float, 3> delta;
					for (std::size_t axis = 0; axis != 3; ++axis) {
						delta[axis] = particle.Position[axis] - m_Model.m_Particles[index - 1].Position[axis];
					}
					const float length = std::sqrt(SectionSquaredLength(delta));
					if ((std::isfinite(length) && !(length > 0)) || (!m_Model.m_UnorderedJellyComparison && !std::isfinite(length))) {
						throw std::runtime_error("Emote bobble sections require finite nonzero actual segment length");
					}
					output.LengthScale = std::sqrt(m_Model.m_SegmentLength / length);
					for (auto& value : delta) {
						value = value / length;
					}
					if (m_Definition.OrientationInheritance > 0) {
						const auto rotation = SectionDirectionRotation(previous_direction, delta, m_Definition.OrientationInheritance);
						normal = RotateSectionVector(rotation, normal);
						tangent = RotateSectionVector(rotation, tangent);
						previous_direction = delta;
					}
				}
				const float radius = (m_Definition.RadiusInheritance * output.LengthScale + (1.0f - m_Definition.RadiusInheritance)) * m_Definition.Radii[index];
				output.Radius = {radius * normal[2], radius};
				output.Angle = PrepareEmotePhysicalAngle(normal[1], normal[0]);
				output.RelativeAngle = PrepareEmotePhysicalAngle(tangent[1], tangent[0]) - output.Angle;
				for (const auto value : {output.Center[0], output.Center[1], output.Radius[0], output.Radius[1], output.Angle, output.RelativeAngle, output.LengthScale}) {
					if (!m_Model.m_UnorderedJellyComparison && !std::isfinite(value)) {
						throw std::runtime_error("Emote bobble section publication exceeded its finite actual domain");
					}
				}
			}
			m_Ready = true;
		} catch (...) {
			m_Valid = false;
			throw;
		}
	}

	std::span<const EmoteBobbleSection> EmoteBobbleSections::Sections() const {
		m_Model.RequireIdle();
		if (!m_Valid || !m_Ready) {
			throw std::runtime_error("Emote bobble sections require completed valid native publication");
		}

		return m_Sections;
	}
} // namespace sgre::scene
