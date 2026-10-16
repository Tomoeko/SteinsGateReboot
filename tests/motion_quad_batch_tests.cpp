#include "sgre/scene/motion_quad_batch.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::resource::MotionPoint;
using sgre::scene::MotionBatchMode;
using sgre::scene::MotionBatchSubmission;
using sgre::scene::MotionQuadBatch;
using sgre::scene::MotionQuadBatchBuffers;
using sgre::scene::MotionQuadBatchInput;

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

struct FixtureStorage {
	std::array<MotionPoint, 256> Positions{};
	std::array<MotionPoint, 256> Uvs{};
	std::array<std::uint32_t, 256> Colors{};
	std::array<std::uint32_t, 256> Indices{};

	MotionQuadBatchBuffers Buffers(std::size_t count = 256) {
		MotionQuadBatchBuffers buffers;
		buffers.Positions = std::span(Positions).first(count);
		buffers.Uvs = std::span(Uvs).first(count);
		buffers.Colors = std::span(Colors).first(count);
		buffers.Indices = std::span(Indices).first(count);

		return buffers;
	}
};

static MotionQuadBatchInput Input(std::uint32_t key = 7) {
	MotionQuadBatchInput input;
	input.Key = key;
	input.PixelUvs.X = 32;
	input.PixelUvs.Y = 64;
	input.PixelUvs.Width = 128;
	input.PixelUvs.Height = 256;
	input.InverseTextureSize.X = 1.0f / 512;
	input.InverseTextureSize.Y = 1.0f / 1024;
	for (std::size_t index = 0; index < 4; ++index) {
		input.Positions[index].X = static_cast<float>(index) + 0.25f;
		input.Positions[index].Y = -static_cast<float>(index) - 0.5f;
		input.Colors[index] = 100 + static_cast<std::uint32_t>(index);
	}

	return input;
}

