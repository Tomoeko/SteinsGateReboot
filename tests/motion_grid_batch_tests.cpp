#include "sgre/scene/motion_quad_batch.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("grid batch fixture expected a real input/capacity rejection");
}

struct GridStorage {
	std::array<sgre::resource::MotionPoint, 2048> Positions{};
	std::array<sgre::resource::MotionPoint, 2048> Uvs{};
	std::array<std::uint32_t, 2048> Colors{};
	std::array<std::uint32_t, 2048> Indices{};

	sgre::scene::MotionQuadBatchBuffers Buffers() {
		return {Positions, Uvs, Colors, Indices};
	}
};

static void AppendPoints(std::vector<std::uint32_t>& output, std::span<const sgre::resource::MotionPoint> points) {
	for (const auto& point : points) {
		output.push_back(std::bit_cast<std::uint32_t>(point.X));
		output.push_back(std::bit_cast<std::uint32_t>(point.Y));
	}
}

static std::vector<std::uint32_t> ReadWords(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	Require(file && file.tellg() >= 8 && file.tellg() <= 4 * 1024 * 1024 && file.tellg() % 4 == 0, "native grid batch vector is absent or exceeds its bounded word input");
	const auto count = static_cast<std::size_t>(file.tellg()) / 4;
	file.seekg(0);
	std::vector<std::uint32_t> words(count);
	for (auto& word : words) {
		for (std::uint32_t shift = 0; shift < 32; shift += 8) {
			const auto byte = file.get();
			Require(byte != std::char_traits<char>::eof(), "native grid batch vector is truncated");
			word |= static_cast<std::uint32_t>(byte) << shift;
		}
	}

	return words;
}

static void CompareNative(const char* path) {
	const auto words = ReadWords(path);
	std::size_t cursor = 0;
	const auto next = [&] {
		Require(cursor < words.size(), "native grid batch vector escaped its actual word count");

		return words[cursor++];
	};
	Require(next() == 0x3142474d && next() == 32, "native grid batch vector header differs");
	std::size_t comparisons = 0;
	for (std::size_t record = 0; record < 32; ++record) {
		GridStorage storage;
		sgre::scene::MotionQuadBatch batch(sgre::scene::MotionBatchMode::IndexedTriangleStrip, storage.Buffers());
		const auto count = next();
		Require(count == 3, "native grid batch append count differs");
		sgre::resource::MotionPoint inverse;
		inverse.X = std::bit_cast<float>(next());
		inverse.Y = std::bit_cast<float>(next());
		std::vector<std::uint32_t> observed;
		const auto submit = [&](const sgre::scene::MotionBatchSubmission& packet) {
			++observed[0];
			observed.push_back(static_cast<std::uint32_t>(packet.Mode));
			observed.push_back(packet.Key);
			observed.push_back(static_cast<std::uint32_t>(packet.Positions.size()));
			observed.push_back(static_cast<std::uint32_t>(packet.Indices.size()));
			AppendPoints(observed, packet.Positions);
			AppendPoints(observed, packet.Uvs);
			observed.insert(observed.end(), packet.Colors.begin(), packet.Colors.end());
			observed.insert(observed.end(), packet.Indices.begin(), packet.Indices.end());
		};
		const auto compare = [&] {
			const auto counts = batch.Counts();
			observed.push_back(batch.Key());
			observed.push_back(static_cast<std::uint32_t>(counts.Positions));
			observed.push_back(static_cast<std::uint32_t>(counts.Uvs));
			observed.push_back(static_cast<std::uint32_t>(counts.Colors));
			observed.push_back(static_cast<std::uint32_t>(counts.Indices));
			AppendPoints(observed, std::span(storage.Positions).first(counts.Positions));
			AppendPoints(observed, std::span(storage.Uvs).first(counts.Uvs));
			observed.insert(observed.end(), storage.Colors.begin(), storage.Colors.begin() + static_cast<std::ptrdiff_t>(counts.Colors));
			observed.insert(observed.end(), storage.Indices.begin(), storage.Indices.begin() + static_cast<std::ptrdiff_t>(counts.Indices));
			const auto expected_count = next();
			Require(expected_count == observed.size() && expected_count <= words.size() - cursor, "native grid batch observation size differs");
			for (const auto actual : observed) {
				Require(actual == next(), "grid batch staging/submitted position, UV, color, indices or count differs from actual native instructions");
			}
			++comparisons;
		};
		for (std::size_t draw = 0; draw < count; ++draw) {
			sgre::scene::MotionGridBatchInput input;
			input.Subdivision.X = next();
			input.Subdivision.Y = next();
			input.Key = next();
			input.PixelUvs.X = std::bit_cast<float>(next());
			input.PixelUvs.Y = std::bit_cast<float>(next());
			input.PixelUvs.Width = std::bit_cast<float>(next());
			input.PixelUvs.Height = std::bit_cast<float>(next());
			input.InverseTextureSize = inverse;
			const auto vertex_count = sgre::scene::MotionMeshGridVertexCount(input.Subdivision);
			Require(vertex_count <= 256, "native grid fixture exceeds its finite input family");
			std::vector<sgre::resource::MotionPoint> positions(vertex_count);
			for (auto& point : positions) {
				point.X = std::bit_cast<float>(next());
				point.Y = std::bit_cast<float>(next());
			}
			for (auto& color : input.Colors) {
				color = next();
			}
			input.Positions = positions;
			observed.assign(1, 0);
			batch.AppendGrid(input, submit);
			compare();
		}
		observed.assign(1, 0);
		batch.Flush(submit);
		compare();
	}
	Require(cursor == words.size() && comparisons == 128, "native grid batch vector has extra records or missing comparisons");
	std::cout << "Original mode2 grid batch append/flush observations: " << comparisons << '\n';
}

