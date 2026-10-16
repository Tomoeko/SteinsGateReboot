#include "sgre/scene/emote_bobble_model.hpp"

#include "sgre/scene/emote_physical_math.hpp"

#include "sgre/scene/emote_bobble_sections.hpp"
#include "sgre/scene/emote_collider.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {
	static constexpr std::size_t kMaximumBobbleParticles = 256;
	static constexpr float kBobbleLengthCompliance = std::bit_cast<float>(std::uint32_t{0x2e2febff});

	static void RequireBobbleDefinition(const EmoteBobbleDefinition& definition) {
		if (definition.MassWeights.size() < 2 || definition.MassWeights.size() > kMaximumBobbleParticles || !(definition.Length > 0) || !(definition.MassScale > 0) || definition.ControlRadius < 0) {
			throw std::runtime_error("Emote bobble requires bounded actual particles and positive length/mass");
		}
		for (const auto value : {definition.Gravity, definition.ReferenceRotation, definition.Length, definition.MassScale, definition.AlignmentSpring, definition.LengthSpring, definition.ControlRadius, definition.CollisionRepulsion, definition.FrictionBegin, definition.FrictionEnd, definition.CoordinateRotation}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote bobble requires finite actual scalar inputs");
			}
		}
		for (std::size_t axis = 0; axis != 3; ++axis) {
			if (!std::isfinite(definition.Direction[axis]) || !std::isfinite(definition.CoordinateScale[axis])) {
				throw std::runtime_error("Emote bobble requires finite actual vector inputs");
			}
		}
		for (std::size_t index = 0; index != definition.MassWeights.size(); ++index) {
			const float weight = definition.MassWeights[index];
			if (!std::isfinite(weight) || (index == 0 ? weight < 0 : !(weight > 0))) {
				throw std::runtime_error("Emote bobble requires a finite nonnegative root and positive actual free-particle mass weights");
			}
		}
	}

	static float BobbleSquaredLength(const std::array<float, 3>& vector) {
		return (vector[1] * vector[1] + vector[0] * vector[0]) + vector[2] * vector[2];
	}

	static std::array<float, 3> RotateBobbleVector(const std::array<float, 3>& vector, float angle) {
		// Native3039c0 then3037e0: preserve quaternion multiplication order, including zero products.
		const float half_angle = angle * 0.5f;
		const float sine = PrepareEmotePhysicalSine(half_angle);
		const float cosine = PrepareEmotePhysicalCosine(half_angle);
		const std::array<float, 4> rotation{sine * 0.0f, sine * 0.0f, sine, cosine};
		const std::array<float, 4> conjugate{-rotation[0], -rotation[1], -rotation[2], rotation[3]};
		const float x = ((rotation[3] * vector[0] - rotation[2] * vector[1]) + rotation[1] * vector[2]) + rotation[0] * 0.0f;
		const float y = ((vector[0] * rotation[2] + rotation[3] * vector[1]) - vector[2] * rotation[0]) + rotation[1] * 0.0f;
		const float z = ((vector[1] * rotation[0] - vector[0] * rotation[1]) + rotation[3] * vector[2]) + rotation[2] * 0.0f;
		const float w = ((-rotation[0] * vector[0] - vector[1] * rotation[1]) - rotation[2] * vector[2]) + rotation[3] * 0.0f;
		return {((conjugate[0] * w - conjugate[1] * z) + conjugate[2] * y) + conjugate[3] * x, ((conjugate[1] * w + conjugate[0] * z) - conjugate[2] * x) + conjugate[3] * y, ((conjugate[1] * x - conjugate[0] * y) + conjugate[2] * w) + conjugate[3] * z};
	}

	EmoteBobbleModel::EmoteBobbleModel(const EmoteBobbleDefinition& definition)
	    : m_Definition(definition) {
		RequireBobbleDefinition(definition);
		m_Acceleration = RotateBobbleVector({0, 1, 0}, definition.ReferenceRotation);
		for (auto& value : m_Acceleration) {
			value = definition.Gravity * value;
		}
		m_Direction = definition.Direction;
		m_SegmentLength = definition.Length / static_cast<float>(definition.MassWeights.size() - 1);
		m_MaximumLength = m_SegmentLength * definition.ControlRadius;
		m_MaximumSquared = m_MaximumLength * m_MaximumLength;
		// Native324001 conjugates coordinate scale by definition+8, the actual reference rotation.
		const float sine = PrepareEmotePhysicalSine(definition.ReferenceRotation);
		const float cosine = PrepareEmotePhysicalCosine(definition.ReferenceRotation);
		const float reverse_sine = PrepareEmotePhysicalSine(-definition.ReferenceRotation);
		const float reverse_cosine = PrepareEmotePhysicalCosine(-definition.ReferenceRotation);
		const float a = reverse_cosine * definition.CoordinateScale[1] + -reverse_sine * 0.0f;
		const float b = -reverse_sine * definition.CoordinateScale[0] + reverse_cosine * 0.0f;
		const float c = reverse_sine * definition.CoordinateScale[1] + reverse_cosine * 0.0f;
		const float d = reverse_cosine * definition.CoordinateScale[0] + reverse_sine * 0.0f;
		m_CoordinateMatrix = {c * -sine + cosine * d, b * cosine + a * -sine, sine * d + cosine * c, a * cosine + b * sine};
		m_Particles.resize(definition.MassWeights.size());
		m_LengthLambdas.resize(m_Particles.size());
		float free_inverse_mass = 0;
		for (std::size_t index = 0; index != m_Particles.size(); ++index) {
			auto& particle = m_Particles[index];
			const float fraction = static_cast<float>(index) / static_cast<float>(m_Particles.size() - 1);
			particle.Friction = (1.0f - fraction) * definition.FrictionBegin + fraction * definition.FrictionEnd;
			particle.Mass = definition.MassWeights[index] * definition.MassScale;
			particle.InverseMass = particle.Mass == 0 ? 0.0f : 1.0f / particle.Mass;
			if (!std::isfinite(particle.Mass) || !std::isfinite(particle.InverseMass)) {
				throw std::runtime_error("Emote bobble derived mass exceeds its finite actual domain");
			}
			particle.Fixed = index == 0;
			if (index != 0) {
				free_inverse_mass = free_inverse_mass + particle.InverseMass;
			}
		}
		// Native324440 sums free-particle inverse mass, then multiplies each actual mass.
		for (std::size_t index = 1; index != m_Particles.size(); ++index) {
			auto& particle = m_Particles[index];
			particle.AlignmentSpring = (free_inverse_mass * definition.AlignmentSpring) * particle.Mass;
			particle.LengthSpring = (free_inverse_mass * definition.LengthSpring) * particle.Mass;
			if (!std::isfinite(particle.AlignmentSpring) || !std::isfinite(particle.LengthSpring) || !std::isfinite(particle.InverseMass)) {
				throw std::runtime_error("Emote bobble derived mass/springs exceed their finite domain");
			}
		}
	}

	EmoteBobbleModel::EmoteBobbleModel(const EmoteBobbleDefinition& definition, const EmoteBobbleSectionDefinition& sections)
	    : EmoteBobbleModel(definition) {
		m_CompletedSections = std::make_unique<EmoteBobbleSections>(*this, sections);
	}

	EmoteBobbleModel::~EmoteBobbleModel() noexcept = default;

	void EmoteBobbleModel::RequireIdle() const {
		if (m_Operating || !m_Valid) {
			throw std::runtime_error("Emote bobble is active or invalid after a failed numerical update");
		}
	}

	void EmoteBobbleModel::SetPosture(const EmoteBobblePosture& posture) {
		SetPosture(posture, PrepareEmotePhysicalCosine(posture.Rotation), PrepareEmotePhysicalSine(posture.Rotation));
	}

	void EmoteBobbleModel::SetPosture(const EmoteBobblePosture& posture, float cosine, float sine) {
		RequireIdle();
		if (!std::isfinite(cosine) || !std::isfinite(sine)) {
			throw std::runtime_error("Emote bobble posture requires finite actual cosine/sine");
		}
		for (const auto value : posture.Coordinate) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote bobble posture requires finite actual coordinates");
			}
		}
		if (!std::isfinite(posture.Rotation)) {
			throw std::runtime_error("Emote bobble posture requires finite actual rotation");
		}
		const float x = posture.Coordinate[0];
		const float y = posture.Coordinate[1];
		m_Anchor = {y * m_CoordinateMatrix[1] + x * m_CoordinateMatrix[0], y * m_CoordinateMatrix[3] + x * m_CoordinateMatrix[2], m_Definition.CoordinateScale[2] * posture.Coordinate[2]};
		m_CoordinateOffset = {m_Anchor[0] - x, m_Anchor[1] - y};
		m_Particles[0].Position = m_Anchor;
		if (!m_PostureSet || m_PostureRotation != posture.Rotation) {
			m_Direction[0] = cosine * m_Definition.Direction[0] - sine * m_Definition.Direction[1];
			m_Direction[1] = m_Definition.Direction[0] * sine + cosine * m_Definition.Direction[1];
			m_Direction[2] = m_Definition.Direction[2];
			m_PostureRotation = posture.Rotation;
		}
		m_PostureSet = true;
	}

	void EmoteBobbleModel::Initialize() {
		RequireIdle();
		if (!m_PostureSet) {
			throw std::runtime_error("Emote bobble initialization requires actual posture");
		}
		m_UnorderedJellyComparison = false;
		m_Particles[0].Position = m_Anchor;
		std::array<float, 3> spacing;
		for (std::size_t axis = 0; axis != 3; ++axis) {
			spacing[axis] = m_SegmentLength * m_Direction[axis];
		}
		for (std::size_t index = 1; index != m_Particles.size(); ++index) {
			auto& particle = m_Particles[index];
			for (std::size_t axis = 0; axis != 3; ++axis) {
				particle.Position[axis] = spacing[axis] + m_Particles[index - 1].Position[axis];
				particle.Previous[axis] = particle.Position[axis];
				particle.Velocity[axis] = 0;
			}
		}
		m_Initialized = true;
		RefreshCompletedSections();
	}

	void EmoteBobbleModel::ClearForces() {
		RequireIdle();
		for (std::size_t index = 1; index != m_Particles.size(); ++index) {
			m_Particles[index].Force = {};
		}
	}

	void EmoteBobbleModel::AttachCollider(EmoteCollider& collider) {
		RequireIdle();
		if (m_CompletedSections == nullptr || m_Colliders.size() >= 4096) {
			throw std::runtime_error("Emote bobble collider attachment requires its actual completed-section owner and bounded collider list");
		}
		m_Colliders.push_back(&collider);
	}

	void EmoteBobbleModel::RefreshCompletedSections() {
		if (m_CompletedSections == nullptr) {
			return;
		}
		try {
			m_CompletedSections->Refresh();
		} catch (...) {
			m_Valid = false;
			throw;
		}
	}

	void EmoteBobbleModel::AddForce(std::size_t particle, const std::array<float, 3>& force) {
		RequireIdle();
		if (particle == 0 || particle >= m_Particles.size()) {
			throw std::runtime_error("Emote bobble force requires an actual free particle");
		}
		for (const auto value : force) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote bobble force requires finite actual components");
			}
		}
		for (std::size_t axis = 0; axis != 3; ++axis) {
			m_Particles[particle].Force[axis] = m_Particles[particle].Force[axis] + force[axis];
		}
	}

	void EmoteBobbleModel::AddAccelerationAndSprings(float seconds) {
		const auto rotated = RotateBobbleVector(m_Acceleration, m_PostureRotation - m_Definition.ReferenceRotation);
		std::array<float, 3> acceleration;
		std::array<float, 3> rest;
		for (std::size_t axis = 0; axis != 3; ++axis) {
			acceleration[axis] = (m_Acceleration[axis] - rotated[axis] * 1.0f) * seconds;
			rest[axis] = m_SegmentLength * m_Direction[axis];
		}
		for (std::size_t index = 1; index != m_Particles.size(); ++index) {
			auto& particle = m_Particles[index];
			auto& previous = m_Particles[index - 1];
			const float step_inverse_mass = seconds * particle.InverseMass;
			const float x = step_inverse_mass * particle.Force[0] + (acceleration[0] + particle.Velocity[0]);
			const float y = step_inverse_mass * particle.Force[1] + (acceleration[1] + particle.Velocity[1]);
			const float z = (acceleration[2] + particle.Velocity[2]) + step_inverse_mass * particle.Force[2];
			particle.Velocity = {x, y, z};
			std::array<float, 3> delta;
			for (std::size_t axis = 0; axis != 3; ++axis) {
				delta[axis] = particle.Position[axis] - previous.Position[axis];
			}
			const float length = std::sqrt((delta[0] * delta[0] + delta[1] * delta[1]) + delta[2] * delta[2]);
			if ((std::isfinite(length) && !(length > 0)) || (!m_UnorderedJellyComparison && !std::isfinite(length))) {
				throw std::runtime_error("Emote bobble spring requires a finite nonzero actual preceding segment");
			}
			const float length_gain = (m_SegmentLength / length - 1.0f) * particle.LengthSpring;
			std::array<float, 3> spring;
			for (std::size_t axis = 0; axis != 3; ++axis) {
				spring[axis] = ((rest[axis] + previous.Position[axis]) - particle.Position[axis]) * particle.AlignmentSpring + length_gain * delta[axis];
				particle.Velocity[axis] = spring[axis] * step_inverse_mass + particle.Velocity[axis];
			}
			if (!previous.Fixed) {
				const float previous_step = seconds * previous.InverseMass;
				for (std::size_t axis = 0; axis != 3; ++axis) {
					previous.Velocity[axis] = previous.Velocity[axis] - spring[axis] * previous_step;
				}
			}
		}
	}

	void EmoteBobbleModel::ApplyColliders(float seconds, std::span<const EmoteBobbleSection> completed_sections) {
		if (m_Colliders.empty()) {
			return;
		}
		if (completed_sections.size() != m_Particles.size()) {
			throw std::runtime_error("Emote bobble contacts require every actual prior completed particle section");
		}

		const float gain = seconds * m_Definition.CollisionRepulsion;
		for (std::size_t index = 1; index != m_Particles.size(); ++index) {
			auto& particle = m_Particles[index];
			const resource::MotionPoint displacement{particle.Position[0] - particle.Previous[0], particle.Position[1] - particle.Previous[1]};
			const auto& section = completed_sections[index];
			EmoteCollisionEllipse ellipse;
			ellipse.Center = {section.Center[0] - m_CoordinateOffset[0], section.Center[1] - m_CoordinateOffset[1]};
			ellipse.Radius = {section.Radius[0], section.Radius[1]};
			ellipse.Rotation = section.Angle;
			for (auto* collider : m_Colliders) {
				if (!collider->Enabled()) {
					continue;
				}
				resource::MotionPoint projected;
				if (collider->ProjectSection(projected, ellipse, displacement)) {
					particle.Velocity[0] = (projected.X - ellipse.Center.X) * gain + particle.Velocity[0];
					particle.Velocity[1] = (projected.Y - ellipse.Center.Y) * gain + particle.Velocity[1];
				}
			}
		}
	}

	void EmoteBobbleModel::ProjectAdhocVelocities(float seconds) {
		const float inverse_seconds = 1.0f / seconds;
		for (std::size_t index = 1; index != m_Particles.size(); ++index) {
			auto& particle = m_Particles[index];
			std::array<float, 3> predicted;
			std::array<float, 3> delta;
			for (std::size_t axis = 0; axis != 3; ++axis) {
				predicted[axis] = seconds * particle.Velocity[axis] + particle.Position[axis];
				delta[axis] = predicted[axis] - m_Particles[index - 1].Position[axis];
			}
			const float squared = BobbleSquaredLength(delta);
			if (squared > m_MaximumSquared) {
				const float length = std::sqrt(squared);
				const float correction = (length - m_MaximumLength) / length;
				for (std::size_t axis = 0; axis != 3; ++axis) {
					predicted[axis] = predicted[axis] - correction * delta[axis];
					particle.Velocity[axis] = (predicted[axis] - particle.Position[axis]) * inverse_seconds;
				}
			}
		}
	}

	void EmoteBobbleModel::SolveCompliantLengths(float seconds) {
		for (auto& value : m_LengthLambdas) {
			value = 0;
		}
		for (std::size_t iteration = 0; iteration != 20; ++iteration) {
			for (std::size_t index = 1; index != m_Particles.size(); ++index) {
				auto& previous = m_Particles[index - 1];
				auto& particle = m_Particles[index];
				std::array<float, 3> direction;
				for (std::size_t axis = 0; axis != 3; ++axis) {
					direction[axis] = particle.Predicted[axis] - previous.Predicted[axis];
				}
				const float squared = BobbleSquaredLength(direction);
				if (squared <= m_MaximumSquared) {
					continue;
				}
				const float length = std::sqrt(squared);
				const float alpha = kBobbleLengthCompliance / (seconds * seconds);
				const float numerator = (length - m_MaximumLength) - m_LengthLambdas[index] * alpha;
				const float denominator = (previous.InverseMass + particle.InverseMass) + alpha;
				const float delta_lambda = numerator / denominator;
				m_LengthLambdas[index] = delta_lambda + m_LengthLambdas[index];
				for (std::size_t axis = 0; axis != 3; ++axis) {
					direction[axis] = direction[axis] / length;
					direction[axis] = direction[axis] * delta_lambda;
					previous.Predicted[axis] = direction[axis] * previous.InverseMass + previous.Predicted[axis];
					particle.Predicted[axis] = particle.Predicted[axis] - direction[axis] * particle.InverseMass;
				}
			}
		}
	}

	void EmoteBobbleModel::SolveAdhocLengths() {
		for (std::size_t iteration = 0; iteration != 10; ++iteration) {
			bool corrected = false;
			for (std::size_t index = 1; index != m_Particles.size(); ++index) {
				auto& previous = m_Particles[index - 1];
				auto& particle = m_Particles[index];
				std::array<float, 3> delta;
				for (std::size_t axis = 0; axis != 3; ++axis) {
					delta[axis] = particle.Predicted[axis] - previous.Predicted[axis];
				}
				const float squared = BobbleSquaredLength(delta);
				if (squared <= m_MaximumSquared) {
					continue;
				}
				const float length = std::sqrt(squared);
				const float gain = (length - m_MaximumLength) / length;
				for (std::size_t axis = 0; axis != 3; ++axis) {
					delta[axis] = delta[axis] * gain;
					if (!previous.Fixed) {
						previous.Predicted[axis] = delta[axis] * 0.5f + previous.Predicted[axis];
					}
					particle.Predicted[axis] = particle.Predicted[axis] - delta[axis] * 0.5f;
				}
				corrected = true;
			}
			if (!corrected) {
				break;
			}
		}
	}

	void EmoteBobbleModel::Step(float seconds) {
		if (m_UnorderedJellyComparison) {
			throw std::runtime_error("unordered Jelly particles require actual reinitialization before an ordinary finite step");
		}
		StepImpl(seconds, false);
	}

	void EmoteBobbleModel::StepForUnorderedJellyComparison(float seconds) {
		if (seconds != 0 || m_Definition.AdhocSimulation || !m_Colliders.empty()) {
			throw std::runtime_error("native unordered Jelly comparison requires the measured zero-second ordinary model without colliders");
		}
		StepImpl(seconds, true);
	}

	void EmoteBobbleModel::StepImpl(float seconds, bool unordered_comparison) {
		RequireIdle();
		if (!m_Initialized || (!unordered_comparison && !(seconds > 0)) || !std::isfinite(seconds)) {
			throw std::runtime_error("Emote bobble step requires initialized particles and finite positive seconds");
		}
		if (!m_Definition.Enabled) {
			Initialize();
			return;
		}
		m_UnorderedJellyComparison = unordered_comparison;
		const auto completed_sections = m_Colliders.empty() ? std::span<const EmoteBobbleSection>{} : m_CompletedSections->Sections();
		m_Operating = true;
		try {
			AddAccelerationAndSprings(seconds);
			ApplyColliders(seconds, completed_sections);
			if (m_Definition.AdhocSimulation) {
				ProjectAdhocVelocities(seconds);
			}
			for (auto& particle : m_Particles) {
				for (std::size_t axis = 0; axis != 3; ++axis) {
					particle.Predicted[axis] = particle.Fixed ? particle.Position[axis] : seconds * particle.Velocity[axis] + particle.Position[axis];
				}
			}
			if (m_Definition.AdhocSimulation) {
				SolveAdhocLengths();
			} else {
				SolveCompliantLengths(seconds);
			}
			const float inverse_seconds = 1.0f / seconds;
			for (auto& particle : m_Particles) {
				if (particle.Fixed) {
					continue;
				}
				for (std::size_t axis = 0; axis != 3; ++axis) {
					particle.Velocity[axis] = (particle.Predicted[axis] - particle.Position[axis]) * inverse_seconds;
					particle.Previous[axis] = particle.Position[axis];
					particle.Position[axis] = particle.Predicted[axis];
				}
			}
			for (auto& particle : m_Particles) {
				if (!particle.Fixed) {
					const float damping = seconds * particle.Friction;
					for (auto& value : particle.Velocity) {
						value = value - value * damping;
					}
				}
				for (std::size_t axis = 0; axis != 3; ++axis) {
					if (!unordered_comparison && (!std::isfinite(particle.Position[axis]) || !std::isfinite(particle.Predicted[axis]) || !std::isfinite(particle.Velocity[axis]))) {
						throw std::runtime_error("Emote bobble numerical update exceeded its finite actual domain");
					}
				}
			}
			m_Operating = false;
			RefreshCompletedSections();
		} catch (...) {
			m_Operating = false;
			m_Valid = false;
			throw;
		}
	}

	void EmoteBobbleModel::WritePoints(std::span<std::array<float, 2>> destination) const {
		RequireIdle();
		if (!m_Initialized || destination.size() != m_Particles.size()) {
			throw std::runtime_error("Emote bobble point output requires its exact actual initialized particle count");
		}
		for (std::size_t index = 0; index != m_Particles.size(); ++index) {
			destination[index] = {m_Particles[index].Position[0] - m_CoordinateOffset[0], m_Particles[index].Position[1] - m_CoordinateOffset[1]};
		}
	}

	std::span<const EmoteBobbleParticle> EmoteBobbleModel::Particles() const noexcept {
		return m_Particles;
	}
	void EmoteBobbleModel::RequireCanAssignState(const EmoteBobbleModel& source) const {
		RequireIdle();
		source.RequireIdle();
		if (m_CompletedSections && source.m_CompletedSections) {
			m_CompletedSections->RequireCanAssignState(*source.m_CompletedSections);
		}
	}

	void EmoteBobbleModel::AssignState(const EmoteBobbleModel& source) {
		RequireCanAssignState(source);
		if (this == &source) {
			return;
		}

		m_Anchor = source.m_Anchor;
		m_Direction = source.m_Direction;
		m_PostureRotation = source.m_PostureRotation;
		std::copy_n(source.m_Particles.begin(), std::min(m_Particles.size(), source.m_Particles.size()), m_Particles.begin());
		if (m_CompletedSections && source.m_CompletedSections) {
			m_CompletedSections->AssignState(*source.m_CompletedSections);
		}
		m_PostureSet = source.m_PostureSet;
		m_Initialized = source.m_Initialized;
	}

} // namespace sgre::scene
