#pragma once

#include "sgre/resource/cff_outline.hpp"

#include <cstdint>
#include <vector>

namespace sgre::resource {

	enum class CffPointKind : std::uint8_t {
		OnCurve = 1,
		CubicControl = 2
	};

	struct CffDesignPoint {
		std::int32_t X = 0;
		std::int32_t Y = 0;
		CffPointKind Kind = CffPointKind::OnCurve;
	};

	struct CffDesignOutline {
		std::vector<CffDesignPoint> Points;
		// Inclusive point indices preserve native contour order and closure.
		std::vector<std::uint16_t> ContourEnds;
	};

	// Recovers the native no-hint builder's point tags and contour normalization.
	// The supplied font corpus has integral design coordinates; fractional inputs remain unsupported.
	[[nodiscard]] CffDesignOutline PrepareCffDesignOutline(const CffOutline& outline);

} // namespace sgre::resource
