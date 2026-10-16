#include "sgre/scene/affine_layer_parameters.hpp"

#include <stdexcept>

namespace sgre::scene {

	template <class State>
	static auto& ScalarStorage(State& state, AffineScalar property) {
		switch (property) {
		case AffineScalar::PositionX:
			return state.PositionX;
		case AffineScalar::PositionY:
			return state.PositionY;
		case AffineScalar::PositionZ:
			return state.PositionZ;
		case AffineScalar::Rotation:
			return state.Rotation;
		case AffineScalar::ZoomX:
			return state.ZoomX;
		case AffineScalar::ZoomY:
			return state.ZoomY;
		case AffineScalar::SlantX:
			return state.SlantX;
		case AffineScalar::SlantY:
			return state.SlantY;
		case AffineScalar::OffsetX:
			return state.OffsetX;
		case AffineScalar::OffsetY:
			return state.OffsetY;
		case AffineScalar::OffsetZ:
			return state.OffsetZ;
		case AffineScalar::CenterX:
			return state.CenterX;
		case AffineScalar::CenterY:
			return state.CenterY;
		case AffineScalar::OriginX:
			return state.OriginX;
		case AffineScalar::OriginY:
			return state.OriginY;
		case AffineScalar::OriginZ:
			return state.OriginZ;
		case AffineScalar::AbsoluteBase:
			return state.AbsoluteBase;
		case AffineScalar::BaseScale:
			return state.BaseScale;
		case AffineScalar::ImageX:
			return state.ImageX;
		case AffineScalar::ImageY:
			return state.ImageY;
		case AffineScalar::ImageRotation:
			return state.ImageRotation;
		case AffineScalar::ImageZoom:
			return state.ImageZoom;
		}

		throw std::runtime_error("unknown affine scalar property");
	}

	template <class State>
	static auto& PivotStorage(State& state, AffinePivot pivot, AffineAxis axis) {
		if (axis != AffineAxis::X && axis != AffineAxis::Y) {
			throw std::runtime_error("unknown affine pivot axis");
		}

		if (pivot == AffinePivot::Zoom) {
			return axis == AffineAxis::X ? state.ZoomCenterX : state.ZoomCenterY;
		}
		if (pivot == AffinePivot::Rotation) {
			return axis == AffineAxis::X ? state.RotationCenterX : state.RotationCenterY;
		}

		throw std::runtime_error("unknown affine pivot kind");
	}

	static bool IsPercentScale(AffineScalar property) noexcept {
		return property == AffineScalar::ZoomX || property == AffineScalar::ZoomY || property == AffineScalar::ImageZoom;
	}

	AffineLayerParameterState AffineLayerParameters::State() const noexcept {
		return m_State;
	}

	float AffineLayerParameters::Get(AffineScalar property) const {
		const auto value = ScalarStorage(m_State, property);

		return IsPercentScale(property) ? value * 100.0f : value;
	}

	AffineParameterInvalidation AffineLayerParameters::Invalidate(bool sort) noexcept {
		AffineParameterInvalidation result;
		result.Matrix = true;
		result.Sort = sort;

		return result;
	}

	AffineParameterInvalidation AffineLayerParameters::Set(AffineScalar property, float value) {
		auto& storage = ScalarStorage(m_State, property);
		if (property == AffineScalar::PositionZ && storage == value) {
			return {};
		}

		storage = IsPercentScale(property) ? value / 100.0f : value;
		if (property == AffineScalar::BaseScale) {
			// Native division uses binary64 before converting the reciprocal back to binary32.
			m_State.InverseBaseScale = static_cast<float>(1.0 / static_cast<double>(value));
		}

		return Invalidate(property == AffineScalar::PositionZ || property == AffineScalar::OriginZ || property == AffineScalar::AbsoluteBase);
	}

	AffineParameterInvalidation AffineLayerParameters::SetZoom(float percent) {
		const auto factor = percent / 100.0f;
		m_State.ZoomX = factor;
		m_State.ZoomY = factor;

		return Invalidate();
	}

	AffineParameterInvalidation AffineLayerParameters::SetFlip(AffineAxis axis, bool flipped) {
		if (axis == AffineAxis::X) {
			m_State.FlipX = flipped;
		} else if (axis == AffineAxis::Y) {
			m_State.FlipY = flipped;
		} else {
			throw std::runtime_error("unknown affine flip axis");
		}

		return Invalidate();
	}

	float AffineLayerParameters::GetPivot(AffinePivot pivot, AffineAxis axis) const {
		const auto& coordinate = PivotStorage(m_State, pivot, axis);

		return coordinate.Enabled ? coordinate.Value : (axis == AffineAxis::X ? m_State.CenterX : m_State.CenterY);
	}

	AffineParameterInvalidation AffineLayerParameters::SetPivot(AffinePivot pivot, AffineAxis axis, std::optional<float> value) {
		auto& coordinate = PivotStorage(m_State, pivot, axis);
		coordinate.Enabled = value.has_value();
		if (value) {
			coordinate.Value = *value;
		}

		return Invalidate();
	}

	AffineParameterInvalidation AffineLayerParameters::SetOrder(std::int32_t order) {
		if (m_State.Order == order) {
			return {};
		}

		m_State.Order = order;

		return Invalidate(true);
	}

	AffineParameterInvalidation AffineLayerParameters::Assign(const AffineLayerParameters& source) {
		m_State = source.m_State;

		return Invalidate(true);
	}

} // namespace sgre::scene
