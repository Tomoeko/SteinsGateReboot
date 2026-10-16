#include "sgre/scene/emote_jelly_physics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

static void Require(bool condition, const std::string& context) {
	if (!condition) {
		throw std::runtime_error(context);
	}
}

class NativeJellyReader {
public:
	explicit NativeJellyReader(std::span<const std::uint8_t> bytes) : m_Bytes(bytes) {
	}

	std::uint32_t Word() {
		Require(m_Cursor <= m_Bytes.size() && m_Bytes.size() - m_Cursor >= 4, "truncated actual Jelly entity vector");
		std::uint32_t word = 0;
		for (std::uint32_t shift = 0; shift < 32; shift += 8) {
			word |= static_cast<std::uint32_t>(m_Bytes[m_Cursor++]) << shift;
		}

		return word;
	}

	float Float() {
		return std::bit_cast<float>(Word());
	}

	bool Boolean() {
		const auto value = Word();
		Require(value <= 1, "actual Jelly Boolean vector input changed");

		return value != 0;
	}

	sgre::resource::MotionPoint Point() {
		const auto x = Float();
		const auto y = Float();

		return {x, y};
	}

	std::vector<sgre::resource::MotionPoint> Points(std::uint32_t limit) {
		const auto count = Word();
		Require(count <= limit, "actual Jelly point vector exceeds its bounded domain");
		std::vector<sgre::resource::MotionPoint> result(count);
		for (auto& point : result) {
			point = Point();
		}

		return result;
	}

	bool Complete() const {
		return m_Cursor == m_Bytes.size();
	}

private:
	std::span<const std::uint8_t> m_Bytes;
	std::size_t m_Cursor = 0;
};

static sgre::scene::EmoteJellyDefinition ReadDefinition(NativeJellyReader& reader) {
	sgre::scene::EmoteJellyDefinition definition;
	definition.Substeps = reader.Word();
	definition.Speed = reader.Float();
	definition.Gravity = reader.Float();
	definition.AutoMass = reader.Boolean();
	definition.Mass = reader.Float();
	definition.Density = reader.Float();
	definition.SizeHorizontal = reader.Float();
	definition.SizeVertical = reader.Float();
	definition.ParticleCount = reader.Word();
	definition.DefaultRotation = reader.Float();
	definition.TiltFactor = reader.Float();
	definition.StretchFactor = reader.Float();
	definition.HeightRate = reader.Float();
	definition.Friction = reader.Float();
	definition.ElasticHorizontal = reader.Float();
	definition.ElasticVertical = reader.Float();
	for (auto& value : definition.CoordinateScale) {
		value = reader.Float();
	}
	definition.Sensitivity = reader.Float();
	definition.CollisionRepulsion = reader.Float();
	definition.AdhocSimulation = reader.Boolean();
	definition.SampleDensity = reader.Float();
	definition.LimitLengthRatio = reader.Float();
	definition.ReverseSampleOrder = reader.Boolean();
	definition.CorrectFix = reader.Boolean();
	definition.CorrectScale = reader.Boolean();
	definition.Fixes = reader.Points(3);
	for (auto& value : definition.TipTilt) {
		value = reader.Float();
	}
	for (auto& value : definition.RigidRange) {
		value = reader.Float();
	}
	const auto count = reader.Word();
	Require(count <= 64, "native Jelly fixture tress count exceeds its captured domain");
	definition.Tresses.resize(count);
	for (auto& tress : definition.Tresses) {
		tress.Coordinate = reader.Point();
		tress.Radius = reader.Float();
		for (auto& value : tress.Tilt) {
			value = reader.Float();
		}
	}

	return definition;
}

static void Close(float actual, float expected, float& maximum, const std::string& context) {
	const float error = std::abs(actual - expected);
	maximum = std::max(maximum, error);
	Require(std::isfinite(actual) && error <= 0.000008f * std::max(1.0f, std::abs(expected)), "actual Jelly entity differs from original: " + context + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected) + " actual_bits=" + std::to_string(std::bit_cast<std::uint32_t>(actual)) + " expected_bits=" + std::to_string(std::bit_cast<std::uint32_t>(expected)));
}

static void ComparePoint(const sgre::resource::MotionPoint& actual, const sgre::resource::MotionPoint& expected, float& maximum, const std::string& context) {
	Close(actual.X, expected.X, maximum, context + " X");
	Close(actual.Y, expected.Y, maximum, context + " Y");
}

static void ComparePoints(std::span<const sgre::resource::MotionPoint> actual, NativeJellyReader& reader, float& maximum, const std::string& context) {
	const auto count = reader.Word();
	Require(count == actual.size(), context + " actual native cardinality differs");
	for (std::size_t index = 0; index < actual.size(); ++index) {
		ComparePoint(actual[index], reader.Point(), maximum, context + " point " + std::to_string(index));
	}
}

static void CompareParticles(std::span<const sgre::scene::EmoteBobbleParticle> particles, NativeJellyReader& reader, float& maximum, const std::string& context) {
	Require(reader.Word() == particles.size(), context + " actual particle cardinality differs");
	for (std::size_t index = 0; index < particles.size(); ++index) {
		const auto& particle = particles[index];
		const auto label = context + " particle " + std::to_string(index);
		for (const auto& field : {particle.Previous, particle.Predicted, particle.Position, particle.Velocity, particle.Force}) {
			for (const auto value : field) {
				Close(value, reader.Float(), maximum, label + " physical vector");
			}
		}
		Close(particle.Friction, reader.Float(), maximum, label + " friction");
		Close(particle.Mass, reader.Float(), maximum, label + " mass");
		Close(particle.InverseMass, reader.Float(), maximum, label + " inverse mass");
		Require(particle.Fixed == reader.Boolean(), label + " fixed state differs");
		// The native constructor leaves root spring bytes uninitialized; only free-particle coefficients are live.
		if (index != 0) {
			Close(particle.AlignmentSpring, reader.Float(), maximum, label + " alignment spring");
			Close(particle.LengthSpring, reader.Float(), maximum, label + " length spring");
		}
	}
}

