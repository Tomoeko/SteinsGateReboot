#include "sgre/scene/emote_collider.hpp"

#include "sgre/scene/emote_physical_math.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {
	static constexpr std::uint32_t kMaximumColliderSubsteps = 4096;
	static constexpr float kMaximumColliderSweepSamples = 4096;
	static constexpr std::size_t kMaximumColliderEntitySamples = 1024 * 1024;

	static void RequireColliderPoint(const resource::MotionPoint& point) {
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("Emote collider requires finite actual points");
		}
	}

	static float ColliderReciprocalLength(float squared) {
		if (!(squared > 0) || !std::isfinite(squared)) {
			throw std::runtime_error("Emote collider boundary requires a finite nonzero direction");
		}
		const auto half = squared * 0.5f;
		const auto initial = std::bit_cast<float>(std::uint32_t{0x5f3759df} - (std::bit_cast<std::uint32_t>(squared) >> 1));
		const auto first = (1.5f - (half * initial) * initial) * initial;
		return (1.5f - (half * first) * first) * first;
	}

	EmoteCollider::EmoteCollider(EmoteColliderDefinition definition)
	    : m_Definition(definition) {
		RequireColliderPoint(definition.First);
		RequireColliderPoint(definition.Second);
		RequireColliderPoint(definition.Radius);
		if (!std::isfinite(definition.Rotation)) {
			throw std::runtime_error("Emote collider requires finite actual rotation");
		}
		if (definition.Kind == EmoteColliderKind::HalfPlane) {
			if (definition.First.X == definition.Second.X && definition.First.Y == definition.Second.Y) {
				throw std::runtime_error("Emote half-plane requires distinct authored endpoints");
			}
		} else if ((definition.Kind != EmoteColliderKind::SolidEllipse && definition.Kind != EmoteColliderKind::InteriorEllipse) || !(definition.Radius.X > 0) || !(definition.Radius.Y > 0)) {
			throw std::runtime_error("Emote collider requires a recovered shape and positive ellipse radii");
		}
		m_LocalCosine = PrepareEmotePhysicalCosine(definition.Rotation);
		m_LocalSine = PrepareEmotePhysicalSine(definition.Rotation);
	}

	void EmoteCollider::SetEnabled(bool enabled) noexcept {
		m_Enabled = enabled;
	}

	void EmoteCollider::SetPosture(const EmoteColliderPosture& posture) {
		RequireColliderPoint(posture.Coordinate);
		for (const auto value : {posture.Rotation, posture.Cosine, posture.Sine}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote collider requires its finite actual five-word posture");
			}
		}
		// Native2f8aa0 snapshots the preceding frame even when the same coordinates arrive again.
		m_Previous = m_Current;
		m_Current = posture;
		m_Interpolated = m_Previous;
		m_StepCount = 0;
		m_StepIndex = 0;
	}

	void EmoteCollider::ResetMotion() {
		m_Previous = m_Current;
		m_Interpolated = m_Current;
		m_TranslationStep = {};
		m_RotationStep = 0;
		m_StepCosine = 1;
		m_StepSine = 0;
		m_StepCount = 0;
		m_StepIndex = 0;
	}

	void EmoteCollider::ClearContact() noexcept {
		m_Contacted = false;
	}

	void EmoteCollider::PrepareSubsteps(std::uint32_t count) {
		if (count == 0 || count > kMaximumColliderSubsteps) {
			throw std::runtime_error("Emote collider interpolation requires bounded positive native substeps");
		}
		m_Interpolated = m_Previous;
		const auto divisor = static_cast<float>(count);
		m_TranslationStep.X = (m_Current.Coordinate.X - m_Previous.Coordinate.X) / divisor;
		m_TranslationStep.Y = (m_Current.Coordinate.Y - m_Previous.Coordinate.Y) / divisor;
		m_RotationStep = m_Current.Rotation - m_Previous.Rotation;
		m_StepCosine = m_Previous.Cosine * m_Current.Cosine + m_Previous.Sine * m_Current.Sine;
		m_StepSine = m_Current.Sine * m_Previous.Cosine - m_Previous.Sine * m_Current.Cosine;
		if (count != 1) {
			m_RotationStep /= divisor;
			m_StepCosine = PrepareEmotePhysicalCosine(m_RotationStep);
			m_StepSine = PrepareEmotePhysicalSine(m_RotationStep);
		}
		m_StepCount = count;
		m_StepIndex = 0;
		m_RemainingSamples = kMaximumColliderEntitySamples;
	}

	void EmoteCollider::AdvanceSubstep() {
		if (m_StepCount == 0 || m_StepIndex >= kMaximumColliderSubsteps * kMaximumColliderSubsteps) {
			throw std::runtime_error("Emote collider step requires its actual prepared interpolation count");
		}
		m_Interpolated.Coordinate.X += m_TranslationStep.X;
		m_Interpolated.Coordinate.Y += m_TranslationStep.Y;
		m_Interpolated.Rotation += m_RotationStep;
		const auto next_sine = m_Interpolated.Cosine * m_StepSine + m_StepCosine * m_Interpolated.Sine;
		m_Interpolated.Cosine = m_StepCosine * m_Interpolated.Cosine - m_StepSine * m_Interpolated.Sine;
		m_Interpolated.Sine = next_sine;
		++m_StepIndex;
	}

	bool EmoteCollider::ProjectHalfPlane(resource::MotionPoint& point, float particle_radius) const {
		const auto& pose = m_Interpolated;
		const auto& first = m_Definition.First;
		const auto& second = m_Definition.Second;
		const resource::MotionPoint first_world{first.X * pose.Cosine + pose.Coordinate.X - first.Y * pose.Sine, first.X * pose.Sine + pose.Coordinate.Y + first.Y * pose.Cosine};
		const resource::MotionPoint second_world{second.X * pose.Cosine + pose.Coordinate.X - second.Y * pose.Sine, second.X * pose.Sine + pose.Coordinate.Y + second.Y * pose.Cosine};
		const auto dx = second_world.X - first_world.X;
		const auto dy = second_world.Y - first_world.Y;
		const auto inverse_length = ColliderReciprocalLength(dy * dy + dx * dx);
		const auto offset_x = (inverse_length * dy) * particle_radius;
		const auto offset_y = (-inverse_length * dx) * particle_radius;
		const auto normal_y = -dx;
		const auto constant = (offset_y + first_world.Y) * (offset_x + second_world.X) - (offset_x + first_world.X) * (offset_y + second_world.Y);
		const auto side = normal_y * point.Y + point.X * dy + constant;
		if (!(side <= 0)) {
			return false;
		}
		const auto negative_x = -dy;
		const auto tangent_coordinate = (-normal_y * point.X) - negative_x * point.Y;
		const auto inverse_denominator = 1.0f / (normal_y * normal_y - negative_x * dy);
		point.X = (negative_x * constant - tangent_coordinate * normal_y) * inverse_denominator;
		point.Y = -(normal_y * constant - tangent_coordinate * dy) * inverse_denominator;
		return true;
	}

	bool EmoteCollider::ProjectEllipse(resource::MotionPoint& point, const resource::MotionPoint& particle_displacement, float particle_radius) {
		const auto interior = m_Definition.Kind == EmoteColliderKind::InteriorEllipse;
		const auto smallest_radius = std::min(m_Definition.Radius.X, m_Definition.Radius.Y);
		if (interior && particle_radius > smallest_radius) {
			throw std::runtime_error("Emote oversized interior-particle ellipse intersection producer is not recovered");
		}
		const auto expansion = interior ? -particle_radius : particle_radius;
		const auto radius_x = m_Definition.Radius.X + expansion;
		const auto radius_y = m_Definition.Radius.Y + expansion;
		if (!(radius_x > 0) || !(radius_y > 0)) {
			throw std::runtime_error("Emote collider expanded ellipse is degenerate");
		}
		const auto& pose = m_Interpolated;
		const auto center_x = m_Definition.First.X * pose.Cosine + pose.Coordinate.X - m_Definition.First.Y * pose.Sine;
		const auto center_y = m_Definition.First.X * pose.Sine + pose.Coordinate.Y + m_Definition.First.Y * pose.Cosine;
		const auto sine = m_LocalCosine * pose.Sine + m_LocalSine * pose.Cosine;
		const auto cosine = m_LocalCosine * pose.Cosine - m_LocalSine * pose.Sine;
		const auto inverse_x = 1.0f / radius_x;
		const auto inverse_y = 1.0f / radius_y;
		const auto xx = cosine * inverse_x;
		const auto xy = sine * inverse_x;
		const auto yx = -sine * inverse_y;
		const auto yy = inverse_y * cosine;
		const auto ty = (sine * center_x - cosine * center_y) * inverse_y;
		// Native2fa2c0 uses -sine*centerY - cosine*centerX for the first affine translation.
		const auto translation_x = (-sine * center_y - cosine * center_x) * inverse_x;
		const auto relative_x = particle_displacement.X - m_TranslationStep.X;
		const auto relative_y = particle_displacement.Y - m_TranslationStep.Y;
		const auto distance = std::sqrt(relative_y * relative_y + relative_x * relative_x);
		const auto requested = std::max(1.0f, std::ceil((distance + distance) / smallest_radius));
		if (!std::isfinite(requested) || requested > kMaximumColliderSweepSamples) {
			throw std::runtime_error("Emote collider sweep exceeds bounded actual particle motion");
		}
		const auto count = static_cast<std::uint32_t>(requested);
		if (count > m_RemainingSamples) {
			throw std::runtime_error("Emote collider entity sweep work exceeds bounded actual sample storage");
		}
		m_RemainingSamples -= count;
		const auto original = point;
		for (std::uint32_t index = 0; index != count; ++index) {
			const auto fraction = static_cast<float>(count - index - 1) / static_cast<float>(count);
			point.X = original.X - fraction * relative_x;
			point.Y = original.Y - fraction * relative_y;
			const auto unit_x = (xy * point.Y + xx * point.X) + translation_x;
			const auto unit_y = (yy * point.Y + yx * point.X) + ty;
			const auto squared = unit_y * unit_y + unit_x * unit_x;
			if (!(interior ? squared >= 1.0f : squared <= 1.0f)) {
				continue;
			}
			const auto tangent_x = (-unit_x * radius_y) * inverse_x;
			const auto tangent_y = (radius_x * unit_y) * inverse_y;
			const auto cross = (radius_y * inverse_x - radius_x * inverse_y) * (unit_y * unit_x);
			const auto tangent_squared = tangent_x * tangent_x + tangent_y * tangent_y;
			const auto discriminant = tangent_squared - cross * cross;
			resource::MotionPoint boundary;
			if (tangent_squared == 0 || discriminant < 0) {
				const auto reciprocal = ColliderReciprocalLength(squared);
				boundary = {unit_x * reciprocal, reciprocal * unit_y};
			} else {
				const auto root = std::sqrt(discriminant);
				const auto reciprocal = 1.0f / tangent_squared;
				boundary.X = -(root * tangent_x + cross * tangent_y) * reciprocal;
				boundary.Y = (root * tangent_y - cross * tangent_x) * reciprocal;
				const resource::MotionPoint other{(root * tangent_x - cross * tangent_y) * reciprocal, -(root * tangent_y + cross * tangent_x) * reciprocal};
				const auto first_x = unit_x - boundary.X;
				const auto first_y = unit_y - boundary.Y;
				const auto other_x = unit_x - other.X;
				const auto other_y = unit_y - other.Y;
				if (!(other_y * other_y + other_x * other_x > first_y * first_y + first_x * first_x)) {
					boundary = other;
				}
			}
			// Keep native inverse-affine construction and multiplication order rather than rebuilding from radii.
			const auto inverse_determinant = 1.0f / (yx * xy - yy * xx);
			const auto inverse_xx = -yy * inverse_determinant;
			const auto inverse_xy = xy * inverse_determinant;
			const auto inverse_yx = yx * inverse_determinant;
			const auto inverse_yy = -xx * inverse_determinant;
			const auto inverse_tx = (yy * translation_x - ty * xy) * inverse_determinant;
			const auto inverse_ty = (ty * xx - yx * translation_x) * inverse_determinant;
			point.X = (boundary.Y * inverse_xy + boundary.X * inverse_xx) + inverse_tx;
			point.Y = (boundary.Y * inverse_yy + boundary.X * inverse_yx) + inverse_ty;
			return true;
		}
		return false;
	}

	bool EmoteCollider::Project(resource::MotionPoint& point, const resource::MotionPoint& particle_displacement, float particle_radius) {
		RequireColliderPoint(point);
		RequireColliderPoint(particle_displacement);
		if (m_StepIndex == 0 || !std::isfinite(particle_radius) || particle_radius < 0) {
			throw std::runtime_error("Emote collider projection requires its actual substep and finite particle radius");
		}
		if (!m_Enabled) {
			return false;
		}
		if (m_Definition.Kind == EmoteColliderKind::HalfPlane) {
			if (m_RemainingSamples == 0) {
				throw std::runtime_error("Emote collider entity work exceeds bounded actual sample storage");
			}
			--m_RemainingSamples;
		}
		const auto contacted = m_Definition.Kind == EmoteColliderKind::HalfPlane ? ProjectHalfPlane(point, particle_radius) : ProjectEllipse(point, particle_displacement, particle_radius);
		RequireColliderPoint(point);
		m_Contacted = m_Contacted || contacted;
		return contacted;
	}

	bool EmoteCollider::ProjectSection(resource::MotionPoint& projected, const EmoteCollisionEllipse& particle, const resource::MotionPoint& particle_displacement) {
		RequireColliderPoint(particle_displacement);
		if (m_StepIndex == 0) {
			throw std::runtime_error("Emote section projection requires its actual prepared collider substep");
		}
		if (!m_Enabled) {
			return false;
		}
		const auto& pose = m_Interpolated;
		if (m_Definition.Kind == EmoteColliderKind::HalfPlane) {
			if (m_RemainingSamples == 0) {
				throw std::runtime_error("Emote section projection exceeds bounded actual contact work");
			}
			--m_RemainingSamples;
			const auto& first = m_Definition.First;
			const auto& second = m_Definition.Second;
			const resource::MotionPoint first_world{first.X * pose.Cosine + pose.Coordinate.X - first.Y * pose.Sine, first.X * pose.Sine + pose.Coordinate.Y + first.Y * pose.Cosine};
			const resource::MotionPoint second_world{second.X * pose.Cosine + pose.Coordinate.X - second.Y * pose.Sine, second.X * pose.Sine + pose.Coordinate.Y + second.Y * pose.Cosine};
			// Native2fb330 does not set the ellipse-specific contact latch.
			return ProjectEmoteEllipseHalfPlane(projected, particle, first_world, second_world);
		}
		EmoteCollisionEllipse fixed;
		fixed.Center.X = m_Definition.First.X * pose.Cosine + pose.Coordinate.X - m_Definition.First.Y * pose.Sine;
		fixed.Center.Y = m_Definition.First.X * pose.Sine + pose.Coordinate.Y + m_Definition.First.Y * pose.Cosine;
		fixed.Radius = m_Definition.Radius;
		fixed.Rotation = m_Definition.Rotation + pose.Rotation;
		const resource::MotionPoint relative{particle_displacement.X - m_TranslationStep.X, particle_displacement.Y - m_TranslationStep.Y};
		const auto contacted = ProjectEmoteEllipseContact(projected, particle, relative, fixed, m_Definition.Kind == EmoteColliderKind::InteriorEllipse, m_RemainingSamples);
		m_Contacted = m_Contacted || contacted;

		return contacted;
	}

	bool EmoteCollider::Enabled() const noexcept {
		return m_Enabled;
	}

	bool EmoteCollider::Contacted() const noexcept {
		return m_Contacted;
	}

	const EmoteColliderPosture& EmoteCollider::InterpolatedPosture() const noexcept {
		return m_Interpolated;
	}
} // namespace sgre::scene