static void TestGridConnectionsAndGuards() {
	GridStorage storage;
	sgre::scene::MotionQuadBatch batch(sgre::scene::MotionBatchMode::IndexedTriangleStrip, storage.Buffers());
	std::array<sgre::resource::MotionPoint, 9> positions{};
	sgre::scene::MotionGridBatchInput input;
	input.Positions = positions;
	input.Subdivision.X = 2;
	input.Subdivision.Y = 2;
	input.InverseTextureSize.X = 1.0f / 64;
	input.InverseTextureSize.Y = 1.0f / 32;
	input.PixelUvs.Width = 16;
	input.PixelUvs.Height = 8;
	input.Colors = {0xff000000, 0xff0000ff, 0xffff0000, 0xffffffff};
	input.Key = 4;
	batch.AppendGrid(input, {});
	batch.AppendGrid(input, {});
	Require(batch.Counts().Positions == 18 && batch.Counts().Indices == 32, "grid batching lost shared row-major vertices or per-row degenerate connections");
	const auto before = batch.Counts();
	auto invalid = input;
	invalid.Subdivision.X = 3;
	RequireFailure([&] {
		batch.AppendGrid(invalid, {});
	});
	invalid = input;
	invalid.PixelUvs.Width = std::numeric_limits<float>::max();
	invalid.InverseTextureSize.X = 1.0f;
	RequireFailure([&] {
		batch.AppendGrid(invalid, {});
	});
	Require(batch.Counts() == before && batch.Key() == 4, "invalid grid input changed an already queued native batch");
	batch.Flush([&](const auto& packet) {
		const std::array<std::uint32_t, 30> expected{0, 3, 1, 4, 2, 5, 5, 3, 3, 6, 4, 7, 5, 8, 8, 9, 9, 12, 10, 13, 11, 14, 14, 12, 12, 15, 13, 16, 14, 17};
		Require(packet.Indices.size() == expected.size() && std::equal(expected.begin(), expected.end(), packet.Indices.begin()), "native grid strips lost internal row/command degenerate indices");
	});
	GridStorage small;
	auto buffers = small.Buffers();
	buffers.Indices = buffers.Indices.first(6);
	sgre::scene::MotionQuadBatch bounded(sgre::scene::MotionBatchMode::IndexedTriangleStrip, buffers);
	RequireFailure([&] {
		bounded.AppendGrid(input, {});
	});
	Require(bounded.Counts() == sgre::scene::MotionBatchCounts{}, "undersized grid streams consumed input");
}

int main(int argc, char** argv) {
	try {
		TestGridConnectionsAndGuards();
		Require(argc == 1 || argc == 2, "grid batch fixture expects an optional native vector");
		if (argc == 2) {
			CompareNative(argv[1]);
		}
		std::cout << "Motion grid batch tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
