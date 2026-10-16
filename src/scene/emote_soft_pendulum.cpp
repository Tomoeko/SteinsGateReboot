#include "sgre/scene/emote_soft_pendulum.hpp"

#include "sgre/scene/emote_catenary_curve.hpp"
#include "sgre/scene/emote_collider.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sgre::scene {
	static constexpr std::size_t kMaximumSoftPendulumParticles = 256;
	static constexpr std::size_t kMaximumSoftPendulumTrailingPoints = 4096;
	static constexpr double kNativeLengthCompliance = std::bit_cast<double>(std::uint64_t{0x3dc5fd7fe1796495});

	static void RequireDefinition(const EmoteSoftPendulumDefinition& definition) {
		if (definition.ParticleCount < 2 || definition.ParticleCount > kMaximumSoftPendulumParticles || definition.TrailingPoints > kMaximumSoftPendulumTrailingPoints || !(definition.Mass > 0) || !(definition.Length > 0) || definition.ControlRadius < 0 || definition.CollisionHardness > 1 || (definition.FixedTip && (definition.ParticleCount < 3 || definition.TrailingPoints != 0 || definition.BottomRatio < 0 || definition.BottomRatio > 1))) {
			throw std::runtime_error("Emote soft pendulum requires bounded particles and positive actual mass/length");
		}
		for (const auto value : {definition.Mass, definition.Length, definition.Elastic, definition.RotationInheritance, definition.ControlRadius, definition.CoordinateRotation, definition.ReferenceRotation, definition.TrailingLength, definition.FrictionBegin, definition.FrictionEnd, definition.CollisionRepulsion, definition.BottomRatio}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote soft pendulum definition requires finite actual scalars");
			}
		}
		for (std::size_t axis = 0; axis != 3; ++axis) {
			if (!std::isfinite(definition.Acceleration[axis]) || !std::isfinite(definition.CoordinateScale[axis])) {
				throw std::runtime_error("Emote soft pendulum definition requires finite actual vectors");
			}
		}
	}

	static double SquaredLength(const std::array<double, 3>& point) {
		// Native SSE2 reduction sums Y²+X², then Z².
		return (point[1] * point[1] + point[0] * point[0]) + point[2] * point[2];
	}

	EmoteSoftPendulum::EmoteSoftPendulum(const EmoteSoftPendulumDefinition& definition)
	    : m_Definition(definition) {
		RequireDefinition(definition);
		for (std::size_t axis = 0; axis != 3; ++axis) {
			m_InitialAcceleration[axis] = definition.Acceleration[axis];
		}
		m_Acceleration = m_InitialAcceleration;
		const auto acceleration_squared = SquaredLength(m_InitialAcceleration);
		if ((!definition.FixedTip && !(acceleration_squared > 0)) || !std::isfinite(acceleration_squared)) {
			throw std::runtime_error("Emote soft pendulum requires its nonzero finite native initialization direction");
		}
		m_ReciprocalAccelerationLength = 1.0 / std::sqrt(acceleration_squared);
		m_SegmentLength = static_cast<double>(definition.Length) / static_cast<double>(definition.ParticleCount - 1);
		m_SegmentSquared = m_SegmentLength * m_SegmentLength;
		// Native3208aa keeps this producer calculation in float before storing the double gain.
		const float elastic_per_length = definition.Elastic / definition.Length;
		m_ElasticGain = elastic_per_length * static_cast<float>(definition.ParticleCount - (definition.FixedTip ? 2 : 1));
		if (!std::isfinite(m_ElasticGain)) {
			throw std::runtime_error("Emote soft pendulum elasticity exceeds its finite actual domain");
		}
		const double angle = definition.CoordinateRotation;
		const double sine = std::sin(angle);
		const double cosine = std::cos(angle);
		const double reverse_sine = std::sin(-angle);
		const double reverse_cosine = std::cos(-angle);
		const double first_x = reverse_cosine * definition.CoordinateScale[0] + reverse_sine * 0.0;
		const double first_y = -reverse_sine * definition.CoordinateScale[0] + reverse_cosine * 0.0;
		const double second_x = reverse_sine * definition.CoordinateScale[1] + reverse_cosine * 0.0;
		const double second_y = reverse_cosine * definition.CoordinateScale[1] + -reverse_sine * 0.0;
		m_CoordinateMatrix = {cosine * first_x + -sine * second_x, cosine * first_y + -sine * second_y, cosine * second_x + sine * first_x, cosine * second_y + sine * first_y};
		m_Particles.resize(definition.ParticleCount);
		m_LengthLambdas.resize(definition.ParticleCount);
		m_TrailingPositions.resize(definition.TrailingPoints);
		if (definition.TrailingPoints != 0) {
			m_TrailingSpacing = static_cast<double>(definition.TrailingLength) / static_cast<double>(definition.TrailingPoints);
		}
		double total_weight = 0;
		for (std::size_t index = 0; index != m_Particles.size(); ++index) {
			auto& particle = m_Particles[index];
			const auto fraction = static_cast<double>(index) / static_cast<double>(m_Particles.size() - 1);
			auto weight = fraction;
			if (definition.FixedTip) {
				weight = 1.0 - std::abs((static_cast<double>(definition.BottomRatio) - fraction) / static_cast<double>(std::max(1.0f - definition.BottomRatio, definition.BottomRatio)));
			}
			particle.Friction = (1.0 - weight) * static_cast<double>(definition.FrictionBegin) + static_cast<double>(definition.FrictionEnd) * weight;
			particle.Fixed = index == 0 || (definition.FixedTip && index + 1 == m_Particles.size());
			if (!particle.Fixed) {
				particle.Mass = definition.MassTiltedDistribution ? std::pow(std::max(0.01, particle.Friction), 1.5) : 1.0;
				total_weight += particle.Mass;
			}
		}
		for (auto& particle : m_Particles) {
			if (!particle.Fixed) {
				particle.Mass = (static_cast<double>(definition.Mass) / total_weight) * particle.Mass;
				particle.InverseMass = 1.0 / particle.Mass;
			}
		}
	}

	void EmoteSoftPendulum::RequireIdle() const {
		if (m_Operating || !m_Valid) {
			throw std::runtime_error("Emote soft pendulum is active or invalid after a failed physical update");
		}
	}

	void EmoteSoftPendulum::SetPosture(const EmoteSoftPendulumPosture& posture) {
		RequireIdle();
		for (const auto value : posture.Coordinate) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote soft pendulum posture requires finite actual coordinates");
			}
		}
		if (!std::isfinite(posture.Rotation)) {
			throw std::runtime_error("Emote soft pendulum posture requires a finite actual rotation");
		}
		const double x = posture.Coordinate[0];
		const double y = posture.Coordinate[1];
		m_Anchor = {y * m_CoordinateMatrix[1] + x * m_CoordinateMatrix[0], y * m_CoordinateMatrix[3] + x * m_CoordinateMatrix[2], static_cast<double>(posture.Coordinate[2]) * m_Definition.CoordinateScale[2]};
		m_CoordinateOffset = {m_Anchor[0] - x, m_Anchor[1] - y};
		if (!m_Definition.FixedTip) {
			m_Particles[0].Position = m_Anchor;
		}
		if (posture.Rotation != m_PostureRotation) {
			if (m_Definition.RotationInheritance != 0) {
				m_DirectionPrepared = false;
			}
			m_PostureRotation = posture.Rotation;
		}
		if (!m_DirectionPrepared) {
			const auto rotation = (static_cast<double>(posture.Rotation) - static_cast<double>(m_Definition.ReferenceRotation)) * static_cast<double>(m_Definition.RotationInheritance);
			const auto sine = std::sin(rotation);
			const auto cosine = std::cos(rotation);
			m_Acceleration[0] = m_InitialAcceleration[0] * cosine - m_InitialAcceleration[1] * sine;
			m_Acceleration[1] = m_InitialAcceleration[0] * sine + m_InitialAcceleration[1] * cosine;
			m_DirectionPrepared = true;
		}
		UpdateTrailingPositions(false);
		m_PostureSet = true;
	}

	void EmoteSoftPendulum::UpdateTrailingPositions(bool from_last_particle) {
		std::array<double, 3> position;
		std::array<double, 3> step;
		for (std::size_t axis = 0; axis != 3; ++axis) {
			const auto direction = m_ReciprocalAccelerationLength * m_Acceleration[axis];
			position[axis] = from_last_particle ? m_Particles.back().Position[axis] : direction * static_cast<double>(m_Definition.Length) + m_Anchor[axis];
			step[axis] = from_last_particle ? (m_TrailingSpacing * m_ReciprocalAccelerationLength) * m_Acceleration[axis] : direction * m_TrailingSpacing;
		}
		for (auto& trailing : m_TrailingPositions) {
			for (std::size_t axis = 0; axis != 3; ++axis) {
				position[axis] += step[axis];
				trailing[axis] = position[axis];
			}
		}
	}

	void EmoteSoftPendulum::SetFixedEnds(const std::array<float, 2>& first, const std::array<float, 2>& last) {
		RequireIdle();
		if (!m_Definition.FixedTip || !m_PostureSet) {
			throw std::runtime_error("Emote Catenary endpoint admission requires its actual two-anchor posture owner");
		}
		for (const auto value : {first[0], first[1], last[0], last[1]}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote Catenary requires finite actual endpoint coordinates");
			}
		}
		m_Particles.front().Position = {static_cast<double>(first[0]) + m_CoordinateOffset[0], static_cast<double>(first[1]) + m_CoordinateOffset[1], m_Anchor[2]};
		m_Particles.back().Position = {static_cast<double>(last[0]) + m_CoordinateOffset[0], static_cast<double>(last[1]) + m_CoordinateOffset[1], m_Anchor[2]};
		m_EndsSet = true;
	}

	void EmoteSoftPendulum::InitializeCatenary() {
		if (!m_EndsSet) {
			throw std::runtime_error("Emote Catenary initialization requires both actual fixed endpoints");
		}
		const auto angle = 3.14159265358979323846264338327950288 - (static_cast<double>(m_PostureRotation) - static_cast<double>(m_Definition.ReferenceRotation)) * static_cast<double>(m_Definition.RotationInheritance);
		const auto cosine = std::cos(angle);
		const auto sine = std::sin(angle);
		const auto tx = (-m_Anchor[1] * -sine) + (-m_Anchor[0] * cosine) + 0.0;
		const auto ty = (-m_Anchor[1] * cosine) + (-m_Anchor[0] * sine) + 0.0;
		const auto first = m_Particles.front().Position;
		const auto last = m_Particles.back().Position;
		const std::array<double, 2> source{first[1] * -sine + first[0] * cosine + tx, first[1] * cosine + first[0] * sine + ty};
		const std::array<double, 2> destination{last[1] * -sine + last[0] * cosine + tx, last[1] * cosine + last[0] * sine + ty};
		const auto curve = PrepareEmoteCatenaryCurve(source, destination, m_Definition.Length);
		for (std::size_t index = 1; index + 1 < m_Particles.size(); ++index) {
			auto& particle = m_Particles[index];
			if (curve) {
				const auto point = curve->SampleArc(source[0], destination[0], index, m_Particles.size() - 1);
				particle.Position = {point[1] * sine + point[0] * cosine + m_Anchor[0], point[1] * cosine + point[0] * -sine + m_Anchor[1], m_Anchor[2]};
			} else {
				const auto ratio = static_cast<double>(index) / static_cast<double>(m_Particles.size() - 1);
				particle.Position = {(last[0] - first[0]) * ratio + first[0], (last[1] - first[1]) * ratio + first[1], m_Anchor[2]};
			}
			particle.Previous = particle.Position;
			particle.Predicted = particle.Position;
			particle.Velocity = {};
		}
		m_Initialized = true;
		m_BoundsReady = false;
	}

	void EmoteSoftPendulum::Initialize() {
		RequireIdle();
		if (!m_PostureSet) {
			throw std::runtime_error("Emote soft pendulum initialization requires its actual preceding posture");
		}
		m_UnorderedStringComparison = false;
		if (m_Definition.FixedTip) {
			InitializeCatenary();
			return;
		}
		const auto scale = m_SegmentLength * m_ReciprocalAccelerationLength;
		for (std::size_t index = 1; index != m_Particles.size(); ++index) {
			auto& particle = m_Particles[index];
			for (std::size_t axis = 0; axis != 3; ++axis) {
				particle.Position[axis] = m_Acceleration[axis] * scale + m_Particles[index - 1].Position[axis];
			}
			particle.Previous = particle.Position;
			particle.Predicted = particle.Position;
			particle.Velocity = {};
		}
		UpdateTrailingPositions(true);
		m_Initialized = true;
		m_BoundsReady = false;
	}

	void EmoteSoftPendulum::ClearForcesAndMeasureBounds() {
		RequireIdle();
		if (!m_Initialized) {
			throw std::runtime_error("Emote soft pendulum force phase requires actual initialized particles");
		}
		m_Bounds = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
		for (auto& particle : m_Particles) {
			if (!particle.Fixed) {
				particle.Force = {};
			}
			m_Bounds.Left = std::min(m_Bounds.Left, particle.Position[0]);
			m_Bounds.Top = std::min(m_Bounds.Top, particle.Position[1]);
			m_Bounds.Right = std::max(m_Bounds.Right, particle.Position[0]);
			m_Bounds.Bottom = std::max(m_Bounds.Bottom, particle.Position[1]);
		}
		m_BoundsReady = true;
	}

	void EmoteSoftPendulum::AddForce(std::size_t particle, const std::array<float, 3>& force) {
		RequireIdle();
		if (!m_Initialized || particle >= m_Particles.size()) {
			throw std::runtime_error("Emote soft pendulum force requires an actual initialized particle");
		}
		for (const auto value : force) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote soft pendulum force must be finite");
			}
		}
		for (std::size_t axis = 0; axis != 3; ++axis) {
			m_Particles[particle].Force[axis] += force[axis];
		}
	}

	void EmoteSoftPendulum::AddAcceleration(double seconds) {
		for (std::size_t index = 1; index != m_Particles.size(); ++index) {
			auto& particle = m_Particles[index];
			const auto& previous = m_Particles[index - 1];
			if (!particle.Fixed) {
				for (std::size_t axis = 0; axis != 2; ++axis) {
					particle.Velocity[axis] = (seconds * particle.InverseMass) * particle.Force[axis] + (seconds * m_Acceleration[axis] + particle.Velocity[axis]);
				}
				particle.Velocity[2] = (seconds * m_Acceleration[2] + particle.Velocity[2]) + (seconds * particle.InverseMass) * particle.Force[2];
			}
			std::array<double, 3> displacement;
			for (std::size_t axis = 0; axis != 3; ++axis) {
				displacement[axis] = particle.Position[axis] - previous.Position[axis];
			}
			const auto squared = SquaredLength(displacement);
			particle.AccelerationProjection = (displacement[1] * m_Acceleration[1] + displacement[0] * m_Acceleration[0]) + displacement[2] * m_Acceleration[2];
			if (m_ElasticGain > 0 && m_SegmentSquared > squared) {
				if (!(squared > 0)) {
					throw std::runtime_error("Emote soft pendulum compressed pair has an unsupported zero separation");
				}
				const auto distance = std::sqrt(squared);
				const auto difference = m_SegmentLength - distance;
				const auto maximum_inverse_mass = std::max(previous.InverseMass, particle.InverseMass);
				const auto impulse = ((difference * m_ElasticGain) * maximum_inverse_mass) * seconds;
				const auto lower = -difference / seconds;
				const auto upper = difference / seconds;
				const auto factor = std::min(impulse <= lower ? lower : impulse, upper) / distance;
				for (std::size_t axis = 0; axis != 3; ++axis) {
					const auto correction = displacement[axis] * factor;
					m_Particles[index - 1].Velocity[axis] -= (correction * previous.InverseMass) / maximum_inverse_mass;
					particle.Velocity[axis] += (particle.InverseMass * correction) / maximum_inverse_mass;
				}
			}
		}
	}

	void EmoteSoftPendulum::SolveLengths(double seconds) {
		std::fill(m_LengthLambdas.begin(), m_LengthLambdas.end(), 0.0);
		const auto compliance = kNativeLengthCompliance / (seconds * seconds);
		for (std::size_t iteration = 0; iteration != m_Particles.size() * 5; ++iteration) {
			for (std::size_t index = 1; index != m_Particles.size(); ++index) {
				auto& previous = m_Particles[index - 1];
				auto& particle = m_Particles[index];
				std::array<double, 3> displacement;
				for (std::size_t axis = 0; axis != 3; ++axis) {
					displacement[axis] = particle.Predicted[axis] - previous.Predicted[axis];
				}
				const auto squared = (displacement[0] * displacement[0] + displacement[1] * displacement[1]) + displacement[2] * displacement[2];
				if (!(squared > m_SegmentSquared)) {
					continue;
				}
				const auto distance = std::sqrt(squared);
				const auto inverse_previous = previous.Fixed ? 0.0 : previous.InverseMass;
				const auto inverse_current = particle.Fixed ? 0.0 : particle.InverseMass;
				const auto increment = ((distance - m_SegmentLength) - m_LengthLambdas[index] * compliance) / ((inverse_current + inverse_previous) + compliance);
				m_LengthLambdas[index] += increment;
				for (std::size_t axis = 0; axis != 3; ++axis) {
					const auto correction = (displacement[axis] / distance) * increment;
					previous.Predicted[axis] += correction * inverse_previous;
					particle.Predicted[axis] -= correction * inverse_current;
				}
			}
		}
	}

	void EmoteSoftPendulum::ProjectAdhocVelocities(double seconds) {
		const auto inverse_seconds = 1.0 / seconds;
		for (std::size_t index = 0; index < m_Particles.size(); ++index) {
			auto& particle = m_Particles[index];
			if (particle.Fixed) {
				continue;
			}
			std::array<double, 3> prediction;
			std::array<double, 3> adjustment{};
			for (std::size_t axis = 0; axis < 3; ++axis) {
				prediction[axis] = particle.Velocity[axis] * seconds + particle.Position[axis];
			}
			// Native3201a0 tests both neighbors' actual positions, not their predictions or new velocities.
			for (std::size_t side = 0; side < 2; ++side) {
				if ((side == 0 && index == 0) || (side == 1 && index + 1 == m_Particles.size())) {
					continue;
				}
				const auto neighbor = side == 0 ? index - 1 : index + 1;
				std::array<double, 3> displacement;
				for (std::size_t axis = 0; axis < 3; ++axis) {
					displacement[axis] = prediction[axis] - m_Particles[neighbor].Position[axis];
				}
				const auto squared = SquaredLength(displacement);
				if (squared <= m_SegmentSquared) {
					continue;
				}
				const auto distance = std::sqrt(squared);
				const auto factor = (distance - m_SegmentLength) / distance;
				for (std::size_t axis = 0; axis < 3; ++axis) {
					const auto projected = prediction[axis] - factor * displacement[axis];
					const auto delta = (projected - particle.Position[axis]) * inverse_seconds - particle.Velocity[axis];
					adjustment[axis] += delta;
				}
			}
			for (std::size_t axis = 0; axis < 3; ++axis) {
				particle.Velocity[axis] = adjustment[axis] + particle.Velocity[axis];
			}
		}
	}

	void EmoteSoftPendulum::SolveAdhocLengths() {
		// Native31f968 selects10 for adhoc; default XPBD keeps its separate5*particleCount budget.
		static constexpr std::size_t kAdhocIterations = 10;
		for (std::size_t iteration = 0; iteration < kAdhocIterations; ++iteration) {
			bool corrected = false;
			for (std::size_t index = 1; index < m_Particles.size(); ++index) {
				auto& previous = m_Particles[index - 1];
				auto& particle = m_Particles[index];
				// Constructor-default native mode+190=0 skips gravity-opposed segments. Colliders remain separate.
				if (!m_Definition.FixedTip && particle.AccelerationProjection < 0) {
					continue;
				}
				std::array<double, 3> displacement;
				for (std::size_t axis = 0; axis < 3; ++axis) {
					displacement[axis] = particle.Predicted[axis] - previous.Predicted[axis];
				}
				const auto squared = SquaredLength(displacement);
				if (squared <= m_SegmentSquared) {
					continue;
				}
				const auto distance = std::sqrt(squared);
				const auto factor = (distance - m_SegmentLength) / distance;
				for (std::size_t axis = 0; axis < 3; ++axis) {
					const auto correction = displacement[axis] * factor;
					if (!previous.Fixed) {
						previous.Predicted[axis] += correction * 0.5;
					}
					if (!particle.Fixed) {
						particle.Predicted[axis] -= correction * 0.5;
					}
				}
				corrected = true;
			}
			if (!corrected) {
				break;
			}
		}
	}

	void EmoteSoftPendulum::AttachCollider(EmoteCollider& collider) {
		RequireIdle();
		if (m_Colliders.size() >= 4096) {
			throw std::runtime_error("Emote soft pendulum collider count exceeds bounded native attachment storage");
		}
		m_Colliders.push_back(&collider);
	}

	void EmoteSoftPendulum::ApplyCollisions(bool velocity_response, double seconds) {
		const auto project = [&](EmoteCollider& collider, EmoteSoftPendulumParticle& particle) {
			if (particle.Fixed || !collider.Enabled()) {
				return;
			}
			const auto& position = velocity_response ? particle.Position : particle.Predicted;
			resource::MotionPoint point{static_cast<float>(position[0] - m_CoordinateOffset[0]), static_cast<float>(position[1] - m_CoordinateOffset[1])};
			const auto original = point;
			const resource::MotionPoint displacement{static_cast<float>(particle.Position[0] - particle.Previous[0]), static_cast<float>(particle.Position[1] - particle.Previous[1])};
			if (!collider.Project(point, displacement, m_Definition.ControlRadius)) {
				return;
			}
			if (velocity_response) {
				const auto gain = seconds * static_cast<double>(m_Definition.CollisionRepulsion);
				particle.Velocity[0] += static_cast<double>(point.X - original.X) * gain;
				particle.Velocity[1] += static_cast<double>(point.Y - original.Y) * gain;
			} else {
				particle.Predicted[0] = static_cast<double>(point.X) + m_CoordinateOffset[0];
				particle.Predicted[1] = static_cast<double>(point.Y) + m_CoordinateOffset[1];
				particle.Collided = true;
			}
		};
		if (velocity_response) {
			for (auto& particle : m_Particles) {
				for (auto* collider : m_Colliders) {
					project(*collider, particle);
				}
			}
		} else {
			for (auto& particle : m_Particles) {
				particle.Collided = false;
			}
			for (auto* collider : m_Colliders) {
				for (auto& particle : m_Particles) {
					project(*collider, particle);
				}
			}
		}
	}

	void EmoteSoftPendulum::Step(float seconds) {
		StepImpl(seconds, false);
	}

	// Original3211f0 still runs at zero; only the enclosing String comparison may consume its transient state.
	void EmoteSoftPendulum::StepForUnorderedStringComparison(float seconds) {
		if (seconds != 0 || m_Definition.AdhocSimulation || m_Definition.FixedTip || !m_Colliders.empty()) {
			throw std::runtime_error("Emote String unordered step requires actual zero seconds without adhoc, fixed-tip or collider producers");
		}
		StepImpl(seconds, true);
	}

	void EmoteSoftPendulum::StepImpl(float seconds, bool unordered_comparison) {
		RequireIdle();
		if (!m_Initialized || !std::isfinite(seconds) || (unordered_comparison ? seconds != 0 : (!(seconds > 0) || m_UnorderedStringComparison))) {
			throw std::runtime_error("Emote soft pendulum requires initialized state and positive finite seconds");
		}
		m_Operating = true;
		try {
			m_UnorderedStringComparison = unordered_comparison;
			const double step = seconds;
			AddAcceleration(step);
			if (m_Definition.CollisionHardness == 1) {
				ApplyCollisions(true, step);
			}
			if (m_Definition.AdhocSimulation) {
				ProjectAdhocVelocities(step);
			}
			for (auto& particle : m_Particles) {
				for (std::size_t axis = 0; axis != 3; ++axis) {
					particle.Predicted[axis] = particle.Fixed ? particle.Position[axis] : step * particle.Velocity[axis] + particle.Position[axis];
				}
			}
			if (m_Definition.CollisionHardness == 0) {
				ApplyCollisions(false, step);
			}
			if (m_Definition.AdhocSimulation) {
				SolveAdhocLengths();
			} else {
				SolveLengths(step);
			}
			const auto inverse_step = 1.0 / step;
			for (auto& particle : m_Particles) {
				if (!particle.Fixed) {
					for (std::size_t axis = 0; axis != 3; ++axis) {
						particle.Previous[axis] = particle.Position[axis];
						particle.Velocity[axis] = (particle.Predicted[axis] - particle.Position[axis]) * inverse_step;
						particle.Position[axis] = particle.Predicted[axis];
					}
				}
			}
			for (std::size_t index = 1; index != m_Particles.size(); ++index) {
				auto& particle = m_Particles[index];
				const auto damping = step * particle.Friction;
				for (std::size_t axis = 0; axis != 3; ++axis) {
					particle.Velocity[axis] = particle.Velocity[axis] - damping * particle.Velocity[axis];
					if (!unordered_comparison && (!std::isfinite(particle.Position[axis]) || !std::isfinite(particle.Velocity[axis]))) {
						throw std::runtime_error("Emote soft pendulum update exceeds its finite physical domain");
					}
				}
			}
			m_Operating = false;
		} catch (...) {
			m_Operating = false;
			m_Valid = false;
			throw;
		}
	}

	std::span<const EmoteSoftPendulumParticle> EmoteSoftPendulum::Particles() const noexcept {
		return m_Particles;
	}

	void EmoteSoftPendulum::WritePoints(std::span<std::array<float, 2>> destination) const {
		WritePointsImpl(destination, false);
	}

	void EmoteSoftPendulum::WritePointsImpl(std::span<std::array<float, 2>> destination, bool unordered_comparison) const {
		RequireIdle();
		if (m_UnorderedStringComparison && !unordered_comparison) {
			throw std::runtime_error("Emote String transient points require the private native comparison transaction");
		}
		if (!m_Initialized || destination.size() != m_Particles.size() + m_TrailingPositions.size()) {
			throw std::runtime_error("Emote soft pendulum point output requires its exact initialized physical point count");
		}
		for (std::size_t index = 0; index != m_Particles.size(); ++index) {
			destination[index] = {static_cast<float>(m_Particles[index].Position[0] - m_CoordinateOffset[0]), static_cast<float>(m_Particles[index].Position[1] - m_CoordinateOffset[1])};
		}
		// Native320590 subtracts the posture offset only from physical particles.
		for (std::size_t index = 0; index != m_TrailingPositions.size(); ++index) {
			destination[m_Particles.size() + index] = {static_cast<float>(m_TrailingPositions[index][0]), static_cast<float>(m_TrailingPositions[index][1])};
		}
	}

	void EmoteSoftPendulum::WriteSegmentLengths(std::span<float> destination) const {
		RequireIdle();
		if (destination.size() != m_Particles.size() - 1 + m_TrailingPositions.size()) {
			throw std::runtime_error("Emote soft pendulum segment output requires its exact actual segment count");
		}
		std::fill_n(destination.begin(), m_Particles.size() - 1, static_cast<float>(m_SegmentLength));
		std::fill(destination.begin() + static_cast<std::ptrdiff_t>(m_Particles.size() - 1), destination.end(), static_cast<float>(m_TrailingSpacing));
	}

	std::span<const std::array<double, 3>> EmoteSoftPendulum::TrailingPositions() const noexcept {
		return m_TrailingPositions;
	}

	const EmoteSoftPendulumBounds& EmoteSoftPendulum::Bounds() const {
		RequireIdle();
		if (!m_BoundsReady) {
			throw std::runtime_error("Emote soft pendulum bounds require the actual preceding force-reset phase");
		}
		return m_Bounds;
	}
	void EmoteSoftPendulum::RequireCanAssignState(const EmoteSoftPendulum& source) const {
		RequireIdle();
		source.RequireIdle();
	}

	void EmoteSoftPendulum::AssignState(const EmoteSoftPendulum& source) {
		RequireCanAssignState(source);
		if (this == &source) {
			return;
		}

		m_Anchor = source.m_Anchor;
		m_PostureRotation = source.m_PostureRotation;
		std::copy_n(source.m_Particles.begin(), std::min(m_Particles.size(), source.m_Particles.size()), m_Particles.begin());
		std::copy_n(source.m_TrailingPositions.begin(), std::min(m_TrailingPositions.size(), source.m_TrailingPositions.size()), m_TrailingPositions.begin());
		m_DirectionPrepared = false;
		m_PostureSet = source.m_PostureSet;
		m_Initialized = source.m_Initialized;
	}

} // namespace sgre::scene