static void RejectMalformed(const sgre::scene::EmoteJellyDefinition& valid) {
	const auto reject = [](const auto& definition) {
		bool rejected = false;
		try {
			sgre::scene::EmoteJellyPhysics physics(definition);
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Require(rejected, "owning Jelly must reject malformed actual constructor inputs");
	};

	auto changed = valid;
	changed.Substeps = 0;
	reject(changed);
	changed = valid;
	changed.ParticleCount = 1;
	reject(changed);
	changed = valid;
	changed.Tresses.front().Tilt[0] = 2;
	reject(changed);
	changed = valid;
	changed.Speed = std::numeric_limits<float>::infinity();
	reject(changed);
	changed = valid;
	changed.SizeHorizontal = 0;
	reject(changed);
	changed = valid;
	changed.RigidRange[0] = std::numeric_limits<float>::quiet_NaN();
	reject(changed);
}

static void Replay(std::span<const std::uint8_t> bytes) {
	NativeJellyReader reader(bytes);
	Require(reader.Word() == 0x3150454aU, "actual Jelly entity vector magic changed");
	const auto count = reader.Word();
	Require(count >= 1 && count <= 64, "actual Jelly constructor vector count is out of bounds");
	std::size_t phases = 0;
	std::size_t tresses = 0;
	float maximum = 0;
	for (std::uint32_t case_index = 0; case_index < count; ++case_index) {
		const auto original_index = reader.Word();
		const auto context = "case " + std::to_string(original_index);
		const auto definition = ReadDefinition(reader);
		sgre::scene::EmoteJellyPhysics physics(definition);
		Require(physics.EncodedDegree() == reader.Word(), context + " actual degree differs");
		ComparePoint(physics.Origin(), reader.Point(), maximum, context + " constructor origin");
		Require(physics.TressCount() == definition.Tresses.size(), context + " lost actual models");
		for (std::size_t part = 0; part < physics.TressCount(); ++part) {
			const auto part_context = context + " tress " + std::to_string(part);
			ComparePoints(physics.InitialRings(part), reader, maximum, part_context + " original constructor rings");
			const auto bindings = physics.Bindings(part);
			Require(reader.Word() == bindings.size(), part_context + " binding count differs");
			for (std::size_t index = 0; index < bindings.size(); ++index) {
				const auto& binding = bindings[index];
				Require(binding.Section == reader.Word(), part_context + " original ring binding differs");
				Require(binding.Direction == reader.Word(), part_context + " original direction binding differs");
				const auto label = part_context + " binding " + std::to_string(index);
				Close(binding.Circumference, reader.Float(), maximum, label + " circumference");
				Close(binding.Longitudinal, reader.Float(), maximum, label + " longitudinal");
				ComparePoint(binding.Rest, reader.Point(), maximum, label + " normalized rest");
				Require(binding.Priority == std::bit_cast<std::int32_t>(reader.Word()), label + " signed primary order differs");
				Require(binding.Order == reader.Float(), label + " float secondary order differs");
			}
			++tresses;
		}

		const auto phase_count = reader.Word();
		Require(phase_count >= 1 && phase_count <= 64, context + " phase count changed");
		for (std::uint32_t phase = 0; phase < phase_count; ++phase) {
			sgre::scene::EmotePhysicalPosture posture;
			for (auto& value : posture.Coordinate) {
				value = reader.Float();
			}
			posture.Rotation = reader.Float();
			posture.Cosine = reader.Float();
			posture.Sine = reader.Float();
			const auto reset = reader.Boolean();
			const auto delta = reader.Float();
			physics.SetPosture(posture);
			if (reset) {
				physics.Initialize();
			} else {
				physics.ClearForces();
				physics.Update(delta);
			}
			const auto samples = physics.PrepareSamples();
			const auto& output = physics.PrepareLattice(0, 1, {});
			const auto phase_context = context + " phase " + std::to_string(phase);
			for (std::size_t part = 0; part < physics.TressCount(); ++part) {
				const auto part_context = phase_context + " tress " + std::to_string(part);
				CompareParticles(physics.Particles(part), reader, maximum, part_context);
				ComparePoints(physics.PreparedRings(part), reader, maximum, part_context + " actual dynamic rings");
				ComparePoints(physics.PreparedTargets(part), reader, maximum, part_context + " actual fitted targets");
			}
			ComparePoints(samples, reader, maximum, phase_context + " global fitted samples");
			ComparePoints(output.Controls, reader, maximum, phase_context + " normalized mesh controls");
			++phases;
		}

		if (case_index == 0) {
			RejectMalformed(definition);
		}
	}
	Require(reader.Complete(), "actual Jelly entity vector has trailing observations");
	std::cout << "Jelly owning actual constructor " << count << " cases/" << tresses << " tresses/" << phases << " reset-update-output phases PASS maximum=" << maximum << '\n';
}

// Complete original310740 input and original316b10/316230/315c30 observations.
// Only expected outputs are embedded; no particle, sample or lattice setter exists in the owner.
static constexpr std::array<std::uint32_t, 1997> kNativeDefault{
    0x3150454aU, 0x00000001U, 0x00000000U, 0x00000001U, 0x3f800000U, 0x411ccccdU, 0x00000000U, 0x40200000U,
    0x40000000U, 0x3f800000U, 0x3fc00000U, 0x00000002U, 0x00000000U, 0x3e800000U, 0x3f000000U, 0x3eb33333U,
    0x3e000000U, 0x3e800000U, 0x3e800000U, 0x3f800000U, 0x3f800000U, 0x3f800000U, 0x3f800000U, 0x42c80000U,
    0x00000000U, 0x40000000U, 0x40000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000001U, 0x3f000000U,
    0x3f000000U, 0x00000000U, 0x00000000U, 0x3f800000U, 0x00000000U, 0x00000000U, 0x3f800000U, 0x3f800000U,
    0x00000001U, 0x3f000000U, 0x3f000000U, 0x3e800000U, 0x00000000U, 0x00000000U, 0x3f800000U, 0x00000033U,
    0x3f000000U, 0x3f000000U, 0x00000030U, 0x00000000U, 0x3e800001U, 0xbdc3ef18U, 0x3e6c8360U, 0xbe3504f4U,
    0x3e3504f4U, 0xbe6c8360U, 0x3dc3ef17U, 0xbe800001U, 0xb23bbd2fU, 0xbe6c8360U, 0xbdc3ef1aU, 0xbe3504f4U,
    0xbe3504f4U, 0xbdc3ef12U, 0xbe6c8362U, 0x32bbbd2fU, 0xbe800001U, 0x3dc3ef17U, 0xbe6c8360U, 0x3e3504f6U,
    0xbe3504f2U, 0x3e6c8363U, 0xbdc3ef0dU, 0x3e800001U, 0x314cde30U, 0x3e6c835fU, 0x3dc3ef1dU, 0x3e3504f0U,
    0x3e3504f8U, 0x3dc3ef17U, 0x3e6c8361U, 0x00000000U, 0x3daa99bdU, 0xbd02926fU, 0x3d9d9d46U, 0xbd7143feU,
    0x3d7143feU, 0xbd9d9d46U, 0x3d02926eU, 0xbdaa99bdU, 0xb17a38beU, 0xbd9d9d46U, 0xbd029270U, 0xbd7143feU,
    0xbd7143feU, 0xbd02926bU, 0xbd9d9d47U, 0x31fa38beU, 0xbdaa99bdU, 0x3d02926eU, 0xbd9d9d46U, 0x3d714401U,
    0xbd7143fbU, 0x3d9d9d48U, 0xbd029268U, 0x3daa99bdU, 0x30888693U, 0x3d9d9d45U, 0x3d029272U, 0x3d7143f9U,
    0x3d714403U, 0x3d02926eU, 0x3d9d9d46U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000006U, 0x00000000U, 0x00000005U, 0x3eaba54aU, 0x3ef70a0eU,
    0x3eb61963U, 0x3ee38e39U, 0x00000000U, 0x3f800000U, 0x00000000U, 0x00000002U, 0x3f2a2d5bU, 0x3ef70a0bU,
    0x3eb61963U, 0x3f0e38e4U, 0x00000000U, 0x40000000U, 0x00000000U, 0x00000007U, 0x3f7ffffaU, 0x3efff350U,
    0x3f000000U, 0x3ec71c72U, 0x00000000U, 0x3f800000U, 0x00000001U, 0x00000000U, 0x00000000U, 0x3f800000U,
    0x3f000000U, 0x3f000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x0000000aU, 0x3f2a2d52U, 0x3ef70a0bU,
    0x3f24f34eU, 0x3ee38e39U, 0x00000000U, 0x3f800000U, 0x00000000U, 0x0000000dU, 0x3eaba535U, 0x3ef70a09U,
    0x3f24f34eU, 0x3f0e38e4U, 0x00000000U, 0x40000000U, 0x00000008U, 0x00000000U, 0x80000000U, 0x00000000U,
    0x00000000U, 0x3f800000U, 0x00000000U, 0x00000001U, 0x3f800000U, 0x00000002U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x3e000000U, 0x00000000U, 0x00000000U,
    0x00000001U, 0x00000000U, 0x00000000U, 0x3db33333U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x3db33333U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x3e000000U, 0x40200000U, 0x3ecccccdU, 0x00000000U, 0x3e800000U, 0x3e800000U, 0x00000030U, 0x00000000U,
    0x3e800001U, 0xbdc3ef18U, 0x3e6c8360U, 0xbe3504f4U, 0x3e3504f4U, 0xbe6c8360U, 0x3dc3ef17U, 0xbe800001U,
    0xb23bbd2fU, 0xbe6c8360U, 0xbdc3ef1aU, 0xbe3504f4U, 0xbe3504f4U, 0xbdc3ef12U, 0xbe6c8362U, 0x32bbbd2fU,
    0xbe800001U, 0x3dc3ef17U, 0xbe6c8360U, 0x3e3504f6U, 0xbe3504f2U, 0x3e6c8363U, 0xbdc3ef0dU, 0x3e800001U,
    0x314cde30U, 0x3e6c835fU, 0x3dc3ef1dU, 0x3e3504f0U, 0x3e3504f8U, 0x3dc3ef17U, 0x3e6c8361U, 0x00000000U,
    0x3daa99bdU, 0xbd02926fU, 0x3d9d9d46U, 0xbd7143feU, 0x3d7143feU, 0xbd9d9d46U, 0x3d02926eU, 0xbdaa99bdU,
    0xb17a38beU, 0xbd9d9d46U, 0xbd029270U, 0xbd7143feU, 0xbd7143feU, 0xbd02926bU, 0xbd9d9d47U, 0x31fa38beU,
    0xbdaa99bdU, 0x3d02926eU, 0xbd9d9d46U, 0x3d714401U, 0xbd7143fbU, 0x3d9d9d48U, 0xbd029268U, 0x3daa99bdU,
    0x30888693U, 0x3d9d9d45U, 0x3d029272U, 0x3d7143f9U, 0x3d714403U, 0x3d02926eU, 0x3d9d9d46U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000006U,
    0x3eb61964U, 0x3ee38e39U, 0x3eb61963U, 0x3f0e38e4U, 0x3f000000U, 0x3ec71c72U, 0x3f000000U, 0x3f000000U,
    0x3f24f34eU, 0x3ee38e39U, 0x3f24f34eU, 0x3f0e38e4U, 0x00000010U, 0x00000000U, 0x00000000U, 0x3eaaaaabU,
    0x00000000U, 0x3f2aaaabU, 0x00000000U, 0x3f800000U, 0x00000000U, 0x00000000U, 0x3eaaaaabU, 0x3eaaaaacU,
    0x3eaaaaabU, 0x3f2aaaabU, 0x3eaaaaaaU, 0x3f800000U, 0x3eaaaaabU, 0x00000000U, 0x3f2aaaabU, 0x3eaaaaabU,
    0x3f2aaaabU, 0x3f2aaaaaU, 0x3f2aaaabU, 0x3f800000U, 0x3f2aaaabU, 0x00000000U, 0x3f800000U, 0x3eaaaaabU,
    0x3f800000U, 0x3f2aaaabU, 0x3f800000U, 0x3f800000U, 0x3f800000U, 0x00000010U, 0xb3800000U, 0x33800000U,
    0x3eaaaaaaU, 0x33800000U, 0x3f2aaaa9U, 0x33800000U, 0x3f7fffffU, 0x33800000U, 0xb3800000U, 0x3eaaaaaeU,
    0x3eaaaab2U, 0x3eaaaab7U, 0x3f2aaaabU, 0x3eaaaaa0U, 0x3f800004U, 0x3eaaaaaeU, 0xb3800000U, 0x3f2aaaabU,
    0x3eaaaab0U, 0x3f2aaaa5U, 0x3f2aaaa4U, 0x3f2aaaaeU, 0x3f7ffffbU, 0x3f2aaaabU, 0xb3800000U, 0x3f800000U,
    0x3eaaaaaaU, 0x3f800004U, 0x3f2aaaa9U, 0x3f7ffffdU, 0x3f7fffffU, 0x3f800000U, 0x3d4ccccdU, 0xbccccccdU,
    0x3c23d70aU, 0x3e000000U, 0x3f7e00abU, 0x3dff5577U, 0x00000000U, 0x40000000U, 0x00000002U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x3d4ccccdU, 0xbccccccdU, 0x3c23d70aU, 0x3d4ccccdU, 0xbccccccdU, 0x3c23d70aU,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x3e000000U, 0x00000000U,
    0x00000000U, 0x00000001U, 0x39330328U, 0x36a6bf9eU, 0x3db33347U, 0x3a32c697U, 0x37ff2542U, 0x3db33368U,
    0x3a32c697U, 0x37ff2542U, 0x3db33368U, 0x3cfac4d9U, 0x3ac7b349U, 0x3776fbffU, 0x00000000U, 0x00000000U,
    0x00000000U, 0x3e000000U, 0x40200000U, 0x3ecccccdU, 0x00000000U, 0x3e800000U, 0x3e800000U, 0x00000030U,
    0x3c9a4420U, 0x3e646714U, 0xbd96facaU, 0x3e44da94U, 0xbe16f9bcU, 0x3e03703eU, 0xbe43ae8dU, 0x3d287628U,
    0xbe4acd7aU, 0xbd661125U, 0xbe2b40fbU, 0xbe184a32U, 0xbdd3ad48U, 0xbe63c688U, 0xbc841f6cU, 0xbe883daeU,
    0x3da63bc7U, 0xbe8bcd23U, 0x3e31e3caU, 0xbe780dc8U, 0x3e7d6023U, 0xbe36a36eU, 0x3e950a7aU, 0xbdbaa16fU,
    0x3e9899f0U, 0x3bca22b8U, 0x3e88d3b0U, 0x3dca2e02U, 0x3e503d06U, 0x3e309358U, 0x3dedd4acU, 0x3e5d4828U,
    0xbc1ad4afU, 0x3da5337eU, 0xbd21346aU, 0x3d900493U, 0xbd815e05U, 0x3d49d65dU, 0xbd9e397bU, 0x3ca9dc2bU,
    0xbda2c7ebU, 0xbc338d85U, 0xbd8e57bfU, 0xbd27da87U, 0xbd480b0cU, 0xbd849f79U, 0xbca90a97U, 0xbda11e74U,
    0x3c312d89U, 0xbda5139aU, 0x3d26ca9dU, 0xbd8fe4afU, 0x3d842920U, 0xbd499690U, 0x3da10496U, 0xbca95c8bU,
    0x3da59305U, 0x3c348ca8U, 0x3d9122d9U, 0x3d281a53U, 0x3d4da13aU, 0x3d84bf5fU, 0x3cb43709U, 0x3da13e58U,
    0x3a32c697U, 0x37ff2542U, 0x3a32c697U, 0x37ff2542U, 0x3a32c697U, 0x37ff2542U, 0x3a32c697U, 0x37ff2542U,
    0x3a32c697U, 0x37ff2542U, 0x3a32c697U, 0x37ff2542U, 0x3a32c697U, 0x37ff2542U, 0x3a32c697U, 0x37ff2542U,
    0x3a32c697U, 0x37ff2542U, 0x3a32c697U, 0x37ff2542U, 0x3a32c697U, 0x37ff2542U, 0x3a32c697U, 0x37ff2542U,
    0x3a32c697U, 0x37ff2542U, 0x3a32c697U, 0x37ff2542U, 0x3a32c697U, 0x37ff2542U, 0x3a32c697U, 0x37ff2542U,
    0x00000006U, 0x3eab48daU, 0x3ee8b209U, 0x3eab48daU, 0x3f10a0adU, 0x3ef44636U, 0x3eccbeaeU, 0x3ee88b42U,
    0x3f0549b5U, 0x3f1f0a4cU, 0x3ee8f34bU, 0x3f1f0a4cU, 0x3f10c14eU, 0x00000010U, 0x00000000U, 0x00000000U,
    0x3eaaaaabU, 0x00000000U, 0x3f2aaaabU, 0x00000000U, 0x3f800000U, 0x00000000U, 0x00000000U, 0x3eaaaaabU,
    0x3e9f0c5dU, 0x3eb01aadU, 0x3f24e467U, 0x3eafe05eU, 0x3f800000U, 0x3eaaaaabU, 0x00000000U, 0x3f2aaaabU,
    0x3ea1762dU, 0x3f2cb819U, 0x3f25d846U, 0x3f2cc02eU, 0x3f800000U, 0x3f2aaaabU, 0x00000000U, 0x3f800000U,
    0x3eaaaaabU, 0x3f800000U, 0x3f2aaaabU, 0x3f800000U, 0x3f800000U, 0x3f800000U, 0x00000010U, 0x3cd4b4c0U,
    0xbc3f9180U, 0x3eb7f5f9U, 0xbc3f9180U, 0x3f315051U, 0xbc3f9180U, 0x3f8352d4U, 0xbc3f9180U, 0x3cd4b4d0U,
    0x3ea4ae21U, 0x3e971710U, 0x3eb51480U, 0x3f22d3d6U, 0x3eb19436U, 0x3f8352d8U, 0x3ea4ae21U, 0x3cd4b4d0U,
    0x3f27ac65U, 0x3eaacf46U, 0x3f2a64d7U, 0x3f278afaU, 0x3f2b96c0U, 0x3f8352d1U, 0x3f27ac65U, 0x3cd4b4c0U,
    0x3f7d01bcU, 0x3eb7f5f9U, 0x3f7d01c4U, 0x3f315051U, 0x3f7d01b8U, 0x3f8352d4U, 0x3f7d01bbU, 0x3dcccccdU,
    0xbd4ccccdU, 0x3ca3d70aU, 0xbe4ccccdU, 0x3f7ae5a5U, 0xbe4b6ff9U, 0x00000000U, 0x40500000U, 0x00000002U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x3dcccccdU, 0xbd4ccccdU, 0x3ca3d70aU, 0x3dcccccdU, 0xbd4ccccdU,
    0x3ca3d70aU, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x3e000000U,
    0x00000000U, 0x00000000U, 0x00000001U, 0x3ad2d0f0U, 0x38c82a8eU, 0x3db333b6U, 0x3aa9c315U, 0x394cb59cU,
    0x3db333d3U, 0x3aa9c315U, 0x394cb59cU, 0x3db333d3U, 0xbc8dca28U, 0x3bb4acdfU, 0x374850ecU, 0x00000000U,
    0x00000000U, 0x00000000U, 0x3e000000U, 0x40200000U, 0x3ecccccdU, 0x00000000U, 0x3e800000U, 0x3e800000U,
    0x00000030U, 0x3e194264U, 0x3e47b274U, 0x3d557eb7U, 0x3e480fccU, 0xbd1c3130U, 0x3e222c8dU, 0xbddbdf0eU,
    0x3db79aa8U, 0xbe147f41U, 0xb9ae6a80U, 0xbe14dc9aU, 0xbdc873d8U, 0xbdddf2b4U, 0xbe40a5e5U, 0xbd22687cU,
    0xbe83c491U, 0x3d4e29a4U, 0xbe970c6dU, 0x3e176d1eU, 0xbe973b19U, 0x3e73d91bU, 0xbe844978U, 0x3e9d5e2cU,
    0xbe4233b6U, 0x3eb0a606U, 0xbdcc1e62U, 0x3eb0d4b2U, 0xbb0b1e10U, 0x3e9de311U, 0x3db47f08U, 0x3e7566eeU,
    0x3e2122bbU, 0x3c85c697U, 0x3d984c6eU, 0xbc4ce653U, 0x3d973368U, 0xbd20bb45U, 0x3d7e4a3bU, 0xbd74f5e8U,
    0x3d279633U, 0xbd918b63U, 0x3c5df770U, 0xbd920c0aU, 0xbc81dc11U, 0xbd77d2a1U, 0xbd2f5831U, 0xbd2503dbU,
    0xbd8178b1U, 0xbc611c62U, 0xbd977fb8U, 0x3c775717U, 0xbd9666b2U, 0x3d2b577aU, 0xbd7cb0ceU, 0x3d7f921eU,
    0xbd25fcc2U, 0x3d96d97bU, 0xbc5791c4U, 0x3d975a21U, 0x3c850eebU, 0x3d813767U, 0x3d30f1a2U, 0x3d2fa010U,
    0x3d824567U, 0x3aa9c315U, 0x394cb59cU, 0x3aa9c315U, 0x394cb59cU, 0x3aa9c315U, 0x394cb59cU, 0x3aa9c315U,
    0x394cb59cU, 0x3aa9c315U, 0x394cb59cU, 0x3aa9c315U, 0x394cb59cU, 0x3aa9c315U, 0x394cb59cU, 0x3aa9c315U,
    0x394cb59cU, 0x3aa9c315U, 0x394cb59cU, 0x3aa9c315U, 0x394cb59cU, 0x3aa9c315U, 0x394cb59cU, 0x3aa9c315U,
    0x394cb59cU, 0x3aa9c315U, 0x394cb59cU, 0x3aa9c315U, 0x394cb59cU, 0x3aa9c315U, 0x394cb59cU, 0x3aa9c315U,
    0x394cb59cU, 0x00000006U, 0x3e9dc896U, 0x3ee8dd3eU, 0x3e9dc896U, 0x3f104272U, 0x3ee4b00aU, 0x3ecd75caU,
    0x3ec95d5eU, 0x3f050c98U, 0x3f16bf2eU, 0x3ee939d0U, 0x3f16bf2eU, 0x3f1070bbU, 0x00000010U, 0x00000000U,
    0x00000000U, 0x3eaaaaabU, 0x00000000U, 0x3f2aaaabU, 0x00000000U, 0x3f800000U, 0x00000000U, 0x00000000U,
    0x3eaaaaabU, 0x3e902ed4U, 0x3eb03143U, 0x3f1cd4c9U, 0x3eaff1f2U, 0x3f800000U, 0x3eaaaaabU, 0x00000000U,
    0x3f2aaaabU, 0x3e95d311U, 0x3f2c72a0U, 0x3f1f224dU, 0x3f2c8561U, 0x3f800000U, 0x3f2aaaabU, 0x00000000U,
    0x3f800000U, 0x3eaaaaabU, 0x3f800000U, 0x3f2aaaabU, 0x3f800000U, 0x3f800000U, 0x3f800000U, 0x00000010U,
    0x3d783848U, 0xbc370280U, 0x3ec9b1b8U, 0xbc370280U, 0x3f3a2e30U, 0xbc370280U, 0x3f87c1c3U, 0xbc370280U,
    0x3d783850U, 0x3ea4f298U, 0x3e81c0a6U, 0x3eb73dcbU, 0x3f15b2feU, 0x3eb2e9cdU, 0x3f87c1c7U, 0x3ea4f299U,
    0x3d783848U, 0x3f27cea1U, 0x3eaed36aU, 0x3f28ff64U, 0x3f21bf24U, 0x3f2ad247U, 0x3f87c1c2U, 0x3f27cea0U,
    0x3d783850U, 0x3f7d23f8U, 0x3ec9b1b6U, 0x3f7d2400U, 0x3f3a2e30U, 0x3f7d23f4U, 0x3f87c1c3U, 0x3f7d23f8U,
    0x3e19999aU, 0xbd99999aU, 0x3cf5c28fU, 0x3e99999aU, 0x3f7490efU, 0x3e974e6dU, 0x00000000U, 0x3f400000U,
    0x00000002U, 0x00000000U, 0x00000000U, 0x00000000U, 0x3e19999aU, 0xbd99999aU, 0x3cf5c28fU, 0x3e19999aU,
    0xbd99999aU, 0x3cf5c28fU, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x3e000000U, 0x00000000U, 0x00000000U, 0x00000001U, 0x3aa9c315U, 0x394cb59cU, 0x3db333d3U, 0x3b233534U,
    0xb9218988U, 0x3db271e2U, 0x3b233534U, 0xb9218988U, 0x3db271e2U, 0x3dc382d4U, 0xbce48be6U, 0xbcf20c48U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x3e000000U, 0x40200000U, 0x3ecccccdU, 0x00000000U, 0x3e800000U,
    0x3e800000U, 0x00000030U, 0x3d9be4c6U, 0x3e27c424U, 0xbc1e2c70U, 0x3df0659cU, 0xbd91a896U, 0x3d2a8ee0U,
    0xbdca9a0eU, 0xbd54697aU, 0xbdb5eeaeU, 0xbe187404U, 0xbd2d9800U, 0xbe70492fU, 0x3d08a454U, 0xbe979d59U,
    0x3e01e72eU, 0xbea5d9b8U, 0x3e6540d2U, 0xbea0aedfU, 0x3e9e8afdU, 0xbe88e634U, 0x3ebe03c0U, 0xbe443d4fU,
    0x3ecc401eU, 0xbdc8fe6bU, 0x3ec71546U, 0xba92cb00U, 0x3eaf4c9aU, 0x3dad5f2eU, 0x3e88850dU, 0x3e15a11bU,
    0x3e314c08U, 0x3e3219d5U, 0xbc939e7cU, 0x3d8779aeU, 0xbd29bde5U, 0x3d55144aU, 0xbd6e4788U, 0x3cf5584bU,
    0xbd867e7eU, 0x3b57e68fU, 0xbd8098b8U, 0xbcc3aba5U, 0xbd4cb093U, 0xbd425da6U, 0xbceef14cU, 0xbd82b375U,
    0xbb682ec4U, 0xbd905e7dU, 0x3cbc6bccU, 0xbd881b38U, 0x3d3e248bU, 0xbd56575eU, 0x3d815718U, 0xbcf7de6bU,
    0x3d90b1d3U, 0xbb6c1761U, 0x3d8acc0cU, 0x3cc1257dU, 0x3d611737U, 0x3d411a94U, 0x3d0bdf47U, 0x3d8211ecU,
    0x3c0ba657U, 0x3d8fbcf3U, 0x3b233534U, 0xb9218988U, 0x3b233534U, 0xb9218988U, 0x3b233534U, 0xb9218988U,
    0x3b233534U, 0xb9218988U, 0x3b233534U, 0xb9218988U, 0x3b233534U, 0xb9218988U, 0x3b233534U, 0xb9218988U,
    0x3b233534U, 0xb9218988U, 0x3b233534U, 0xb9218988U, 0x3b233534U, 0xb9218988U, 0x3b233534U, 0xb9218988U,
    0x3b233534U, 0xb9218988U, 0x3b233534U, 0xb9218988U, 0x3b233534U, 0xb9218988U, 0x3b233534U, 0xb9218988U,
    0x3b233534U, 0xb9218988U, 0x00000006U, 0x3e9bad80U, 0x3ef72974U, 0x3e9bad80U, 0x3f16e68fU, 0x3ee197a4U,
    0x3edd1f1cU, 0x3ec32c46U, 0x3f13a495U, 0x3f14cfeaU, 0x3ef81c06U, 0x3f14cfebU, 0x3f175fd7U, 0x00000010U,
    0x00000000U, 0x00000000U, 0x3eaaaaabU, 0x00000000U, 0x3f2aaaabU, 0x00000000U, 0x3f800000U, 0x00000000U,
    0x00000000U, 0x3eaaaaabU, 0x3e8d9c8aU, 0x3ebf4813U, 0x3f1afa1bU, 0x3ebe4cffU, 0x3f800000U, 0x3eaaaaabU,
    0x00000000U, 0x3f2aaaabU, 0x3e93e84eU, 0x3f321d25U, 0x3f1d9a62U, 0x3f323bf1U, 0x3f800000U, 0x3f2aaaabU,
    0x00000000U, 0x3f800000U, 0x3eaaaaabU, 0x3f800000U, 0x3f2aaaabU, 0x3f800000U, 0x3f800000U, 0x3f800000U,
    0x00000010U, 0x3d8a5c00U, 0xbd31e450U, 0x3ecd41acU, 0xbd31e450U, 0x3f3bf62aU, 0xbd31e450U, 0x3f88a5c1U,
    0xbd31e450U, 0x3d8a5c00U, 0x3e946e22U, 0x3e8098ebU, 0x3ed4a3d8U, 0x3f114f84U, 0x3ec5c663U, 0x3f88a5c4U,
    0x3e946e24U, 0x3d8a5c04U, 0x3f1f8c66U, 0x3eb22409U, 0x3f283960U, 0x3f1f838eU, 0x3f2d2893U, 0x3f88a5beU,
    0x3f1f8c66U, 0x3d8a5c08U, 0x3f74e1bcU, 0x3ecd41aeU, 0x3f74e1c4U, 0x3f3bf62bU, 0x3f74e1b8U, 0x3f88a5c0U,
    0x3f74e1bbU, 0x3e4ccccdU, 0xbdcccccdU, 0x3d23d70aU, 0x00000000U, 0x3f800000U, 0x00000000U, 0x00000000U,
    0x3f800000U, 0x00000002U, 0x00000000U, 0x00000000U, 0x00000000U, 0x3e4ccccdU, 0xbdcccccdU, 0x3d23d70aU,
    0x3e4ccccdU, 0xbdcccccdU, 0x3d23d70aU, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x3e000000U, 0x00000000U, 0x00000000U, 0x00000001U, 0x3b233534U, 0xb9218988U, 0x3db271e2U,
    0x3d41e106U, 0xbcb8cdbbU, 0x3d9c5a76U, 0x3d41e106U, 0xbcb8cdbbU, 0x3d9c5a76U, 0x402bd700U, 0xbfabb638U,
    0xbf25574bU, 0x00000000U, 0x00000000U, 0x00000000U, 0x3e000000U, 0x40200000U, 0x3ecccccdU, 0x00000000U,
    0x3e800000U, 0x3e800000U, 0x00000030U, 0x3e4cccccU, 0x3e19999cU, 0x3dd5aa80U, 0x3e061cfaU, 0x3cbe3ec0U,
    0x3d9d3d1cU, 0xbcfdb4a0U, 0xbb8ddb50U, 0xbd4cccd8U, 0xbdcccccdU, 0xbcfdb4a0U, 0xbe485df3U, 0x3cbe3ec0U,
    0xbe8db5adU, 0x3dd5aa86U, 0xbea974e4U, 0x3e4ccccdU, 0xbeb33334U, 0x3e97622cU, 0xbea974e3U, 0x3ec0e8e1U,
    0xbe8db5acU, 0x3edca818U, 0xbe485decU, 0x3ee66667U, 0xbdccccccU, 0x3edca816U, 0xbb8ddaf0U, 0x3ec0e8deU,
    0x3d9d3d24U, 0x3e97622cU, 0x3e061cfbU, 0x3d41e106U, 0x3d4329e0U, 0x3caef7a0U, 0x3d280622U, 0xba2db1c0U,
    0x3cca7949U, 0xbc7beda4U, 0x3a9f0ab0U, 0xbca84a76U, 0xbcd43dd4U, 0xbc7beda4U, 0xbd571f7cU, 0xba2db140U,
    0xbd98b894U, 0x3caef7abU, 0xbdb5a9f0U, 0x3d41e10aU, 0xbdbdfbceU, 0x3d96231fU, 0xbdb069efU, 0x3dc33c6cU,
    0xbd8f052eU, 0x3de15ebeU, 0xbd3dc60bU, 0x3debf3a4U, 0xbc9d5da4U, 0x3de15ebcU, 0x3bf28e14U, 0x3dc33c67U,
    0x3cf146e1U, 0x3d96231eU, 0x3d328622U, 0x3d41e106U, 0xbcb8cdbbU, 0x3d41e106U, 0xbcb8cdbbU, 0x3d41e106U,
    0xbcb8cdbbU, 0x3d41e106U, 0xbcb8cdbbU, 0x3d41e106U, 0xbcb8cdbbU, 0x3d41e106U, 0xbcb8cdbbU, 0x3d41e106U,
    0xbcb8cdbbU, 0x3d41e106U, 0xbcb8cdbbU, 0x3d41e106U, 0xbcb8cdbbU, 0x3d41e106U, 0xbcb8cdbbU, 0x3d41e106U,
    0xbcb8cdbbU, 0x3d41e106U, 0xbcb8cdbbU, 0x3d41e106U, 0xbcb8cdbbU, 0x3d41e106U, 0xbcb8cdbbU, 0x3d41e106U,
    0xbcb8cdbbU, 0x3d41e106U, 0xbcb8cdbbU, 0x00000006U, 0x3e93a0c5U, 0x3ef0e65eU, 0x3e93a0c4U, 0x3f13d5a4U,
    0x3ed8eccdU, 0x3ed68f3fU, 0x3eb1d5bbU, 0x3f0d3773U, 0x3f1078b1U, 0x3ef1d6c5U, 0x3f1078b2U, 0x3f144dd7U,
    0x00000010U, 0x00000000U, 0x00000000U, 0x3eaaaaabU, 0x00000000U, 0x3f2aaaabU, 0x00000000U, 0x3f800000U,
    0x00000000U, 0x00000000U, 0x3eaaaaabU, 0x3e84fc8fU, 0x3eb8aee2U, 0x3f16bbb6U, 0x3eb82d13U, 0x3f800000U,
    0x3eaaaaabU, 0x00000000U, 0x3f2aaaabU, 0x3e8d10a8U, 0x3f2f8579U, 0x3f1a0ee2U, 0x3f2fb523U, 0x3f800000U,
    0x3f2aaaabU, 0x00000000U, 0x3f800000U, 0x3eaaaaabU, 0x3f800000U, 0x3f2aaaabU, 0x3f800000U, 0x3f800000U,
    0x3f800000U, 0x00000010U, 0x3db1a678U, 0xbcef8ae0U, 0x3ed7144aU, 0xbcef8ae0U, 0x3f40df79U, 0xbcef8ae0U,
    0x3f8b1a68U, 0xbcef8ae0U, 0x3db1a678U, 0x3e9bb1ffU, 0x3e63a7e7U, 0x3ec80af8U, 0x3f0bb061U, 0x3ebeaf17U,
    0x3f8b1a6cU, 0x3e9bb1ffU, 0x3db1a678U, 0x3f232e54U, 0x3eb2006bU, 0x3f27d9acU, 0x3f1d4f5fU, 0x3f2c1343U,
    0x3f8b1a66U, 0x3f232e54U, 0x3db1a678U, 0x3f7883aaU, 0x3ed7144aU, 0x3f7883b2U, 0x3f40df79U, 0x3f7883a6U,
    0x3f8b1a68U, 0x3f7883aaU, 0x3e800000U, 0xbe000000U, 0x3d4ccccdU, 0x3e000000U, 0x3f7e00abU, 0x3dff5577U,
    0x00000000U, 0x40000000U, 0x00000002U, 0x00000000U, 0x00000000U, 0x00000000U, 0x3e800000U, 0xbe000000U,
    0x3d4cccceU, 0x3e800000U, 0xbe000000U, 0x3d4cccceU, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x3e000000U, 0x00000000U, 0x00000000U, 0x00000001U, 0x3dbcf394U, 0xbd37f9c8U,
    0x3d864f62U, 0x3e0cbd7cU, 0xbd89a48dU, 0x3d60a1fdU, 0x3e0cbd7cU, 0xbd89a48dU, 0x3d60a1fdU, 0x402d2154U,
    0xbfaad96aU, 0xbf249bf0U, 0x00000000U, 0x00000000U, 0x00000000U, 0x3e000000U, 0x40200000U, 0x3ecccccdU,
    0x00000000U, 0x3e800000U, 0x3e800000U, 0x00000030U, 0x3e601551U, 0x3dfc015aU, 0x3e014f68U, 0x3dbce85cU,
    0x3d574c44U, 0x3ce84eb8U, 0x3c11e400U, 0xbd712372U, 0x3affa980U, 0xbe1feab0U, 0x3d062f48U, 0xbe7eb099U,
    0x3dc5ec52U, 0xbea51678U, 0x3e3c48e0U, 0xbebb70e1U, 0x3e8ff558U, 0xbebf0056U, 0x3ebf584cU, 0xbeaf3a17U,
    0x3ee51678U, 0xbe8e84eaU, 0x3efb70e1U, 0xbe43b71eU, 0x3eff0056U, 0xbdc02aa2U, 0x3eef3a16U, 0xbaa7b2c0U,
    0x3ece84eaU, 0x3d9459e4U, 0x3ea1db92U, 0x3dedc382U, 0x3e033eadU, 0x3bd7da60U, 0x3dd00781U, 0xbb9295a0U,
    0x3da4c050U, 0xbccd7ef0U, 0x3d8b3e7bU, 0xbd5570aaU, 0x3d87641fU, 0xbda6c61aU, 0x3d99c766U, 0xbdde6488U,
    0x3dbf9bacU, 0xbe048df8U, 0x3df31e9aU, 0xbe1035c2U, 0x3e163c4bU, 0xbe106360U, 0x3e317738U, 0xbe050fe0U,
    0x3e471ad1U, 0xbddfe95dU, 0x3e53dbbbU, 0xbda890c2U, 0x3e55c8e8U, 0xbd590600U, 0x3e4c9744U, 0xbcd39246U,
    0x3e39ad21U, 0xbba2d280U, 0x3e1febacU, 0x3bd226b0U, 0x3e0cbd7cU, 0xbd89a48dU, 0x3e0cbd7cU, 0xbd89a48dU,
    0x3e0cbd7cU, 0xbd89a48dU, 0x3e0cbd7cU, 0xbd89a48dU, 0x3e0cbd7cU, 0xbd89a48dU, 0x3e0cbd7cU, 0xbd89a48dU,
    0x3e0cbd7cU, 0xbd89a48dU, 0x3e0cbd7cU, 0xbd89a48dU, 0x3e0cbd7cU, 0xbd89a48dU, 0x3e0cbd7cU, 0xbd89a48dU,
    0x3e0cbd7cU, 0xbd89a48dU, 0x3e0cbd7cU, 0xbd89a48dU, 0x3e0cbd7cU, 0xbd89a48dU, 0x3e0cbd7cU, 0xbd89a48dU,
    0x3e0cbd7cU, 0xbd89a48dU, 0x3e0cbd7cU, 0xbd89a48dU, 0x00000006U, 0x3e9e8cf4U, 0x3eef4d0bU, 0x3e9e8cf4U,
    0x3f135f5fU, 0x3ee54274U, 0x3ed4d014U, 0x3eca8240U, 0x3f0c2e60U, 0x3f16ea4fU, 0x3ef0c3b4U, 0x3f16ea50U,
    0x3f141ab3U, 0x00000010U, 0x00000000U, 0x00000000U, 0x3eaaaaabU, 0x00000000U, 0x3f2aaaabU, 0x00000000U,
    0x3f800000U, 0x00000000U, 0x00000000U, 0x3eaaaaabU, 0x3e90e87aU, 0x3eb72e51U, 0x3f1d01dfU, 0x3eb73de6U,
    0x3f800000U, 0x3eaaaaabU, 0x00000000U, 0x3f2aaaabU, 0x3e967000U, 0x3f2f2059U, 0x3f1f4961U, 0x3f2f8081U,
    0x3f800000U, 0x3f2aaaabU, 0x00000000U, 0x3f800000U, 0x3eaaaaabU, 0x3f800000U, 0x3f2aaaabU, 0x3f800000U,
    0x3f800000U, 0x3f800000U, 0x00000010U, 0x3d732b20U, 0xbcdd2080U, 0x3ec91011U, 0xbcdd2080U, 0x3f39dd5cU,
    0xbcdd20a0U, 0x3f879959U, 0xbcdd2080U, 0x3d732b20U, 0x3e9cd8a4U, 0x3e83fdb4U, 0x3ec28222U, 0x3f1556eaU,
    0x3ebe424dU, 0x3f87995dU, 0x3e9cd8a4U, 0x3d732b20U, 0x3f23c1a7U, 0x3eafddf0U, 0x3f281fe7U, 0x3f2172e0U,
    0x3f2cfd77U, 0x3f879957U, 0x3f23c1a7U, 0x3d732b18U, 0x3f7916fdU, 0x3ec91011U, 0x3f791706U, 0x3f39dd5cU,
    0x3f7916faU, 0x3f879959U, 0x3f7916fdU, 0x3e99999aU, 0xbe19999aU, 0x3d75c28fU, 0xbe4ccccdU, 0x3f7ae5a5U,
    0xbe4b6ff9U, 0x00000000U, 0x40500000U, 0x00000002U, 0x00000000U, 0x00000000U, 0x00000000U, 0x3e99999bU,
    0xbe19999bU, 0x3d75c290U, 0x3e99999bU, 0xbe19999bU, 0x3d75c290U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x3e000000U, 0x00000000U, 0x00000000U, 0x00000001U, 0x3e707bc5U,
    0xbdec3b3bU, 0x3d01ab52U, 0x3e90b204U, 0xbe0e9c02U, 0x3ca4bd7eU, 0x3e90b204U, 0xbe0e9c02U, 0x3ca4bd7eU,
    0x4028e98dU, 0xbfa9306fU, 0xbf235bb2U, 0x00000000U, 0x00000000U, 0x00000000U, 0x3e000000U, 0x40200000U,
    0x3ecccccdU, 0x00000000U, 0x3e800000U, 0x3e800000U, 0x00000030U, 0x3eb3079aU, 0x3dc29818U, 0x3e81163fU,
    0x3dc352c8U, 0x3e25c084U, 0x3d6f1894U, 0x3dbdba92U, 0xbc299140U, 0x3d61363cU, 0xbdcd7b3aU, 0x3d5fc0d8U,
    0xbe4aa054U, 0x3dbba6ecU, 0xbe938626U, 0x3e2432b1U, 0xbeb6f7c5U, 0x3e802b9cU, 0xbeca3fa1U, 0x3eb21cf7U,
    0xbeca6e4dU, 0x3ee052f6U, 0xbeb77cacU, 0x3f01e24aU, 0xbe944d0fU, 0x3f0b8637U, 0xbe4c7599U, 0x3f0b9d8dU,
    0xbdd125c0U, 0x3f0224bcU, 0xbc426e40U, 0x3ee119dfU, 0x3d6af14cU, 0x3e9a7ebdU, 0xbd37b0e8U, 0x3e898b87U,
    0xbd53329cU, 0x3e735dfcU, 0xbd92b29bU, 0x3e5aa6a0U, 0xbdd0e296U, 0x3e4cb442U, 0xbe0d58caU, 0x3e4ba672U,
    0xbe32717dU, 0x3e57a642U, 0xbe52159cU, 0x3e6ee00bU, 0xbe6773fcU, 0x3e86e54dU, 0xbe6f4bcaU, 0x3e97d883U,
    0xbe686b5dU, 0x3ea7b50cU, 0xbe53deb6U, 0x3eb410bbU, 0xbe34c6b7U, 0x3ebb09e9U, 0xbe0fdf3bU, 0x3ebb90d1U,
    0xbdd58d0dU, 0x3eb590e8U, 0xbd9644ceU, 0x3ea9f405U, 0xbd571024U, 0x3e90b204U, 0xbe0e9c02U, 0x3e90b204U,
    0xbe0e9c02U, 0x3e90b204U, 0xbe0e9c02U, 0x3e90b204U, 0xbe0e9c02U, 0x3e90b204U, 0xbe0e9c02U, 0x3e90b204U,
    0xbe0e9c02U, 0x3e90b204U, 0xbe0e9c02U, 0x3e90b204U, 0xbe0e9c02U, 0x3e90b204U, 0xbe0e9c02U, 0x3e90b204U,
    0xbe0e9c02U, 0x3e90b204U, 0xbe0e9c02U, 0x3e90b204U, 0xbe0e9c02U, 0x3e90b204U, 0xbe0e9c02U, 0x3e90b204U,
    0xbe0e9c02U, 0x3e90b204U, 0xbe0e9c02U, 0x3e90b204U, 0xbe0e9c02U, 0x00000006U, 0x3eb1c863U, 0x3ee18795U,
    0x3eb1c862U, 0x3f0e43c2U, 0x3efb176eU, 0x3ec61769U, 0x3ef62e5fU, 0x3f0134a1U, 0x3f225efeU, 0x3ee5cc27U,
    0x3f225efeU, 0x3f10660bU, 0x00000010U, 0x00000000U, 0x00000000U, 0x3eaaaaabU, 0x00000000U, 0x3f2aaaabU,
    0x00000000U, 0x3f800000U, 0x00000000U, 0x00000000U, 0x3eaaaaabU, 0x3ea5f148U, 0x3ea9a0bcU, 0x3f28281aU,
    0x3ead1c4cU, 0x3f800000U, 0x3eaaaaabU, 0x00000000U, 0x3f2aaaabU, 0x3ea6f527U, 0x3f2aca65U, 0x3f28935dU,
    0x3f2c3ce0U, 0x3f800000U, 0x3f2aaaabU, 0x00000000U, 0x3f800000U, 0x3eaaaaabU, 0x3f800000U, 0x3f2aaaabU,
    0x3f800000U, 0x3f800000U, 0x3f800000U, 0x00000010U, 0x3c328e60U, 0xbb423400U, 0x3eb03f22U, 0xbb423400U,
    0x3f2d74e5U, 0xbb423400U, 0x3f81651eU, 0xbb423400U, 0x3c328e70U, 0x3ea92646U, 0x3ea399baU, 0x3e9ac1faU,
    0x3f26bb9cU, 0x3eb63c87U, 0x3f816522U, 0x3ea92646U, 0x3c328e80U, 0x3f29e877U, 0x3eaba5e8U, 0x3f290a3eU,
    0x3f28f688U, 0x3f30d2baU, 0x3f81651cU, 0x3f29e877U, 0x3c328e70U, 0x3f7f3dceU, 0x3eb03f20U, 0x3f7f3dd6U,
    0x3f2d74e5U, 0x3f7f3dcaU, 0x3f81651eU, 0x3f7f3dceU, 0x3eb33333U, 0xbe333333U, 0x3d8f5c29U, 0x3e99999aU,
    0x3f7490efU, 0x3e974e6dU, 0x00000000U, 0x3f400000U, 0x00000002U, 0x00000000U, 0x00000000U, 0x00000000U,
    0x3eb33333U, 0xbe333333U, 0x3d8f5c29U, 0x3eb33333U, 0xbe333333U, 0x3d8f5c29U, 0x00000000U, 0x00000000U,
    0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x3e000000U, 0x00000000U, 0x00000000U, 0x00000001U,
    0x3e90b204U, 0xbe0e9c02U, 0x3ca4bd7eU, 0x3ea1d1a0U, 0xbe1f757aU, 0x3c46d45dU, 0x3ea1d1a0U, 0xbe1f757aU,
    0x3c46d45dU, 0x402af79aU, 0xbfa83b4aU, 0xbf230ef4U, 0x00000000U, 0x00000000U, 0x00000000U, 0x3e000000U,
    0x40200000U, 0x3ecccccdU, 0x00000000U, 0x3e800000U, 0x3e800000U, 0x00000030U, 0x3e8d5f98U, 0x3d82bb7aU,
    0x3e42ea07U, 0x3c8e6338U, 0x3e03f883U, 0xbd6f0abcU, 0x3dceff8eU, 0xbe1b80c6U, 0x3de3aaeeU, 0xbe7eda6cU,
    0x3e2166ceU, 0xbeab57cbU, 0x3e6ef5e3U, 0xbecad08cU, 0x3ea759feU, 0xbed90cebU, 0x3ed906d0U, 0xbed3e212U,
    0x3f0278b2U, 0xbebc1968U, 0x3f123514U, 0xbe9551dbU, 0x3f195343U, 0xbe4ae59cU, 0x3f16bdd6U, 0xbdcf17faU,
    0x3f0ad980U, 0xbc7b6d00U, 0x3eeeeb74U, 0x3d3cead0U, 0x3ebf0c6bU, 0x3d9766dcU, 0x3e960fb5U, 0xbda6e1f1U,
    0x3e88da36U, 0xbdd1e86eU, 0x3e7ee381U, 0xbe06c3b7U, 0x3e7689fdU, 0xbe2855a6U, 0x3e79ed4aU, 0xbe488daaU,
    0x3e8444b0U, 0xbe628413U, 0x3e90126cU, 0xbe724507U, 0x3e9e93d2U, 0xbe756a8aU, 0x3ead938cU, 0xbe6b79fcU,
    0x3ebac90aU, 0xbe55f6bdU, 0x3ec43180U, 0xbe38273cU, 0x3ec85e42U, 0xbe16954cU, 0x3ec6ac9bU, 0xbdecba95U,
    0x3ebf5e90U, 0xbdb8cdc1U, 0x3eb390d3U, 0xbd994bd9U, 0x3ea50f6fU, 0xbd9300d3U, 0x3ea1d1a0U, 0xbe1f757aU,
    0x3ea1d1a0U, 0xbe1f757aU, 0x3ea1d1a0U, 0xbe1f757aU, 0x3ea1d1a0U, 0xbe1f757aU, 0x3ea1d1a0U, 0xbe1f757aU,
    0x3ea1d1a0U, 0xbe1f757aU, 0x3ea1d1a0U, 0xbe1f757aU, 0x3ea1d1a0U, 0xbe1f757aU, 0x3ea1d1a0U, 0xbe1f757aU,
    0x3ea1d1a0U, 0xbe1f757aU, 0x3ea1d1a0U, 0xbe1f757aU, 0x3ea1d1a0U, 0xbe1f757aU, 0x3ea1d1a0U, 0xbe1f757aU,
    0x3ea1d1a0U, 0xbe1f757aU, 0x3ea1d1a0U, 0xbe1f757aU, 0x3ea1d1a0U, 0xbe1f757aU, 0x00000006U, 0x3eb00e93U,
    0x3ee64d20U, 0x3eb00e92U, 0x3f0f246cU, 0x3ef9284aU, 0x3ecceb8fU, 0x3ef24fe6U, 0x3f04dafbU, 0x3f215dffU,
    0x3eeb1617U, 0x3f215dfeU, 0x3f1188e8U, 0x00000010U, 0x00000000U, 0x00000000U, 0x3eaaaaabU, 0x00000000U,
    0x3f2aaaabU, 0x00000000U, 0x3f800000U, 0x00000000U, 0x00000000U, 0x3eaaaaabU, 0x3ea41080U, 0x3eae6605U,
    0x3f272db9U, 0x3eb19ec4U, 0x3f800000U, 0x3eaaaaabU, 0x00000000U, 0x3f2aaaabU, 0x3ea57a9fU, 0x3f2bab0eU,
    0x3f27c2a2U, 0x3f2d4311U, 0x3f800000U, 0x3f2aaaabU, 0x00000000U, 0x3f800000U, 0x3eaaaaabU, 0x3f800000U,
    0x3f2aaaabU, 0x3f800000U, 0x3f800000U, 0x3f800000U, 0x00000010U, 0x3c78dbd0U, 0xbc350680U, 0x3eb2718cU,
    0xbc350680U, 0x3f2e8e1aU, 0xbc350680U, 0x3f81f1b8U, 0xbc350680U, 0x3c78dbe0U, 0x3ea50279U, 0x3ea0ae9cU,
    0x3ea9f888U, 0x3f254296U, 0x3ebff268U, 0x3f81f1bcU, 0x3ea5027aU, 0x3c78dbf0U, 0x3f27d691U, 0x3eabecf3U,
    0x3f24994eU, 0x3f2856b5U, 0x3f2f3df8U, 0x3f81f1b6U, 0x3f27d691U, 0x3c78dc00U, 0x3f7d2be7U, 0x3eb2718dU,
    0x3f7d2bf0U, 0x3f2e8e1bU, 0x3f7d2be3U, 0x3f81f1b8U, 0x3f7d2be6U};

static void TestZeroSpeedConstruction() {
	sgre::scene::EmoteJellyDefinition normal_definition;
	normal_definition.ParticleCount = 2;
	normal_definition.Tresses.emplace_back();
	auto zero_definition = normal_definition;
	zero_definition.Speed = 0;
	sgre::scene::EmoteJellyPhysics normal(normal_definition);
	sgre::scene::EmoteJellyPhysics zero(zero_definition);
	normal.Initialize();
	zero.Initialize();
	const auto normal_samples = normal.PrepareSamples();
	const auto zero_samples = zero.PrepareSamples();
	Require(normal_samples.size() == zero_samples.size() && !zero_samples.empty(), "native zero-speed construction owns the same real initialized fitting grid");
	for (std::size_t index = 0; index != zero_samples.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(normal_samples[index].X) == std::bit_cast<std::uint32_t>(zero_samples[index].X) && std::bit_cast<std::uint32_t>(normal_samples[index].Y) == std::bit_cast<std::uint32_t>(zero_samples[index].Y), "stored zero speed must not alter actual constructor/Initialize samples");
	}
	const auto& normal_lattice = normal.PrepareLattice(0, 1, {});
	const auto& zero_lattice = zero.PrepareLattice(0, 1, {});
	Require(normal_lattice.EncodedDegree == zero_lattice.EncodedDegree && normal_lattice.Controls.size() == zero_lattice.Controls.size(), "stored zero speed preserves the real initial lattice owner");
	for (std::size_t index = 0; index != zero_lattice.Controls.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(normal_lattice.Controls[index].X) == std::bit_cast<std::uint32_t>(zero_lattice.Controls[index].X) && std::bit_cast<std::uint32_t>(normal_lattice.Controls[index].Y) == std::bit_cast<std::uint32_t>(zero_lattice.Controls[index].Y), "zero-speed initial controls remain actual constructor output");
	}
	bool rejected = false;
	try {
		zero.Update(1);
	} catch (const std::runtime_error& error) {
		rejected = std::string(error.what()).find("zero effective solver time") != std::string::npos;
	}
	Require(rejected, "native324cf0 divides by effective seconds; zero speed cannot claim a finite successful step");

	normal_definition.Speed = -1;
	rejected = false;
	try {
		sgre::scene::EmoteJellyPhysics negative(normal_definition);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected, "negative-speed solver domain remains unsupported");
}

int main(int argc, char** argv) {
	try {
		std::vector<std::uint8_t> bytes;
		if (argc == 2) {
			const std::filesystem::path path(argv[1]);
			const auto size = std::filesystem::file_size(path);
			Require(size <= 16 * 1024 * 1024, "actual Jelly entity replay vector is too large");
			bytes.resize(static_cast<std::size_t>(size));
			std::ifstream input(path, std::ios::binary);
			Require(static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))), "cannot read actual Jelly entity vector");
		} else {
			Require(argc == 1, "usage: emote_jelly_physics_tests [native-jelly-entity.bin]");
			bytes.reserve(kNativeDefault.size() * 4);
			for (const auto value : kNativeDefault) {
				for (std::uint32_t shift = 0; shift < 32; shift += 8) {
					bytes.push_back(static_cast<std::uint8_t>(value >> shift));
				}
			}
		}
		Replay(bytes);
		TestZeroSpeedConstruction();

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
