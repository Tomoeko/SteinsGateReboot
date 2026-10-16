#include "sgre/scene/emote_ellipse_collision.hpp"

#include "sgre/scene/emote_physical_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {
	static void RequireEllipsePoint(const resource::MotionPoint& point) {
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("Emote ellipse collision requires finite actual points");
		}
	}

	static void RequireCollisionEllipse(const EmoteCollisionEllipse& ellipse) {
		RequireEllipsePoint(ellipse.Center);
		RequireEllipsePoint(ellipse.Radius);
		if (!(ellipse.Radius.X > 0) || !(ellipse.Radius.Y > 0) || !std::isfinite(ellipse.Rotation)) {
			throw std::runtime_error("Emote ellipse collision requires positive finite actual section radii/rotation");
		}
	}

	static void RequireCollisionParticleEllipse(const EmoteCollisionEllipse& ellipse) {
		RequireEllipsePoint(ellipse.Center);
		RequireEllipsePoint(ellipse.Radius);
		// Native section325fc0 retains radius*normal.z, including a reversed horizontal axis.
		if (ellipse.Radius.X == 0 || !(ellipse.Radius.Y > 0) || !std::isfinite(ellipse.Rotation)) {
			throw std::runtime_error("Emote ellipse collision requires a finite nonzero signed section axis and positive vertical radius");
		}
	}

	static resource::MotionPoint TransformEllipsePoint(const resource::MotionMeshMatrix& matrix, const resource::MotionPoint& point) {
		return {(point.X * matrix.XX + point.Y * matrix.XY) + matrix.TranslationX, (point.Y * matrix.YY + point.X * matrix.YX) + matrix.TranslationY};
	}

	static resource::MotionMeshMatrix InverseEllipseMatrix(const resource::MotionMeshMatrix& matrix) {
		const auto determinant = matrix.YY * matrix.XX - matrix.YX * matrix.XY;
		if (!std::isfinite(determinant) || determinant == 0) {
			throw std::runtime_error("Emote ellipse collision requires an invertible actual affine matrix");
		}
		const auto reciprocal = 1.0f / determinant;
		resource::MotionMeshMatrix inverse;
		inverse.XX = reciprocal * matrix.YY;
		inverse.XY = -matrix.XY * reciprocal;
		inverse.YX = -matrix.YX * reciprocal;
		inverse.YY = reciprocal * matrix.XX;
		inverse.TranslationX = (matrix.XY * matrix.TranslationY - matrix.YY * matrix.TranslationX) * reciprocal;
		inverse.TranslationY = (matrix.YX * matrix.TranslationX - matrix.XX * matrix.TranslationY) * reciprocal;

		return inverse;
	}

	static resource::MotionMeshMatrix ComposeEllipseMatrices(const resource::MotionMeshMatrix& left, const resource::MotionMeshMatrix& right) {
		resource::MotionMeshMatrix result;
		result.XX = left.XY * right.YX + left.XX * right.XX;
		result.XY = left.XY * right.YY + left.XX * right.XY;
		result.YX = left.YY * right.YX + left.YX * right.XX;
		result.YY = left.YY * right.YY + left.YX * right.XY;
		result.TranslationX = (right.TranslationY * left.XY + right.TranslationX * left.XX) + left.TranslationX;
		result.TranslationY = (right.TranslationY * left.YY + right.TranslationX * left.YX) + left.TranslationY;

		return result;
	}

	static resource::MotionMeshMatrix NormalizeCollisionEllipse(const EmoteCollisionEllipse& ellipse) {
		const auto sine = PrepareEmotePhysicalSine(-ellipse.Rotation);
		const auto cosine = PrepareEmotePhysicalCosine(-ellipse.Rotation);
		const auto horizontal = 1.0f / ellipse.Radius.X;
		const auto vertical = 1.0f / ellipse.Radius.Y;
		resource::MotionMeshMatrix result;
		result.XX = cosine * horizontal;
		result.XY = -sine * horizontal;
		result.YX = sine * vertical;
		result.YY = cosine * vertical;
		result.TranslationX = ((-ellipse.Center.X * cosine) + (-ellipse.Center.Y * -sine)) * horizontal;
		result.TranslationY = ((-ellipse.Center.Y * cosine) + (-ellipse.Center.X * sine)) * vertical;

		return result;
	}

	struct CollisionEllipseConic {
		resource::MotionPoint Center;
		float XX = 0;
		float XY = 0;
		float YY = 0;
	};

	static CollisionEllipseConic PrepareCollisionConic(const EmoteCollisionEllipse& ellipse) {
		const auto sine = PrepareEmotePhysicalSine(ellipse.Rotation);
		const auto cosine = PrepareEmotePhysicalCosine(ellipse.Rotation);
		const auto horizontal = 1.0f / ellipse.Radius.X;
		const auto vertical = 1.0f / ellipse.Radius.Y;
		CollisionEllipseConic result;
		result.Center = ellipse.Center;
		result.XX = ((sine * sine) * vertical) * vertical + ((cosine * cosine) * horizontal) * horizontal;
		result.XY = (horizontal * horizontal - vertical * vertical) * ((cosine + cosine) * sine);
		result.YY = ((sine * sine) * horizontal) * horizontal + ((cosine * cosine) * vertical) * vertical;

		return result;
	}

	static CollisionEllipseConic TransformCollisionConic(const CollisionEllipseConic& conic, const resource::MotionMeshMatrix& matrix) {
		const auto inverse = InverseEllipseMatrix(matrix);
		CollisionEllipseConic result;
		result.Center = TransformEllipsePoint(matrix, conic.Center);
		result.XX = (inverse.XX * conic.XY) * inverse.YX + (inverse.XX * conic.XX) * inverse.XX + (inverse.YX * conic.YY) * inverse.YX;
		result.XY = ((inverse.YX * inverse.XY + inverse.YY * inverse.XX) * conic.XY + ((conic.XX + conic.XX) * inverse.XX) * inverse.XY) + ((conic.YY + conic.YY) * inverse.YX) * inverse.YY;
		result.YY = (inverse.XY * conic.XY) * inverse.YY + (inverse.XY * conic.XX) * inverse.XY + (inverse.YY * conic.YY) * inverse.YY;

		return result;
	}

	static EmoteCollisionEllipse ResolveCollisionConic(const CollisionEllipseConic& conic) {
		const auto difference = conic.XX - conic.YY;
		const auto twice_angle = PrepareEmotePhysicalAngle(conic.XY, difference);
		const auto ratio = difference / PrepareEmotePhysicalCosine(twice_angle);
		const auto total = conic.XX + conic.YY;
		EmoteCollisionEllipse result;
		result.Center = conic.Center;
		result.Radius.X = 1.0f / std::sqrt((ratio + total) * 0.5f);
		result.Radius.Y = 1.0f / std::sqrt((total - ratio) * 0.5f);
		result.Rotation = twice_angle * 0.5f;
		RequireCollisionEllipse(result);

		return result;
	}

	static EmoteCollisionEllipse TransformCollisionEllipse(const EmoteCollisionEllipse& ellipse, const resource::MotionMeshMatrix& matrix) {
		return ResolveCollisionConic(TransformCollisionConic(PrepareCollisionConic(ellipse), matrix));
	}

	static resource::MotionPoint ClosestUnitCircleLine(const resource::MotionPoint& point, float horizontal, float vertical, float constant) {
		const auto squared = horizontal * horizontal + vertical * vertical;
		const auto discriminant = squared - constant * constant;
		if (squared == 0 || discriminant < 0) {
			const auto norm = std::sqrt(point.X * point.X + point.Y * point.Y);
			if (!(norm > 0) || !std::isfinite(norm)) {
				throw std::runtime_error("Emote ellipse contact radial fallback exceeds the finite native domain");
			}
			const auto reciprocal = 1.0f / norm;

			return {point.X * reciprocal, point.Y * reciprocal};
		}
		const auto root = std::sqrt(discriminant);
		const auto reciprocal = 1.0f / squared;
		const resource::MotionPoint first{-(root * vertical + constant * horizontal) * reciprocal, (root * horizontal - constant * vertical) * reciprocal};
		const resource::MotionPoint second{(root * vertical - constant * horizontal) * reciprocal, -(root * horizontal + constant * vertical) * reciprocal};
		const auto first_x = point.X - first.X;
		const auto first_y = point.Y - first.Y;
		const auto second_x = point.X - second.X;
		const auto second_y = point.Y - second.Y;

		return second_y * second_y + second_x * second_x <= first_y * first_y + first_x * first_x ? second : first;
	}

	bool ProjectEmoteEllipseHalfPlane(resource::MotionPoint& projected, const EmoteCollisionEllipse& particle, const resource::MotionPoint& first, const resource::MotionPoint& second) {
		RequireCollisionParticleEllipse(particle);
		RequireEllipsePoint(first);
		RequireEllipsePoint(second);
		const auto normalization = NormalizeCollisionEllipse(particle);
		const auto left = TransformEllipsePoint(normalization, first);
		const auto right = TransformEllipsePoint(normalization, second);
		const auto normal_x = right.Y - left.Y;
		const auto normal_y = -(right.X - left.X);
		const auto constant = right.X * left.Y - left.X * right.Y;
		const auto norm = std::sqrt(normal_y * normal_y + normal_x * normal_x);
		if (!(norm > 0) || !std::isfinite(norm)) {
			throw std::runtime_error("Emote ellipse half-plane requires distinct actual endpoints");
		}
		if (constant > norm) {
			return false;
		}
		const auto world_x = normal_x * normalization.XX + normal_y * normalization.YX;
		const auto world_y = normal_x * normalization.XY + normal_y * normalization.YY;
		const auto world_constant = (normal_x * normalization.TranslationX + normal_y * normalization.TranslationY) + (constant - norm);
		const auto side = (world_x * particle.Center.X + world_y * particle.Center.Y) + world_constant;
		const auto reciprocal = 1.0f / (world_y * world_y + world_x * world_x);
		projected = {particle.Center.X - (world_x * side) * reciprocal, particle.Center.Y - (world_y * side) * reciprocal};
		RequireEllipsePoint(projected);

		return true;
	}

	bool ProjectEmoteEllipseContact(resource::MotionPoint& projected, const EmoteCollisionEllipse& particle, const resource::MotionPoint& relative_displacement, const EmoteCollisionEllipse& collider, bool interior, std::size_t& remaining_samples) {
		RequireCollisionParticleEllipse(particle);
		RequireCollisionEllipse(collider);
		RequireEllipsePoint(relative_displacement);
		const auto smallest_radius = std::min(collider.Radius.X, collider.Radius.Y);
		if (interior && std::min(particle.Radius.X, particle.Radius.Y) > smallest_radius) {
			throw std::runtime_error("Emote oversized interior section swap projection remains unrecovered");
		}
		const auto distance = std::sqrt(relative_displacement.X * relative_displacement.X + relative_displacement.Y * relative_displacement.Y);
		const auto requested = std::max(1.0f, std::ceil((distance + distance) / smallest_radius));
		if (!std::isfinite(requested) || requested > 4096 || requested > static_cast<float>(remaining_samples)) {
			throw std::runtime_error("Emote ellipse contact exceeds bounded actual sweep work");
		}
		const auto count = static_cast<std::uint32_t>(requested);
		remaining_samples -= count;
		for (std::uint32_t index = 0; index < count; ++index) {
			const auto fraction = static_cast<float>(count - index - 1) / static_cast<float>(count);
			auto moving = particle;
			moving.Center.X = particle.Center.X - relative_displacement.X * fraction;
			moving.Center.Y = particle.Center.Y - relative_displacement.Y * fraction;
			const auto normalization = NormalizeCollisionEllipse(moving);
			auto expanded = TransformCollisionEllipse(collider, normalization);
			expanded.Radius.X = expanded.Radius.X + (interior ? -1.0f : 1.0f);
			expanded.Radius.Y = expanded.Radius.Y + (interior ? -1.0f : 1.0f);
			RequireCollisionEllipse(expanded);
			const auto expanded_normalization = NormalizeCollisionEllipse(expanded);
			const auto unit = TransformEllipsePoint(expanded_normalization, {});
			const auto squared = unit.X * unit.X + unit.Y * unit.Y;
			if (!(interior ? squared >= 1.0f : squared <= 1.0f)) {
				continue;
			}
			const auto inverse = InverseEllipseMatrix(normalization);
			const auto world_conic = TransformCollisionConic(PrepareCollisionConic(expanded), inverse);
			const auto world = ResolveCollisionConic(world_conic);
			const auto back = ComposeEllipseMatrices(inverse, InverseEllipseMatrix(expanded_normalization));
			auto axes_ellipse = world;
			axes_ellipse.Radius = {1, 1};
			// Native2f964a composes BACK*AXES in this order; reversing it changes genuine contact outputs.
			const auto axes_normalization = NormalizeCollisionEllipse(axes_ellipse);
			const auto combined = ComposeEllipseMatrices(back, axes_normalization);
			// Native2f96f3 resolves the third transform from retained conic coefficients, not rounded principal radii.
			const auto aligned = ResolveCollisionConic(TransformCollisionConic(world_conic, axes_normalization));
			const auto point = TransformEllipsePoint(combined, unit);
			const auto normal_x = (-point.X * aligned.Radius.Y) / aligned.Radius.X;
			const auto normal_y = (point.Y * aligned.Radius.X) / aligned.Radius.Y;
			const auto constant = (aligned.Radius.Y / aligned.Radius.X - aligned.Radius.X / aligned.Radius.Y) * (point.Y * point.X);
			const auto line_matrix = InverseEllipseMatrix(InverseEllipseMatrix(combined));
			const auto horizontal = line_matrix.XX * normal_y + line_matrix.YX * normal_x;
			const auto vertical = line_matrix.XY * normal_y + line_matrix.YY * normal_x;
			const auto offset = (line_matrix.TranslationX * normal_y + line_matrix.TranslationY * normal_x) + constant;
			projected = TransformEllipsePoint(back, ClosestUnitCircleLine(unit, horizontal, vertical, offset));
			RequireEllipsePoint(projected);

			return true;
		}

		return false;
	}
} // namespace sgre::scene
