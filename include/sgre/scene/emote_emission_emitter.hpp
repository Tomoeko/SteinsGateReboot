#pragma once

#include "sgre/scene/emote_emission_pool.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sgre::scene {
	// Borrowed native MEmote+190 stream, separate from eye-setup randomness.
	struct EmoteEmissionRandomState {
		std::uint64_t State = 0;
		std::uint64_t Increment = 1;
	};

	struct EmoteEmissionRange {
		float Value = 0;
		float Variation = 0;
		float Minimum = 0;
		float Maximum = 1;
	};

	struct EmoteEmissionMotionVariant {
		std::string Motion;
		std::uint32_t SourceSize = 1;
		std::uint32_t Frames = 1;
		std::uint32_t Weight = 1;
	};

	struct EmoteEmissionParticleDefinition {
		float PixelsPerMeter = 1;
		EmoteEmissionRange Scale{1, 0, 0, 1};
		EmoteEmissionRange TerminalScaleRatio{1, 0, 0, 1};
		EmoteEmissionRange Density;
		EmoteEmissionRange Velocity;
		EmoteEmissionRange TerminalVelocityRatio{1, 0, 0, 1};
		EmoteEmissionRange Lifespan{1, 0, 0, 1};
		EmoteEmissionRange FadeIn;
		EmoteEmissionRange FadeOut;
		float SourceSize = 1;
		float MotionInterval = 0;
		std::uint32_t MotionCycles = 0;
		std::vector<EmoteEmissionMotionVariant> Motions;
		resource::MotionPoint SourceOrigin;
		bool StartWithEmissionAngle = false;
		bool FaceTravelDirection = false;
		EmoteEmissionRange AngularVelocity;
		EmoteEmissionRange AngularOffset;
		EmoteEmissionRange WaveAmplitude;
		EmoteEmissionRange TerminalWaveAmplitudeRatio{1, 0, 0, 1};
		EmoteEmissionRange WaveFrequency;
		EmoteEmissionRange WavePhase;
		float GravityStrength = 0;
		float GravityDirection = 0;
	};

	enum class EmoteEmissionGeometry : std::uint8_t {
		Point,
		Rectangle
	};

	struct EmoteEmissionEmitterDefinition {
		std::uint32_t Emitter = 0;
		EmoteEmissionGeometry Geometry = EmoteEmissionGeometry::Point;
		resource::MotionPoint First;
		resource::MotionPoint Second;
		float DepthOffset = 0;
		bool IgnoreCollision = false;
		EmoteEmissionRange Angle{0, 0, 0, 360};
		EmoteEmissionRange Frequency{1, 0, 1, 1};
		EmoteEmissionRange GenerationCount{1, 0, 1, 1};
		EmoteEmissionParticleDefinition Particle;
	};

	struct EmoteEmissionEmitterPosture {
		resource::MotionPoint Coordinate;
		float Depth = 0;
		float Rotation = 0;
		float Cosine = 1;
		float Sine = 0;
	};

	// Actual automatic point/rectangle producer. Pool and the containing shared RNG outlive it.
	class EmoteEmissionEmitter {
	public:
		EmoteEmissionEmitter(EmoteEmissionPool& pool, EmoteEmissionRandomState& actual_random, const EmoteEmissionEmitterDefinition& definition);
		EmoteEmissionEmitter(const EmoteEmissionEmitter&) = delete;
		EmoteEmissionEmitter& operator=(const EmoteEmissionEmitter&) = delete;
		// Native assignment copies mutable values while retaining this owner's resources and bindings.
		void RequireCanAssignState(const EmoteEmissionEmitter& source) const;
		void AssignState(const EmoteEmissionEmitter& source);
		void SetPosture(const EmoteEmissionEmitterPosture& posture);
		void Step(float ticks);
		[[nodiscard]] float Countdown() const noexcept;
		[[nodiscard]] bool Emitted() const noexcept;

	private:
		[[nodiscard]] std::uint32_t Next();
		[[nodiscard]] std::uint32_t Bounded(std::uint32_t bound);
		[[nodiscard]] float Unit();
		[[nodiscard]] float Sample(const EmoteEmissionRange& range);
		[[nodiscard]] float SampleAngle();
		void Generate();
		EmoteEmissionPool& m_Pool;
		EmoteEmissionRandomState& m_Random;
		EmoteEmissionEmitterDefinition m_Definition;
		EmoteEmissionEmitterPosture m_Posture;
		std::vector<std::uint32_t> m_WeightedMotions;
		float m_Countdown = 0;
		bool m_PostureSet = false;
		bool m_Emitted = false;
		bool m_Operating = false;
		bool m_Valid = true;
	};
} // namespace sgre::scene
