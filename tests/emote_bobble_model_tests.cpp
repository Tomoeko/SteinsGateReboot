#include "sgre/scene/emote_bobble_model.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct NativeStep {
	float Seconds = 0;
	sgre::scene::EmoteBobblePosture Posture;
	std::vector<std::array<float, 3>> Forces;
	std::vector<std::array<std::uint32_t, 21>> Expected;
};

struct NativeCase {
	sgre::scene::EmoteBobbleDefinition Definition;
	std::vector<NativeStep> Steps;
};

static void Require(bool condition, const std::string& message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Action>
static void Reject(Action&& action, const std::string& context) {
	try {
		action();
	} catch (const std::runtime_error&) {
		return;
	}
	throw std::runtime_error("missing bobble rejection: " + context);
}

static std::array<std::uint32_t, 21> ParticleWords(const sgre::scene::EmoteBobbleParticle& particle) {
	std::array<std::uint32_t, 21> result;
	std::size_t index = 0;
	for (const auto& vector : {particle.Previous, particle.Predicted, particle.Position, particle.Velocity, particle.Force}) {
		for (const auto value : vector) {
			result[index++] = std::bit_cast<std::uint32_t>(value);
		}
	}
	for (const auto value : {particle.Friction, particle.Mass, particle.InverseMass}) {
		result[index++] = std::bit_cast<std::uint32_t>(value);
	}
	result[index++] = particle.Fixed ? 1 : 0;
	result[index++] = std::bit_cast<std::uint32_t>(particle.AlignmentSpring);
	result[index] = std::bit_cast<std::uint32_t>(particle.LengthSpring);

	return result;
}

static std::size_t ReplayCase(const NativeCase& input, std::size_t case_index) {
	sgre::scene::EmoteBobbleModel model(input.Definition);
	sgre::scene::EmoteBobblePosture initial;
	model.SetPosture(initial);
	model.Initialize();
	std::size_t words = 0;
	for (std::size_t step_index = 0; step_index != input.Steps.size(); ++step_index) {
		const auto& step = input.Steps[step_index];
		Require(step.Forces.size() == model.Particles().size() && step.Expected.size() == model.Particles().size(), "native corpus requires the actual whole particle chain");
		model.SetPosture(step.Posture);
		model.ClearForces();
		for (std::size_t index = 1; index != step.Forces.size(); ++index) {
			model.AddForce(index, step.Forces[index]);
		}
		model.Step(step.Seconds);
		for (std::size_t particle = 0; particle != model.Particles().size(); ++particle) {
			const auto actual = ParticleWords(model.Particles()[particle]);
			for (std::size_t field = 0; field != actual.size(); ++field) {
				// Native leaves root spring bytes unused and uninitialized.
				if (particle == 0 && field >= 19) {
					continue;
				}
				Require(actual[field] == step.Expected[particle][field], "native bobble mismatch case " + std::to_string(case_index) + ", step " + std::to_string(step_index) + ", particle " + std::to_string(particle) + ", word " + std::to_string(field) + ": actual " + std::to_string(actual[field]) + ", expected " + std::to_string(step.Expected[particle][field]));
				++words;
			}
		}
	}

	return words;
}

static NativeCase EmbeddedCase0() {
	NativeCase result;
	result.Definition.Length = 1.0f;
	result.Definition.MassScale = 2.0f;
	result.Definition.AlignmentSpring = 1.0f;
	result.Definition.LengthSpring = 2.0f;
	result.Definition.FrictionBegin = 0.125f;
	result.Definition.FrictionEnd = 0.0f;
	result.Definition.AdhocSimulation = false;
	result.Definition.MassWeights = {1.0f, 1.25f};
	{
		NativeStep step;
		step.Seconds = 0.0166666675f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3e000000u, 0x40000000u, 0x3f000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0x00000000u, 0x3f800000u, 0x00000000u, 0x00000000u, 0x3f800000u, 0x00000000u, 0x00000000u, 0x3f800000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x40200000u, 0x3ecccccdu, 0x00000000u, 0x3f800000u, 0x40000000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.125f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {1.5f, -1.0f, 0.0f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3e000000u, 0x40000000u, 0x3f000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0x00000000u, 0x3f800000u, 0x00000000u, 0x3c19999au, 0x3f7e6666u, 0x00000000u, 0x3c19999au, 0x3f7e6666u, 0x00000000u, 0x3d99999au, 0xbd4ccd00u, 0x00000000u, 0x3fc00000u, 0xbf800000u, 0x00000000u, 0x00000000u, 0x40200000u, 0x3ecccccdu, 0x00000000u, 0x3f800000u, 0x40000000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.25f;
		step.Posture.Coordinate = {0.25f, -2.0f, 0.5f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.125f, -1.0f, -1.125f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0x3e34527cu, 0xbf7c9bb4u, 0x3ea3c4feu, 0x3e800000u, 0xc0000000u, 0x3f000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3e000000u, 0x40000000u, 0x3f000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0x3c19999au, 0x3f7e6666u, 0x00000000u, 0x3dd5f3aau, 0xbb9a9980u, 0x3e14c2c8u, 0x3dd5f3aau, 0xbb9a9980u, 0x3e14c2c8u, 0x3ec2c077u, 0xc07f9b99u, 0x3f14c2c8u, 0x3e000000u, 0xbf800000u, 0xbf900000u, 0x00000000u, 0x40200000u, 0x3ecccccdu, 0x00000000u, 0x3f800000u, 0x40000000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.0333333351f;
		step.Posture.Coordinate = {-0.125f, 3.0f, -0.25f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.375f, 0.25f, -0.875f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0xbd06e190u, 0x3fe76b65u, 0xbdbda53eu, 0xbe000000u, 0x40400000u, 0xbe800000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3e000000u, 0x40000000u, 0x3f000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0x3dd5f3aau, 0xbb9a9980u, 0x3e14c2c8u, 0x3d31d8c0u, 0x3f51c866u, 0x3d1b5654u, 0x3d31d8c0u, 0x3f51c866u, 0x3d1b5654u, 0xbfea6daau, 0x41c5cdbfu, 0xc04e1cbfu, 0x3ec00000u, 0x3e800000u, 0xbf600000u, 0x00000000u, 0x40200000u, 0x3ecccccdu, 0x00000000u, 0x3f800000u, 0x40000000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.0166666675f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.5f, -0.875f, 0.125f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0x3ab15a22u, 0x3e03c91eu, 0xbad796ccu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3e000000u, 0x40000000u, 0x3f000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0x3d31d8c0u, 0x3f51c866u, 0x3d1b5654u, 0x3c426734u, 0x3f9074a8u, 0xbc6c50efu, 0x3c426734u, 0x3f9074a8u, 0xbc6c50efu, 0xbff25607u, 0x41945db6u, 0xc04903e6u, 0x3f000000u, 0xbf600000u, 0x3e000000u, 0x00000000u, 0x40200000u, 0x3ecccccdu, 0x00000000u, 0x3f800000u, 0x40000000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.125f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.625f, 0.25f, 0.5f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0xbdb31a11u, 0x3fae8f13u, 0xbe23a39eu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3e000000u, 0x40000000u, 0x3f000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0x3c426734u, 0x3f9074a8u, 0xbc6c50efu, 0xbe1aa36du, 0x4016b74au, 0xbe8d49aau, 0xbe1aa36du, 0x4016b74au, 0xbe8d49aau, 0xbfa6c9e0u, 0x411cf9ecu, 0xc005e723u, 0x3f200000u, 0x3e800000u, 0x3f000000u, 0x00000000u, 0x40200000u, 0x3ecccccdu, 0x00000000u, 0x3f800000u, 0x40000000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.25f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {-0.5f, -1.5f, 0.75f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0xbe5824a6u, 0x4003122du, 0xbeabeb7au, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3e000000u, 0x40000000u, 0x3f000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0xbe1aa36du, 0x4016b74au, 0xbe8d49aau, 0xbe9fe0f4u, 0x4041e750u, 0xbefe559fu, 0xbe9fe0f4u, 0x4041e750u, 0xbefe559fu, 0xbf251e7bu, 0x402cc018u, 0xbf6217eau, 0xbf000000u, 0xbfc00000u, 0x3f400000u, 0x00000000u, 0x40200000u, 0x3ecccccdu, 0x00000000u, 0x3f800000u, 0x40000000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.0333333351f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {-1.125f, 1.125f, -0.625f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0xbe02377au, 0x3f97f4cau, 0xbe4d16d7u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3e000000u, 0x40000000u, 0x3f000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0xbe9fe0f4u, 0x4041e750u, 0xbefe559fu, 0xbe6dc718u, 0x400abcb8u, 0xbebb3f7fu, 0xbe6dc718u, 0x400abcb8u, 0xbebb3f7fu, 0x4019b645u, 0xc1cedfb9u, 0x407b92f7u, 0xbf900000u, 0x3f900000u, 0xbf200000u, 0x00000000u, 0x40200000u, 0x3ecccccdu, 0x00000000u, 0x3f800000u, 0x40000000u},
		};
		result.Steps.push_back(std::move(step));
	}

	return result;
}

static NativeCase EmbeddedCase7() {
	NativeCase result;
	result.Definition.Length = 3.5f;
	result.Definition.MassScale = 0.5f;
	result.Definition.AlignmentSpring = 0.25f;
	result.Definition.LengthSpring = 0.5f;
	result.Definition.FrictionBegin = 0.0f;
	result.Definition.FrictionEnd = 0.5f;
	result.Definition.AdhocSimulation = true;
	result.Definition.MassWeights = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 2.75f};
	{
		NativeStep step;
		step.Seconds = 0.0166666675f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f000000u, 0x40000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0x00000000u, 0x3f000000u, 0x00000000u, 0x00000000u, 0x3f000000u, 0x00000000u, 0x00000000u, 0x3f000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3d924925u, 0x3f200000u, 0x3fcccccdu, 0x00000000u, 0x3f95d8d8u, 0x4015d8d8u},
		    {0x00000000u, 0x3f800000u, 0x00000000u, 0x00000000u, 0x3f800000u, 0x00000000u, 0x00000000u, 0x3f800000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3e124925u, 0x3f400000u, 0x3faaaaabu, 0x00000000u, 0x3fb3d104u, 0x4033d104u},
		    {0x00000000u, 0x3fc00000u, 0x00000000u, 0x00000000u, 0x3fc00000u, 0x00000000u, 0x00000000u, 0x3fc00000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3e5b6db7u, 0x3f600000u, 0x3f924925u, 0x00000000u, 0x3fd1c92fu, 0x4051c92fu},
		    {0x00000000u, 0x40000000u, 0x00000000u, 0x00000000u, 0x40000000u, 0x00000000u, 0x00000000u, 0x40000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3e924925u, 0x3f800000u, 0x3f800000u, 0x00000000u, 0x3fefc15au, 0x406fc15au},
		    {0x00000000u, 0x40200000u, 0x00000000u, 0x00000000u, 0x40200000u, 0x00000000u, 0x00000000u, 0x40200000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3eb6db6eu, 0x3f900000u, 0x3f638e39u, 0x00000000u, 0x4006dcc3u, 0x4086dcc3u},
		    {0x00000000u, 0x40400000u, 0x00000000u, 0x00000000u, 0x40400000u, 0x00000000u, 0x00000000u, 0x40400000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3edb6db7u, 0x3fa00000u, 0x3f4ccccdu, 0x00000000u, 0x4015d8d8u, 0x4095d8d8u},
		    {0x00000000u, 0x40600000u, 0x00000000u, 0x00000000u, 0x40600000u, 0x00000000u, 0x00000000u, 0x40600000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f000000u, 0x3fb00000u, 0x3f3a2e8cu, 0x00000000u, 0x4024d4eeu, 0x40a4d4eeu},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.125f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {-1.5f, 0.625f, -0.375f}, {0.5f, 1.25f, 1.5f}, {-1.25f, 1.375f, -1.5f}, {-0.125f, 0.25f, -1.5f}, {-0.75f, 0.75f, -0.75f}, {1.5f, 1.125f, 0.25f}, {0.625f, 0.75f, 0.75f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f000000u, 0x40000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0x00000000u, 0x3f000000u, 0x00000000u, 0xbd0bd385u, 0x3f004653u, 0xbbff6c58u, 0xbd0bd385u, 0x3f004653u, 0xbbff6c58u, 0xbe8a93ebu, 0x3c0b6485u, 0xbd7d2485u, 0xbfc00000u, 0x3f200000u, 0xbec00000u, 0x3d924925u, 0x3f200000u, 0x3fcccccdu, 0x00000000u, 0x3f95d8d8u, 0x4015d8d8u},
		    {0x00000000u, 0x3f800000u, 0x00000000u, 0x3bfdb778u, 0x3f7fc252u, 0x3cdd6ba6u, 0x3bfdb778u, 0x3f7fc252u, 0x3cdd6ba6u, 0x3d792f9fu, 0xbbf25025u, 0x3e597771u, 0x3f000000u, 0x3fa00000u, 0x3fc00000u, 0x3e124925u, 0x3f400000u, 0x3faaaaabu, 0x00000000u, 0x3fb3d104u, 0x4033d104u},
		    {0x00000000u, 0x3fc00000u, 0x00000000u, 0xbca34255u, 0x3fbf8e06u, 0xbcc3c038u, 0xbca34255u, 0x3fbf8e06u, 0xbcc3c038u, 0xbe1ee2d7u, 0xbcddd8e5u, 0xbe3e81edu, 0xbfa00000u, 0x3fb00000u, 0xbfc00000u, 0x3e5b6db7u, 0x3f600000u, 0x3f924925u, 0x00000000u, 0x3fd1c92fu, 0x4051c92fu},
		    {0x00000000u, 0x40000000u, 0x00000000u, 0xbb258dc6u, 0x3fff9988u, 0xbcbc9138u, 0xbb258dc6u, 0x3fff9988u, 0xbcbc9138u, 0xbc9fa423u, 0xbcc59e49u, 0xbe35d52du, 0xbe000000u, 0x3e800000u, 0xbfc00000u, 0x3e924925u, 0x3f800000u, 0x3f800000u, 0x00000000u, 0x3fefc15au, 0x406fc15au},
		    {0x00000000u, 0x40200000u, 0x00000000u, 0xbc1e6833u, 0x401fd08du, 0xbc282ba5u, 0xbc1e6833u, 0x401fd08du, 0xbc282ba5u, 0xbd9755d5u, 0xbcb552e5u, 0xbda0a9b2u, 0xbf400000u, 0x3f400000u, 0xbf400000u, 0x3eb6db6eu, 0x3f900000u, 0x3f638e39u, 0x00000000u, 0x4006dcc3u, 0x4086dcc3u},
		    {0x00000000u, 0x40400000u, 0x00000000u, 0x3c917eb5u, 0x403fc502u, 0x3b3db42du, 0x3c917eb5u, 0x403fc502u, 0x3b3db42du, 0x3e09b359u, 0xbcdf53dbu, 0x3cb38a86u, 0x3fc00000u, 0x3f900000u, 0x3e800000u, 0x3edb6db7u, 0x3fa00000u, 0x3f4ccccdu, 0x00000000u, 0x4015d8d8u, 0x4095d8d8u},
		    {0x00000000u, 0x40600000u, 0x00000000u, 0x3be75fd9u, 0x405fc2a6u, 0x3c089f57u, 0x3be75fd9u, 0x405fc2a6u, 0x3c089f57u, 0x3d58e9dbu, 0xbce61180u, 0x3d801562u, 0x3f200000u, 0x3f400000u, 0x3f400000u, 0x3f000000u, 0x3fb00000u, 0x3f3a2e8cu, 0x00000000u, 0x4024d4eeu, 0x40a4d4eeu},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.25f;
		step.Posture.Coordinate = {0.25f, -1.0f, 0.5f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {-1.25f, 0.0f, 1.375f}, {0.625f, 0.5f, -0.75f}, {0.625f, 1.125f, -0.875f}, {0.25f, -1.375f, 0.875f}, {0.5f, -0.25f, -0.875f}, {0.875f, -0.75f, -1.125f}, {-1.25f, -1.0f, -0.75f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0x3e800000u, 0xbf800000u, 0x3f000000u, 0x3e800000u, 0xbf800000u, 0x3f000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f000000u, 0x40000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0xbd0bd385u, 0x3f004653u, 0xbbff6c58u, 0x3e29f267u, 0xbe4c4511u, 0x3ea6ec13u, 0x3e29f267u, 0xbe4c4511u, 0x3ea6ec13u, 0x3f493e94u, 0xc03023bdu, 0x3fa7dc73u, 0xbfa00000u, 0x00000000u, 0x3fb00000u, 0x3d924925u, 0x3f200000u, 0x3fcccccdu, 0x00000000u, 0x3f95d8d8u, 0x4015d8d8u},
		    {0x3bfdb778u, 0x3f7fc252u, 0x3cdd6ba6u, 0x3de19335u, 0x3ed6c3dau, 0x3e50a310u, 0x3de19335u, 0x3ed6c3dau, 0x3e50a310u, 0x3eca3a53u, 0xc00f13cfu, 0x3f2e7f1fu, 0x3f200000u, 0x3f000000u, 0xbf400000u, 0x3e124925u, 0x3f400000u, 0x3faaaaabu, 0x00000000u, 0x3fb3d104u, 0x4033d104u},
		    {0xbca34255u, 0x3fbf8e06u, 0xbcc3c038u, 0x3d8cd398u, 0x3f826fefu, 0x3dd4a168u, 0x3d8cd398u, 0x3f826fefu, 0x3dd4a168u, 0x3eabe918u, 0xbfe75fa0u, 0x3ef78e3du, 0x3f200000u, 0x3f900000u, 0xbf600000u, 0x3e5b6db7u, 0x3f600000u, 0x3f924925u, 0x00000000u, 0x3fd1c92fu, 0x4051c92fu},
		    {0xbb258dc6u, 0x3fff9988u, 0xbcbc9138u, 0x3d33d985u, 0x3fcc6663u, 0x3cf99b93u, 0x3d33d985u, 0x3fcc6663u, 0x3cf99b93u, 0x3e309c7fu, 0xbfbe2baeu, 0x3e4b703au, 0x3e800000u, 0xbfb00000u, 0x3f600000u, 0x3e924925u, 0x3f800000u, 0x3f800000u, 0x00000000u, 0x3fefc15au, 0x406fc15au},
		    {0xbc1e6833u, 0x401fd08du, 0xbc282ba5u, 0x3d0dbc6fu, 0x4009a18fu, 0xbc771b1bu, 0x3d0dbc6fu, 0x4009a18fu, 0xbc771b1bu, 0x3e25259fu, 0xbfa19f84u, 0xbc8fc672u, 0x3f000000u, 0xbe800000u, 0xbf600000u, 0x3eb6db6eu, 0x3f900000u, 0x3f638e39u, 0x00000000u, 0x4006dcc3u, 0x4086dcc3u},
		    {0x3c917eb5u, 0x403fc502u, 0x3b3db42du, 0x3cfe4b32u, 0x402b6133u, 0xbd04a4aeu, 0x3cfe4b32u, 0x402b6133u, 0xbd04a4aeu, 0x3d424896u, 0xbf91a459u, 0xbe010485u, 0x3f600000u, 0xbf400000u, 0xbf900000u, 0x3edb6db7u, 0x3fa00000u, 0x3f4ccccdu, 0x00000000u, 0x4015d8d8u, 0x4095d8d8u},
		    {0x3be75fd9u, 0x405fc2a6u, 0x3c089f57u, 0xbb133c92u, 0x404b4d97u, 0xbcbcb63du, 0xbb133c92u, 0x404b4d97u, 0xbcbcb63du, 0xbd056f2fu, 0xbf8f3369u, 0xbde0e52bu, 0xbfa00000u, 0xbf800000u, 0xbf400000u, 0x3f000000u, 0x3fb00000u, 0x3f3a2e8cu, 0x00000000u, 0x4024d4eeu, 0x40a4d4eeu},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.0333333351f;
		step.Posture.Coordinate = {-0.125f, 1.5f, -0.25f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {1.5f, -1.375f, 1.375f}, {0.375f, 0.125f, 1.0f}, {-0.125f, 1.0f, -1.5f}, {0.375f, 0.25f, -0.125f}, {0.25f, 0.0f, -1.25f}, {0.125f, 1.125f, 1.0f}, {-0.5f, -1.375f, 0.25f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0xbe000000u, 0x3fc00000u, 0xbe800000u, 0xbe000000u, 0x3fc00000u, 0xbe800000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f000000u, 0x40000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0x3e29f267u, 0xbe4c4511u, 0x3ea6ec13u, 0xbd043c30u, 0x3f83bc1cu, 0xbd90d4dfu, 0xbd043c30u, 0x3f83bc1cu, 0xbd90d4dfu, 0xc0bddd5au, 0x42131694u, 0xc13dfb23u, 0x3fc00000u, 0xbfb00000u, 0x3fb00000u, 0x3d924925u, 0x3f200000u, 0x3fcccccdu, 0x00000000u, 0x3f95d8d8u, 0x4015d8d8u},
		    {0x3de19335u, 0x3ed6c3dau, 0x3e50a310u, 0x3d8403cdu, 0x3f134b1du, 0x3deca2cdu, 0x3d8403cdu, 0x3f134b1du, 0x3deca2cdu, 0xbfae9708u, 0x4094f400u, 0xc0288aabu, 0x3ec00000u, 0x3e000000u, 0x3f800000u, 0x3e124925u, 0x3f400000u, 0x3faaaaabu, 0x00000000u, 0x3fb3d104u, 0x4033d104u},
		    {0x3d8cd398u, 0x3f826fefu, 0x3dd4a168u, 0x3d9a1e55u, 0x3f881c5bu, 0x3dd7918eu, 0x3d9a1e55u, 0x3f881c5bu, 0x3dd7918eu, 0x3e45f47eu, 0x3fa8fd6bu, 0x3d2f068eu, 0xbe000000u, 0x3f800000u, 0xbfc00000u, 0x3e5b6db7u, 0x3f600000u, 0x3f924925u, 0x00000000u, 0x3fd1c92fu, 0x4051c92fu},
		    {0x3d33d985u, 0x3fcc6663u, 0x3cf99b93u, 0x3d5d65bbu, 0x3fc7bd05u, 0x3d2d5d6cu, 0x3d5d65bbu, 0x3fc7bd05u, 0x3d2d5d6cu, 0x3e9a51edu, 0xbf8a840du, 0x3eb45ea4u, 0x3ec00000u, 0x3e800000u, 0xbe000000u, 0x3e924925u, 0x3f800000u, 0x3f800000u, 0x00000000u, 0x3fefc15au, 0x406fc15au},
		    {0x3d0dbc6fu, 0x4009a18fu, 0xbc771b1bu, 0x3d2b8399u, 0x4003cd3fu, 0xbb752cadu, 0x3d2b8399u, 0x4003cd3fu, 0xbb752cadu, 0x3e5cad16u, 0xc02ccc67u, 0x3eac200cu, 0x3e800000u, 0x00000000u, 0xbfa00000u, 0x3eb6db6eu, 0x3f900000u, 0x3f638e39u, 0x00000000u, 0x4006dcc3u, 0x4086dcc3u},
		    {0x3cfe4b32u, 0x402b6133u, 0xbd04a4aeu, 0x3cfa1bdbu, 0x4023cbb0u, 0xbce2cf27u, 0x3cfa1bdbu, 0x4023cbb0u, 0xbce2cf27u, 0xbc778218u, 0xc0604545u, 0x3e0e3a95u, 0x3e000000u, 0x3f900000u, 0x3f800000u, 0x3edb6db7u, 0x3fa00000u, 0x3f4ccccdu, 0x00000000u, 0x4015d8d8u, 0x4095d8d8u},
		    {0xbb133c92u, 0x404b4d97u, 0xbcbcb63du, 0x3ab6847au, 0x4043bdc3u, 0xbcde2640u, 0x3ab6847au, 0x4043bdc3u, 0xbcde2640u, 0x3ddbdce6u, 0xc05f12edu, 0xbdf69a15u, 0xbf000000u, 0xbfb00000u, 0x3e800000u, 0x3f000000u, 0x3fb00000u, 0x3f3a2e8cu, 0x00000000u, 0x4024d4eeu, 0x40a4d4eeu},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.0166666675f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {1.375f, 1.0f, 1.25f}, {0.5f, 0.875f, 0.875f}, {0.25f, -1.25f, 0.0f}, {-1.25f, 0.125f, 0.625f}, {-0.625f, -0.125f, -0.125f}, {-0.375f, 1.375f, -0.25f}, {-1.125f, -1.0f, 0.5f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f000000u, 0x40000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0xbd043c30u, 0x3f83bc1cu, 0xbd90d4dfu, 0xbd2032dcu, 0x3efbe1d6u, 0xbda47512u, 0xbd2032dcu, 0x3efbe1d6u, 0xbda47512u, 0xbed17a1eu, 0xc200c78du, 0xbf1304a2u, 0x3fb00000u, 0x3f800000u, 0x3fa00000u, 0x3d924925u, 0x3f200000u, 0x3fcccccdu, 0x00000000u, 0x3f95d8d8u, 0x4015d8d8u},
		    {0x3d8403cdu, 0x3f134b1du, 0x3deca2cdu, 0x3d2b7e30u, 0x3f275bbau, 0x3d93333au, 0x3d2b7e30u, 0x3f275bbau, 0x3d93333au, 0xbfad17e4u, 0x409620dfu, 0xc0274afdu, 0x3f000000u, 0x3f600000u, 0x3f600000u, 0x3e124925u, 0x3f400000u, 0x3faaaaabu, 0x00000000u, 0x3fb3d104u, 0x4033d104u},
		    {0x3d9a1e55u, 0x3f881c5bu, 0x3dd7918eu, 0x3da04844u, 0x3f899c67u, 0x3dd957c7u, 0x3da04844u, 0x3f899c67u, 0x3dd957c7u, 0x3e3840f1u, 0x3f336108u, 0x3d54280cu, 0x3e800000u, 0xbfa00000u, 0x00000000u, 0x3e5b6db7u, 0x3f600000u, 0x3f924925u, 0x00000000u, 0x3fd1c92fu, 0x4051c92fu},
		    {0x3d5d65bbu, 0x3fc7bd05u, 0x3d2d5d6cu, 0x3d709139u, 0x3fc56f2eu, 0x3d462637u, 0x3d709139u, 0x3fc56f2eu, 0x3d462637u, 0x3e8f16ecu, 0xbf8995dcu, 0x3eb8ff58u, 0xbfa00000u, 0x3e000000u, 0x3f200000u, 0x3e924925u, 0x3f800000u, 0x3f800000u, 0x00000000u, 0x3fefc15au, 0x406fc15au},
		    {0x3d2b8399u, 0x4003cd3fu, 0xbb752cadu, 0x3d3997c5u, 0x4000eb77u, 0x3af15826u, 0x3d3997c5u, 0x4000eb77u, 0x3af15826u, 0x3e51ecc6u, 0xc02be361u, 0x3eaa7847u, 0xbf200000u, 0xbe000000u, 0xbe000000u, 0x3eb6db6eu, 0x3f900000u, 0x3f638e39u, 0x00000000u, 0x4006dcc3u, 0x4086dcc3u},
		    {0x3cfa1bdbu, 0x4023cbb0u, 0xbce2cf27u, 0x3cf74682u, 0x402013cau, 0xbcd0320bu, 0x3cf74682u, 0x402013cau, 0xbcd0320bu, 0xbca8c9feu, 0xc05d81f2u, 0x3e0a9b0bu, 0xbec00000u, 0x3fb00000u, 0xbe800000u, 0x3edb6db7u, 0x3fa00000u, 0x3f4ccccdu, 0x00000000u, 0x4015d8d8u, 0x4095d8d8u},
		    {0x3ab6847au, 0x4043bdc3u, 0xbcde2640u, 0x3b429e34u, 0x404002adu, 0xbcedc3b6u, 0x3b429e34u, 0x404002adu, 0xbcedc3b6u, 0x3dc02efeu, 0xc05dfb9cu, 0xbde8463au, 0xbf900000u, 0xbf800000u, 0x3f000000u, 0x3f000000u, 0x3fb00000u, 0x3f3a2e8cu, 0x00000000u, 0x4024d4eeu, 0x40a4d4eeu},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.125f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.375f, -1.25f, 1.25f}, {1.125f, 1.25f, 0.0f}, {0.875f, 0.625f, -1.5f}, {1.25f, 1.375f, 0.625f}, {1.25f, 1.5f, 0.75f}, {-1.0f, -0.25f, 1.375f}, {0.75f, 1.5f, -0.25f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f000000u, 0x40000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0xbd2032dcu, 0x3efbe1d6u, 0xbda47512u, 0xbd0683c8u, 0x3d57fe89u, 0xbd9a2845u, 0xbd0683c8u, 0x3d57fe89u, 0xbd9a2845u, 0x3d4ba2fau, 0xc05ee000u, 0x3d235420u, 0x3ec00000u, 0xbfa00000u, 0x3fa00000u, 0x3d924925u, 0x3f200000u, 0x3fcccccdu, 0x00000000u, 0x3f95d8d8u, 0x4015d8d8u},
		    {0x3d2b7e30u, 0x3f275bbau, 0x3d93333au, 0xbcca4ed4u, 0x3f0d71cbu, 0xbdb32b2eu, 0xbcca4ed4u, 0x3f0d71cbu, 0xbdb32b2eu, 0xbf05e39cu, 0xbf4b9bc4u, 0xbfa04538u, 0x3f900000u, 0x3fa00000u, 0x00000000u, 0x3e124925u, 0x3f400000u, 0x3faaaaabu, 0x00000000u, 0x3fb3d104u, 0x4033d104u},
		    {0x3da04844u, 0x3f899c67u, 0x3dd957c7u, 0x3da441d0u, 0x3f8301d1u, 0x3d328286u, 0x3da441d0u, 0x3f8301d1u, 0x3d328286u, 0x3c7792a2u, 0xbecda9adu, 0xbef95066u, 0x3f600000u, 0x3f200000u, 0xbfc00000u, 0x3e5b6db7u, 0x3f600000u, 0x3f924925u, 0x00000000u, 0x3fd1c92fu, 0x4051c92fu},
		    {0x3d709139u, 0x3fc56f2eu, 0x3d462637u, 0x3de80c9fu, 0x3fb6fd02u, 0x3dd3d07eu, 0x3de80c9fu, 0x3fb6fd02u, 0x3dd3d07eu, 0x3ed78c4eu, 0xbf5ee182u, 0x3ed96d3eu, 0x3fa00000u, 0x3fb00000u, 0x3f200000u, 0x3e924925u, 0x3f800000u, 0x3f800000u, 0x00000000u, 0x3fefc15au, 0x406fc15au},
		    {0x3d3997c5u, 0x4000eb77u, 0x3af15826u, 0x3db497feu, 0x3fd9aca3u, 0x3d5e349du, 0x3db497feu, 0x3fd9aca3u, 0x3d5e349du, 0x3ea7c16bu, 0xc0197d0cu, 0x3ecd1490u, 0x3fa00000u, 0x3fc00000u, 0x3f400000u, 0x3eb6db6eu, 0x3f900000u, 0x3f638e39u, 0x00000000u, 0x4006dcc3u, 0x4086dcc3u},
		    {0x3cf74682u, 0x402013cau, 0xbcd0320bu, 0x3c7316d2u, 0x4004e636u, 0x3c075417u, 0x3c7316d2u, 0x4004e636u, 0x3c075417u, 0xbdedfd94u, 0xc04dc6ceu, 0x3e828a6fu, 0xbf800000u, 0xbe800000u, 0x3fb00000u, 0x3edb6db7u, 0x3fa00000u, 0x3f4ccccdu, 0x00000000u, 0x4015d8d8u, 0x4095d8d8u},
		    {0x3b429e34u, 0x404002adu, 0xbcedc3b6u, 0x3cc34366u, 0x4024b683u, 0xbd37c0c9u, 0x3cc34366u, 0x4024b683u, 0xbd37c0c9u, 0x3e2040a6u, 0xc04cbb3bu, 0xbdf343fcu, 0x3f400000u, 0x3fc00000u, 0xbe800000u, 0x3f000000u, 0x3fb00000u, 0x3f3a2e8cu, 0x00000000u, 0x4024d4eeu, 0x40a4d4eeu},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.25f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.625f, -0.5f, -1.125f}, {0.375f, -1.25f, 0.0f}, {-0.5f, -0.375f, -1.125f}, {-0.875f, 1.5f, -1.125f}, {0.375f, -0.625f, 0.5f}, {0.375f, 1.375f, -0.625f}, {1.375f, -1.0f, 0.375f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f000000u, 0x40000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0xbd0683c8u, 0x3d57fe89u, 0xbd9a2845u, 0xbc9f3503u, 0xbe7e9228u, 0xbe566a36u, 0xbc9f3503u, 0xbe7e9228u, 0xbe566a36u, 0x3d57b903u, 0xbf978798u, 0xbf06e241u, 0x3f200000u, 0xbf000000u, 0xbf900000u, 0x3d924925u, 0x3f200000u, 0x3fcccccdu, 0x00000000u, 0x3f95d8d8u, 0x4015d8d8u},
		    {0xbcca4ed4u, 0x3f0d71cbu, 0xbdb32b2eu, 0xbd343e19u, 0x3e7d03e0u, 0xbe8a9aa5u, 0xbd343e19u, 0x3e7d03e0u, 0xbe8a9aa5u, 0xbd98872du, 0xbf96cbe0u, 0xbf34ec48u, 0x3ec00000u, 0xbfa00000u, 0x00000000u, 0x3e124925u, 0x3f400000u, 0x3faaaaabu, 0x00000000u, 0x3fb3d104u, 0x4033d104u},
		    {0x3da441d0u, 0x3f8301d1u, 0x3d328286u, 0x3cdf08d4u, 0x3f3adc7fu, 0xbe25da66u, 0x3cdf08d4u, 0x3f3adc7fu, 0xbe25da66u, 0xbe4d5f41u, 0xbf8e40f0u, 0xbf473471u, 0xbf000000u, 0xbec00000u, 0xbf900000u, 0x3e5b6db7u, 0x3f600000u, 0x3f924925u, 0x00000000u, 0x3fd1c92fu, 0x4051c92fu},
		    {0x3de80c9fu, 0x3fb6fd02u, 0x3dd3d07eu, 0x3e0c7795u, 0x3f93b1f4u, 0x3da24dd5u, 0x3e0c7795u, 0x3f93b1f4u, 0x3da24dd5u, 0x3db59296u, 0xbf8316c6u, 0xbdb7e54fu, 0xbf600000u, 0x3fc00000u, 0xbf900000u, 0x3e924925u, 0x3f800000u, 0x3f800000u, 0x00000000u, 0x3fefc15au, 0x406fc15au},
		    {0x3db497feu, 0x3fd9aca3u, 0x3d5e349du, 0x3e3daaacu, 0x3f8cb55eu, 0x3e3467d2u, 0x3e3daaacu, 0x3f8cb55eu, 0x3e3467d2u, 0x3eb4febbu, 0xc00c3019u, 0x3ee369b7u, 0x3ec00000u, 0xbf200000u, 0x3f000000u, 0x3eb6db6eu, 0x3f900000u, 0x3f638e39u, 0x00000000u, 0x4006dcc3u, 0x4086dcc3u},
		    {0x3c7316d2u, 0x4004e636u, 0x3c075417u, 0x3c16b1feu, 0x3fb0db16u, 0x3d13e472u, 0x3c16b1feu, 0x3fb0db16u, 0x3d13e472u, 0xbca4fd31u, 0xc01ed387u, 0x3dcbadd3u, 0x3ec00000u, 0x3fb00000u, 0xbf200000u, 0x3edb6db7u, 0x3fa00000u, 0x3f4ccccdu, 0x00000000u, 0x4015d8d8u, 0x4095d8d8u},
		    {0x3cc34366u, 0x4024b683u, 0xbd37c0c9u, 0x3dfec8d8u, 0x3fdd491fu, 0xbd52186cu, 0x3dfec8d8u, 0x3fdd491fu, 0xbd52186cu, 0x3eb438feu, 0xc03d3ed4u, 0xbcb86575u, 0x3fb00000u, 0xbf800000u, 0x3ec00000u, 0x3f000000u, 0x3fb00000u, 0x3f3a2e8cu, 0x00000000u, 0x4024d4eeu, 0x40a4d4eeu},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeStep step;
		step.Seconds = 0.0333333351f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {1.5f, -0.375f, 1.25f}, {0.125f, -1.375f, -1.25f}, {0.125f, -1.125f, -1.5f}, {0.25f, -0.75f, 0.875f}, {-0.5f, 1.25f, 0.25f}, {-0.125f, -0.375f, 0.0f}, {1.125f, 0.5f, 0.125f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3f000000u, 0x40000000u, 0x00000001u, 0x00000000u, 0x00000000u},
		    {0xbc9f3503u, 0xbe7e9228u, 0xbe566a36u, 0xbc7a6206u, 0xbe91958du, 0xbe66cc11u, 0xbc7a6206u, 0xbe91958du, 0xbe66cc11u, 0x3dfe827fu, 0xbf88e9e4u, 0xbef5260cu, 0x3fc00000u, 0xbec00000u, 0x3fa00000u, 0x3d924925u, 0x3f200000u, 0x3fcccccdu, 0x00000000u, 0x3f95d8d8u, 0x4015d8d8u},
		    {0xbd343e19u, 0x3e7d03e0u, 0xbe8a9aa5u, 0xbd3774fbu, 0x3e572922u, 0xbe9647afu, 0xbd3774fbu, 0x3e572922u, 0xbe9647afu, 0xbcbff1dbu, 0xbf8d473bu, 0xbf2e4e14u, 0x3e000000u, 0xbfb00000u, 0xbfa00000u, 0x3e124925u, 0x3f400000u, 0x3faaaaabu, 0x00000000u, 0x3fb3d104u, 0x4033d104u},
		    {0x3cdf08d4u, 0x3f3adc7fu, 0xbe25da66u, 0x3cb85a17u, 0x3f318c2cu, 0xbe3d74fbu, 0x3cb85a17u, 0x3f318c2cu, 0xbe3d74fbu, 0xbe100604u, 0xbf8ab565u, 0xbf2fc3a7u, 0x3e000000u, 0xbf900000u, 0xbfc00000u, 0x3e5b6db7u, 0x3f600000u, 0x3f924925u, 0x00000000u, 0x3fd1c92fu, 0x4051c92fu},
		    {0x3e0c7795u, 0x3f93b1f4u, 0x3da24dd5u, 0x3e0c1900u, 0x3f8de508u, 0x3d8e077du, 0x3e0c1900u, 0x3f8de508u, 0x3d8e077du, 0xbc2fa6ffu, 0xbfac5b64u, 0xbe969cd6u, 0x3e800000u, 0xbf400000u, 0x3f600000u, 0x3e924925u, 0x3f800000u, 0x3f800000u, 0x00000000u, 0x3fefc15au, 0x406fc15au},
		    {0x3e3daaacu, 0x3f8cb55eu, 0x3e3467d2u, 0x3e49ab52u, 0x3f8372deu, 0x3e44d3f2u, 0x3e49ab52u, 0x3f8372deu, 0x3e44d3f2u, 0x3eb1e509u, 0xc0093e31u, 0x3ef36722u, 0xbf000000u, 0x3fa00000u, 0x3e800000u, 0x3eb6db6eu, 0x3f900000u, 0x3f638e39u, 0x00000000u, 0x4006dcc3u, 0x4086dcc3u},
		    {0x3c16b1feu, 0x3fb0db16u, 0x3d13e472u, 0x3c0cbac6u, 0x3fa63a8du, 0x3d21777fu, 0x3c0cbac6u, 0x3fa63a8du, 0x3d21777fu, 0xbc935996u, 0xc01d210du, 0x3dc8b51bu, 0xbe000000u, 0xbec00000u, 0x00000000u, 0x3edb6db7u, 0x3fa00000u, 0x3f4ccccdu, 0x00000000u, 0x4015d8d8u, 0x4095d8d8u},
		    {0x3dfec8d8u, 0x3fdd491fu, 0xbd52186cu, 0x3e0c4120u, 0x3fd0d29cu, 0xbd54804cu, 0x3e0c4120u, 0x3fd0d29cu, 0xbd54804cu, 0x3ebdb75eu, 0xc037d40bu, 0xbc8df09fu, 0x3f900000u, 0x3f000000u, 0x3e000000u, 0x3f000000u, 0x3fb00000u, 0x3f3a2e8cu, 0x00000000u, 0x4024d4eeu, 0x40a4d4eeu},
		};
		result.Steps.push_back(std::move(step));
	}

	return result;
}

static std::uint32_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t& cursor) {
	if (cursor > bytes.size() || 4 > bytes.size() - cursor) {
		throw std::runtime_error("truncated EBC1 native bobble vector");
	}
	const auto result = static_cast<std::uint32_t>(bytes[cursor]) | static_cast<std::uint32_t>(bytes[cursor + 1]) << 8 | static_cast<std::uint32_t>(bytes[cursor + 2]) << 16 | static_cast<std::uint32_t>(bytes[cursor + 3]) << 24;
	cursor += 4;

	return result;
}

