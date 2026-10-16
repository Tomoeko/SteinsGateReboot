#include "sgre/scene/layers.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static void RequireFiniteTransform(const LayerAffineTransform& transform) {
		if (!std::isfinite(transform.XX) || !std::isfinite(transform.XY) || !std::isfinite(transform.TranslationX) || !std::isfinite(transform.YX) || !std::isfinite(transform.YY) || !std::isfinite(transform.TranslationY)) {
			throw std::runtime_error("layer transform contains nonfinite or overflowing coefficients");
		}
	}

	static LayerPoint RequireFinitePoint(LayerPoint point) {
		if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
			throw std::runtime_error("layer coordinate contains nonfinite or overflowing components");
		}

		return point;
	}

	LayerAffineTransform ComposeLayerTransforms(const LayerAffineTransform& local, const LayerAffineTransform& parent) {
		RequireFiniteTransform(local);
		RequireFiniteTransform(parent);
		LayerAffineTransform result;
		result.XX = local.XX * parent.XX + parent.XY * local.YX;
		result.XY = local.YY * parent.XY + local.XY * parent.XX;
		result.TranslationX = local.TranslationY * parent.XY + local.TranslationX * parent.XX + parent.TranslationX;
		result.YX = parent.YY * local.YX + local.XX * parent.YX;
		result.YY = parent.YY * local.YY + parent.YX * local.XY;
		result.TranslationY = parent.YY * local.TranslationY + parent.YX * local.TranslationX + parent.TranslationY;
		RequireFiniteTransform(result);

		return result;
	}

	LayerAffineTransform PrepareLayerLocalTransform(const LayerTransform& transform) {
		static_cast<void>(RequireFinitePoint(transform.Center));
		static_cast<void>(RequireFinitePoint(transform.Offset));
		static_cast<void>(RequireFinitePoint(transform.Scale));
		if (!std::isfinite(transform.AngleRadians)) {
			throw std::runtime_error("layer transform angle is nonfinite");
		}

		LayerAffineTransform origin;
		origin.TranslationX = -transform.Center.X;
		origin.TranslationY = -transform.Center.Y;
		LayerAffineTransform scale;
		scale.XX = transform.Scale.X;
		scale.YY = transform.Scale.Y;
		const auto scaled = ComposeLayerTransforms(origin, scale);
		LayerAffineTransform rotation;
		rotation.XX = static_cast<float>(std::cos(static_cast<double>(transform.AngleRadians)));
		rotation.XY = -static_cast<float>(std::sin(static_cast<double>(transform.AngleRadians)));
		rotation.YX = -rotation.XY;
		rotation.YY = rotation.XX;
		const auto rotated = ComposeLayerTransforms(scaled, rotation);
		LayerAffineTransform center;
		center.TranslationX = transform.Center.X;
		center.TranslationY = transform.Center.Y;
		const auto centered = ComposeLayerTransforms(rotated, center);
		LayerAffineTransform offset;
		offset.TranslationX = -transform.Offset.X;
		offset.TranslationY = -transform.Offset.Y;

		return ComposeLayerTransforms(centered, offset);
	}

	LayerPoint TransformLayerPoint(const LayerAffineTransform& transform, LayerPoint point) {
		RequireFiniteTransform(transform);
		static_cast<void>(RequireFinitePoint(point));
		LayerPoint result;
		result.X = transform.XY * point.Y + transform.XX * point.X + transform.TranslationX;
		result.Y = transform.YY * point.Y + transform.YX * point.X + transform.TranslationY;

		return RequireFinitePoint(result);
	}

	LayerPoint InverseTransformLayerPoint(const LayerAffineTransform& transform, LayerPoint point) {
		RequireFiniteTransform(transform);
		static_cast<void>(RequireFinitePoint(point));
		const auto determinant = transform.YY * transform.XX - transform.YX * transform.XY;
		if (determinant == 0 || !std::isfinite(determinant)) {
			throw std::runtime_error("layer transform is singular or its determinant overflows");
		}

		const auto reciprocal = 1.0f / determinant;
		const auto translation_x = (transform.TranslationY * transform.XY - transform.TranslationX * transform.YY) * reciprocal;
		const auto translation_y = (transform.TranslationX * transform.YX - transform.TranslationY * transform.XX) * reciprocal;
		LayerPoint result;
		result.X = reciprocal * transform.YY * point.X - reciprocal * transform.XY * point.Y + translation_x;
		result.Y = reciprocal * transform.XX * point.Y - reciprocal * transform.YX * point.X + translation_y;

		return RequireFinitePoint(result);
	}

} // namespace sgre::scene
