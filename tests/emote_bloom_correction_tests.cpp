#include "sgre/scene/emote_bloom_physics.hpp"

#include "emote_bloom_correction_native_vectors.hpp"
#include "emote_bloom_native_vectors.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("missing expected Bloom owner rejection");
}

static sgre::scene::EmoteBloomDefinition Definition(const NativeBloomCase& input) {
	const auto& fields = input.Scalars;
	sgre::scene::EmoteBloomDefinition definition;
	definition.Substeps = input.Substeps;
	definition.DefaultRotation = fields[1];
	definition.Speed = fields[2];
	definition.Mass = fields[3];
	definition.LimitLengthRatio = fields[4];
	definition.SizeHorizontal = fields[5];
	definition.SizeVertical = fields[6];
	definition.Origin = {fields[7], fields[8]};
	definition.Center = {fields[9], fields[10]};
	definition.Elastic = fields[11];
	definition.Friction = fields[12];
	definition.RotateWeight = fields[13];
	definition.TwistWeight = fields[14];
	definition.TrapeziumWeight = fields[15];
	definition.PitchWeight = fields[16];
	definition.ControlPointSize = fields[17];
	definition.CoordinateScale = {fields[18], fields[19], fields[20]};
	definition.CollisionRepulsion = fields[21];
	definition.CollisionHardness = 0;
	definition.Straightness = fields[23];
	for (std::size_t index = 0; index != input.PointCount; ++index) {
		definition.Tresses.push_back({input.Tresses[index][0], input.Tresses[index][1]});
	}
	return definition;
}

static sgre::scene::EmotePhysicalPosture Posture(const std::array<float, 6>& input) {
	sgre::scene::EmotePhysicalPosture result;
	result.Coordinate = {input[0], input[1], input[2]};
	result.Rotation = input[3];
	result.Cosine = input[4];
	result.Sine = input[5];
	return result;
}

struct NativeComparison {
	std::size_t Compared = 0;
	std::size_t Exact = 0;
	float MaximumScaledError = 0;

	void Word(float actual, std::uint32_t expected_word, const std::string& context) {
		const float expected = std::bit_cast<float>(expected_word);
		Require(std::isfinite(actual) && std::isfinite(expected), context + " must preserve finite native state");
		const float scaled = std::abs(actual - expected) / std::max(1.0f, std::abs(expected));
		// Selected before any C++ run; native float CRT trigonometry is not assumed cross-CRT bit-identical.
		Require(scaled <= 8.0e-6f, context + " exceeds preselected native bound: actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
		MaximumScaledError = std::max(MaximumScaledError, scaled);
		Exact += std::bit_cast<std::uint32_t>(actual) == expected_word;
		++Compared;
	}
};

static void TestOriginalCorrectedOutput() {
	NativeComparison comparison;
	for (std::size_t case_index = 0; case_index != kNativeBloomCorrections.size(); ++case_index) {
		const auto& input = kNativeBloomCases[8 + case_index];
		const auto& native = kNativeBloomCorrections[case_index];
		auto definition = Definition(input);
		definition.CorrectScale = native.CorrectScale;
		definition.CorrectFix = native.CorrectFix;
		for (std::size_t index = 0; index != native.FixCount; ++index) {
			definition.Fixes.push_back({native.Fixes[index][0], native.Fixes[index][1]});
		}
		sgre::scene::EmoteBloomPhysics owner(definition);
		owner.SetPosture(Posture(input.InitialPose));
		owner.Initialize();
		for (std::size_t step_index = 0; step_index != input.Steps.size(); ++step_index) {
			const auto& step = input.Steps[step_index];
			const auto& result = native.Steps[step_index];
			owner.SetPosture(Posture(step.Pose));
			owner.ClearForces();
			for (std::size_t point = 0; point != input.PointCount; ++point) {
				owner.AddForce(point, step.Forces[point]);
			}
			owner.Update(step.Elapsed);
			std::array<sgre::resource::MotionPoint, 3> targets{};
			for (std::size_t index = 0; index != native.FixCount; ++index) {
				targets[index] = {result.Targets[index][0], result.Targets[index][1]};
			}
			const auto& lattice = owner.PrepareLattice(result.Angle, result.Scale, std::span(targets).first(native.FixCount));
			for (std::size_t index = 0; index != lattice.Controls.size(); ++index) {
				const auto context = "Bloom corrected case " + std::to_string(case_index) + " step " + std::to_string(step_index) + " control " + std::to_string(index);
				comparison.Word(lattice.Controls[index].X, result.Controls[index * 2], context + " X");
				comparison.Word(lattice.Controls[index].Y, result.Controls[index * 2 + 1], context + " Y");
			}
			if (native.CorrectFix && native.FixCount != 0) {
				RequireFailure([&] {
					static_cast<void>(owner.PrepareLattice(result.Angle, result.Scale, {}));
				},
				    "every real fix target");
			}
		}
	}
	std::cout << "native Bloom correction words " << comparison.Compared << " bit-exact " << comparison.Exact << " max scaled error " << comparison.MaximumScaledError << '\n';
}
int main() {
	try {
		TestOriginalCorrectedOutput();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
