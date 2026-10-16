#pragma once

#include "sgre/text/cff_glyph_path.hpp"

#include <optional>
#include <vector>

namespace sgre::text {

	struct StrokeRoundJoinInput {
		CffGlyphPathPoint Center;
		CffGlyphPathPoint Start;
		CffGlyphPathPoint End;
		CffGlyphPathPoint IncomingNormal;
		CffGlyphPathPoint IntersectionOffset;
		double SignedRadius = 0;
		double SignedWidth = 0;
	};

	struct StrokeContourSides {
		std::vector<CffGlyphPathCommand> Left;
		std::vector<CffGlyphPathCommand> Right;
	};

	struct StrokeSegmentJoinInput {
		CffGlyphPathPoint Center;
		// Actual next LINE endpoint; curve joins do not use the line-only inner intersection test.
		CffGlyphPathPoint NextTangentEnd;
		CffGlyphPathPoint IncomingNormal;
		CffGlyphPathPoint OutgoingNormal;
		double Width = 0;
	};

	// Native435451: a short segment is skipped before normal computation.
	[[nodiscard]] std::optional<CffGlyphPathPoint> PrepareStrokeLineNormal(CffGlyphPathPoint start, CffGlyphPathPoint end);
	// Native round4 outer join emits one or two independent cubic commands.
	[[nodiscard]] std::vector<CffGlyphPathCommand> PrepareRoundStrokeJoin(const StrokeRoundJoinInput& input);
	// Native cap2 uses the actual center and ordered side endpoints, including its truncated cubic weight.
	[[nodiscard]] std::vector<CffGlyphPathCommand> PrepareRoundStrokeCap(CffGlyphPathPoint center, CffGlyphPathPoint start, CffGlyphPathPoint end);
	// Curve continuation keeps the inner center and next offset-start. The caller appends its real curve geometry.
	void AppendStrokeSegmentJoin(StrokeContourSides& sides, const StrokeSegmentJoinInput& input);
	// Adds an actual LINE continuation, including native same-normal endpoint folding. Returns false for a skipped short segment.
	[[nodiscard]] bool AppendStrokeLineSegment(StrokeContourSides& sides, CffGlyphPathPoint center, CffGlyphPathPoint end, CffGlyphPathPoint incoming_normal, double width);

} // namespace sgre::text
