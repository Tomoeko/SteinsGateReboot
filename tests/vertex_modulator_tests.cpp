#include "sgre/scene/vertex_modulator.hpp"

#include "sgre/io/file_reader.hpp"

#include "vertex_modulator_native_vectors.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::resource::MotionPoint;
using sgre::scene::DistortionVertexModulator;
using sgre::scene::RasterVertexModulator;
using sgre::scene::VertexModulator;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static float Float(std::uint32_t word) {
	return std::bit_cast<float>(word);
}

static std::array<float, 12> State(const VertexModulator& model, std::uint32_t kind) {
	const auto range = model.Range();
	if (kind == 0) {
		const auto state = static_cast<const RasterVertexModulator&>(model).State();

		return {range.Left, range.Top, range.Right, range.Bottom, state.CycleFrame, state.CycleLines, state.Amplitude, state.Phase};
	}

	const auto state = static_cast<const DistortionVertexModulator&>(model).State();

	return {range.Left, range.Top, range.Right, range.Bottom, state.CycleFrameX, state.CycleFrameY, state.CycleLinesX, state.CycleLinesY, state.AmplitudeX, state.AmplitudeY, state.PhaseX, state.PhaseY};
}

static void Compare(float actual, std::uint32_t expected_word, std::string_view phase, std::size_t case_index, std::size_t word_index) {
	const auto expected = Float(expected_word);
	if (!std::isfinite(actual) || !std::isfinite(expected) || std::abs(actual - expected) > 8e-6f * std::max(1.0f, std::abs(expected))) {
		throw std::runtime_error("native modulator mismatch phase=" + std::string(phase) + " case=" + std::to_string(case_index) + " word=" + std::to_string(word_index) + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
	}
}

static void Replay(std::span<const NativeModulatorCase> cases) {
	std::size_t differing_distortion_operations = 0;
	for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
		const auto& witness = cases[case_index];
		Require(witness.Kind <= 1, "native witness must select a genuine concrete modulator");
		std::unique_ptr<VertexModulator> model;
		if (witness.Kind == 0) {
			model = std::make_unique<RasterVertexModulator>();
		} else {
			model = std::make_unique<DistortionVertexModulator>();
		}

		const auto initial = State(*model, witness.Kind);
		const std::size_t state_count = witness.Kind == 0 ? 8 : 12;
		for (std::size_t index = 0; index < state_count; ++index) {
			Require(std::bit_cast<std::uint32_t>(initial[index]) == witness.Constructor[index], "actual constructor state must match unchanged original instructions word for word");
		}

		model->SetRange(Float(witness.Range[0]), Float(witness.Range[1]), Float(witness.Range[2]) - Float(witness.Range[0]), Float(witness.Range[3]) - Float(witness.Range[1]));
		if (witness.Kind == 0) {
			auto& raster = static_cast<RasterVertexModulator&>(*model);
			raster.SetCycleFrame(Float(witness.Parameters[0]));
			raster.SetCycleLines(Float(witness.Parameters[1]));
			raster.SetAmplitude(Float(witness.Parameters[2]));
		} else {
			auto& distortion = static_cast<DistortionVertexModulator&>(*model);
			distortion.SetCycleFrame(Float(witness.Parameters[0]));
			distortion.SetCycleFrameY(Float(witness.Parameters[1]));
			distortion.SetCycleLines(Float(witness.Parameters[2]));
			distortion.SetCycleLinesY(Float(witness.Parameters[3]));
			distortion.SetAmplitude(Float(witness.Parameters[4]));
			distortion.SetAmplitudeY(Float(witness.Parameters[5]));
		}

		for (const auto& step : witness.Steps) {
			model->Advance(Float(step.Delta));
			const auto state = State(*model, witness.Kind);
			for (std::size_t index = 0; index < state_count; ++index) {
				Compare(state[index], step.State[index], "phase", case_index, index);
			}

			std::array<MotionPoint, 22> positions{};
			for (std::size_t index = 0; index < positions.size(); ++index) {
				positions[index] = {Float(step.Input[index * 2]), Float(step.Input[index * 2 + 1])};
				const auto single = model->ApplyPoint(positions[index]);
				Compare(single.X, step.Single[index * 2], "point-x", case_index, index);
				Compare(single.Y, step.Single[index * 2 + 1], "point-y", case_index, index);
			}

			model->Apply(positions);
			for (std::size_t index = 0; index < positions.size(); ++index) {
				Compare(positions[index].X, step.Vector[index * 2], "vector-x", case_index, index);
				Compare(positions[index].Y, step.Vector[index * 2 + 1], "vector-y", case_index, index);
				if (witness.Kind == 1 && step.Vector[index * 2 + 1] != step.Single[index * 2 + 1]) {
					++differing_distortion_operations;
				}
			}
		}
	}

	Require(differing_distortion_operations != 0, "native witness must discriminate vector cosf from point sinf instead of replacing one operation with the other");
}

static void TestFiniteBoundaryAndPreflight() {
	RasterVertexModulator raster;
	raster.SetRange(0, 0, 8, 8);
	raster.SetCycleLines(0);
	Require(raster.ApplyPoint({0, 4}).X == 0 && raster.ApplyPoint({8, 4}).X == 8, "native exact X boundary bypass must precede unsupported zero-period arithmetic");
	bool rejected = false;
	try {
		static_cast<void>(raster.ApplyPoint({4, 4}));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "unmeasured zero-period arithmetic must retain a precise operation gate");
	raster.SetCycleLines(8);
	std::array<MotionPoint, 2> positions{{{4, 4}, {std::numeric_limits<float>::quiet_NaN(), 1}}};
	rejected = false;
	try {
		raster.Apply(positions);
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && positions[0].X == 4, "all actual vector inputs must be finite before the first authored vertex mutation");
}

static std::vector<NativeModulatorCase> ReadWitness(const char* path) {
	sgre::io::FileReader input(path);
	const auto length = input.Size();
	Require(length >= 8 && length <= 4 * 1024 * 1024, "native witness file exceeds bounded input");
	const auto bytes = input.Read(0, static_cast<std::size_t>(length));
	std::size_t position = 0;
	const auto word = [&]() {
		Require(position <= bytes.size() && 4 <= bytes.size() - position, "native witness word exceeds its actual input");
		std::uint32_t value = 0;
		for (std::size_t index = 0; index < 4; ++index) {
			value |= static_cast<std::uint32_t>(bytes[position++]) << (index * 8);
		}

		return value;
	};
	Require(word() == 0x31444d56u, "native modulator witness tag mismatch");
	const auto count = word();
	Require(count != 0 && count <= 64, "native modulator witness count mismatch");
	std::vector<NativeModulatorCase> cases(count);
	const auto array = [&](auto& target) {
		for (auto& value : target) {
			value = word();
		}
	};
	for (auto& witness : cases) {
		witness.Kind = word();
		array(witness.Constructor);
		array(witness.Range);
		array(witness.Parameters);
		for (auto& step : witness.Steps) {
			step.Delta = word();
			array(step.State);
			array(step.Input);
			array(step.Vector);
			array(step.Single);
		}
	}

	Require(position == bytes.size(), "native witness file has unexplained trailing bytes");

	return cases;
}

int main(int argc, char** argv) {
	try {
		Require(argc <= 2, "usage: vertex_modulator_tests [native-vectors.bin]");
		if (argc == 2) {
			const auto cases = ReadWitness(argv[1]);
			Replay(cases);
		} else {
			Replay(kNativeModulatorCases);
		}

		TestFiniteBoundaryAndPreflight();
		std::cout << "native vertex modulator constructor/phase/vector/point checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
