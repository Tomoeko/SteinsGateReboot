#include "sgre/scene/emote_catenary_physics.hpp"

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
	throw std::runtime_error("Catenary accepted a missing actual owner or unsupported physical step");
}

static sgre::scene::EmoteCatenaryDefinition ActualDefinition() {
	sgre::scene::EmoteCatenaryDefinition definition;
	definition.SizeHorizontal = 2;
	definition.SizeVertical = 3;
	definition.Length = 3;
	definition.Mass = 2;
	definition.ParticleCount = 4;
	definition.Origin = {0.3f, 0.6f};
	definition.Fixes = {{{0.2f, 0.15f}, {0.8f, 0.65f}}};
	definition.CoordinateScale = {0.9f, 1.1f, 1};
	return definition;
}

static void TestRealInitializationAndBounds() {
	auto definition = ActualDefinition();
	sgre::scene::EmoteCatenaryPhysics entity(definition);
	Require(entity.Particles().size() == 4 && entity.Particles().front().Fixed && entity.Particles().back().Fixed && !entity.Particles()[1].Fixed, "actual306190 owns the genuine two-fixed-end physical model");
	RequireFailure([&] {
		entity.Initialize();
	},
	    "actual posture");
	RequireFailure([&] {
		static_cast<void>(entity.PrepareLattice());
	},
	    "initialized physical owner");
	sgre::scene::EmotePhysicalPosture posture;
	entity.SetPosture(posture);
	RequireFailure([&] {
		entity.Initialize();
	},
	    "both world fixes");
	const std::array<sgre::resource::MotionPoint, 2> targets{{{0, 0}, {2, 1}}};
	entity.SetCorrection(0, 1);
	entity.SetWorldFixes(targets);
	entity.Initialize();
	const auto& mesh = entity.PrepareLattice();
	Require(mesh.EncodedDegree == 0x43 && mesh.Controls.size() == 20, "actual306190 selects vertical cubic/quartic controls from real dimensions");
	RequireFailure([&] {
		entity.Update(0);
	},
	    "positive actual scaled ticks");
	entity.ClearForcesAndMeasureBounds();
	entity.Update(1);
	Require(entity.Particles().front().Position[0] == 0 && entity.Particles().back().Position[0] == 2, "first-use settling retains the two actual world anchors");
	const auto& settled = entity.PrepareLattice();
	Require(std::all_of(settled.Controls.begin(), settled.Controls.end(), [](const auto& point) {
		return std::isfinite(point.X) && std::isfinite(point.Y);
	}),
	    "genuine first-use settling produces finite normalized controls");
	definition.Speed = 0;
	sgre::scene::EmoteCatenaryPhysics frozen(definition);
	frozen.SetPosture(posture);
	frozen.SetWorldFixes(targets);
	frozen.Initialize();
	static_cast<void>(frozen.PrepareLattice());
	RequireFailure([&] {
		frozen.Update(1);
	},
	    "positive actual scaled ticks");
	definition.ParticleCount = 2;
	RequireFailure([&] {
		sgre::scene::EmoteCatenaryPhysics invalid(definition);
	},
	    "two-anchor particles");
}

class NativeReader {
public:
	explicit NativeReader(const char* path)
	    : Stream(path, std::ios::binary) {
		Require(Stream.good(), "original Catenary entity vector file is unavailable");
	}
	std::uint32_t Word() {
		std::array<char, 4> data{};
		Stream.read(data.data(), 4);
		Require(Stream.good(), "original Catenary entity vector file ended early");
		return static_cast<std::uint32_t>(static_cast<unsigned char>(data[0])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[1])) << 8) | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[2])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[3])) << 24);
	}
	float Float() {
		const auto value = std::bit_cast<float>(Word());
		Require(std::isfinite(value), "original Catenary entity input or output is nonfinite");
		return value;
	}
	double Double() {
		const auto low = Word();
		const auto high = Word();
		const auto value = std::bit_cast<double>(static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32));
		Require(std::isfinite(value), "original Catenary entity state is nonfinite");
		return value;
	}
	bool Complete() {
		return Stream.peek() == std::char_traits<char>::eof();
	}

private:
	std::ifstream Stream;
};

static void Compare(double actual, double expected, double& maximum, std::size_t& count, const std::string& context) {
	const auto error = std::abs(actual - expected) / std::max(1.0, std::abs(expected));
	if (!std::isfinite(actual) || error > 0.000008) {
		throw std::runtime_error(context + " expected=" + std::to_string(expected) + " actual=" + std::to_string(actual) + " scaledError=" + std::to_string(error));
	}
	maximum = std::max(maximum, error);
	++count;
}