static float ReadFloat(std::span<const std::uint8_t> bytes, std::size_t& cursor) {
	return std::bit_cast<float>(ReadWord(bytes, cursor));
}

static std::size_t ReplayNativeFile(const std::filesystem::path& path) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	if (!stream || stream.tellg() < 8 || stream.tellg() > 1024 * 1024) {
		throw std::runtime_error("native bobble corpus requires a bounded actual EBC1 file");
	}
	const auto size = static_cast<std::size_t>(stream.tellg());
	std::vector<std::uint8_t> bytes(size);
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
	Require(static_cast<std::size_t>(stream.gcount()) == size && std::string(bytes.begin(), bytes.begin() + 4) == "EBC1", "native bobble corpus magic/length changed");
	std::size_t cursor = 4;
	const auto count = ReadWord(bytes, cursor);
	Require(count == 8, "native bobble corpus must retain all eight measured sequences");
	std::size_t words = 0;
	for (std::size_t case_index = 0; case_index != count; ++case_index) {
		NativeCase input;
		const auto particles = ReadWord(bytes, cursor);
		Require(particles >= 2 && particles <= 256, "native bobble corpus particle bound changed");
		const auto adhoc = ReadWord(bytes, cursor);
		Require(adhoc <= 1, "native bobble corpus solver selector changed");
		input.Definition.AdhocSimulation = adhoc != 0;
		input.Definition.Length = ReadFloat(bytes, cursor) * static_cast<float>(particles - 1);
		input.Definition.FrictionBegin = ReadFloat(bytes, cursor);
		input.Definition.FrictionEnd = ReadFloat(bytes, cursor);
		input.Definition.MassScale = ReadFloat(bytes, cursor);
		input.Definition.AlignmentSpring = ReadFloat(bytes, cursor);
		input.Definition.LengthSpring = ReadFloat(bytes, cursor);
		for (std::size_t index = 0; index != particles; ++index) {
			input.Definition.MassWeights.push_back(ReadFloat(bytes, cursor));
		}
		const auto steps = ReadWord(bytes, cursor);
		Require(steps == 8, "native bobble corpus phase count changed");
		for (std::size_t step_index = 0; step_index != steps; ++step_index) {
			NativeStep step;
			step.Seconds = ReadFloat(bytes, cursor);
			for (auto& value : step.Posture.Coordinate) {
				value = ReadFloat(bytes, cursor);
			}
			step.Forces.resize(particles);
			for (auto& force : step.Forces) {
				for (auto& value : force) {
					value = ReadFloat(bytes, cursor);
				}
			}
			step.Expected.resize(particles);
			for (auto& expected : step.Expected) {
				for (auto& value : expected) {
					value = ReadWord(bytes, cursor);
				}
			}
			input.Steps.push_back(std::move(step));
		}
		words += ReplayCase(input, case_index);
	}
	Require(cursor == bytes.size(), "native bobble corpus trailing data changed");

	return words;
}

