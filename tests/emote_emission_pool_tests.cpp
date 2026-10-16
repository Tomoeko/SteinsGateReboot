#include "sgre/scene/emote_emission_pool.hpp"

#include "emote_emission_pool_vectors.hpp"

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

static sgre::resource::MotionPoint ReadPoint(EmissionWords& words) {
	const auto x = words.Float();
	const auto y = words.Float();
	return {x, y};
}

static sgre::scene::EmoteEmissionBirth ReadBirth(EmissionWords& words) {
	sgre::scene::EmoteEmissionBirth birth;
	birth.Emitter = words.Next();
	birth.FollowMesh = words.Next() != 0;
	birth.IgnoreCollision = words.Next() != 0;
	birth.FaceTravelDirection = words.Next() != 0;
	birth.Depth = words.Float();
	birth.Coordinate = ReadPoint(words);
	birth.Velocity = ReadPoint(words);
	birth.Acceleration = ReadPoint(words);
	birth.SourceOrigin = ReadPoint(words);
	birth.AngularOffset = words.Float();
	birth.Angle = words.Float();
	birth.AngularVelocity = words.Float();
	birth.ForceFactor = words.Float();
	birth.Radius = words.Float();
	birth.RadiusVelocity = words.Float();
	birth.Lifespan = words.Float();
	birth.FadeIn = words.Float();
	birth.FadeOut = words.Float();
	birth.MotionFrames = words.Next();
	birth.MotionStep = words.Float();
	birth.SourceSize = words.Float();
	birth.WaveAmplitude = words.Float();
	birth.WaveAmplitudeVelocity = words.Float();
	birth.WaveFrequency = words.Float();
	birth.WavePhase = words.Float();
	birth.Gravity = ReadPoint(words);
	return birth;
}

