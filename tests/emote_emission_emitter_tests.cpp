#include "sgre/scene/emote_emission_emitter.hpp"

#include "emote_emission_emitter_vectors.hpp"

#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

class EmissionWords {
public:
	explicit EmissionWords(std::span<const std::uint32_t> words)
	    : m_Words(words) {
	}
	std::uint32_t Next() {
		if (m_Offset == m_Words.size()) {
			throw std::runtime_error("truncated native emission vectors");
		}
		return m_Words[m_Offset++];
	}
	float Float() {
		return std::bit_cast<float>(Next());
	}
	void ObserveError(float error) {
		m_MaximumError = std::max(m_MaximumError, error);
	}
	float MaximumError() const noexcept {
		return m_MaximumError;
	}
	void RequireEnd() const {
		if (m_Offset != m_Words.size()) {
			throw std::runtime_error("unused native emission vectors");
		}
	}

private:
	std::span<const std::uint32_t> m_Words;
	std::size_t m_Offset = 0;
	float m_MaximumError = 0;
};

static void Require(bool accepted, const std::string& message) {
	if (!accepted) {
		throw std::runtime_error(message);
	}
}

static void CompareFloat(float actual, EmissionWords& words, const std::string& context) {
	const auto expected = words.Float();
	const auto tolerance = 8e-6f * std::max(1.0f, std::abs(expected));
	words.ObserveError(std::abs(actual - expected));
	if (!std::isfinite(actual) || !std::isfinite(expected) || std::abs(actual - expected) > tolerance) {
		throw std::runtime_error(context + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected) + " actual_bits=" + std::to_string(std::bit_cast<std::uint32_t>(actual)) + " expected_bits=" + std::to_string(std::bit_cast<std::uint32_t>(expected)));
	}
}

static void CompareState(const sgre::scene::EmoteEmissionPool& pool, EmissionWords& words, const std::string& context) {
	Require(pool.Particles().size() == words.Next(), context + " actual owned particle cardinality");
	for (const auto& particle : pool.Particles()) {
		for (const auto value : {std::uint32_t{particle.New}, particle.Birth.Emitter, std::uint32_t{particle.Birth.FollowMesh}, std::uint32_t{particle.Birth.IgnoreCollision}, std::uint32_t{particle.Birth.FaceTravelDirection}, std::uint32_t{particle.Dynamic}, std::uint32_t{particle.NeedsTravelDirection}, particle.Id}) {
			Require(value == words.Next(), context + " actual particle identity/flag");
		}
		const auto name_size = words.Next();
		Require(name_size == particle.Birth.Motion.size(), context + " actual owned Motion name size");
		for (const auto ch : particle.Birth.Motion) {
			Require(static_cast<unsigned char>(ch) == words.Next(), context + " actual weighted Motion identity");
		}
		const auto& birth = particle.Birth;
		std::size_t field = 0;
		for (const auto value : {birth.Depth, birth.SourceOrigin.X, birth.SourceOrigin.Y, particle.InitialPosition.X, particle.InitialPosition.Y, particle.Previous.X, particle.Previous.Y, particle.Position.X, particle.Position.Y, particle.LocalPosition.X, particle.LocalPosition.Y, particle.ActualPosition.X, particle.ActualPosition.Y, birth.Velocity.X, birth.Velocity.Y, particle.Velocity.X, particle.Velocity.Y, particle.Acceleration.X, particle.Acceleration.Y, birth.AngularOffset, particle.Angle, birth.AngularVelocity, birth.ForceFactor, particle.Radius, birth.RadiusVelocity, birth.Lifespan, particle.Age, birth.FadeIn, birth.FadeOut}) {
			CompareFloat(value, words, context + " field " + std::to_string(field++));
		}
		Require(birth.MotionFrames == words.Next(), context + " actual motion frame count");
		for (const auto value : {birth.MotionStep, birth.SourceSize, particle.WaveAmplitude, birth.WaveAmplitudeVelocity, particle.WaveVector.X, particle.WaveVector.Y, birth.WaveFrequency, particle.WavePhase, birth.Gravity.X, birth.Gravity.Y, particle.TravelAngle, particle.SourceOriginScale}) {
			CompareFloat(value, words, context + " tailfield " + std::to_string(field++));
		}
	}
}

