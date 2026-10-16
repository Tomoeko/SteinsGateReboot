#include "sgre/scene/affine_layer_parameters.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void CheckInvalidation() {
	sgre::scene::AffineLayerParameters parameters;
	const auto initial = parameters.State();
	Require(initial.PositionX == 0 && initial.PositionY == 0 && initial.PositionZ == 0 && initial.ZoomX == 1 && initial.ZoomY == 1 && initial.BaseScale == 1 && initial.InverseBaseScale == 1 && initial.ImageZoom == 1, "affine scalar defaults");
	const auto same_depth = parameters.Set(sgre::scene::AffineScalar::PositionZ, -0.0f);
	Require(!same_depth.Matrix && !same_depth.Sort && !std::signbit(parameters.State().PositionZ), "equal signed-zero depth preserves prior storage and leaves matrix/sort unchanged");
	const auto same_order = parameters.SetOrder(0);
	Require(!same_order.Matrix && !same_order.Sort, "equal order is inert");
	const auto same_x = parameters.Set(sgre::scene::AffineScalar::PositionX, 0);
	Require(same_x.Matrix && !same_x.Sort, "equal ordinary coordinates still invalidate transforms");
	const auto origin = parameters.Set(sgre::scene::AffineScalar::OriginZ, 0);
	Require(origin.Matrix && origin.Sort, "origin depth always requests native sorting");
	const auto base = parameters.Set(sgre::scene::AffineScalar::AbsoluteBase, 0);
	Require(base.Matrix && base.Sort, "absolute base always requests native sorting");
	const auto depth = parameters.Set(sgre::scene::AffineScalar::PositionZ, 4);
	Require(depth.Matrix && depth.Sort && parameters.Get(sgre::scene::AffineScalar::PositionZ) == 4, "changed depth invalidates both matrix and manager order");
	const auto nan = std::numeric_limits<float>::quiet_NaN();
	Require(parameters.Set(sgre::scene::AffineScalar::PositionZ, nan).Sort && parameters.Set(sgre::scene::AffineScalar::PositionZ, nan).Sort, "unordered depth is never the equality fast path");
}

static void CheckScaleUnits() {
	sgre::scene::AffineLayerParameters parameters;
	Require(parameters.Get(sgre::scene::AffineScalar::ZoomX) == 100 && parameters.Get(sgre::scene::AffineScalar::ImageZoom) == 100, "native public scale units are percentages");
	Require(parameters.SetZoom(-125).Matrix, "paired scale changes invalidate");
	Require(parameters.State().ZoomX == -1.25f && parameters.State().ZoomY == -1.25f && parameters.Get(sgre::scene::AffineScalar::ZoomY) == -125, "paired zoom stores one normalized factor");
	static_cast<void>(parameters.Set(sgre::scene::AffineScalar::ImageZoom, 250));
	Require(parameters.State().ImageZoom == 2.5f && parameters.Get(sgre::scene::AffineScalar::ImageZoom) == 250, "image zoom has its own percent conversion");
	static_cast<void>(parameters.Set(sgre::scene::AffineScalar::BaseScale, 3));
	Require(parameters.State().BaseScale == 3 && std::bit_cast<std::uint32_t>(parameters.State().InverseBaseScale) == 0x3eaaaaab, "base reciprocal rounds after binary64 division");
	static_cast<void>(parameters.Set(sgre::scene::AffineScalar::BaseScale, -0.0f));
	Require(std::isinf(parameters.State().InverseBaseScale) && std::signbit(parameters.State().InverseBaseScale), "signed-zero base preserves negative reciprocal infinity");
}

static void CheckPivotsAndAssignment() {
	sgre::scene::AffineLayerParameters source;
	static_cast<void>(source.Set(sgre::scene::AffineScalar::CenterX, 8));
	static_cast<void>(source.Set(sgre::scene::AffineScalar::CenterY, 9));
	Require(source.GetPivot(sgre::scene::AffinePivot::Zoom, sgre::scene::AffineAxis::X) == 8 && source.GetPivot(sgre::scene::AffinePivot::Rotation, sgre::scene::AffineAxis::Y) == 9, "unset pivots follow ordinary center independently");
	static_cast<void>(source.SetPivot(sgre::scene::AffinePivot::Zoom, sgre::scene::AffineAxis::X, 0));
	static_cast<void>(source.SetPivot(sgre::scene::AffinePivot::Rotation, sgre::scene::AffineAxis::Y, 17));
	Require(source.GetPivot(sgre::scene::AffinePivot::Zoom, sgre::scene::AffineAxis::X) == 0 && source.GetPivot(sgre::scene::AffinePivot::Rotation, sgre::scene::AffineAxis::Y) == 17, "enabled zero pivot is distinct from fallback");
	static_cast<void>(source.SetPivot(sgre::scene::AffinePivot::Rotation, sgre::scene::AffineAxis::Y, {}));
	Require(!source.State().RotationCenterY.Enabled && source.State().RotationCenterY.Value == 17 && source.GetPivot(sgre::scene::AffinePivot::Rotation, sgre::scene::AffineAxis::Y) == 9, "clearing an override retains inactive storage but restores fallback");
	static_cast<void>(source.SetFlip(sgre::scene::AffineAxis::Y, true));
	static_cast<void>(source.SetOrder(-7));
	sgre::scene::AffineLayerParameters destination;
	const auto effect = destination.Assign(source);
	Require(effect.Matrix && effect.Sort && destination.State().Order == -7 && destination.State().FlipY && destination.State().RotationCenterY.Value == 17 && !destination.State().RotationCenterY.Enabled, "assignment includes inactive overrides and requests matrix/order refresh");
	const auto self = source.Assign(source);
	Require(self.Matrix && self.Sort && source.State().CenterX == 8, "self-assignment preserves values and still invalidates");
}

template <class Operation>
static void RequireError(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("unknown affine selector should be rejected");
}

static void CheckSelectors() {
	sgre::scene::AffineLayerParameters parameters;
	RequireError([&] {
		static_cast<void>(parameters.Set(static_cast<sgre::scene::AffineScalar>(255), 13));
	});
	RequireError([&] {
		static_cast<void>(parameters.SetFlip(static_cast<sgre::scene::AffineAxis>(255), true));
	});
	RequireError([&] {
		static_cast<void>(parameters.SetPivot(static_cast<sgre::scene::AffinePivot>(255), sgre::scene::AffineAxis::X, 13));
	});
	RequireError([&] {
		static_cast<void>(parameters.GetPivot(sgre::scene::AffinePivot::Rotation, static_cast<sgre::scene::AffineAxis>(255)));
	});
	Require(parameters.State().PositionX == 0 && !parameters.State().FlipX && !parameters.State().ZoomCenterX.Enabled, "selector failure preserves scalar state");
}

int main() {
	try {
		CheckInvalidation();
		CheckScaleUnits();
		CheckPivotsAndAssignment();
		CheckSelectors();
		std::cout << "Four affine parameter fixture groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
