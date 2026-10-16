#include "sgre/scene/motion_batch_flush.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::resource::MotionPoint;
using sgre::scene::MotionBatchCounts;
using sgre::scene::MotionBatchMode;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected failure: " + std::string(expected));
}

static void TestStripAndListViews() {
	std::array<MotionPoint, 6> positions{};
	std::array<MotionPoint, 8> uvs{};
	std::array<std::uint32_t, 7> colors{};
	constexpr std::array<std::uint32_t, 6> indices{0, 0, 2, 1, 3, 3};
	const auto strip = sgre::scene::PrepareMotionBatchFlush(MotionBatchMode::TriangleStrip, 0x80000001, positions, uvs, colors, indices);
	Require(strip.Submission && strip.Submission->Key == 0x80000001 && strip.Submission->Positions.size() == 4 && strip.Submission->Positions.data() == positions.data() + 1 && strip.Submission->Uvs.data() == uvs.data() + 1 && strip.Submission->Colors.data() == colors.data() + 1 && strip.Submission->Indices.empty() && strip.RetainedCounts == MotionBatchCounts{}, "unindexed native strip did not remove all three streams' outer entries or preserve its key");
	const auto indexed = sgre::scene::PrepareMotionBatchFlush(MotionBatchMode::IndexedTriangleStrip, 17, std::span(positions).first(4), uvs, colors, indices);
	Require(indexed.Submission && indexed.Submission->Positions.size() == 4 && indexed.Submission->Positions.data() == positions.data() && indexed.Submission->Indices.size() == 4 && indexed.Submission->Indices.data() == indices.data() + 1 && indexed.Submission->Indices[1] == 2 && indexed.RetainedCounts == MotionBatchCounts{}, "indexed native strip removed vertices instead of its outer duplicate indices");
	const auto list = sgre::scene::PrepareMotionBatchFlush(MotionBatchMode::TriangleList, 23, positions, uvs, colors, indices);
	Require(list.Submission && list.Submission->Positions.size() == 6 && list.Submission->Uvs.size() == 6 && list.Submission->Colors.size() == 6 && list.Submission->Indices.data() == nullptr, "unindexed native list retained surplus streams or used staged indices");
	const auto indexed_list = sgre::scene::PrepareMotionBatchFlush(MotionBatchMode::IndexedTriangleList, 29, positions, uvs, colors, indices);
	Require(indexed_list.Submission && indexed_list.Submission->Indices.data() == indices.data() && indexed_list.Submission->Indices.size() == 6, "indexed native list trimmed its original index range");
}

static void TestEmptyAndShortStripPhases() {
	std::array<MotionPoint, 2> points{};
	std::array<std::uint32_t, 2> words{};
	const auto empty = sgre::scene::PrepareMotionBatchFlush(static_cast<MotionBatchMode>(0), 0, {}, points, words, words);
	MotionBatchCounts retained;
	retained.Uvs = 2;
	retained.Colors = 2;
	retained.Indices = 2;
	Require(!empty.Submission && empty.RetainedCounts == retained, "empty positions changed unrelated active streams or inspected an unused topology");
	const auto zero_draw = sgre::scene::PrepareMotionBatchFlush(MotionBatchMode::TriangleStrip, 0, points, points, words, {});
	Require(zero_draw.Submission && zero_draw.Submission->Positions.empty() && zero_draw.Submission->Positions.data() == points.data() + 1, "two strip sentinels must still produce a submission with zero selected vertices");
	const auto zero_indices = sgre::scene::PrepareMotionBatchFlush(MotionBatchMode::IndexedTriangleStrip, 0, points, points, words, words);
	Require(zero_indices.Submission && zero_indices.Submission->Indices.empty() && zero_indices.Submission->Indices.data() == words.data() + 1, "two index sentinels lost the original nonnull selected pointer");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareMotionBatchFlush(MotionBatchMode::TriangleStrip, 0, std::span(points).first(1), points, words, {}));
	},
	    "two outer duplicate entries");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareMotionBatchFlush(MotionBatchMode::IndexedTriangleStrip, 0, points, points, words, {}));
	},
	    "two outer duplicate indices");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareMotionBatchFlush(MotionBatchMode::TriangleList, 0, points, {}, words, {}));
	},
	    "parallel vertex streams");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareMotionBatchFlush(static_cast<MotionBatchMode>(0), 0, points, points, words, {}));
	},
	    "not recovered");
}