static void TestQuadModesAndConnections() {
	constexpr std::array<MotionBatchMode, 4> modes{MotionBatchMode::TriangleStrip, MotionBatchMode::IndexedTriangleStrip, MotionBatchMode::TriangleList, MotionBatchMode::IndexedTriangleList};
	for (const auto mode : modes) {
		FixtureStorage storage;
		MotionQuadBatch batch(mode, storage.Buffers());
		const auto input = Input();
		batch.Append(input, {});
		batch.Append(input, {});
		std::size_t calls = 0;
		batch.Flush([&](const MotionBatchSubmission& submission) {
			++calls;
			Require(batch.Key() == 7 && batch.Counts().Positions > 0 && submission.Key == 7, "batch key or staging ends changed before submission");
			if (mode == MotionBatchMode::TriangleStrip) {
				constexpr std::array<std::uint32_t, 10> colors{100, 102, 101, 103, 103, 100, 100, 102, 101, 103};
				Require(submission.Positions.size() == 10 && std::equal(colors.begin(), colors.end(), submission.Colors.begin(), submission.Colors.end()), "native unindexed strip lost internal degenerate connections");
			} else if (mode == MotionBatchMode::IndexedTriangleStrip) {
				constexpr std::array<std::uint32_t, 10> indices{0, 2, 1, 3, 3, 4, 4, 6, 5, 7};
				Require(submission.Positions.size() == 8 && std::equal(indices.begin(), indices.end(), submission.Indices.begin(), submission.Indices.end()), "native indexed strip lost row-major vertices or base-adjusted connections");
			} else if (mode == MotionBatchMode::TriangleList) {
				constexpr std::array<std::uint32_t, 12> colors{100, 101, 102, 102, 101, 103, 100, 101, 102, 102, 101, 103};
				Require(submission.Positions.size() == 12 && std::equal(colors.begin(), colors.end(), submission.Colors.begin(), submission.Colors.end()), "native unindexed triangle-list order differs");
			} else {
				constexpr std::array<std::uint32_t, 12> indices{0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
				Require(submission.Positions.size() == 8 && std::equal(indices.begin(), indices.end(), submission.Indices.begin(), submission.Indices.end()), "native indexed triangle-list base adjustment differs");
			}

			Require(submission.Uvs[0].X == 0.0625f && submission.Uvs[0].Y == 0.0625f, "native quad UV origin differs");
		});
		Require(calls == 1 && batch.Counts() == sgre::scene::MotionBatchCounts{} && batch.Key() == 7, "native batch did not reset counts after success or changed its retained key");
	}
}

static void TestKeyChangeFailureAndCapacity() {
	FixtureStorage storage;
	MotionQuadBatch batch(MotionBatchMode::TriangleStrip, storage.Buffers(6));
	const auto first = Input(7);
	const auto second = Input(9);
	RequireFailure([&] {
		MotionQuadBatch invalid(static_cast<MotionBatchMode>(0), storage.Buffers());
	},
	    "recovered batch mode");
	RequireFailure([&] {
		MotionQuadBatch short_buffers(MotionBatchMode::TriangleStrip, storage.Buffers(5));
	},
	    "fit one quad");
	batch.Append(first, {});
	auto malformed = second;
	malformed.PixelUvs.Width = std::bit_cast<float>(std::uint32_t{0x7fc12345});
	RequireFailure([&] {
		batch.Append(malformed, [&](const auto&) {
			throw std::runtime_error("invalid input submitted the previous key");
		});
	},
	    "finite pixel UVs");
	Require(batch.Key() == 7 && batch.Counts().Positions == 6, "quad validation mutated a queued batch before rejecting its input");
	RequireFailure([&] {
		batch.Append(first, {});
	},
	    "capacity");
	RequireFailure([&] {
		batch.Append(second, [&](const auto& submission) {
			Require(submission.Key == 7 && batch.Key() == 7 && batch.Counts().Positions == 6, "key change modified its prior batch before submission");
			throw std::runtime_error("fixture submission failure");
		});
	},
	    "fixture submission failure");
	Require(batch.Key() == 7 && batch.Counts().Positions == 6 && storage.Colors[0] == 100, "failed key-change flush consumed new input or destroyed the prior batch");
	std::size_t calls = 0;
	batch.Append(second, [&](const auto& submission) {
		++calls;
		Require(submission.Key == 7, "key-change retry submitted the new key too early");
		RequireFailure([&] {
			batch.Append(first, {});
		},
		    "during submission");
		RequireFailure([&] {
			batch.Flush({});
		},
		    "recursively");
	});
	Require(calls == 1 && batch.Key() == 9 && batch.Counts().Positions == 6, "successful key-change retry did not append the new quad");
	batch.Flush([&](const auto&) {
		++calls;
	});
	batch.Flush({});
	Require(calls == 2, "empty native batch submitted again");
}

static std::uint32_t ReadWord(std::ifstream& input) {
	std::array<char, 4> bytes{};
	input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(input.good(), "bounded motion quad oracle is truncated");

	return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[0])) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 8) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3])) << 24);
}

static void AppendPointWords(std::vector<std::uint32_t>& words, std::span<const MotionPoint> points) {
	for (const auto& point : points) {
		words.push_back(std::bit_cast<std::uint32_t>(point.X));
		words.push_back(std::bit_cast<std::uint32_t>(point.Y));
	}
}

static std::vector<std::uint32_t> SubmissionWords(const MotionBatchSubmission& submission) {
	std::vector<std::uint32_t> words{static_cast<std::uint32_t>(submission.Mode), submission.Key, static_cast<std::uint32_t>(submission.Positions.size()), static_cast<std::uint32_t>(submission.Indices.size())};
	AppendPointWords(words, submission.Positions);
	AppendPointWords(words, submission.Uvs);
	words.insert(words.end(), submission.Colors.begin(), submission.Colors.end());
	words.insert(words.end(), submission.Indices.begin(), submission.Indices.end());

	return words;
}

