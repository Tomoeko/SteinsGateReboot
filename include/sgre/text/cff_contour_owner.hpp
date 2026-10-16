#pragma once

#include "sgre/text/cff_charstring_mask.hpp"
#include "sgre/text/cff_pending_contour.hpp"
#include "sgre/text/cff_stem_declarations.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::text {

	enum class CffContourCallbackKind : std::uint8_t {
		Move = 1,
		Line = 2,
		Cubic = 4
	};

	struct CffContourCallback {
		CffContourCallbackKind Kind = CffContourCallbackKind::Move;
		// Move/line use from/to in slots 0/1; cubic uses from/control1/control2/end.
		std::array<CffFixedPoint, 4> Points{};
	};

	struct CffContourOwnerStep {
		std::array<CffContourCallback, 5> Callbacks{};
		std::size_t CallbackCount = 0;
	};

	struct CffContourConfiguration {
		CffBlueZones BlueZones;
		CffHintPointTransform Transform;
		CffContourDarkeningState Darkening;
		CffContourJunctionLimits JunctionLimits;
		std::int32_t BaseScale16Dot16 = 65536;
		std::int32_t VerticalOrigin16Dot16 = 0;
		bool HintingEnabled = true;
	};

	struct CffContourOwnerState {
		CffContourConfiguration Configuration;
		CffDeclaredStemBuffer HorizontalStems;
		std::size_t VerticalStemCount = 0;
		CffHintMask Mask;
		CffHintMap InitialMap;
		CffHintMap CurrentMap;
		CffHintMap FirstContourMap;
		CffPendingContourSegment Pending;
		CffFixedPoint CurrentLogicalPoint;
		CffFixedPoint FirstLogicalPoint;
		CffFixedPoint FirstDarkenedStart;
		CffFixedPoint FirstDarkenedDirectionPoint;
		bool ContourOpen = false;
		bool ClosingContour = false;
		bool FirstSegment = true;
		bool HasPendingSegment = false;
	};

	class CffContourOwner {
	public:
		// Prepared stems are copied; no input span or font resource lifetime is retained.
		explicit CffContourOwner(const CffContourConfiguration& configuration, std::span<const CffStemHint> horizontal_stems = {}, std::size_t vertical_stem_count = 0, CffHintMask mask = {});
		[[nodiscard]] CffContourOwnerStep MoveTo(CffFixedPoint point);
		[[nodiscard]] CffContourOwnerStep LineTo(CffFixedPoint point);
		[[nodiscard]] CffContourOwnerStep CubicTo(CffFixedPoint first_control, CffFixedPoint second_control, CffFixedPoint endpoint);
		[[nodiscard]] CffContourOwnerStep CloseContour();
		void AppendHorizontalStems(std::span<const CffStemHint> stems);
		void AppendVerticalStemCount(std::size_t count);
		[[nodiscard]] std::size_t UpdateHintMask(std::span<const std::uint8_t> program_bytes);
		[[nodiscard]] CffCounterMaskPreparation ApplyCounterMask(std::span<const std::uint8_t> program_bytes);
		[[nodiscard]] const CffContourOwnerState& State() const;

	private:
		void RebuildCurrentMap();
		void BeginContour(CffFixedPoint start, CffContourOwnerStep& step);
		CffFixedPoint FlushPending(CffFixedPoint next_start, CffFixedPoint next_end, bool closing, CffContourOwnerStep& step);

		CffContourOwnerState m_State;
	};

} // namespace sgre::text
