#include "sgre/scene/emote_catenary_curve.hpp"
#include "sgre/scene/emote_soft_pendulum.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("two-anchor model accepted a missing actual endpoint owner");
}

static void TestActualAnchorsAndCurve() {
	const auto curve = sgre::scene::PrepareEmoteCatenaryCurve({0, 0}, {2, 0}, 3);
	Require(curve.has_value(), "actual catenary fits endpoints with longer rest length");
	const auto first = curve->SampleArc(0, 2, 0, 4);
	const auto last = curve->SampleArc(0, 2, 4, 4);
	const auto middle = curve->SampleArc(0, 2, 2, 4);
	Require(std::abs(first[0]) < 1.0e-8 && std::abs(first[1]) < 1.0e-8 && std::abs(last[0] - 2) < 1.0e-8 && std::abs(last[1]) < 1.0e-8 && middle[1] < 0, "native analytic curve retains endpoints and its genuine sag");
	Require(!sgre::scene::PrepareEmoteCatenaryCurve({0, 0}, {0, 2}, 3), "native equal-X solver returns absence for the real reset fallback");
	Require(!sgre::scene::PrepareEmoteCatenaryCurve({0, 0}, {2, 0}, 1), "native insufficient-length root solve returns absence");
	sgre::scene::EmoteSoftPendulumDefinition definition;
	definition.FixedTip = true;
	definition.ParticleCount = 4;
	definition.Length = 3;
	definition.Mass = 2;
	definition.BottomRatio = 0.5f;
	definition.FrictionBegin = 0.1f;
	definition.FrictionEnd = 0.5f;
	// Mode1's analytic reset never consumes the zero-gravity reciprocal cached by320770.
	definition.Acceleration = {0, 0, 0};
	sgre::scene::EmoteSoftPendulum model(definition);
	Require(model.Particles().front().Fixed && model.Particles().back().Fixed && !model.Particles()[1].Fixed, "actual native31f520 has two fixed endpoints and genuine free interior mass");
	Require(model.Particles().front().Mass == 0 && model.Particles().back().Mass == 0 && model.Particles()[1].Mass == 1, "native mass belongs only to actual free interior particles");
	RequireFailure([&] {
		model.Initialize();
	},
	    "preceding posture");
	sgre::scene::EmoteSoftPendulumPosture posture;
	model.SetPosture(posture);
	RequireFailure([&] {
		model.Initialize();
	},
	    "both actual fixed endpoints");
	model.SetFixedEnds({0, 0}, {2, 0});
	model.Initialize();
	const auto before = model.Particles()[1].Position;
	model.ClearForcesAndMeasureBounds();
	model.Step(1.0f / 60.0f);
	Require(model.Particles().front().Position[0] == 0 && model.Particles().back().Position[0] == 2, "actual fixed endpoints survive a real two-anchor step");
	Require(std::isfinite(model.Particles()[1].Position[0]) && std::isfinite(before[0]), "native zero-gravity mode1 initialization/step remain finite without a fake gravity vector");
}

class NativeReader {
public:
	explicit NativeReader(const char* path)
	    : Stream(path, std::ios::binary) {
		Require(Stream.good(), "native Catenary vector file is unavailable");
	}
	std::uint32_t Word() {
		std::array<char, 4> data{};
		Stream.read(data.data(), 4);
		Require(Stream.good(), "native Catenary input ended early");
		return static_cast<std::uint32_t>(static_cast<unsigned char>(data[0])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[1])) << 8) | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[2])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[3])) << 24);
	}
	float Float() {
		const auto value = std::bit_cast<float>(Word());
		Require(std::isfinite(value), "native Catenary scalar is nonfinite");
		return value;
	}
	double Double() {
		const auto low = Word();
		const auto high = Word();
		const auto value = std::bit_cast<double>(static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32));
		Require(std::isfinite(value), "native Catenary expected state is nonfinite");
		return value;
	}
	bool Complete() {
		return Stream.peek() == std::char_traits<char>::eof();
	}

private:
	std::ifstream Stream;
};