static sgre::scene::EmoteEmissionRange Range(float value, float variation, float minimum, float maximum) {
	return {value, variation, minimum, maximum};
}

static sgre::scene::EmoteEmissionEmitterDefinition Definition(bool rectangle, bool varied) {
	sgre::scene::EmoteEmissionEmitterDefinition definition;
	definition.Geometry = rectangle ? sgre::scene::EmoteEmissionGeometry::Rectangle : sgre::scene::EmoteEmissionGeometry::Point;
	definition.First = {-.25f, .1f};
	definition.Second = {.4f, .6f};
	definition.Angle = Range(45, varied ? 10.0f : 0.0f, 0, 360);
	definition.Frequency = Range(30, 0, 1, 100);
	definition.GenerationCount = Range(2, 0, 1, 4);
	auto& particle = definition.Particle;
	particle.PixelsPerMeter = 100;
	particle.Scale = Range(1, 0, .1f, 4);
	particle.TerminalScaleRatio = Range(.75f, 0, .1f, 4);
	particle.Density = Range(.3f, varied ? .1f : 0.0f, .1f, 2);
	particle.Velocity = Range(2, varied ? .25f : 0.0f, .5f, 4);
	particle.TerminalVelocityRatio = Range(.6f, varied ? .1f : 0.0f, .1f, 2);
	particle.Lifespan = Range(.15f, 0, .05f, 1);
	particle.FadeIn = Range(.01f, 0, 0, 1);
	particle.FadeOut = Range(.02f, 0, 0, 1);
	particle.SourceSize = 4;
	particle.SourceOrigin = {.001f, -.002f};
	particle.FaceTravelDirection = rectangle;
	particle.AngularVelocity = Range(5, 0, -100, 100);
	particle.AngularOffset = Range(10, varied ? 2.0f : 0.0f, -100, 100);
	particle.WaveAmplitude = Range(.05f, 0, 0, 1);
	particle.TerminalWaveAmplitudeRatio = Range(.5f, 0, 0, 2);
	particle.WaveFrequency = Range(1, 0, 0, 100);
	particle.WavePhase = Range(30, 0, 0, 360);
	particle.GravityStrength = .1f;
	particle.GravityDirection = 90;
	return definition;
}

static std::uint64_t ReadWide(EmissionWords& words) {
	const auto lower = words.Next();
	return std::uint64_t{lower} | (std::uint64_t{words.Next()} << 32);
}

