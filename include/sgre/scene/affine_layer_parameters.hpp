#pragma once

#include <cstdint>
#include <optional>

namespace sgre::scene {

	enum class AffineScalar : std::uint8_t {
		PositionX,
		PositionY,
		PositionZ,
		Rotation,
		ZoomX,
		ZoomY,
		SlantX,
		SlantY,
		OffsetX,
		OffsetY,
		OffsetZ,
		CenterX,
		CenterY,
		OriginX,
		OriginY,
		OriginZ,
		AbsoluteBase,
		BaseScale,
		ImageX,
		ImageY,
		ImageRotation,
		ImageZoom
	};

	enum class AffineAxis : std::uint8_t {
		X,
		Y
	};

	enum class AffinePivot : std::uint8_t {
		Zoom,
		Rotation
	};

	struct AffineOptionalCoordinate {
		float Value = 0;
		bool Enabled = false;
	};

	struct AffineLayerParameterState {
		float PositionX = 0;
		float PositionY = 0;
		float PositionZ = 0;
		bool FlipX = false;
		bool FlipY = false;
		float Rotation = 0;
		float ZoomX = 1;
		float ZoomY = 1;
		float SlantX = 0;
		float SlantY = 0;
		float OffsetX = 0;
		float OffsetY = 0;
		float OffsetZ = 0;
		float CenterX = 0;
		float CenterY = 0;
		AffineOptionalCoordinate ZoomCenterX;
		AffineOptionalCoordinate ZoomCenterY;
		AffineOptionalCoordinate RotationCenterX;
		AffineOptionalCoordinate RotationCenterY;
		float OriginX = 0;
		float OriginY = 0;
		float OriginZ = 0;
		std::int32_t Order = 0;
		float AbsoluteBase = 0;
		float BaseScale = 1;
		float InverseBaseScale = 1;
		float ImageX = 0;
		float ImageY = 0;
		float ImageRotation = 0;
		float ImageZoom = 1;
	};

	struct AffineParameterInvalidation {
		// The actual owner applies these effects to its matrix and dependent views/manager.
		bool Matrix = false;
		bool Sort = false;
	};

	class AffineLayerParameters {
	public:
		[[nodiscard]] AffineLayerParameterState State() const noexcept;
		[[nodiscard]] float Get(AffineScalar property) const;
		// Zoom getters/setters use percent; State exposes the stored scale factors.
		[[nodiscard]] AffineParameterInvalidation Set(AffineScalar property, float value);
		[[nodiscard]] AffineParameterInvalidation SetZoom(float percent);
		[[nodiscard]] AffineParameterInvalidation SetFlip(AffineAxis axis, bool flipped);
		[[nodiscard]] float GetPivot(AffinePivot pivot, AffineAxis axis) const;
		[[nodiscard]] AffineParameterInvalidation SetPivot(AffinePivot pivot, AffineAxis axis, std::optional<float> value);
		[[nodiscard]] AffineParameterInvalidation SetOrder(std::int32_t order);
		[[nodiscard]] AffineParameterInvalidation Assign(const AffineLayerParameters& source);

	private:
		[[nodiscard]] AffineParameterInvalidation Invalidate(bool sort = false) noexcept;
		AffineLayerParameterState m_State;
	};

} // namespace sgre::scene
