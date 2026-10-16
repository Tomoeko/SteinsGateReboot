#include "sgre/scene/motion_particle_birth.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace sgre::scene {

	static constexpr float kParticleFullRadians = 6.28318548f;
	static constexpr float kParticleDegreesPerRadian = 57.2957764f;
	static constexpr float kParticleRadiansPerDegree = 0.0174532924f;
	static constexpr std::size_t kMaximumParticleAngleWraps = 65536;

	static void RequireParticleBirthDomain(const MotionParticleDefinition& definition, const MotionParticleNumericState& numeric, const MotionParticleBirthSource& source) {
		if (definition.Particle < 0 || definition.Particle > 2 || definition.TriangularVolume || definition.InheritAngle || definition.InheritVelocity != 0 || (definition.FlyDirection != 0 && definition.FlyDirection != 1) || definition.ApplyZoomToVelocity < 0 || definition.ApplyZoomToVelocity > 2 || source.CoordinatePlane > 1) {
			throw std::runtime_error("motion particle birth requires the recovered planar modes and measured direction/inheritance policies; spatial volume and lifetime-directed producers remain open");
		}

		for (const auto value : {source.Transform.XX, source.Transform.XY, source.Transform.YX, source.Transform.YY, source.Coordinate[0], source.Coordinate[1], source.Coordinate[2], source.Displacement[0], source.Displacement[1], source.Displacement[2], source.DeltaTicks, source.Origin.X, source.Origin.Y, source.PlayerRootTransform.XX, source.PlayerRootTransform.XY, source.PlayerRootTransform.YX, source.PlayerRootTransform.YY, numeric.FrequencyMin, numeric.FrequencyMax, numeric.VelocityMin, numeric.VelocityMax, numeric.AngleMin, numeric.AngleMax, numeric.ZoomMin, numeric.ZoomMax, numeric.Range, definition.AccelerationRatio}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("motion particle birth requires finite actual source and numeric fields");
			}
		}

		const auto angle_limit = static_cast<float>(kMaximumParticleAngleWraps) * 360.0f;
		if (std::abs(numeric.AngleMin) > angle_limit || std::abs(numeric.AngleMax) > angle_limit) {
			throw std::runtime_error("motion particle rotation exceeds its bounded native repeated-wrap work");
		}
	}

	static float SampleParticleRange(float minimum, float maximum, MotionPlayerRandom& random) {
		if (minimum == maximum) {
			return minimum;
		}

		const auto sample = random.NextUnitFloat();
		const auto extent = maximum - minimum;
		const auto offset = sample * extent;

		return offset + minimum;
	}

	static float NormalizeParticleDegrees(float angle) {
		while (angle < 0) {
			angle += 360.0f;
		}

		while (angle >= 360.0f) {
			angle += -360.0f;
		}

		return angle;
	}

	static resource::MotionPoint ParticlePlanarOffset(const MotionParticleDefinition& definition, MotionPlayerRandom& random) {
		resource::MotionPoint offset;
		if (definition.Particle == 1) {
			auto angle = random.NextUnitFloat();
			angle *= kParticleFullRadians;
			angle += 0.0f;
			const auto radial_sample = random.NextUnitFloat() + 0.0f;
			const auto radius = std::sqrt(radial_sample) * 16.0f;
			offset.X = std::cos(angle) * radius;
			offset.Y = std::sin(angle) * radius;
		} else if (definition.Particle == 2) {
			offset.X = random.NextUnitFloat() * 32.0f;
			offset.X -= 16.0f;
			offset.Y = random.NextUnitFloat() * 32.0f;
			offset.Y -= 16.0f;
		}

		return offset;
	}

	static float ParticleRootScale(const MotionLinearTransform& transform) {
		const auto diagonal = transform.YY * transform.XX;
		const auto opposite = transform.YX * transform.XY;
		const auto determinant = diagonal - opposite;

		return std::sqrt(std::abs(determinant));
	}

	static void RequireParticleBirthOutput(const MotionParticleBirthState& state) {
		for (const auto value : {state.Coordinate[0], state.Coordinate[1], state.Coordinate[2], state.Velocity[0], state.Velocity[1], state.Velocity[2], state.AngleDegrees, state.Zoom, state.Attenuation}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("motion particle birth output exceeds its finite recovered domain");
			}
		}
	}

	MotionParticleBirthState EvaluateMotionParticleBirth(const MotionParticleDefinition& definition, const MotionParticleNumericState& numeric, const MotionParticleBirthSource& source, MotionPlayerRandom& random) {
		RequireParticleBirthDomain(definition, numeric, source);
		auto local = ParticlePlanarOffset(definition, random);
		local.X -= source.Origin.X;
		local.Y -= source.Origin.Y;
		auto offset_x = source.Transform.XY * local.Y;
		offset_x += source.Transform.XX * local.X;
		auto offset_y = source.Transform.YY * local.Y;
		offset_y += source.Transform.YX * local.X;
		const auto speed = SampleParticleRange(numeric.VelocityMin, numeric.VelocityMax, random);
		auto direction = definition.FlyDirection == 1 ? std::atan2(offset_y, offset_x) : std::atan2(source.Transform.YX, source.Transform.XX);
		direction *= kParticleDegreesPerRadian;
		direction += SampleParticleRange(-numeric.Range, numeric.Range, random);
		direction *= kParticleRadiansPerDegree;

		MotionParticleBirthState state;
		state.Coordinate[0] = offset_x + source.Coordinate[0];
		if (source.CoordinatePlane == 0) {
			state.Coordinate[1] = offset_y + source.Coordinate[1];
			state.Coordinate[2] = 0.0f + source.Coordinate[2];
			state.Velocity[0] = std::cos(direction) * speed;
			state.Velocity[0] *= 1.0f;
			state.Velocity[1] = std::sin(direction) * speed;
			state.Velocity[1] *= 1.0f;
			state.Velocity[2] = 0.0f * speed;
		} else {
			state.Coordinate[1] = 0.0f + source.Coordinate[1];
			state.Coordinate[2] = offset_y + source.Coordinate[2];
			state.Velocity[0] = std::cos(direction) * speed;
			state.Velocity[0] *= 1.0f;
			state.Velocity[1] = 0.0f * speed;
			state.Velocity[2] = std::sin(direction) * speed;
			state.Velocity[2] *= 1.0f;
		}

		state.FlipX = source.FlipX;
		state.FlipY = source.FlipY;
		auto angle = SampleParticleRange(numeric.AngleMin, numeric.AngleMax, random);
		if (source.FlipY != source.FlipX) {
			angle = -angle;
		}

		state.AngleDegrees = NormalizeParticleDegrees(angle);
		state.Zoom = SampleParticleRange(numeric.ZoomMin, numeric.ZoomMax, random);
		if (!source.IndependentLayerInherit) {
			state.Zoom = ParticleRootScale(source.PlayerRootTransform) * state.Zoom;
		}

		if (definition.ApplyZoomToVelocity == 1) {
			for (auto& velocity : state.Velocity) {
				velocity = state.Zoom * velocity;
			}
		} else if (definition.ApplyZoomToVelocity == 2) {
			for (auto& velocity : state.Velocity) {
				velocity /= state.Zoom;
			}
		}

		state.Opacity = definition.InheritOpacity >= 1 ? source.Opacity : 255;
		state.Attenuation = definition.AccelerationRatio;
		RequireParticleBirthOutput(state);

		return state;
	}

} // namespace sgre::scene
