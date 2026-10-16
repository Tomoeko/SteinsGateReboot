#include "sgre/scene/emote_emission_emitter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {
	static constexpr std::size_t kMaximumEmissionBatch = 4096;
	static constexpr float kEmitterRadians = 0.01745329238474369049072265625f;
	static constexpr float kEmitterDegrees = 57.2957763671875f;

	static void RequireEmitterRange(const EmoteEmissionRange& range) {
		for (const auto value : {range.Value, range.Variation, range.Minimum, range.Maximum}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote emitter range requires finite actual inputs");
			}
		}
		if (range.Minimum > range.Maximum || range.Variation < 0 || !std::isfinite(range.Value + range.Variation) || !std::isfinite(range.Value - range.Variation)) {
			throw std::runtime_error("Emote emitter range exceeds its bounded native sampling domain");
		}
	}

	static void RequireEmitterDefinition(const EmoteEmissionEmitterDefinition& definition) {
		const auto& particle = definition.Particle;
		for (const auto* range : {&definition.Angle, &definition.Frequency, &definition.GenerationCount, &particle.Scale, &particle.TerminalScaleRatio, &particle.Density, &particle.Velocity, &particle.TerminalVelocityRatio, &particle.Lifespan, &particle.FadeIn, &particle.FadeOut, &particle.AngularVelocity, &particle.AngularOffset, &particle.WaveAmplitude, &particle.TerminalWaveAmplitudeRatio, &particle.WaveFrequency, &particle.WavePhase}) {
			RequireEmitterRange(*range);
		}
		for (const auto value : {definition.First.X, definition.First.Y, definition.Second.X, definition.Second.Y, definition.DepthOffset, particle.PixelsPerMeter, particle.SourceSize, particle.MotionInterval, particle.SourceOrigin.X, particle.SourceOrigin.Y, particle.GravityStrength, particle.GravityDirection}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote emitter definition requires finite actual geometry/source inputs");
			}
		}
		if ((definition.Geometry != EmoteEmissionGeometry::Point && definition.Geometry != EmoteEmissionGeometry::Rectangle) || !(particle.PixelsPerMeter > 0) || !(particle.SourceSize > 0) || !(definition.Angle.Maximum > 0) || (definition.Frequency.Variation == 0 ? !(definition.Frequency.Value > 0) : !(std::max(definition.Frequency.Value - definition.Frequency.Variation, definition.Frequency.Minimum) > 0) || !(std::min(definition.Frequency.Value + definition.Frequency.Variation, definition.Frequency.Maximum) > 0))) {
			throw std::runtime_error("Emote emitter requires recovered point/rectangle geometry and positive actual source/frequency domains");
		}
	}

	EmoteEmissionEmitter::EmoteEmissionEmitter(EmoteEmissionPool& pool, EmoteEmissionRandomState& actual_random, const EmoteEmissionEmitterDefinition& definition)
	    : m_Pool(pool), m_Random(actual_random), m_Definition(definition) {
		RequireEmitterDefinition(definition);
		if (definition.Particle.Motions.size() > 4096) {
			throw std::runtime_error("Emote emitter weighted Motion variants exceed actual bounded owner storage");
		}
		std::size_t count = 0;
		for (const auto& variant : definition.Particle.Motions) {
			if (variant.Motion.empty() || variant.Motion.size() > 4096 || variant.Motion.find('\0') != std::string::npos || variant.SourceSize == 0 || variant.Weight > 65536 - count) {
				throw std::runtime_error("Emote emitter requires bounded actual weighted Motion names/source dimensions");
			}
			count += variant.Weight;
		}
		if (!definition.Particle.Motions.empty() && count == 0) {
			throw std::runtime_error("Emote emitter actual Motion selection has no positive weight");
		}
		m_WeightedMotions.reserve(count);
		for (std::size_t index = 0; index != definition.Particle.Motions.size(); ++index) {
			m_WeightedMotions.insert(m_WeightedMotions.end(), definition.Particle.Motions[index].Weight, static_cast<std::uint32_t>(index));
		}
	}

	std::uint32_t EmoteEmissionEmitter::Next() {
		const auto old = m_Random.State;
		m_Random.State = old * std::uint64_t{0x5851f42d4c957f2d} + (m_Random.Increment | 1);
		const auto value = static_cast<std::uint32_t>((old >> 45) ^ (old >> 27));
		const auto output = std::rotr(value, static_cast<int>(old >> 59));
		return output;
	}

	float EmoteEmissionEmitter::Unit() {
		return static_cast<float>(Next() >> 8) * 5.9604644775390625e-8f;
	}

	std::uint32_t EmoteEmissionEmitter::Bounded(std::uint32_t bound) {
		const auto threshold = (std::uint32_t{0} - bound) % bound;
		for (std::size_t attempt = 0; attempt != kMaximumEmissionBatch; ++attempt) {
			const auto product = std::uint64_t{Next()} * bound;
			if (static_cast<std::uint32_t>(product) >= threshold) {
				return static_cast<std::uint32_t>(product >> 32);
			}
		}
		throw std::runtime_error("Emote emitter bounded integer selection exceeded native RNG rejection work");
	}

	float EmoteEmissionEmitter::Sample(const EmoteEmissionRange& range) {
		if (range.Variation == 0) {
			return range.Value;
		}
		const auto upper = std::min(range.Value + range.Variation, range.Maximum);
		const auto lower = std::max(range.Value - range.Variation, range.Minimum);
		return Unit() * (upper - lower) + lower;
	}

	float EmoteEmissionEmitter::SampleAngle() {
		const auto& range = m_Definition.Angle;
		const auto lower = range.Value - range.Variation;
		auto angle = (Unit() * ((range.Value + range.Variation) - lower)) + lower;
		// Native31ca90 always draws once, even for zero angular variation.
		std::size_t wraps = 0;
		while (angle < 0) {
			if (++wraps > kMaximumEmissionBatch) {
				throw std::runtime_error("Emote emitter angular wrap exceeds bounded native work");
			}
			angle = angle + range.Maximum;
		}
		while (angle >= range.Maximum) {
			if (++wraps > kMaximumEmissionBatch) {
				throw std::runtime_error("Emote emitter angular wrap exceeds bounded native work");
			}
			angle = angle - range.Maximum;
		}
		return angle;
	}

	void EmoteEmissionEmitter::SetPosture(const EmoteEmissionEmitterPosture& posture) {
		if (m_Operating || !m_Valid) {
			throw std::runtime_error("Emote emitter is active or invalid after failed generation");
		}
		for (const auto value : {posture.Coordinate.X, posture.Coordinate.Y, posture.Depth, posture.Rotation, posture.Cosine, posture.Sine}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote emitter posture requires finite actual source transforms");
			}
		}
		if (!std::isfinite(posture.Depth + m_Definition.DepthOffset)) {
			throw std::runtime_error("Emote emitter source depth exceeds its finite native domain");
		}
		m_Posture = posture;
		m_Posture.Depth = posture.Depth + m_Definition.DepthOffset;
		m_PostureSet = true;
	}

	void EmoteEmissionEmitter::Generate() {
		const auto& definition = m_Definition.Particle;
		auto point = m_Definition.First;
		if (m_Definition.Geometry == EmoteEmissionGeometry::Rectangle) {
			// Native31d0a0 samples Y first, then X from the same actual shared stream.
			point.Y = Unit() * (m_Definition.Second.Y - point.Y) + point.Y;
			point.X = Unit() * (m_Definition.Second.X - point.X) + point.X;
		}
		EmoteEmissionBirth birth;
		birth.Emitter = m_Definition.Emitter;
		birth.IgnoreCollision = m_Definition.IgnoreCollision;
		birth.Depth = m_Posture.Depth;
		birth.Coordinate.X = ((point.X * m_Posture.Cosine - point.Y * m_Posture.Sine) + 0.0f) + m_Posture.Coordinate.X;
		birth.Coordinate.Y = ((point.Y * m_Posture.Cosine + point.X * m_Posture.Sine) + 0.0f) + m_Posture.Coordinate.Y;
		const auto angle = SampleAngle() * kEmitterRadians;
		const auto cosine = std::cos(angle);
		const auto sine = std::sin(angle);
		const auto direction_y = m_Posture.Sine * cosine + m_Posture.Cosine * sine;
		const auto direction_x = m_Posture.Cosine * cosine - m_Posture.Sine * sine;
		const auto world_angle = angle + m_Posture.Rotation;
		birth.ForceFactor = Sample(definition.Density);
		birth.Lifespan = Sample(definition.Lifespan);
		const auto speed = Sample(definition.Velocity);
		const auto terminal_velocity = Sample(definition.TerminalVelocityRatio);
		if (!(birth.Lifespan > 0)) {
			throw std::runtime_error("Emote emitter requires a positive actual lifespan before velocity/scale rates");
		}
		const auto acceleration = (terminal_velocity * speed - speed) / birth.Lifespan;
		const auto x = (direction_x - direction_y * 0.0f) + 0.0f;
		const auto y = (direction_x * 0.0f + direction_y) + 0.0f;
		birth.Velocity = {x * speed, y * speed};
		birth.Acceleration = {x * acceleration, y * acceleration};
		birth.FadeIn = Sample(definition.FadeIn);
		birth.FadeOut = Sample(definition.FadeOut);
		birth.SourceSize = definition.SourceSize;
		if (!m_WeightedMotions.empty()) {
			const auto index = m_WeightedMotions[Bounded(static_cast<std::uint32_t>(m_WeightedMotions.size()))];
			const auto& variant = definition.Motions[index];
			birth.Motion = variant.Motion;
			birth.MotionFrames = variant.Frames;
			birth.SourceSize = static_cast<float>(variant.SourceSize);
			birth.MotionStep = definition.MotionCycles == 0 ? definition.MotionInterval : birth.Lifespan / static_cast<float>(definition.MotionCycles);
		}
		const auto source_radius = (birth.SourceSize * 0.5f) / definition.PixelsPerMeter;
		birth.Radius = Sample(definition.Scale) * source_radius;
		const auto terminal_radius = Sample(definition.TerminalScaleRatio) * birth.Radius;
		birth.RadiusVelocity = (terminal_radius - birth.Radius) / birth.Lifespan;
		birth.AngularOffset = Sample(definition.AngularOffset);
		birth.AngularVelocity = Sample(definition.AngularVelocity);
		birth.Angle = ((definition.StartWithEmissionAngle || definition.FaceTravelDirection) ? world_angle * kEmitterDegrees : 0.0f) + birth.AngularOffset;
		birth.FaceTravelDirection = definition.FaceTravelDirection;
		birth.WaveAmplitude = Sample(definition.WaveAmplitude);
		if (birth.WaveAmplitude != 0) {
			const auto terminal_amplitude = Sample(definition.TerminalWaveAmplitudeRatio);
			birth.WaveAmplitudeVelocity = (terminal_amplitude * birth.WaveAmplitude - birth.WaveAmplitude) / birth.Lifespan;
			birth.WaveFrequency = Sample(definition.WaveFrequency);
			birth.WavePhase = Sample(definition.WavePhase);
		}
		if (definition.GravityStrength != 0) {
			const auto gravity_angle = definition.GravityDirection * kEmitterRadians;
			birth.Gravity.X = std::cos(gravity_angle) * definition.GravityStrength;
			birth.Gravity.Y = std::sin(gravity_angle) * definition.GravityStrength;
		}
		birth.SourceOrigin = definition.SourceOrigin;
		m_Pool.Spawn(birth);
	}

	void EmoteEmissionEmitter::Step(float ticks) {
		if (m_Operating || !m_Valid || !m_PostureSet || !std::isfinite(ticks) || ticks < 0) {
			throw std::runtime_error("Emote emitter step requires its current actual source posture and finite nonnegative ticks");
		}
		m_Operating = true;
		m_Emitted = false;
		try {
			m_Countdown = m_Countdown - ticks / 60.0f;
			std::size_t count = 0;
			while (m_Countdown < 0) {
				if (++count > kMaximumEmissionBatch) {
					throw std::runtime_error("Emote emitter cadence exceeds bounded actual generation work");
				}
				const auto frequency = Sample(m_Definition.Frequency);
				if (!(frequency > 0)) {
					throw std::runtime_error("Emote emitter sampled an unsupported nonpositive automatic frequency");
				}
				const auto period = 1.0f / frequency;
				if (!std::isfinite(period)) {
					throw std::runtime_error("Emote emitter period exceeds its finite actual domain");
				}
				m_Countdown = period + m_Countdown;
			}
			if (count != 0) {
				m_Emitted = true;
				const auto generation = std::max(std::floor(Sample(m_Definition.GenerationCount)), 1.0f);
				const auto total = static_cast<float>(count) * generation;
				if (!std::isfinite(total) || total > static_cast<float>(kMaximumEmissionBatch)) {
					throw std::runtime_error("Emote emitter sampled generation exceeds bounded actual birth work");
				}
				const auto births = static_cast<std::uint32_t>(total);
				for (std::uint32_t index = 0; index != births; ++index) {
					Generate();
				}
			}
			m_Operating = false;
		} catch (...) {
			m_Operating = false;
			m_Valid = false;
			throw;
		}
	}

	float EmoteEmissionEmitter::Countdown() const noexcept {
		return m_Countdown;
	}

	bool EmoteEmissionEmitter::Emitted() const noexcept {
		return m_Emitted;
	}
	void EmoteEmissionEmitter::RequireCanAssignState(const EmoteEmissionEmitter& source) const {
		if (m_Operating || source.m_Operating || !m_Valid || !source.m_Valid || m_Definition.Geometry != source.m_Definition.Geometry) {
			throw std::runtime_error("Emote emitter assignment requires idle valid matching native geometry owners");
		}
	}

	void EmoteEmissionEmitter::AssignState(const EmoteEmissionEmitter& source) {
		RequireCanAssignState(source);
		m_Posture = source.m_Posture;
		m_Countdown = source.m_Countdown;
		m_Emitted = source.m_Emitted;
		m_PostureSet = source.m_PostureSet;
		if (m_Definition.Geometry == EmoteEmissionGeometry::Point) {
			// Point31deb0 imports its coordinates; Rectangle31de70 deliberately retains both destination corners.
			m_Definition.First = source.m_Definition.First;
		}
	}

} // namespace sgre::scene
