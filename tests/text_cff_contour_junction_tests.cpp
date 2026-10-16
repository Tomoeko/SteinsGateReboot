#include "sgre/text/cff_contour_junction.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::text::CffContourJunctionSegments AxisJunction() {
	sgre::text::CffContourJunctionSegments segments;
	segments.PreviousEnd.X16Dot16 = 10 * 65536;
	segments.NextStart.X16Dot16 = 10 * 65536 + 123;
	segments.NextStart.Y16Dot16 = -2 * 65536;
	segments.NextEnd.X16Dot16 = segments.NextStart.X16Dot16;
	segments.NextEnd.Y16Dot16 = 2 * 65536;

	return segments;
}

static void CheckCandidateAndDistanceGate() {
	const auto segments = AxisJunction();
	sgre::text::CffContourJunctionLimits limits;
	limits.MaximumMidpointDistance16Dot16 = 65536;
	const auto accepted = sgre::text::FindCffContourJunction(segments, limits);
	Require(accepted.Accepted && accepted.Candidate && accepted.Candidate->X16Dot16 == segments.NextStart.X16Dot16 && accepted.Candidate->Y16Dot16 == 0, "axis-aligned joining must snap the reduced-vector candidate and accept the inclusive midpoint limit");
	limits.MaximumMidpointDistance16Dot16 = 65535;
	const auto rejected = sgre::text::FindCffContourJunction(segments, limits);
	Require(!rejected.Accepted && rejected.Candidate && rejected.Candidate->X16Dot16 == segments.NextStart.X16Dot16 && rejected.Candidate->Y16Dot16 == 0, "a distance rejection must retain the computed candidate for native pending-path decisions");
}

static void CheckStrictAxisSnapBoundary() {
	const auto segments = AxisJunction();
	sgre::text::CffContourJunctionLimits limits;
	limits.MaximumMidpointDistance16Dot16 = 2 * 65536;
	limits.AxisSnapTolerance16Dot16 = 137;
	const auto unsnapped = sgre::text::FindCffContourJunction(segments, limits);
	Require(unsnapped.Accepted && unsnapped.Candidate && unsnapped.Candidate->X16Dot16 == 655620, "axis snapping must retain the original strict less-than boundary");
	limits.AxisSnapTolerance16Dot16 = 138;
	const auto snapped = sgre::text::FindCffContourJunction(segments, limits);
	Require(snapped.Accepted && snapped.Candidate && snapped.Candidate->X16Dot16 == 655483, "a candidate just inside the axis tolerance must snap exactly");
}

static void CheckParallelAndInvalidLimits() {
	sgre::text::CffContourJunctionSegments segments;
	segments.PreviousEnd.X16Dot16 = 65536;
	segments.NextStart.Y16Dot16 = 65536;
	segments.NextEnd.X16Dot16 = 65536;
	segments.NextEnd.Y16Dot16 = 65536;
	const auto parallel = sgre::text::FindCffContourJunction(segments);
	Require(!parallel.Accepted && !parallel.Candidate, "parallel reduced vectors must not synthesize a candidate");
	sgre::text::CffContourJunctionLimits limits;
	limits.AxisSnapTolerance16Dot16 = -1;
	bool failed = false;
	try {
		(void)sgre::text::FindCffContourJunction(segments, limits);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	Require(failed, "negative prepared junction tolerances must fail before numeric work");
}

int main() {
	try {
		CheckCandidateAndDistanceGate();
		CheckStrictAxisSnapBoundary();
		CheckParallelAndInvalidLimits();
		std::cout << "CFF contour junction tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