static void CompareState(const sgre::scene::EmoteEmissionPool& pool, EmissionWords& words, const std::string& context) {
	Require(pool.Particles().size() == words.Next(), context + " actual owned particle cardinality");
	for (const auto& particle : pool.Particles()) {
		for (const auto value : {std::uint32_t{particle.New}, particle.Birth.Emitter, std::uint32_t{particle.Birth.FollowMesh}, std::uint32_t{particle.Birth.IgnoreCollision}, std::uint32_t{particle.Birth.FaceTravelDirection}, std::uint32_t{particle.Dynamic}, std::uint32_t{particle.NeedsTravelDirection}, particle.Id}) {
			Require(value == words.Next(), context + " actual particle identity/flag");
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

static void Replay(std::span<const std::uint32_t> input) {
	EmissionWords words(input);
	Require(words.Next() == 0x31505045, "native pool vector magic");
	const auto cases = words.Next();
	Require(cases > 0 && cases <= 64, "native pool bounded case count");
	for (std::uint32_t case_index = 0; case_index != cases; ++case_index) {
		sgre::scene::EmoteEmissionPoolDefinition definition;
		definition.PixelsPerMeter = words.Float();
		definition.MaximumParticles = words.Next();
		definition.EmitterCount = 2;
		sgre::scene::EmoteEmissionPool pool(definition);
		const auto triggers = words.Next();
		Require(triggers <= 64, "native pool bounded triggers");
		for (std::uint32_t trigger = 0; trigger != triggers; ++trigger) {
			const auto source = words.Next();
			const auto target = words.Next();
			pool.AddPeriodicTrigger(source, target, words.Float());
		}
		const auto births = words.Next();
		Require(births <= 64, "native pool bounded birth count");
		for (std::uint32_t birth = 0; birth != births; ++birth) {
			pool.Spawn(ReadBirth(words));
			CompareState(pool, words, "case " + std::to_string(case_index) + " birth " + std::to_string(birth));
		}
		const auto phases = words.Next();
		Require(phases <= 64, "native pool bounded phases");
		for (std::uint32_t phase = 0; phase != phases; ++phase) {
			const auto context = "case " + std::to_string(case_index) + " phase " + std::to_string(phase);
			pool.Update(words.Float());
			CompareState(pool, words, context);
			pool.PrepareBounds(-2, -2, 2, 2, .4f);
			const auto queries = words.Next();
			Require(queries <= 64, "native pool bounded queries");
			for (std::uint32_t query = 0; query != queries; ++query) {
				sgre::scene::EmoteCollisionEllipse ellipse;
				ellipse.Center = ReadPoint(words);
				ellipse.Radius = {.25f, .1f};
				ellipse.Rotation = words.Float();
				for (const auto value : pool.SampleEllipse(ellipse)) {
					CompareFloat(value, words, context + " ellipse force");
				}
				for (const auto value : pool.SamplePoint(ellipse.Center, .2f)) {
					CompareFloat(value, words, context + " point force");
				}
			}
			Require(pool.Events().size() == words.Next(), context + " actual event cardinality");
			for (const auto& event : pool.Events()) {
				Require(event.SourceEmitter == words.Next() && event.TargetEmitter == words.Next() && static_cast<std::uint32_t>(event.Kind) == words.Next(), context + " actual event kind/order");
				for (const auto value : {event.LocalPosition.X, event.LocalPosition.Y, event.Position.X, event.Position.Y, event.Angle}) {
					CompareFloat(value, words, context + " actual event vector");
				}
			}
		}
	}
	words.RequireEnd();
	std::cout << "Native emission pool replay PASS cases=" << cases << " maximum_absolute_error=" << words.MaximumError() << '\n';
}

static void TestActualLifecycleAndRejectedInputs() {
	sgre::scene::EmoteEmissionPoolDefinition definition;
	definition.MaximumParticles = 1;
	definition.MaximumPendingBirths = 2;
	sgre::scene::EmoteEmissionPool pool(definition);
	auto rejected = [](auto&& action) {
		try {
			action();
		} catch (const std::runtime_error&) {
			return true;
		}
		return false;
	};
	Require(rejected([&] {
		static_cast<void>(pool.SamplePoint({}, 0));
	}),
	    "force sampling before actual bounds must reject");
	sgre::scene::EmoteEmissionBirth birth;
	birth.ForceFactor = .5f;
	birth.Velocity = {2, -1};
	birth.Lifespan = .1f;
	pool.Spawn(birth);
	pool.PrepareBounds(-1, -1, 1, 1, 0);
	Require(pool.SamplePoint({}, 0) == std::array<float, 3>{1, -.5f, 0}, "actual living particle velocity supplies force");
	Require(pool.SamplePoint({5, 5}, 0) == std::array<float, 3>{}, "nonoverlapping real particles do not supply force");
	pool.Update(1);
	Require(rejected([&] {
		static_cast<void>(pool.SamplePoint({}, 0));
	}),
	    "pool update invalidates prior actual force selection");
	const auto before = pool.Particles().size();
	birth.FollowMesh = true;
	Require(rejected([&] {
		pool.Spawn(birth);
	}) && pool.Particles().size() == before,
	    "unrecovered particle-IO transform rejects before birth");
	birth.FollowMesh = false;
	birth.Emitter = 2;
	Require(rejected([&] {
		pool.Spawn(birth);
	}) && pool.Particles().size() == before,
	    "foreign emitter rejects before birth");
	pool.Update(10);
	Require(pool.Particles().empty() && pool.Events().size() == 1 && pool.Events()[0].Kind == sgre::scene::EmoteEmissionEventKind::Death, "actual lifespan retires its force-producing owner and publishes death");
}

int main(int argc, char** argv) {
	try {
		TestActualLifecycleAndRejectedInputs();
		if (argc == 1) {
			Replay(kNativeEmissionPoolWords);
		} else {
			Require(argc == 2, "optional native EPP1 vector path");
			std::ifstream stream(argv[1], std::ios::binary);
			Require(static_cast<bool>(stream), "native emission vector file open");
			const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(stream), {}};
			Require(bytes.size() <= 8 * 1024 * 1024 && bytes.size() % 4 == 0, "native emission vector bounded aligned file");
			std::vector<std::uint32_t> words;
			for (std::size_t offset = 0; offset != bytes.size(); offset += 4) {
				words.push_back(std::uint32_t{bytes[offset]} | (std::uint32_t{bytes[offset + 1]} << 8) | (std::uint32_t{bytes[offset + 2]} << 16) | (std::uint32_t{bytes[offset + 3]} << 24));
			}
			Replay(words);
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
