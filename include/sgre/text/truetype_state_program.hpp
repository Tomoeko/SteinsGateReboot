#pragma once

#include "sgre/text/truetype_control_program.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::text {

	enum class TrueTypeRoundMode : std::uint8_t {
		HalfGrid = 0,
		Grid = 1,
		DoubleGrid = 2,
		DownToGrid = 3,
		UpToGrid = 4,
		Off = 5,
		Super = 6,
		Super45 = 7
	};

	struct TrueTypeAxisVector {
		std::int16_t X = 0x4000;
		std::int16_t Y = 0;
	};

	struct TrueTypeGraphicsState {
		std::array<std::uint16_t, 3> ReferencePoints{};
		TrueTypeAxisVector DualProjection;
		TrueTypeAxisVector Projection;
		TrueTypeAxisVector Freedom;
		std::int32_t Loop = 1;
		std::int32_t MinimumDistance = 64;
		TrueTypeRoundMode Round = TrueTypeRoundMode::Grid;
		bool AutoFlip = true;
		std::int32_t ControlValueCutIn = 68;
		std::int32_t SingleWidthCutIn = 0;
		std::int32_t SingleWidth = 0;
		std::uint16_t DeltaBase = 9;
		std::uint16_t DeltaShift = 3;
		std::uint8_t InstructionControl = 0;
		bool ScanControl = false;
		std::int32_t ScanType = 0;
		std::array<std::uint16_t, 3> ZonePointers{1, 1, 1};
		std::int32_t RoundPeriod = 64;
		std::int32_t RoundPhase = 0;
		std::int32_t RoundThreshold = 0;
	};

	struct TrueTypeInterpreterMetrics {
		std::uint16_t XPpem = 0;
		std::uint16_t YPpem = 0;
		std::uint16_t CurrentPpem = 0;
		std::int32_t PointSize = 0;
		std::int32_t Scale16Dot16 = 0;
		std::array<std::int32_t, 4> Compensation{};
		std::uint32_t InterpreterVersion = 40;
		bool FaceIsTricky = false;
		bool Rotated = false;
		bool Stretched = false;
		bool HasVariations = false;
		bool Grayscale = false;
		bool Version40Flag372 = false;
		bool Version40Flag376 = false;
	};

	// Storage and already-scaled CVT words persist across preparation/glyph calls.
	// This state slice owns no points, outline, font bytes, or raster admission.
	struct TrueTypeInterpreterState {
		std::vector<std::int32_t> Storage;
		std::vector<std::int32_t> ControlValues;
		TrueTypeGraphicsState Graphics;
		TrueTypeInterpreterMetrics Metrics;
		bool BackwardCompatibility = false;
	};

	[[nodiscard]] TrueTypeControlProgramResult ExecuteTrueTypeStateProgram(const TrueTypeFontProgram& font, TrueTypeProgramCode code, TrueTypeProgramRange entry, TrueTypeInterpreterState& state, bool strict_stack_underflow = false, std::span<const std::int32_t> initial_stack = {});

} // namespace sgre::text
