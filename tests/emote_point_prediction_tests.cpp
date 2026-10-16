#include "sgre/scene/emote_point_prediction.hpp"

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

static void RequireNear(float actual, float expected, std::string_view operation) {
	if (!std::isfinite(actual) || std::abs(actual - expected) > 0.00001f) {
		throw std::runtime_error(std::string(operation) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
	}
}

template <class Operation>
static void RequireThrows(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		if (std::string_view(error.what()).find(expected) != std::string_view::npos) {
			return;
		}
		throw;
	}
	throw std::runtime_error("expected actual prediction-domain rejection");
}

static void TestActualHistoryWarmupAndDegreeFallbacks() {
	const std::array<std::array<float, 4>, 4> expected{{{1, 4, 9, 16}, {1, 7, 14, 23}, {1, 7, 16, 25}, {1, 7, 16, 25}}};
	for (std::uint32_t degree = 0; degree != 4; ++degree) {
		sgre::scene::EmotePointPrediction owner(degree, 1);
		std::array<sgre::resource::MotionPoint, 1> point{{{50, -50}}};
		sgre::resource::MotionMeshMatrix identity;
		owner.Advance(60, true, identity, point);
		RequireNear(point[0].X, 50, "actual reactivation clears timing without replacing current fix point");
		for (std::size_t sample = 0; sample != 4; ++sample) {
			const auto value = static_cast<float>((sample + 1) * (sample + 1));
			point[0] = {value, -value};
			owner.Advance(60, false, identity, point);
			RequireNear(point[0].X, expected[degree][sample], "actual native degree falls back until every required elapsed slot exists");
			RequireNear(point[0].Y, -expected[degree][sample], "prediction retains independent actual coordinate lanes");
		}
	}
}

static void TestCubicIsNativeFiniteDifferenceNotLinear() {
	sgre::scene::EmotePointPrediction owner(3, 1);
	std::array<sgre::resource::MotionPoint, 1> point{};
	sgre::resource::MotionMeshMatrix identity;
	const std::array<float, 4> inputs{0, 1, 8, 27};
	const std::array<float, 4> expected{0, 2, 21, 64};
	for (std::size_t sample = 0; sample != inputs.size(); ++sample) {
		point[0] = {inputs[sample], inputs[sample]};
		owner.Advance(60, false, identity, point);
		RequireNear(point[0].X, expected[sample], "actual cubic velocity differences follow native warmup and arithmetic");
	}
	point[0] = {100, 100};
	owner.Advance(0, false, identity, point);
	RequireNear(point[0].X, 100, "zero actual elapsed forces native degree0 fallback without division");
}

static void TestActualPostureCoordinatesAndIndependentPoints() {
	sgre::scene::EmotePointPrediction owner(1, 2);
	sgre::resource::MotionMeshMatrix first;
	first.TranslationX = -10;
	first.TranslationY = -20;
	std::array<sgre::resource::MotionPoint, 2> points{{{11, 22}, {8, 24}}};
	owner.Advance(60, false, first, points);
	RequireNear(points[0].X, 11, "first actual sample returns through inverse posture");
	sgre::resource::MotionMeshMatrix second;
	second.XX = 0;
	second.XY = 1;
	second.TranslationX = -20;
	second.YX = -1;
	second.YY = 0;
	second.TranslationY = 10;
	points = {{{8, 21}, {6, 18}}};
	owner.Advance(60, false, second, points);
	RequireNear(points[0].X, 8, "stationary normalized fix follows actual rotated posture without invented velocity");
	RequireNear(points[0].Y, 21, "normalized histories are compared before inverse current posture");
	RequireNear(points[1].X, 6, "second actual fix has separate retained history");
	RequireNear(points[1].Y, 18, "all actual points return to current world coordinates");
	second.YY = 0;
	second.YX = 0;
	RequireThrows([&] {
		owner.Advance(60, false, second, points);
	},
	    "invertible actual posture matrix");
	RequireThrows([&] {
		owner.Advance(60, false, first, points);
	},
	    "valid actual point count");
}