static void CompareSnapshot(NativeReader& reader, const sgre::scene::EmoteSoftPendulum& model, float& maximum, std::size_t& compared, const std::string& context) {
	std::size_t ordinal = 0;
	for (const auto& particle : model.Particles()) {
		Require(reader.Word() == static_cast<std::uint32_t>(particle.Fixed), "native Catenary fixed endpoint classification changed");
		std::array<double, 19> actual{};
		actual[0] = particle.Mass;
		actual[1] = particle.InverseMass;
		std::copy(particle.Previous.begin(), particle.Previous.end(), actual.begin() + 2);
		std::copy(particle.Predicted.begin(), particle.Predicted.end(), actual.begin() + 5);
		std::copy(particle.Position.begin(), particle.Position.end(), actual.begin() + 8);
		std::copy(particle.Velocity.begin(), particle.Velocity.end(), actual.begin() + 11);
		std::copy(particle.Force.begin(), particle.Force.end(), actual.begin() + 14);
		actual[17] = particle.Friction;
		actual[18] = particle.AccelerationProjection;
		for (std::size_t word = 0; word != actual.size(); ++word) {
			const auto expected = reader.Double();
			const auto error = std::abs(actual[word] - expected) / std::max(1.0, std::abs(expected));
			if (!std::isfinite(actual[word]) || error > 0.000008) {
				throw std::runtime_error(context + " particle=" + std::to_string(ordinal) + " word=" + std::to_string(word) + " expected=" + std::to_string(expected) + " actual=" + std::to_string(actual[word]) + " error=" + std::to_string(error));
			}
			maximum = std::max(maximum, static_cast<float>(error));
			++compared;
		}
		++ordinal;
	}
}

static void TestOriginal(const char* path) {
	NativeReader reader(path);
	Require(reader.Word() == 0x32435045U && reader.Word() == 8, "native two-anchor vector header changed");
	float maximum = 0;
	std::size_t compared = 0;
	for (std::uint32_t index = 0; index != 8; ++index) {
		Require(reader.Word() == index, "native two-anchor case order changed");
		sgre::scene::EmoteSoftPendulumDefinition definition;
		definition.ParticleCount = reader.Word();
		Require(definition.ParticleCount >= 3 && definition.ParticleCount <= 8, "native two-anchor particle extent changed");
		for (auto& value : definition.Acceleration) {
			value = reader.Float();
		}
		definition.Mass = reader.Float();
		definition.Length = reader.Float();
		definition.Elastic = reader.Float();
		definition.BottomRatio = reader.Float();
		definition.FrictionBegin = reader.Float();
		definition.FrictionEnd = reader.Float();
		definition.AdhocSimulation = reader.Word() != 0;
		definition.MassTiltedDistribution = reader.Word() != 0;
		definition.FixedTip = true;
		definition.RotationInheritance = 0.3f;
		definition.CoordinateRotation = 0.2f;
		definition.CoordinateScale = {0.8f, 1.2f, 1};
		definition.CollisionRepulsion = 100;
		sgre::scene::EmoteSoftPendulum model(definition);
		const auto context = "Catenary case=" + std::to_string(index);
		CompareSnapshot(reader, model, maximum, compared, context + " constructor");
		const auto frame_count = reader.Word();
		Require(frame_count == 4, "native two-anchor phase count changed");
		for (std::uint32_t frame = 0; frame != frame_count; ++frame) {
			sgre::scene::EmoteSoftPendulumPosture posture;
			for (auto& value : posture.Coordinate) {
				value = reader.Float();
			}
			posture.Rotation = reader.Float();
			const std::array<float, 2> first{reader.Float(), reader.Float()};
			const std::array<float, 2> second{reader.Float(), reader.Float()};
			const auto seconds = reader.Float();
			model.SetPosture(posture);
			model.SetFixedEnds(first, second);
			if (frame == 0) {
				model.Initialize();
			}
			CompareSnapshot(reader, model, maximum, compared, context + " frame=" + std::to_string(frame) + " reset/posture");
			model.Step(seconds);
			CompareSnapshot(reader, model, maximum, compared, context + " frame=" + std::to_string(frame) + " step");
		}
	}
	Require(reader.Complete(), "native two-anchor file has unconsumed data");
	std::cout << "Original two-anchor Catenary8 constructors/32 steps/" << compared << " state doubles PASS maximum=" << maximum << '\n';
}

int main(int argc, char** argv) {
	try {
		TestActualAnchorsAndCurve();
		if (argc == 2) {
			TestOriginal(argv[1]);
		} else {
			Require(argc == 1, "Catenary fixture takes one optional native vector path");
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
