#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/emote_ellipse_collision.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sgre::scene {
	struct EmoteEmissionPoolDefinition {
		std::uint32_t MaximumParticles = 4096;
		std::uint32_t EmitterCount = 1;
		// Safety capacity for births before native post-update maxParticles pruning.
		std::uint32_t MaximumPendingBirths = 256;
		float PixelsPerMeter = 1;
	};

	// Actual31a3b0 inputs after an emitter has sampled its authored particle ranges.
	struct EmoteEmissionBirth {
		std::uint32_t Emitter = 0;
		bool FollowMesh = false;
		bool IgnoreCollision = false;
		float Depth = 0;
		resource::MotionPoint Coordinate;
		resource::MotionPoint Velocity;
		resource::MotionPoint Acceleration;
		resource::MotionPoint SourceOrigin;
		bool FaceTravelDirection = false;
		float AngularOffset = 0;
		float Angle = 0;
		float AngularVelocity = 0;
		float ForceFactor = 0;
		float Radius = 1;
		float RadiusVelocity = 0;
		float Lifespan = 1;
		float FadeIn = 0;
		float FadeOut = 0;
		std::string Motion;
		std::uint32_t MotionFrames = 0;
		float MotionStep = 0;
		float SourceSize = 1;
		float WaveAmplitude = 0;
		float WaveAmplitudeVelocity = 0;
		float WaveFrequency = 0;
		float WavePhase = 0;
		resource::MotionPoint Gravity;
	};

	struct EmoteEmissionParticle {
		std::uint32_t Id = 0;
		EmoteEmissionBirth Birth;
		bool New = true;
		bool Dynamic = false;
		bool NeedsTravelDirection = false;
		resource::MotionPoint InitialPosition;
		resource::MotionPoint Previous;
		resource::MotionPoint Position;
		resource::MotionPoint LocalPosition;
		resource::MotionPoint ActualPosition;
		resource::MotionPoint Velocity;
		resource::MotionPoint Acceleration;
		resource::MotionPoint WaveVector;
		float Age = 0;
		float Radius = 0;
		float Angle = 0;
		float WaveAmplitude = 0;
		float WavePhase = 0;
		float TravelAngle = 0;
		float SourceOriginScale = 0;
	};

	enum class EmoteEmissionEventKind : std::uint32_t {
		Collision = 0,
		Death = 1,
		Periodic = 2
	};

	struct EmoteEmissionEvent {
		std::uint32_t SourceEmitter = 0;
		std::uint32_t TargetEmitter = 0xffffffff;
		EmoteEmissionEventKind Kind = EmoteEmissionEventKind::Death;
		resource::MotionPoint LocalPosition;
		resource::MotionPoint Position;
		float Angle = 0;
	};

	class EmoteEmissionPool {
	public:
		explicit EmoteEmissionPool(const EmoteEmissionPoolDefinition& definition);
		EmoteEmissionPool(const EmoteEmissionPool&) = delete;
		EmoteEmissionPool& operator=(const EmoteEmissionPool&) = delete;
		// Native assignment copies mutable values while retaining this owner's resources and bindings.
		void RequireCanAssignState(const EmoteEmissionPool& source) const;
		void AssignState(const EmoteEmissionPool& source);
		void Spawn(const EmoteEmissionBirth& birth);
		void AddPeriodicTrigger(std::uint32_t source_emitter, std::uint32_t target_emitter, float frequency);
		void Update(float ticks);
		// This is the real force-supplier selection phase, not a captured particle setter.
		void PrepareBounds(float left, float top, float right, float bottom, float extra_radius);
		[[nodiscard]] std::array<float, 3> SamplePoint(const resource::MotionPoint& point, float radius) const;
		[[nodiscard]] std::array<float, 3> SampleEllipse(const EmoteCollisionEllipse& ellipse) const;
		// Views remain valid until the next mutating pool operation.
		[[nodiscard]] std::span<const EmoteEmissionParticle> Particles() const noexcept;
		[[nodiscard]] std::span<const EmoteEmissionEvent> Events() const noexcept;

	private:
		struct Trigger {
			std::uint32_t SourceEmitter = 0;
			std::uint32_t TargetEmitter = 0;
			float Frequency = 0;
		};
		void RequireIdle() const;
		void RequirePrepared() const;
		void Integrate(float seconds);
		void RetireExpired(float seconds);
		EmoteEmissionPoolDefinition m_Definition;
		std::vector<EmoteEmissionParticle> m_Particles;
		std::vector<EmoteEmissionEvent> m_Events;
		std::vector<Trigger> m_Triggers;
		std::vector<std::size_t> m_Selected;
		std::size_t m_ParticleLimit = 0;
		std::uint32_t m_NextId = 0;
		float m_MaximumRadius = 0;
		bool m_Prepared = false;
		bool m_Operating = false;
		bool m_Valid = true;
	};
} // namespace sgre::scene