static void TestActualOwnerAndInvalidInputs() {
	sgre::scene::EmoteBobbleDefinition definition;
	definition.MassWeights = {1, 1};
	definition.AdhocSimulation = true;
	sgre::scene::EmoteBobbleModel model(definition);
	Reject([&] {
		model.Initialize();
	},
	    "posture ownership");
	sgre::scene::EmoteBobblePosture posture;
	model.SetPosture(posture);
	model.Initialize();
	model.ClearForces();
	model.AddForce(1, {0, 3, 0});
	model.Step(0.5f);
	Require(model.Particles()[1].Position[1] == 1 && model.Particles()[1].Velocity[1] == 0, "native adhoc velocity cap removes outward motion on the actual single free particle");
	std::array<std::array<float, 2>, 2> points;
	model.WritePoints(points);
	Require(points[0] == std::array<float, 2>{0, 0} && points[1] == std::array<float, 2>{0, 1}, "bobble actual owner publishes particle positions without a captured-state setter");
	Reject([&] {
		model.Step(0);
	},
	    "positive seconds");
	Reject([&] {
		model.AddForce(0, {0, 1, 0});
	},
	    "fixed root force");
	Reject([&] {
		model.AddForce(1, {0, std::numeric_limits<float>::infinity(), 0});
	},
	    "finite force");
	Reject([&] {
		model.WritePoints(std::span<std::array<float, 2>>{});
	},
	    "exact output size");
	definition.MassWeights.clear();
	Reject([&] {
		sgre::scene::EmoteBobbleModel invalid(definition);
	},
	    "bounded actual chain");
	definition.MassWeights = {1, 0};
	Reject([&] {
		sgre::scene::EmoteBobbleModel invalid(definition);
	},
	    "positive mass weights");
}

int main(int argc, char** argv) {
	try {
		Require(argc <= 2, "usage: emote_bobble_model_tests [native-vectors.bin]");
		TestActualOwnerAndInvalidInputs();
		const auto embedded_words = ReplayCase(EmbeddedCase0(), 0) + ReplayCase(EmbeddedCase7(), 7);
		std::cout << "EPBobble original numerical core: " << embedded_words << " embedded words matched exactly" << '\n';
		if (argc == 2) {
			const auto words = ReplayNativeFile(argv[1]);
			std::cout << "EPBobble full native corpus: 64 steps / " << words << " words matched exactly" << '\n';
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
