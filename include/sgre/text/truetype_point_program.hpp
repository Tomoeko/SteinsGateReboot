#pragma once

#include "sgre/text/truetype_state_program.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	struct TrueTypeHintPoint {
		std::int32_t X = 0;
		std::int32_t Y = 0;

		bool operator==(const TrueTypeHintPoint&) const = default;
	};

	struct TrueTypeHintZone {
		std::vector<TrueTypeHintPoint> Original;
		std::vector<TrueTypeHintPoint> Current;
		std::vector<TrueTypeHintPoint> Unscaled;
		std::vector<std::uint8_t> Tags;
		std::vector<std::uint16_t> ContourEnds;
		std::uint32_t FirstPoint = 0;
	};

	// Distinct twilight/glyph arrays persist across calls. Zone selectors refer
	// to these owners, preserving native aliases without copying selected zones.
	struct TrueTypePointState {
		std::array<TrueTypeHintZone, 2> Zones;
		std::int32_t OriginalXScale16Dot16 = 0;
		std::int32_t OriginalYScale16Dot16 = 0;
		bool Composite = false;
		bool IupX = false;
		bool IupY = false;
	};

	// Executes recovered point kernels on prepared owners. This does not perform
	// FT glyph initialization, hinting policy selection, or raster admission.
	[[nodiscard]] TrueTypeControlProgramResult ExecuteTrueTypePointProgram(const TrueTypeFontProgram& font, TrueTypeProgramCode code, TrueTypeProgramRange entry, TrueTypeInterpreterState& state, TrueTypePointState& points, bool strict_stack_underflow = false, std::span<const std::int32_t> initial_stack = {});

} // namespace sgre::text