static void ReplayEmitters(std::span<const std::uint32_t> input) {
	EmissionWords words(input);
	Require(words.Next() == 0x32455045, "native emitter vectors magic");
	const auto cases = words.Next();
	Require(cases > 0 && cases <= 64, "native emitter bounded cases");
	for (std::uint32_t c = 0; c != cases; ++c) {
		const auto ordinal = words.Next();
		const auto rectangle = words.Next() != 0;
		const auto varied = words.Next() != 0;
		const auto angle = words.Float();
		sgre::scene::EmoteEmissionRandomState random;
		random.State = ReadWide(words);
		random.Increment = ReadWide(words);
		sgre::scene::EmoteEmissionPoolDefinition pool_definition;
		pool_definition.MaximumParticles = 64;
		pool_definition.PixelsPerMeter = 100;
		sgre::scene::EmoteEmissionPool pool(pool_definition);
		auto definition = Definition(rectangle, varied);
		definition.Particle.MotionInterval = .02f;
		definition.Particle.MotionCycles = words.Next();
		const auto variants = words.Next();
		Require(variants <= 64, "native emitter bounded actual variants");
		for (std::uint32_t index = 0; index != variants; ++index) {
			sgre::scene::EmoteEmissionMotionVariant variant;
			const auto size = words.Next();
			Require(size <= 4096, "native emitter bounded actual variant name");
			for (std::uint32_t ch = 0; ch != size; ++ch) {
				variant.Motion.push_back(static_cast<char>(words.Next()));
			}
			variant.SourceSize = words.Next();
			variant.Frames = words.Next();
			variant.Weight = words.Next();
			definition.Particle.Motions.push_back(std::move(variant));
		}
		sgre::scene::EmoteEmissionEmitter emitter(pool, random, definition);
		sgre::scene::EmoteEmissionEmitterPosture posture;
		posture.Coordinate = {.3f, -.2f};
		posture.Depth = .1f;
		posture.Rotation = angle;
		posture.Cosine = std::cos(angle);
		posture.Sine = std::sin(angle);
		emitter.SetPosture(posture);
		const auto phases = words.Next();
		Require(phases <= 64, "native emitter bounded phases");
		for (std::uint32_t phase = 0; phase != phases; ++phase) {
			const auto context = "emitter " + std::to_string(ordinal) + " phase " + std::to_string(phase);
			const auto ticks = words.Float();
			emitter.Step(ticks);
			CompareState(pool, words, context + " births");
			pool.Update(ticks);
			CompareState(pool, words, context + " pool");
			Require(random.State == ReadWide(words) && random.Increment == ReadWide(words), context + " exact shared RNG draw order");
			CompareFloat(emitter.Countdown(), words, context + " actual countdown");
			Require(std::uint32_t{emitter.Emitted()} == words.Next(), context + " actual generation flag");
		}
	}
	words.RequireEnd();
	std::cout << "Native point/rectangle emitter replay PASS cases=" << cases << " maximum_absolute_error=" << words.MaximumError() << '\n';
}

static void TestSourceAndClockGuards() {
	sgre::scene::EmoteEmissionPoolDefinition pool_definition;
	sgre::scene::EmoteEmissionPool pool(pool_definition);
	sgre::scene::EmoteEmissionRandomState random;
	random.State = 123;
	random.Increment = 456;
	auto definition = Definition(false, false);
	definition.Frequency.Minimum = 0;
	sgre::scene::EmoteEmissionEmitter emitter(pool, random, definition);
	bool rejected = false;
	try {
		emitter.Step(1);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && random.State == 123 && pool.Particles().empty(), "missing actual emitter source posture rejects before RNG/birth");
	emitter.SetPosture({});
	emitter.Step(0);
	Require(!emitter.Emitted() && random.State == 123 && pool.Particles().empty(), "zero elapsed automatic cadence does not manufacture a first birth");
	emitter.Step(1);
	Require(emitter.Emitted() && pool.Particles().size() == 2 && random.State != 123, "positive actual automatic cadence produces owned particles through its genuine stream");
	const auto saved_random = random.State;
	auto invalid = definition;
	invalid.Particle.Motions.push_back({"real_motion", 4, 1, 0});
	rejected = false;
	try {
		sgre::scene::EmoteEmissionEmitter zero_weights(pool, random, invalid);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && random.State == saved_random, "unselectable actual Motion table rejects before touching the shared RNG");
}

int main(int argc, char** argv) {
	try {
		TestSourceAndClockGuards();
		if (argc == 1) {
			ReplayEmitters(kNativeEmitterWords);
		} else {
			Require(argc == 2, "optional original EPE1 vector path");
			std::ifstream stream(argv[1], std::ios::binary);
			Require(static_cast<bool>(stream), "native emitter vector file open");
			const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(stream), {}};
			Require(bytes.size() <= 8 * 1024 * 1024 && bytes.size() % 4 == 0, "native emitter bounded aligned vectors");
			std::vector<std::uint32_t> words;
			for (std::size_t offset = 0; offset != bytes.size(); offset += 4) {
				words.push_back(std::uint32_t{bytes[offset]} | (std::uint32_t{bytes[offset + 1]} << 8) | (std::uint32_t{bytes[offset + 2]} << 16) | (std::uint32_t{bytes[offset + 3]} << 24));
			}
			ReplayEmitters(words);
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