static void CompareParticles(NativeReader& reader, const sgre::scene::EmoteCatenaryPhysics& entity, double& maximum, std::size_t& count, const std::string& context) {
	std::size_t ordinal = 0;
	for (const auto& particle : entity.Particles()) {
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
			// Native fixed endpoints never initialize the unused preceding-segment projection cell.
			if (particle.Fixed && word == 18) {
				continue;
			}
			Compare(actual[word], expected, maximum, count, context + " particle=" + std::to_string(ordinal) + " word=" + std::to_string(word));
		}
		++ordinal;
	}
}

static void TestOriginal(const char* path) {
	NativeReader reader(path);
	Require(reader.Word() == 0x33435045 && reader.Word() == 4, "original Catenary entity vector header changed");
	double output_maximum = 0, particle_maximum = 0;
	std::size_t output_words = 0, particle_words = 0;
	for (std::uint32_t index = 0; index != 4; ++index) {
		Require(reader.Word() == index, "original Catenary entity vector order changed");
		const auto degree = reader.Word();
		auto definition = ActualDefinition();
		definition.ParticleCount = reader.Word();
		definition.Substeps = reader.Word();
		definition.Speed = reader.Float();
		definition.Mass = reader.Float();
		definition.Gravity = reader.Float();
		definition.GravityRotation = reader.Float();
		definition.DefaultRotation = reader.Float();
		definition.SizeHorizontal = reader.Float();
		definition.SizeVertical = reader.Float();
		definition.Origin = {reader.Float(), reader.Float()};
		definition.Length = reader.Float();
		for (auto& point : definition.Fixes) {
			point = {reader.Float(), reader.Float()};
		}
		const auto point_count = reader.Word();
		Require(point_count <= 7, "original Catenary rest-point extent changed");
		for (std::uint32_t point = 0; point != point_count; ++point) {
			definition.RestPoints.push_back({reader.Float(), reader.Float()});
		}
		definition.CorrectFix = reader.Word() != 0;
		definition.CorrectScale = reader.Word() != 0;
		definition.AdhocSimulation = reader.Word() != 0;
		definition.MassTiltedDistribution = reader.Word() != 0;
		definition.Straightness = 0.25f;
		definition.FrictionTop = 0.15f;
		definition.FrictionBottom = 0.3f;
		definition.BottomRatio = 0.4f;
		definition.Elastic = 0.05f;
		definition.DeformIterations = 3;
		sgre::scene::EmoteCatenaryPhysics entity(definition);
		const auto frame_count = reader.Word();
		Require(frame_count == 4, "original Catenary entity phase count changed");
		for (std::uint32_t frame = 0; frame != frame_count; ++frame) {
			sgre::scene::EmotePhysicalPosture posture;
			for (auto& value : posture.Coordinate) {
				value = reader.Float();
			}
			posture.Rotation = reader.Float();
			posture.Cosine = reader.Float();
			posture.Sine = reader.Float();
			const auto correction_angle = reader.Float();
			const auto correction_scale = reader.Float();
			std::array<sgre::resource::MotionPoint, 2> targets;
			for (auto& point : targets) {
				point = {reader.Float(), reader.Float()};
			}
			const auto ticks = reader.Float();
			entity.SetPosture(posture);
			entity.SetCorrection(correction_angle, correction_scale);
			entity.SetWorldFixes(targets);
			if (frame == 0) {
				entity.Initialize();
			}
			entity.ClearForcesAndMeasureBounds();
			entity.Update(ticks);
			const auto& output = entity.PrepareLattice();
			Require(output.EncodedDegree == degree && output.Controls.size() == 20, "original Catenary normalized output extent changed");
			const auto context = "Catenary case=" + std::to_string(index) + " frame=" + std::to_string(frame);
			for (std::size_t point = 0; point != output.Controls.size(); ++point) {
				Compare(output.Controls[point].X, reader.Float(), output_maximum, output_words, context + " output=" + std::to_string(point) + " X");
				Compare(output.Controls[point].Y, reader.Float(), output_maximum, output_words, context + " output=" + std::to_string(point) + " Y");
			}
			CompareParticles(reader, entity, particle_maximum, particle_words, context);
		}
	}
	Require(reader.Complete(), "original Catenary entity vectors have unconsumed bytes");
	std::cout << "Original Catenary4 constructors/16 phases/" << output_words << " lattice words/" << particle_words << " initialized state doubles PASS maxOutput=" << output_maximum << " maxState=" << particle_maximum << '\n';
}

int main(int argc, char** argv) {
	try {
		TestRealInitializationAndBounds();
		if (argc == 2) {
			TestOriginal(argv[1]);
		} else {
			Require(argc == 1, "Catenary entity fixture takes one optional original vector path");
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
