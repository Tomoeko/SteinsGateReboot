#include "sgre/scene/emote_emission_pool.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::scene {
	static constexpr std::size_t kMaximumEmissionParticles = 65536;
	static constexpr std::size_t kMaximumEmissionEvents = 65536;
	static constexpr std::size_t kMaximumEmissionTriggers = 4096;
	static constexpr float kEmissionRadians = 0.01745329238474369049072265625f;
	static constexpr float kEmissionDegrees = 57.2957763671875f;

	static void RequireEmissionPoint(const resource::MotionPoint& point) {
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("Emote emission requires finite actual point inputs");
		}
	}

	static float EmissionTravelAngle(float y, float x) {
		if (x == 0) {
			return y > 0 ? 1.57079637050628662109375f : y < 0 ? -1.57079637050628662109375f
			                                                  : 0.0f;
		}
		if (std::abs(x) <= std::abs(y)) {
			const auto ratio = x / y;
			const auto magnitude = std::abs(ratio);
			const auto approximation = ratio * 0.785398185253143310546875f - ((magnitude - 1.0f) * ratio) * (magnitude * 0.066299997270107269287109375f + 0.24469999969005584716796875f);
			return (y > 0 ? 1.57079637050628662109375f : -1.57079637050628662109375f) - approximation;
		}
		const auto ratio = y / x;
		const auto magnitude = std::abs(ratio);
		auto approximation = ratio * 0.785398185253143310546875f - ((magnitude - 1.0f) * ratio) * (magnitude * 0.066299997270107269287109375f + 0.24469999969005584716796875f);
		if (x < 0) {
			approximation = approximation + (y < 0 ? -3.1415927410125732421875f : 3.1415927410125732421875f);
		}
		return approximation;
	}

	static float EmissionDeathAngle(float y, float x) {
		// Native31c0b0 deliberately uses one inverse-square-root refinement and a different approximation.
		if (x < 0) {
			const auto reflected = EmissionDeathAngle(y, -x);
			return y >= 0 ? 3.1415927410125732421875f - reflected : -3.1415927410125732421875f - reflected;
		}
		const auto squared = y * y + x * x;
		const auto half = squared * 0.5f;
		const auto initial = std::bit_cast<float>(std::uint32_t{0x5f3759df} - (std::bit_cast<std::uint32_t>(squared) >> 1));
		const auto reciprocal = (1.5f - (half * initial) * initial) * initial;
		return (1.57079637050628662109375f - (reciprocal * x) * 0.666666686534881591796875f) * (reciprocal * y);
	}

	static void RequireEmissionBirth(const EmoteEmissionBirth& birth, std::uint32_t emitter_count) {
		if (birth.Emitter >= emitter_count || birth.FollowMesh || birth.Motion.size() > 4096 || birth.Motion.find('\0') != std::string::npos) {
			throw std::runtime_error("Emote emission birth requires its actual bounded emitter; followMesh still requires its genuine particle-IO transform");
		}
		for (const auto* point : {&birth.Coordinate, &birth.Velocity, &birth.Acceleration, &birth.SourceOrigin, &birth.Gravity}) {
			RequireEmissionPoint(*point);
		}
		for (const auto value : {birth.Depth, birth.AngularOffset, birth.Angle, birth.AngularVelocity, birth.ForceFactor, birth.Radius, birth.RadiusVelocity, birth.Lifespan, birth.FadeIn, birth.FadeOut, birth.MotionStep, birth.SourceSize, birth.WaveAmplitude, birth.WaveAmplitudeVelocity, birth.WaveFrequency, birth.WavePhase}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote emission birth requires finite actual scalar inputs");
			}
		}
		if (!(birth.SourceSize > 0) || birth.Radius < 0 || birth.Lifespan < 0) {
			throw std::runtime_error("Emote emission birth requires positive actual source size and nonnegative radius/lifespan");
		}
	}

	EmoteEmissionPool::EmoteEmissionPool(const EmoteEmissionPoolDefinition& definition)
	    : m_Definition(definition) {
		if (definition.MaximumParticles > kMaximumEmissionParticles || definition.MaximumPendingBirths > kMaximumEmissionParticles - definition.MaximumParticles || definition.MaximumPendingBirths == 0 || definition.EmitterCount > 4096 || !(definition.PixelsPerMeter > 0) || !std::isfinite(definition.PixelsPerMeter)) {
			throw std::runtime_error("Emote emission pool requires bounded actual emitters/particles and positive pixels per meter");
		}
		m_ParticleLimit = static_cast<std::size_t>(definition.MaximumParticles) + definition.MaximumPendingBirths;
		const auto capacity = m_ParticleLimit;
		m_Particles.reserve(capacity);
		m_Selected.reserve(capacity);
		m_Events.reserve(capacity);
		m_Triggers.reserve(kMaximumEmissionTriggers);
	}

	void EmoteEmissionPool::RequireIdle() const {
		if (m_Operating || !m_Valid) {
			throw std::runtime_error("Emote emission pool is active or invalid after a failed physical update");
		}
	}

	void EmoteEmissionPool::Spawn(const EmoteEmissionBirth& birth) {
		RequireIdle();
		RequireEmissionBirth(birth, m_Definition.EmitterCount);
		if (m_Particles.size() == m_ParticleLimit || m_NextId == std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error("Emote emission particle birth exceeds its bounded owned storage or identity sequence");
		}
		EmoteEmissionParticle particle;
		particle.Birth = birth;
		particle.Id = m_NextId;
		particle.InitialPosition = birth.Coordinate;
		particle.Previous = birth.Coordinate;
		particle.Position = birth.Coordinate;
		particle.LocalPosition = birth.Coordinate;
		particle.ActualPosition = birth.Coordinate;
		particle.Velocity = birth.Velocity;
		particle.Acceleration = birth.Acceleration;
		particle.Radius = birth.Radius;
		particle.Angle = birth.Angle;
		particle.WaveAmplitude = birth.WaveAmplitude;
		particle.WavePhase = birth.WavePhase;
		particle.SourceOriginScale = (m_Definition.PixelsPerMeter + m_Definition.PixelsPerMeter) / birth.SourceSize;
		particle.NeedsTravelDirection = birth.FaceTravelDirection || std::ranges::any_of(m_Triggers, [&birth](const auto& trigger) {
			return trigger.SourceEmitter == birth.Emitter;
		});
		particle.TravelAngle = EmissionTravelAngle(birth.Velocity.Y, birth.Velocity.X) * kEmissionDegrees;
		if (birth.FaceTravelDirection) {
			particle.Angle = particle.TravelAngle + birth.AngularOffset;
		}
		const auto radians = particle.Angle * kEmissionRadians;
		const auto cosine = std::cos(radians);
		const auto sine = std::sin(radians);
		const auto scale = particle.SourceOriginScale * birth.Radius;
		particle.ActualPosition.X = birth.Coordinate.X + (birth.SourceOrigin.X * cosine - birth.SourceOrigin.Y * sine) * scale;
		particle.ActualPosition.Y = birth.Coordinate.Y + (birth.SourceOrigin.X * sine + birth.SourceOrigin.Y * cosine) * scale;
		const auto velocity_length = std::sqrt(birth.Velocity.Y * birth.Velocity.Y + birth.Velocity.X * birth.Velocity.X);
		if (velocity_length != 0 && birth.WaveAmplitude != 0) {
			const auto factor = birth.WaveAmplitude / velocity_length;
			particle.WaveVector = {factor * birth.Velocity.Y, -birth.Velocity.X * factor};
			const auto wave = std::sin(birth.WavePhase * kEmissionRadians);
			particle.InitialPosition.X = particle.InitialPosition.X + wave * particle.WaveVector.X;
			particle.InitialPosition.Y = particle.InitialPosition.Y + wave * particle.WaveVector.Y;
			particle.LocalPosition = particle.InitialPosition;
			particle.Dynamic = true;
		} else {
			particle.WaveVector = {birth.WaveAmplitude, 0};
		}
		particle.Dynamic = particle.Dynamic || birth.Gravity.X != 0 || birth.Gravity.Y != 0;
		for (const auto* point : {&particle.InitialPosition, &particle.Position, &particle.LocalPosition, &particle.ActualPosition, &particle.WaveVector}) {
			RequireEmissionPoint(*point);
		}
		if (!std::isfinite(particle.SourceOriginScale) || !std::isfinite(particle.Angle) || !std::isfinite(particle.TravelAngle)) {
			throw std::runtime_error("Emote emission source-origin scale exceeds its finite native domain");
		}
		m_Particles.push_back(std::move(particle));
		m_MaximumRadius = std::max(birth.Radius, m_MaximumRadius);
		++m_NextId;
		m_Prepared = false;
	}

	void EmoteEmissionPool::AddPeriodicTrigger(std::uint32_t source_emitter, std::uint32_t target_emitter, float frequency) {
		RequireIdle();
		if (!m_Particles.empty() || source_emitter >= m_Definition.EmitterCount || target_emitter >= m_Definition.EmitterCount || m_Triggers.size() == kMaximumEmissionTriggers || !std::isfinite(frequency) || frequency < 0) {
			throw std::runtime_error("Emote emission periodic trigger requires actual bounded emitters before particle birth");
		}
		if ((m_Triggers.size() + 2) > kMaximumEmissionEvents / m_ParticleLimit) {
			throw std::runtime_error("Emote emission periodic trigger exceeds bounded event storage");
		}
		m_Events.reserve(m_ParticleLimit * (m_Triggers.size() + 2));
		m_Triggers.push_back({source_emitter, target_emitter, frequency});
	}

	void EmoteEmissionPool::Integrate(float seconds) {
		for (auto& particle : m_Particles) {
			const auto& birth = particle.Birth;
			particle.Previous = particle.Position;
			if (particle.Dynamic) {
				particle.LocalPosition.X = particle.Velocity.X * seconds + particle.LocalPosition.X;
				particle.LocalPosition.Y = particle.Velocity.Y * seconds + particle.LocalPosition.Y;
				particle.Velocity.X = birth.Gravity.X * seconds + particle.Velocity.X;
				particle.Velocity.Y = birth.Gravity.Y * seconds + particle.Velocity.Y;
				if (particle.WaveAmplitude != 0) {
					const auto previous_amplitude = particle.WaveAmplitude;
					particle.WaveAmplitude = seconds * birth.WaveAmplitudeVelocity + previous_amplitude;
					const auto ratio = particle.WaveAmplitude / previous_amplitude;
					particle.WaveVector.X = ratio * particle.WaveVector.X;
					particle.WaveVector.Y = ratio * particle.WaveVector.Y;
					const auto previous_phase = particle.WavePhase;
					particle.WavePhase = (birth.WaveFrequency * 360.0f) * seconds + previous_phase;
					const auto wave = std::sin(particle.WavePhase * kEmissionRadians) - std::sin(previous_phase * kEmissionRadians);
					particle.LocalPosition.X = wave * particle.WaveVector.X + particle.LocalPosition.X;
					particle.LocalPosition.Y = wave * particle.WaveVector.Y + particle.LocalPosition.Y;
				}
			} else {
				const auto half_squared = (particle.Age * particle.Age) * 0.5f;
				particle.LocalPosition.Y = (particle.Age * birth.Velocity.Y + birth.Coordinate.Y) + half_squared * birth.Acceleration.Y;
				particle.LocalPosition.X = (particle.Age * birth.Velocity.X + birth.Coordinate.X) + half_squared * birth.Acceleration.X;
			}
			particle.Velocity.X = seconds * particle.Acceleration.X + particle.Velocity.X;
			particle.Velocity.Y = seconds * particle.Acceleration.Y + particle.Velocity.Y;
			particle.Radius = seconds * birth.RadiusVelocity + particle.Radius;
			particle.Position = particle.LocalPosition;
			particle.New = false;
			const auto difference_x = particle.Position.X - particle.Previous.X;
			const auto difference_y = particle.Position.Y - particle.Previous.Y;
			if (particle.NeedsTravelDirection && (difference_x != 0 || difference_y != 0)) {
				particle.TravelAngle = EmissionTravelAngle(difference_y, difference_x) * kEmissionDegrees;
			}
			particle.Previous = particle.Position;
			particle.Angle = birth.FaceTravelDirection ? particle.TravelAngle + birth.AngularOffset : seconds * birth.AngularVelocity + particle.Angle;
			if (!birth.IgnoreCollision) {
				particle.ActualPosition = particle.Position;
				const auto scale = particle.SourceOriginScale * particle.Radius;
				const auto offset_x = scale * birth.SourceOrigin.X;
				const auto offset_y = scale * birth.SourceOrigin.Y;
				if (offset_x != 0 || offset_y != 0) {
					const auto radians = particle.Angle * kEmissionRadians;
					const auto cosine = std::cos(radians);
					const auto sine = std::sin(radians);
					particle.ActualPosition.X = particle.ActualPosition.X - (cosine * offset_x - sine * offset_y);
					particle.ActualPosition.Y = particle.ActualPosition.Y - (cosine * offset_y + sine * offset_x);
				}
			}
		}
	}

	void EmoteEmissionPool::RetireExpired(float seconds) {
		for (const auto& particle : m_Particles) {
			const auto age = particle.Age + seconds;
			for (const auto& trigger : m_Triggers) {
				if (trigger.SourceEmitter != particle.Birth.Emitter) {
					continue;
				}
				const auto old_period = trigger.Frequency * particle.Age;
				const auto new_period = trigger.Frequency * age;
				if (!std::isfinite(new_period) || new_period >= 2147483648.0f || old_period >= 2147483648.0f) {
					throw std::runtime_error("Emote emission periodic trigger exceeds the native integer conversion domain");
				}
				if (static_cast<std::int32_t>(old_period) != static_cast<std::int32_t>(new_period)) {
					m_Events.push_back({particle.Birth.Emitter, trigger.TargetEmitter, EmoteEmissionEventKind::Periodic, particle.LocalPosition, particle.Position, particle.TravelAngle * kEmissionRadians});
				}
			}
			if (particle.Birth.Lifespan <= age) {
				m_Events.push_back({particle.Birth.Emitter, 0xffffffff, EmoteEmissionEventKind::Death, particle.LocalPosition, particle.Position, EmissionDeathAngle(particle.Velocity.Y, particle.Velocity.X)});
			}
			if (m_Events.size() > kMaximumEmissionEvents) {
				throw std::runtime_error("Emote emission events exceed bounded actual pool storage");
			}
		}
		for (auto& particle : m_Particles) {
			particle.Age = particle.Age + seconds;
		}
		std::erase_if(m_Particles, [](const auto& particle) {
			return particle.Birth.Lifespan <= particle.Age;
		});
		if (m_Particles.size() > m_Definition.MaximumParticles) {
			m_Particles.erase(m_Particles.begin(), m_Particles.end() - static_cast<std::ptrdiff_t>(m_Definition.MaximumParticles));
		}
	}

	void EmoteEmissionPool::Update(float ticks) {
		RequireIdle();
		const auto seconds = ticks / 60.0f;
		if (ticks < 0 || !std::isfinite(seconds) || (m_Triggers.size() + 1) > kMaximumEmissionEvents / std::max(std::size_t{1}, m_Particles.size())) {
			throw std::runtime_error("Emote emission update requires finite nonnegative actual ticks and bounded event work");
		}
		m_Operating = true;
		m_Prepared = false;
		m_Events.clear();
		try {
			Integrate(seconds);
			RetireExpired(seconds);
			for (const auto& particle : m_Particles) {
				for (const auto value : {particle.Age, particle.Radius, particle.Angle, particle.WaveAmplitude, particle.WavePhase, particle.TravelAngle}) {
					if (!std::isfinite(value)) {
						throw std::runtime_error("Emote emission physical update exceeds its finite scalar domain");
					}
				}
				for (const auto* point : {&particle.Position, &particle.LocalPosition, &particle.ActualPosition, &particle.Velocity, &particle.WaveVector}) {
					RequireEmissionPoint(*point);
				}
			}
			m_Operating = false;
		} catch (...) {
			m_Operating = false;
			m_Valid = false;
			throw;
		}
	}

	void EmoteEmissionPool::PrepareBounds(float left, float top, float right, float bottom, float extra_radius) {
		RequireIdle();
		for (const auto value : {left, top, right, bottom, extra_radius}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote emission selection requires finite actual physical bounds");
			}
		}
		if (right < left || bottom < top || extra_radius < 0) {
			throw std::runtime_error("Emote emission selection requires ordered actual physical bounds");
		}
		const auto radius = extra_radius + m_MaximumRadius;
		for (const auto value : {left - radius, top - radius, right + radius, bottom + radius}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote emission expanded physical bounds exceed their finite domain");
			}
		}
		m_Selected.clear();
		for (std::size_t index = 0; index != m_Particles.size(); ++index) {
			const auto& particle = m_Particles[index];
			const auto& position = particle.ActualPosition;
			if (particle.Birth.ForceFactor != 0 && (!particle.Birth.FollowMesh || !particle.New) && position.X >= left - radius && position.Y >= top - radius && position.X <= right + radius && position.Y <= bottom + radius) {
				m_Selected.push_back(index);
			}
		}
		m_Prepared = true;
	}

	void EmoteEmissionPool::RequirePrepared() const {
		RequireIdle();
		if (!m_Prepared) {
			throw std::runtime_error("Emote emission force sampling requires current actual bounds selection");
		}
	}

	std::array<float, 3> EmoteEmissionPool::SamplePoint(const resource::MotionPoint& point, float radius) const {
		RequirePrepared();
		RequireEmissionPoint(point);
		if (!std::isfinite(radius) || radius < 0) {
			throw std::runtime_error("Emote emission point sampling requires a finite nonnegative actual radius");
		}
		std::array<float, 3> force{};
		for (const auto index : m_Selected) {
			const auto& particle = m_Particles[index];
			const auto x = point.X - particle.ActualPosition.X;
			const auto y = point.Y - particle.ActualPosition.Y;
			const auto sum = radius + particle.Radius;
			if (!(sum * sum < y * y + x * x)) {
				force[0] = particle.Birth.ForceFactor * particle.Velocity.X + force[0];
				force[1] = particle.Birth.ForceFactor * particle.Velocity.Y + force[1];
			}
		}
		return force;
	}

	std::array<float, 3> EmoteEmissionPool::SampleEllipse(const EmoteCollisionEllipse& ellipse) const {
		RequirePrepared();
		RequireEmissionPoint(ellipse.Center);
		RequireEmissionPoint(ellipse.Radius);
		if (ellipse.Radius.X < 0 || ellipse.Radius.Y < 0 || !std::isfinite(ellipse.Rotation)) {
			throw std::runtime_error("Emote emission ellipse sampling requires finite nonnegative actual radii and rotation");
		}
		const auto cosine = std::cos(-ellipse.Rotation);
		const auto sine = std::sin(-ellipse.Rotation);
		const auto negative_sine = -sine;
		const auto x_axis = negative_sine * 0.0f + cosine;
		const auto xy = sine * 0.0f + negative_sine;
		const auto y_axis = sine * 0.0f + cosine;
		const auto yx = cosine * 0.0f + sine;
		const auto tx = ((negative_sine * -ellipse.Center.Y) + -ellipse.Center.X * cosine) + 0.0f;
		const auto ty = ((-ellipse.Center.Y * cosine) + -ellipse.Center.X * sine) + 0.0f;
		std::array<float, 3> force{};
		for (const auto index : m_Selected) {
			const auto& particle = m_Particles[index];
			const auto& position = particle.ActualPosition;
			const auto radius_x = particle.Radius + ellipse.Radius.X;
			const auto radius_y = particle.Radius + ellipse.Radius.Y;
			if (radius_x == 0 || radius_y == 0 || !std::isfinite(radius_x) || !std::isfinite(radius_y)) {
				throw std::runtime_error("Emote emission ellipse overlap requires finite nonzero actual expanded axes");
			}
			const auto x = ((position.Y * xy + position.X * x_axis) + tx) / radius_x;
			const auto y = ((position.Y * y_axis + position.X * yx) + ty) / radius_y;
			if (!(1.0f < y * y + x * x)) {
				force[0] = particle.Birth.ForceFactor * particle.Velocity.X + force[0];
				force[1] = particle.Birth.ForceFactor * particle.Velocity.Y + force[1];
			}
		}
		return force;
	}

	std::span<const EmoteEmissionParticle> EmoteEmissionPool::Particles() const noexcept {
		return m_Particles;
	}

	std::span<const EmoteEmissionEvent> EmoteEmissionPool::Events() const noexcept {
		return m_Events;
	}
	void EmoteEmissionPool::RequireCanAssignState(const EmoteEmissionPool& source) const {
		RequireIdle();
		source.RequireIdle();
		if (source.m_Particles.size() > m_ParticleLimit) {
			throw std::runtime_error("Emote emission assignment exceeds the destination owned particle capacity");
		}
		for (const auto& particle : source.m_Particles) {
			if (particle.Birth.Emitter >= m_Definition.EmitterCount || particle.Id == std::numeric_limits<std::uint32_t>::max()) {
				throw std::runtime_error("Emote emission assignment requires destination emitter indices and a bounded next particle ID");
			}
		}
	}

	void EmoteEmissionPool::AssignState(const EmoteEmissionPool& source) {
		RequireCanAssignState(source);
		if (this == &source) {
			return;
		}

		auto particles = source.m_Particles;
		std::uint32_t next_id = 0;
		for (const auto& particle : particles) {
			next_id = std::max(next_id, particle.Id + 1);
		}
		m_Particles = std::move(particles);
		m_NextId = next_id;
		// Selection stores indices into the old pool; the actual next force query must select its new bounds.
		m_Selected.clear();
		m_Prepared = false;
	}

} // namespace sgre::scene