static void CompareSnapshot(std::ifstream& expected, const std::vector<std::vector<std::uint32_t>>& submissions, const MotionQuadBatch& batch, const FixtureStorage& storage) {
	Require(submissions.size() == ReadWord(expected), "native quad key-change submission count differs");
	for (const auto& submission : submissions) {
		for (const auto word : submission) {
			Require(word == ReadWord(expected), "native quad emitted submission word differs");
		}
	}

	const auto counts = batch.Counts();
	Require(batch.Key() == ReadWord(expected) && counts.Positions == ReadWord(expected) && counts.Uvs == ReadWord(expected) && counts.Colors == ReadWord(expected) && counts.Indices == ReadWord(expected), "native quad active key or stream ends differ");
	std::vector<std::uint32_t> words;
	AppendPointWords(words, std::span(storage.Positions).first(counts.Positions));
	AppendPointWords(words, std::span(storage.Uvs).first(counts.Uvs));
	words.insert(words.end(), storage.Colors.begin(), storage.Colors.begin() + static_cast<std::ptrdiff_t>(counts.Colors));
	words.insert(words.end(), storage.Indices.begin(), storage.Indices.begin() + static_cast<std::ptrdiff_t>(counts.Indices));
	for (const auto word : words) {
		Require(word == ReadWord(expected), "native quad active buffer word differs");
	}
}

static void TestNativeReplay(const char* input_path, const char* output_path) {
	std::ifstream input(input_path, std::ios::binary);
	std::ifstream expected(output_path, std::ios::binary);
	Require(input.is_open() && expected.is_open(), "cannot open bounded motion quad oracle");
	for (std::size_t fixture = 0; fixture < 1000; ++fixture) {
		const auto mode = ReadWord(input);
		const auto draws = ReadWord(input);
		MotionPoint inverse;
		inverse.X = std::bit_cast<float>(ReadWord(input));
		inverse.Y = std::bit_cast<float>(ReadWord(input));
		Require(mode >= 1 && mode <= 4 && draws >= 1 && draws <= 8, "motion quad oracle exceeds its fixture bounds");
		FixtureStorage storage;
		MotionQuadBatch batch(static_cast<MotionBatchMode>(mode), storage.Buffers());
		std::vector<std::vector<std::uint32_t>> submissions;
		const auto submit = [&](const auto& submission) {
			submissions.push_back(SubmissionWords(submission));
		};
		for (std::uint32_t draw = 0; draw < draws; ++draw) {
			MotionQuadBatchInput quad;
			quad.InverseTextureSize = inverse;
			quad.Key = ReadWord(input);
			quad.PixelUvs.X = std::bit_cast<float>(ReadWord(input));
			quad.PixelUvs.Y = std::bit_cast<float>(ReadWord(input));
			quad.PixelUvs.Width = std::bit_cast<float>(ReadWord(input));
			quad.PixelUvs.Height = std::bit_cast<float>(ReadWord(input));
			for (auto& point : quad.Positions) {
				point.X = std::bit_cast<float>(ReadWord(input));
				point.Y = std::bit_cast<float>(ReadWord(input));
			}

			for (auto& color : quad.Colors) {
				color = ReadWord(input);
			}

			submissions.clear();
			batch.Append(quad, submit);
			CompareSnapshot(expected, submissions, batch, storage);
		}

		submissions.clear();
		batch.Flush(submit);
		CompareSnapshot(expected, submissions, batch, storage);
	}

	Require(input.peek() == std::ifstream::traits_type::eof() && expected.peek() == std::ifstream::traits_type::eof(), "motion quad oracle has trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 3, "usage: motion_quad_batch_tests [native_input native_output]");
		TestQuadModesAndConnections();
		TestKeyChangeFailureAndCapacity();
		if (argc == 3) {
			TestNativeReplay(argv[1], argv[2]);
		}

		std::cout << "motion quad batch fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