static std::uint32_t ReadWord(std::istream& input) {
	std::array<char, 4> bytes{};
	if (!input.read(bytes.data(), bytes.size())) {
		throw std::runtime_error("truncated original point-prediction vector");
	}
	return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 8) | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3])) << 24);
}

static float ReadFloat(std::istream& input) {
	return std::bit_cast<float>(ReadWord(input));
}

static void TestOriginalInstructionVectors(const char* path) {
	std::ifstream input(path, std::ios::binary);
	std::array<char, 4> magic{};
	if (!input.read(magic.data(), magic.size()) || magic != std::array<char, 4>{'E', 'V', 'P', '1'}) {
		throw std::runtime_error("invalid original point-prediction vector header");
	}
	const auto case_count = ReadWord(input);
	if (case_count == 0 || case_count > 64) {
		throw std::runtime_error("original point-prediction case count exceeds bound");
	}
	std::size_t words = 0;
	for (std::uint32_t index = 0; index != case_count; ++index) {
		const auto degree = ReadWord(input);
		const auto count = ReadWord(input);
		const auto steps = ReadWord(input);
		if (count == 0 || count > 3 || steps == 0 || steps > 16) {
			throw std::runtime_error("original point-prediction step extent exceeds bound");
		}
		sgre::scene::EmotePointPrediction owner(degree, count);
		for (std::uint32_t step = 0; step != steps; ++step) {
			const auto ticks = ReadFloat(input);
			const auto reset = ReadWord(input);
			if (reset > 1) {
				throw std::runtime_error("original point-prediction reset flag is not Boolean");
			}
			sgre::resource::MotionMeshMatrix normalization;
			normalization.XX = ReadFloat(input);
			normalization.XY = ReadFloat(input);
			normalization.TranslationX = ReadFloat(input);
			normalization.YX = ReadFloat(input);
			normalization.YY = ReadFloat(input);
			normalization.TranslationY = ReadFloat(input);
			std::array<sgre::resource::MotionPoint, 3> points{};
			for (std::uint32_t point = 0; point != count; ++point) {
				points[point].X = ReadFloat(input);
				points[point].Y = ReadFloat(input);
			}
			owner.Advance(ticks, reset != 0, normalization, std::span(points).first(count));
			for (std::uint32_t point = 0; point != count; ++point) {
				for (const auto value : {points[point].X, points[point].Y}) {
					const auto expected = ReadWord(input);
					if (std::bit_cast<std::uint32_t>(value) != expected) {
						throw std::runtime_error("original point-prediction mismatch case=" + std::to_string(index) + ", step=" + std::to_string(step) + ", point=" + std::to_string(point) + ", expected=" + std::to_string(std::bit_cast<float>(expected)) + ", actual=" + std::to_string(value));
					}
					++words;
				}
			}
		}
	}
	if (input.peek() != std::char_traits<char>::eof()) {
		throw std::runtime_error("trailing original point-prediction vector bytes");
	}
	std::cout << "Original point prediction: cases=" << case_count << ", exact words=" << words << '\n';
}

int main(int argc, char** argv) {
	try {
		TestActualHistoryWarmupAndDegreeFallbacks();
		TestCubicIsNativeFiniteDifferenceNotLinear();
		TestActualPostureCoordinatesAndIndependentPoints();
		if (argc == 2) {
			TestOriginalInstructionVectors(argv[1]);
		} else if (argc != 1) {
			throw std::runtime_error("emote_point_prediction_tests [ORIGINAL_VECTOR]");
		}
		RequireThrows([] {
			sgre::scene::EmotePointPrediction unknown(4, 1);
		},
		    "degree0..3");
		RequireThrows([] {
			sgre::scene::EmotePointPrediction too_many(3, 4);
		},
		    "three actual fixes");
		std::cout << "Native Velvety fix-point prediction: actual history warmup, linear/quadratic/cubic differences, posture normalization and guards passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
