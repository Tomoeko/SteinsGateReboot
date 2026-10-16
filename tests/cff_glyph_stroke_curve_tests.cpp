#include "sgre/text/cff_glyph_stroke_curve.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view diagnostic) {
	if (!condition) {
		throw std::runtime_error(std::string(diagnostic));
	}
}

static void Compare(double actual, double expected) {
	const auto scale = std::max({1.0, std::abs(actual), std::abs(expected)});
	Require(std::isfinite(actual) && std::abs(actual - expected) <= 16 * std::numeric_limits<double>::epsilon() * scale, "CFF native curve control exceeds the preselected sixteen-double-epsilon scale bound");
}

static std::uint32_t Read32(std::istream& input) {
	std::array<unsigned char, 4> bytes{};
	Require(static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()), 4)), "truncated native stroke word");

	return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

static double ReadDouble(std::istream& input) {
	const auto low = Read32(input);
	const auto high = Read32(input);

	return std::bit_cast<double>(std::uint64_t(low) | (std::uint64_t(high) << 32));
}

static void TestIndependentLoop() {
	const std::array<sgre::text::CffGlyphPathPoint, 4> cubic{{{2, 5}, {2, 1}, {10, 1}, {10, 5}}};
	const auto result = sgre::text::PrepareCffGlyphStrokeCurve(cubic, 4);
	Require(result.Left.size() == 10 && result.Right.size() == 10, "actual native width-four loop half retains ten paired offset quadratics");
	Compare(result.StartNormal.X, 1);
	Compare(result.StartNormal.Y, 0);
	Compare(result.Left.front().Points[0].X, 4);
	Compare(result.Left.front().Points[0].Y, 4.790417186481232);
	Compare(result.Right.front().Points[0].X, 0);
	Compare(result.Right.front().Points[0].Y, 3.9947677176247143);
	Compare(result.Left.back().Points[1].X, 8);
	Compare(result.Left.back().Points[1].Y, 5);
	Compare(result.Right.back().Points[1].X, 12);
	Compare(result.Right.back().Points[1].Y, 5);
}

static void TestOriginalEntryNormalSurvivesAdaptiveSplit() {
	const std::array<sgre::text::CffGlyphPathPoint, 4> incoming{{{19.96875, 9.71875}, {19.65625, 8.3125}, {19.34375, 7.5}, {18.703125, 6.640625}}};
	const std::array<sgre::text::CffGlyphPathPoint, 4> outgoing{{{18.703125, 6.640625}, {17.421875, 4.921875}, {15.1875, 4}, {12.21875, 4}}};
	const auto first = sgre::text::PrepareCffGlyphStrokeCurve(incoming, 4);
	const auto second = sgre::text::PrepareCffGlyphStrokeCurve(outgoing, 4);
	Require(second.Left.size() == 3 && second.Right.size() == 3, "actual native source tangent is retained across the three-piece adaptive offset");
	Require(std::bit_cast<std::uint64_t>(second.StartNormal.X) == 0x3fe9a7e6f114817dULL && std::bit_cast<std::uint64_t>(second.StartNormal.Y) == 0xbfe320128e7a5738ULL, "native cubic entry keeps exact source normal words before the adaptive split");
	Require(std::bit_cast<std::uint64_t>(first.EndNormal.X) == std::bit_cast<std::uint64_t>(second.StartNormal.X) && std::bit_cast<std::uint64_t>(first.EndNormal.Y) == std::bit_cast<std::uint64_t>(second.StartNormal.Y), "native same-tangent comparison must suppress a redundant cubic join and two inner lines");
}