static std::uint32_t ReadWord(std::ifstream& input) {
	std::array<char, 4> bytes{};
	input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(input.good(), "bounded motion batch oracle is truncated");

	return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[0])) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 8) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3])) << 24);
}

static void TestNativeReplay(const char* input_path, const char* output_path) {
	std::ifstream input(input_path, std::ios::binary);
	std::ifstream expected(output_path, std::ios::binary);
	Require(input.is_open() && expected.is_open(), "cannot open bounded motion batch oracle");
	for (std::size_t fixture = 0; fixture < 1000; ++fixture) {
		const auto mode = ReadWord(input);
		const auto position_count = ReadWord(input);
		const auto uv_count = ReadWord(input);
		const auto color_count = ReadWord(input);
		const auto index_count = ReadWord(input);
		const auto key = ReadWord(input);
		Require(mode >= 1 && mode <= 4 && position_count <= 64 && uv_count <= 68 && color_count <= 68 && index_count <= 128, "motion batch oracle exceeds its fixture bounds");
		std::vector<MotionPoint> positions(position_count);
		std::vector<MotionPoint> uvs(uv_count);
		std::vector<std::uint32_t> colors(color_count);
		std::vector<std::uint32_t> indices;
		indices.reserve(128);
		indices.resize(index_count);
		const auto plan = sgre::scene::PrepareMotionBatchFlush(static_cast<MotionBatchMode>(mode), key, positions, uvs, colors, indices);
		Require(plan.Submission.has_value() == (ReadWord(expected) != 0), "native motion batch submission phase differs at fixture " + std::to_string(fixture));
		std::array<std::uint32_t, 13> native{};
		for (auto& word : native) {
			word = ReadWord(expected);
		}

		if (plan.Submission) {
			const auto& submission = *plan.Submission;
			Require(static_cast<std::uint32_t>(submission.Mode) == native[0] && submission.Key == native[1] && submission.Positions.size() == native[2], "native motion batch scalar arguments differ");
			Require(static_cast<std::size_t>(submission.Positions.data() - positions.data()) == native[3] && static_cast<std::size_t>(submission.Uvs.data() - uvs.data()) == native[4] && static_cast<std::size_t>(submission.Colors.data() - colors.data()) == native[5], "native motion batch parallel stream offsets differ");
			Require(submission.Indices.size() == native[6] && (submission.Indices.data() != nullptr) == (native[7] != 0), "native motion batch index count or pointer presence differs");
			if (submission.Indices.data()) {
				Require(static_cast<std::size_t>(submission.Indices.data() - indices.data()) == native[8], "native motion batch index offset differs");
			}

			Require(position_count == native[9] && uv_count == native[10] && color_count == native[11] && index_count == native[12], "native batch streams were already reset during its callback");
		}

		Require(plan.RetainedCounts.Positions == ReadWord(expected) && plan.RetainedCounts.Uvs == ReadWord(expected) && plan.RetainedCounts.Colors == ReadWord(expected) && plan.RetainedCounts.Indices == ReadWord(expected), "native motion batch retained stream counts differ");
	}

	Require(input.peek() == std::ifstream::traits_type::eof() && expected.peek() == std::ifstream::traits_type::eof(), "motion batch oracle has trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 3, "usage: motion_batch_flush_tests [native_input native_output]");
		TestStripAndListViews();
		TestEmptyAndShortStripPhases();
		if (argc == 3) {
			TestNativeReplay(argv[1], argv[2]);
		}

		std::cout << "motion batch flush fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