static void TestNativeZeroControlTangents() {
	const std::array<sgre::text::CffGlyphPathPoint, 4> zero_first{{{8, 8}, {8, 8}, {12, 2}, {16, 8}}};
	const auto native_width_one = sgre::text::PrepareCffGlyphStrokeCurve(zero_first, 1);
	Compare(native_width_one.Left.front().Points[0].X, 8.6694305863200078);
	Compare(native_width_one.Left.front().Points[0].Y, 7.897241939385985);
	const auto first = sgre::text::PrepareCffGlyphStrokeCurve(zero_first, 4);
	Require(!first.Left.empty() && first.Left.size() == first.Right.size(), "native zero first control uses the other tangent and retains paired quadratics");
	Compare(first.StartNormal.X, 6.0 / std::sqrt(52.0));
	Compare(first.StartNormal.Y, 4.0 / std::sqrt(52.0));
	const std::array<sgre::text::CffGlyphPathPoint, 4> zero_last{{{8, 8}, {8, 2}, {16, 2}, {16, 2}}};
	const auto last = sgre::text::PrepareCffGlyphStrokeCurve(zero_last, 4);
	Require(!last.Right.empty() && std::isfinite(last.EndNormal.X) && std::isfinite(last.EndNormal.Y), "native zero last control preserves a finite completed normal from the other tangent");
}

static void TestBoundedFailures() {
	const std::array<sgre::text::CffGlyphPathPoint, 4> cubic{{{2, 5}, {2, 1}, {10, 1}, {10, 5}}};
	bool rejected = false;
	try {
		static_cast<void>(sgre::text::PrepareCffGlyphStrokeCurve(cubic, std::numeric_limits<double>::infinity()));
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "nonfinite width must fail before curve preparation");
	auto invalid = cubic;
	invalid[1].X = std::numeric_limits<double>::quiet_NaN();
	rejected = false;
	try {
		static_cast<void>(sgre::text::PrepareCffGlyphStrokeCurve(invalid, 4));
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "nonfinite actual translated control must fail before curve preparation");
	Require(sgre::text::PrepareCffGlyphStrokeCurve(cubic, 4).Left.size() == 10, "failed pure preparation cannot damage a later actual input");
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(static_cast<bool>(input), "native stroke curve vector input is unavailable");
	Require(Read32(input) == 0x31475343, "native stroke curve vector magic differs");
	const auto count = Read32(input);
	Require(count > 0 && count <= 1024, "native stroke case count exceeds its bounded domain");
	std::size_t words = 0;
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto width = ReadDouble(input);
		const auto cubic_count = Read32(input);
		Require(cubic_count > 0 && cubic_count <= 64, "native stroke case has invalid cubic count");
		sgre::text::CffStrokeCurveSides combined;
		for (std::uint32_t cubic_index = 0; cubic_index < cubic_count; ++cubic_index) {
			std::array<sgre::text::CffGlyphPathPoint, 4> cubic{};
			for (auto& point : cubic) {
				point.X = ReadDouble(input);
				point.Y = ReadDouble(input);
			}
			auto result = sgre::text::PrepareCffGlyphStrokeCurve(cubic, width);
			combined.Left.insert(combined.Left.end(), result.Left.begin(), result.Left.end());
			combined.Right.insert(combined.Right.end(), result.Right.begin(), result.Right.end());
		}
		for (const auto* side : {&combined.Left, &combined.Right}) {
			Require(Read32(input) == side->size(), "whole native offset-quadratic count differs");
			for (const auto& command : *side) {
				Require(command.Kind == sgre::text::CffGlyphPathCommandKind::QuadraticTo, "native curved segment must retain quadratic output");
				for (std::size_t point = 0; point < 2; ++point) {
					Compare(command.Points[point].X, ReadDouble(input));
					Compare(command.Points[point].Y, ReadDouble(input));
					words += 2;
				}
			}
		}
	}
	Require(input.peek() == std::char_traits<char>::eof(), "native stroke curve input has trailing data");
	std::cout << "Native stroke curves cases=" << count << " double words=" << words << '\n';
}

int main(int argc, char** argv) {
	try {
		TestIndependentLoop();
		TestOriginalEntryNormalSurvivesAdaptiveSplit();
		TestNativeZeroControlTangents();
		TestBoundedFailures();
		if (argc == 2) {
			ReplayNative(argv[1]);
		} else {
			Require(argc == 1, "expected optional native curve vector path");
		}
		std::cout << "CFF stroke curve fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
