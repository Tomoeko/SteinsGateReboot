#include "sgre/scene/emote_bobble_model.hpp"
#include "sgre/scene/emote_bobble_sections.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct NativeSectionStep {
	float Seconds = 0;
	sgre::scene::EmoteBobblePosture Posture;
	std::vector<std::array<float, 3>> Forces;
	std::vector<std::array<std::uint32_t, 8>> Expected;
};

struct NativeSectionCase {
	sgre::scene::EmoteBobbleDefinition Model;
	sgre::scene::EmoteBobbleSectionDefinition Sections;
	std::vector<NativeSectionStep> Steps;
};

static void Require(bool condition, const std::string& message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static float ReplaySectionCase(const NativeSectionCase& input, std::size_t case_index) {
	sgre::scene::EmoteBobbleModel model(input.Model);
	sgre::scene::EmoteBobbleSections sections(model, input.Sections);
	sgre::scene::EmoteBobblePosture initial;
	model.SetPosture(initial);
	model.Initialize();
	sections.Refresh();
	float maximum_error = 0;
	for (std::size_t step_index = 0; step_index != input.Steps.size(); ++step_index) {
		const auto& step = input.Steps[step_index];
		model.SetPosture(step.Posture);
		model.ClearForces();
		Require(step.Forces.size() == model.Particles().size() && step.Expected.size() == model.Particles().size(), "Bobble shape replay requires the whole actual chain");
		for (std::size_t index = 1; index != step.Forces.size(); ++index) {
			model.AddForce(index, step.Forces[index]);
		}
		model.Step(step.Seconds);
		sections.Refresh();
		const auto states = sections.Sections();
		for (std::size_t particle = 0; particle != states.size(); ++particle) {
			const auto& actual = states[particle];
			const std::array<float, 7> values{actual.Center[0], actual.Center[1], actual.Radius[0], actual.Radius[1], actual.Angle, actual.RelativeAngle, actual.LengthScale};
			Require(step.Expected[particle][5] == 1, "native publication must retain the actual ellipse kind/flags low word");
			for (std::size_t field = 0; field != values.size(); ++field) {
				const auto expected_field = field < 5 ? field : field + 1;
				const float expected = std::bit_cast<float>(step.Expected[particle][expected_field]);
				const float error = std::abs(values[field] - expected);
				maximum_error = std::max(maximum_error, error);
				// Native CRT and host standard-library angle functions are distinct measured implementations.
				const float tolerance = field < 2 ? 0.0f : 0.000004f * std::max(1.0f, std::abs(expected));
				Require(std::isfinite(values[field]) && error <= tolerance, "native bobble section mismatch case " + std::to_string(case_index) + ", step " + std::to_string(step_index) + ", particle " + std::to_string(particle) + ", field " + std::to_string(field) + ": actual " + std::to_string(values[field]) + ", expected " + std::to_string(expected) + ", error " + std::to_string(error));
			}
		}
	}

	return maximum_error;
}

static NativeSectionCase EmbeddedSectionCase1() {
	NativeSectionCase result;
	result.Model.Length = 2.0f;
	result.Model.MassScale = 0.5f;
	result.Model.AlignmentSpring = 0.25f;
	result.Model.LengthSpring = 0.5f;
	result.Model.FrictionBegin = 0.0f;
	result.Model.FrictionEnd = 0.0f;
	result.Model.AdhocSimulation = false;
	result.Model.MassWeights = {1.0f, 1.25f, 1.5f};
	result.Sections.Normal = {0.479999989f, 0.639999986f, 0.600000024f};
	result.Sections.Tangent = {-0.800000012f, 0.600000024f, 0.0f};
	result.Sections.Radii = {1.0f, 1.25f, 1.5f};
	result.Sections.OrientationInheritance = 0.25f;
	result.Sections.RadiusInheritance = 0.25f;
	{
		NativeSectionStep step;
		step.Seconds = 0.0166666675f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x00000000u, 0x3f800000u, 0x3f400000u, 0x3fa00000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x00000000u, 0x40000000u, 0x3f666667u, 0x3fc00000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.125f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {-0.75f, -0.25f, 0.625f}, {-1.25f, 0.875f, -1.0f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0xbc99d93cu, 0x3f80b0b4u, 0x3f40a062u, 0x3f9fe30cu, 0x3f6e273fu, 0x00000001u, 0x3fc946f0u, 0x3f7f46aeu},
		    {0xbcd48bd8u, 0x40004db3u, 0x3f643fd2u, 0x3fc00000u, 0x3f6ee0f5u, 0x00000001u, 0x3fc88c5eu, 0x3f800000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.25f;
		step.Posture.Coordinate = {0.25f, -2.0f, 0.5f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {1.5f, 1.5f, 0.5f}, {1.5f, 0.75f, 1.375f}};
		step.Expected = {
		    {0x3e800000u, 0xc0000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x3e09d620u, 0x3eebff67u, 0x3f2901bfu, 0x3f916630u, 0x3f7391a4u, 0x00000001u, 0x3fc77d58u, 0x3f228e01u},
		    {0x3da70f95u, 0x3fba3019u, 0x3f602212u, 0x3fc00000u, 0x3f737dcfu, 0x00000001u, 0x3fc7ba08u, 0x3f800000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.0333333351f;
		step.Posture.Coordinate = {-0.125f, 3.0f, -0.25f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {-0.5f, -0.375f, 0.75f}, {1.25f, 0.5f, 0.25f}};
		step.Expected = {
		    {0xbe000000u, 0x40400000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x3d9c1f64u, 0x3f8f0528u, 0x3f6f009au, 0x3f94d1f9u, 0xbd55f7b7u, 0x00000001u, 0x40122b9cu, 0x3f38730au},
		    {0x3dc84d37u, 0x3fb0e4e6u, 0x3f928c61u, 0x3fece125u, 0x3f673048u, 0x00000001u, 0x3fcd37b2u, 0x3ff7adb9u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.0166666675f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {1.5f, 0.625f, 0.5f}, {0.5f, -0.375f, 1.5f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x3d287300u, 0x3f9f9857u, 0x3f3cad0eu, 0x3f9bcd4fu, 0x3f6a4411u, 0x00000001u, 0x3fc99206u, 0x3f6521f7u},
		    {0x3dd95776u, 0x3fac495au, 0x3fbaa980u, 0x4004ec80u, 0x3f32f259u, 0x00000001u, 0x3fd7f2bau, 0x402276aau},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.125f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {-0.125f, -0.875f, -1.125f}, {-0.125f, -1.125f, -1.25f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0xbe35ad60u, 0x3fd0f54eu, 0x3f2a9d31u, 0x3f96dfdbu, 0x3f79c9bbu, 0x00000001u, 0x3fc65c02u, 0x3f4598acu},
		    {0x3e3171fbu, 0x3f89a254u, 0x3f9b48dcu, 0x3fc3d27eu, 0x3e54a465u, 0x00000001u, 0x400114f0u, 0x3f8a3150u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.25f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.25f, 0.5f, 0.5f}, {0.375f, 0.375f, -1.25f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0xbe8530bau, 0x3fba7c06u, 0x3f238dd1u, 0x3f97cde9u, 0x3f80ad98u, 0x00000001u, 0x3fc4bc7au, 0x3f4b8c36u},
		    {0x3e50bc5au, 0x3f4d97b9u, 0x3f98c17eu, 0x3fc00000u, 0x3e66f499u, 0x00000001u, 0x3feff945u, 0x3f800000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.0333333351f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {-1.0f, 0.5f, -0.25f}, {-0.75f, -1.5f, -1.5f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0xbe69ab84u, 0x3f99a7c0u, 0x3f25a59cu, 0x3f9adb49u, 0x3f813905u, 0x00000001u, 0x3fc48815u, 0x3f5f1507u},
		    {0x3e536206u, 0x3f441424u, 0x3f9c2673u, 0x3fc5cce2u, 0x3e93e079u, 0x00000001u, 0x3fe816eau, 0x3f8f77b1u},
		};
		result.Steps.push_back(std::move(step));
	}

	return result;
}

static NativeSectionCase EmbeddedSectionCase7() {
	NativeSectionCase result;
	result.Model.Length = 3.5f;
	result.Model.MassScale = 0.5f;
	result.Model.AlignmentSpring = 0.25f;
	result.Model.LengthSpring = 0.5f;
	result.Model.FrictionBegin = 0.0f;
	result.Model.FrictionEnd = 0.5f;
	result.Model.AdhocSimulation = true;
	result.Model.MassWeights = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 2.75f};
	result.Sections.Normal = {0.479999989f, 0.639999986f, 0.600000024f};
	result.Sections.Tangent = {-0.800000012f, 0.600000024f, 0.0f};
	result.Sections.Radii = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 2.75f};
	result.Sections.OrientationInheritance = 1.0f;
	result.Sections.RadiusInheritance = 0.25f;
	{
		NativeSectionStep step;
		step.Seconds = 0.0166666675f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x00000000u, 0x3f000000u, 0x3f400000u, 0x3fa00000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x00000000u, 0x3f800000u, 0x3f666667u, 0x3fc00000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x00000000u, 0x3fc00000u, 0x3f866667u, 0x3fe00000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x00000000u, 0x40000000u, 0x3f99999au, 0x40000000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x00000000u, 0x40200000u, 0x3faccccdu, 0x40100000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x00000000u, 0x40400000u, 0x3fc00000u, 0x40200000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x00000000u, 0x40600000u, 0x3fd33334u, 0x40300000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.125f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {-1.5f, 0.625f, -0.375f}, {0.5f, 1.25f, 1.5f}, {-1.25f, 1.375f, -1.5f}, {-0.125f, 0.25f, -1.5f}, {-0.75f, 0.75f, -0.75f}, {1.5f, 1.125f, 0.25f}, {0.625f, 0.75f, 0.75f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0xbd0bd385u, 0x3f004653u, 0x3f3c9c14u, 0x3f9fe894u, 0x3f803d05u, 0x00000001u, 0x3fc83b27u, 0x3f7f6a1bu},
		    {0x3bfdb778u, 0x3f7fc252u, 0x3f7602e8u, 0x3fbff446u, 0x3f4e8a7au, 0x00000001u, 0x3fcdeba3u, 0x3f7fc177u},
		    {0xbca34255u, 0x3fbf8e06u, 0x3f6cb851u, 0x3fdff426u, 0x3f827f5eu, 0x00000001u, 0x3fc4a17eu, 0x3f7fc9d3u},
		    {0xbb258dc6u, 0x3fff9988u, 0x3f99919du, 0x3ffff54au, 0x3f63eba0u, 0x00000001u, 0x3fc95876u, 0x3f7fd526u},
		    {0xbc1e6833u, 0x401fd08du, 0x3fb12f58u, 0x400ffbe8u, 0x3f6dc285u, 0x00000001u, 0x3fcaba08u, 0x3f7fe2e2u},
		    {0x3c917eb5u, 0x403fc502u, 0x3fc4c568u, 0x401ffdbcu, 0x3f5baf20u, 0x00000001u, 0x3fcaeadeu, 0x3f7ff17eu},
		    {0x3be75fd9u, 0x405fc2a6u, 0x3fd549b4u, 0x40300000u, 0x3f71473fu, 0x00000001u, 0x3fc9dc86u, 0x3f7ffffeu},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.25f;
		step.Posture.Coordinate = {0.25f, -1.0f, 0.5f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {-1.25f, 0.0f, 1.375f}, {0.625f, 0.5f, -0.75f}, {0.625f, 1.125f, -0.875f}, {0.25f, -1.375f, 0.875f}, {0.5f, -0.25f, -0.875f}, {0.875f, -0.75f, -1.125f}, {-1.25f, -1.0f, -0.75f}};
		step.Expected = {
		    {0x3e800000u, 0xbf800000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0x3e29f267u, 0xbe4c4511u, 0x3f06d491u, 0x3f972b10u, 0x3f8cdd14u, 0x00000001u, 0x3fc17a5cu, 0x3f477a02u},
		    {0x3de19335u, 0x3ed6c3dau, 0x3f2c3085u, 0x3fbaa5d8u, 0x3f8a9000u, 0x00000001u, 0x3fc1bc52u, 0x3f637481u},
		    {0x3d8cd398u, 0x3f826fefu, 0x3f54139cu, 0x3fdabb26u, 0x3f870dd7u, 0x00000001u, 0x3fc24bf5u, 0x3f67e9ceu},
		    {0x3d33d985u, 0x3fcc6663u, 0x3f813ec5u, 0x3ffb44f5u, 0x3f827302u, 0x00000001u, 0x3fc34a80u, 0x3f6d13d5u},
		    {0x3d0dbc6fu, 0x4009a18fu, 0x3f9b5cf7u, 0x400e2708u, 0x3f7aa12cu, 0x00000001u, 0x3fc4d00cu, 0x3f72dcaau},
		    {0x3cfe4b32u, 0x402b6133u, 0x3fb858fbu, 0x401ef066u, 0x3f732911u, 0x00000001u, 0x3fc71472u, 0x3f7935beu},
		    {0xbb133c92u, 0x404b4d97u, 0x3fd7bd96u, 0x40300000u, 0x3f7c9409u, 0x00000001u, 0x3fca00e4u, 0x3f7fffffu},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.0333333351f;
		step.Posture.Coordinate = {-0.125f, 1.5f, -0.25f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {1.5f, -1.375f, 1.375f}, {0.375f, 0.125f, 1.0f}, {-0.125f, 1.0f, -1.5f}, {0.375f, 0.25f, -0.125f}, {0.25f, 0.0f, -1.25f}, {0.125f, 1.125f, 1.0f}, {-0.5f, -1.375f, 0.25f}};
		step.Expected = {
		    {0xbe000000u, 0x3fc00000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0xbd043c30u, 0x3f83bc1cu, 0xbf12df9bu, 0x3f9f84b6u, 0xbfd2ee19u, 0x00000001u, 0xbf285d0eu, 0x3f7ceaf2u},
		    {0x3d8403cdu, 0x3f134b1du, 0xbf284e6du, 0x3fc00000u, 0xbfd119edu, 0x00000001u, 0xbf2f1ec6u, 0x3f800000u},
		    {0x3d9a1e55u, 0x3f881c5bu, 0x3f6709fcu, 0x3fe0abcdu, 0x3f56f356u, 0x00000001u, 0x3fd0a10fu, 0x3f8188b0u},
		    {0x3d5d65bbu, 0x3fc7bd05u, 0x3f6186d5u, 0x3fffe75eu, 0x3f703d83u, 0x00000001u, 0x3fcb78bcu, 0x3f7f9d7au},
		    {0x3d2b8399u, 0x4003cd3fu, 0x3f85edb9u, 0x400ff4cfu, 0x3f691154u, 0x00000001u, 0x3fccecbcu, 0x3f7fb06du},
		    {0x3cfa1bdbu, 0x4023cbb0u, 0x3f9e6627u, 0x401ff9cbu, 0x3f64b04bu, 0x00000001u, 0x3fcf36e2u, 0x3f7fd849u},
		    {0x3ab6847au, 0x4043bdc3u, 0x3fb9586au, 0x40300000u, 0x3f68d949u, 0x00000001u, 0x3fd22ebeu, 0x3f7ffffeu},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.0166666675f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {1.375f, 1.0f, 1.25f}, {0.5f, 0.875f, 0.875f}, {0.25f, -1.25f, 0.0f}, {-1.25f, 0.125f, 0.625f}, {-0.625f, -0.125f, -0.125f}, {-0.375f, 1.375f, -0.25f}, {-1.125f, -1.0f, 0.5f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0xbd2032dcu, 0x3efbe1d6u, 0x3f1ba3c2u, 0x3fa00000u, 0x3f881935u, 0x00000001u, 0x3fc286e1u, 0x3f800000u},
		    {0x3d2b7e30u, 0x3f275bbau, 0x3faba539u, 0x3fd5ce55u, 0xbe432b98u, 0x00000001u, 0x401b502au, 0x3fba2637u},
		    {0x3da04844u, 0x3f899c67u, 0x3f92cf03u, 0x3fe4ca7au, 0x3f4ab168u, 0x00000001u, 0x3fcfaec8u, 0x3f8af361u},
		    {0x3d709139u, 0x3fc56f2eu, 0x3f82f53cu, 0x4000f57cu, 0x3f80b1cau, 0x00000001u, 0x3fc4aa3eu, 0x3f83d5efu},
		    {0x3d3997c5u, 0x4000eb77u, 0x3f986942u, 0x4010f572u, 0x3f7c0621u, 0x00000001u, 0x3fc586c2u, 0x3f8368b1u},
		    {0x3cf74682u, 0x402013cau, 0x3fb20205u, 0x40207ee9u, 0x3f789afau, 0x00000001u, 0x3fc76e99u, 0x3f81961du},
		    {0x3b429e34u, 0x404002adu, 0x3fce5ec0u, 0x40300344u, 0x3f795080u, 0x00000001u, 0x3fca0d8eu, 0x3f800980u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.125f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.375f, -1.25f, 1.25f}, {1.125f, 1.25f, 0.0f}, {0.875f, 0.625f, -1.5f}, {1.25f, 1.375f, 0.625f}, {1.25f, 1.5f, 0.75f}, {-1.0f, -0.25f, 1.375f}, {0.75f, 1.5f, -0.25f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0xbd0683c8u, 0x3d57fe89u, 0xbeada2f4u, 0x3fd28985u, 0x3fb83c03u, 0x00000001u, 0x3fd26f05u, 0x4010dc07u},
		    {0xbcca4ed4u, 0x3f0d71cbu, 0x3f62d1d4u, 0x3fc00000u, 0x3f6e166fu, 0x00000001u, 0x3fc6aa82u, 0x3f7ffffeu},
		    {0x3da441d0u, 0x3f8301d1u, 0x3fa61f7bu, 0x3fe00000u, 0x3f0a3f61u, 0x00000001u, 0x3fe0ff22u, 0x3f800000u},
		    {0x3de80c9fu, 0x3fb6fd02u, 0x3fb65ea0u, 0x40034286u, 0x3f472e8cu, 0x00000001u, 0x3fd231d4u, 0x3f8d0a18u},
		    {0x3db497feu, 0x3fd9aca3u, 0x3f972edbu, 0x401c67e2u, 0x3f8c1cbbu, 0x00000001u, 0x3fc0aa81u, 0x3fac1c08u},
		    {0x3c7316d2u, 0x4004e636u, 0x3fabdd6fu, 0x40258957u, 0x3f95525bu, 0x00000001u, 0x3fc386e9u, 0x3f91b77bu},
		    {0x3cc34366u, 0x4024b683u, 0x3fbde879u, 0x40300000u, 0x3f775b41u, 0x00000001u, 0x3fc24b9cu, 0x3f800000u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.25f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {0.625f, -0.5f, -1.125f}, {0.375f, -1.25f, 0.0f}, {-0.5f, -0.375f, -1.125f}, {-0.875f, 1.5f, -1.125f}, {0.375f, -0.625f, 0.5f}, {0.375f, 1.375f, -0.625f}, {1.375f, -1.0f, 0.375f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0xbc9f3503u, 0xbe7e9228u, 0xbf9f23c8u, 0x3fa99123u, 0xbe5da5cfu, 0x00000001u, 0xc017f6e3u, 0x3f9e9d3bu},
		    {0xbd343e19u, 0x3e7d03e0u, 0x3f34fc52u, 0x3fbfff1eu, 0x3f78e0b5u, 0x00000001u, 0x3fc8f49au, 0x3f7ffb46u},
		    {0x3cdf08d4u, 0x3f3adc7fu, 0x3f972ce0u, 0x3fdfff7eu, 0x3f1b9cb4u, 0x00000001u, 0x3fe2373eu, 0x3f7ffdabu},
		    {0x3e0c7795u, 0x3f93b1f4u, 0x3fc566beu, 0x40000000u, 0x3e7d6e8cu, 0x00000001u, 0x4006961cu, 0x3f7fffffu},
		    {0x3e3daaacu, 0x3f8cb55eu, 0x3ea38f40u, 0x403520dfu, 0xbf9f98c6u, 0x00000001u, 0xbfed95b2u, 0x4002018bu},
		    {0x3c16b1feu, 0x3fb0db16u, 0x3f9981a9u, 0x40271274u, 0x400a47f4u, 0x00000001u, 0xc0a89cfeu, 0x3f96a174u},
		    {0x3dfec8d8u, 0x3fdd491fu, 0x3fcc251au, 0x4036bccau, 0x3fc6e90au, 0x00000001u, 0x3ef88d50u, 0x3f939990u},
		};
		result.Steps.push_back(std::move(step));
	}
	{
		NativeSectionStep step;
		step.Seconds = 0.0333333351f;
		step.Posture.Coordinate = {0.0f, 0.0f, 0.0f};
		step.Forces = {{0.0f, 0.0f, 0.0f}, {1.5f, -0.375f, 1.25f}, {0.125f, -1.375f, -1.25f}, {0.125f, -1.125f, -1.5f}, {0.25f, -0.75f, 0.875f}, {-0.5f, 1.25f, 0.25f}, {-0.125f, -0.375f, 0.0f}, {1.125f, 0.5f, 0.125f}};
		step.Expected = {
		    {0x00000000u, 0x00000000u, 0x3f19999au, 0x3f800000u, 0x3f6d6338u, 0x00000001u, 0x3fc90fdau, 0x3f800000u},
		    {0xbc7a6206u, 0xbe91958du, 0xbf998e4fu, 0x3fa6ef59u, 0xbe8f86c0u, 0x00000001u, 0xc013d635u, 0x3f96311bu},
		    {0xbd3774fbu, 0x3e572922u, 0x3f2aa03fu, 0x3fbffff1u, 0x3f7826a2u, 0x00000001u, 0x3fca0c3du, 0x3f7fffb1u},
		    {0x3cb85a17u, 0x3f318c2cu, 0x3f936e90u, 0x3fdffff7u, 0x3f1acd14u, 0x00000001u, 0x3fe401a0u, 0x3f7fffd9u},
		    {0x3e0c1900u, 0x3f8de508u, 0x3fc285c0u, 0x40000000u, 0x3e4dfb0bu, 0x00000001u, 0x400a21f2u, 0x3f800000u},
		    {0x3e49ab52u, 0x3f8372deu, 0x3dfcb709u, 0x402bc85bu, 0xbfa00b3eu, 0x00000001u, 0xbfd65910u, 0x3fe2c85eu},
		    {0x3c0cbac6u, 0x3fa63a8du, 0x3f83fe0cu, 0x4026dde4u, 0x401be711u, 0x00000001u, 0xc0b05e97u, 0x3f95f93eu},
		    {0x3e0c4120u, 0x3fd0d29cu, 0x3f9cb23bu, 0x403746a0u, 0x3fe08136u, 0x00000001u, 0xbddedf60u, 0x3f952a8du},
		};
		result.Steps.push_back(std::move(step));
	}

	return result;
}

static std::uint32_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t& cursor) {
	if (cursor > bytes.size() || 4 > bytes.size() - cursor) {
		throw std::runtime_error("truncated EBS1 native section vector");
	}
	const auto result = static_cast<std::uint32_t>(bytes[cursor]) | static_cast<std::uint32_t>(bytes[cursor + 1]) << 8 | static_cast<std::uint32_t>(bytes[cursor + 2]) << 16 | static_cast<std::uint32_t>(bytes[cursor + 3]) << 24;
	cursor += 4;

	return result;
}

static float ReadFloat(std::span<const std::uint8_t> bytes, std::size_t& cursor) {
	return std::bit_cast<float>(ReadWord(bytes, cursor));
}

static float ReplaySectionFile(const std::filesystem::path& path) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	if (!stream || stream.tellg() < 8 || stream.tellg() > 1024 * 1024) {
		throw std::runtime_error("native bobble section corpus requires a bounded actual EBS1 file");
	}
	const auto size = static_cast<std::size_t>(stream.tellg());
	std::vector<std::uint8_t> bytes(size);
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
	Require(static_cast<std::size_t>(stream.gcount()) == size && std::string(bytes.begin(), bytes.begin() + 4) == "EBS1", "native bobble section magic/length changed");
	std::size_t cursor = 4;
	const auto count = ReadWord(bytes, cursor);
	Require(count == 8, "native section corpus must retain eight actual chains");
	float maximum_error = 0;
	for (std::size_t case_index = 0; case_index != count; ++case_index) {
		NativeSectionCase input;
		const auto particles = ReadWord(bytes, cursor);
		Require(particles >= 2 && particles <= 256, "native section particle bound changed");
		const auto adhoc = ReadWord(bytes, cursor);
		Require(adhoc <= 1, "native section solver flag changed");
		input.Model.AdhocSimulation = adhoc != 0;
		input.Model.Length = ReadFloat(bytes, cursor) * static_cast<float>(particles - 1);
		input.Model.FrictionBegin = ReadFloat(bytes, cursor);
		input.Model.FrictionEnd = ReadFloat(bytes, cursor);
		input.Model.MassScale = ReadFloat(bytes, cursor);
		input.Model.AlignmentSpring = ReadFloat(bytes, cursor);
		input.Model.LengthSpring = ReadFloat(bytes, cursor);
		for (auto& value : input.Sections.Normal) {
			value = ReadFloat(bytes, cursor);
		}
		for (auto& value : input.Sections.Tangent) {
			value = ReadFloat(bytes, cursor);
		}
		input.Sections.OrientationInheritance = ReadFloat(bytes, cursor);
		input.Sections.RadiusInheritance = ReadFloat(bytes, cursor);
		for (std::size_t index = 0; index != particles; ++index) {
			input.Model.MassWeights.push_back(ReadFloat(bytes, cursor));
		}
		for (std::size_t index = 0; index != particles; ++index) {
			input.Sections.Radii.push_back(ReadFloat(bytes, cursor));
		}
		const auto steps = ReadWord(bytes, cursor);
		Require(steps == 8, "native section phase count changed");
		for (std::size_t step_index = 0; step_index != steps; ++step_index) {
			NativeSectionStep step;
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
			for (auto& shape : step.Expected) {
				for (auto& value : shape) {
					value = ReadWord(bytes, cursor);
				}
			}
			input.Steps.push_back(std::move(step));
		}
		maximum_error = std::max(maximum_error, ReplaySectionCase(input, case_index));
	}
	Require(cursor == bytes.size(), "native section corpus trailing bytes changed");

	return maximum_error;
}

static void TestActualRestSection() {
	sgre::scene::EmoteBobbleDefinition definition;
	definition.MassWeights = {1, 1};
	sgre::scene::EmoteBobbleModel model(definition);
	sgre::scene::EmoteBobbleSectionDefinition section_definition;
	section_definition.Radii = {0.25f, 0.5f};
	sgre::scene::EmoteBobbleSections sections(model, section_definition);
	sgre::scene::EmoteBobblePosture posture;
	model.SetPosture(posture);
	model.Initialize();
	sections.Refresh();
	Require(sections.Sections()[1].Radius == std::array<float, 2>{0.5f, 0.5f} && sections.Sections()[1].Angle == 0 && sections.Sections()[1].RelativeAngle == 0 && sections.Sections()[1].LengthScale == 1, "actual owned rest chain publishes its circular section without a second numerical step");
	model.AddForce(1, {1, 0, 0});
	model.Step(0.125f);
	sections.Refresh();
	Require(sections.Sections()[1].Center[0] == model.Particles()[1].Position[0], "section center must read the actual completed numerical owner");
}

int main(int argc, char** argv) {
	try {
		Require(argc <= 2, "usage: emote_bobble_sections_tests [complete-native-vectors.bin]");
		TestActualRestSection();
		const auto embedded_error = std::max(ReplaySectionCase(EmbeddedSectionCase1(), 1), ReplaySectionCase(EmbeddedSectionCase7(), 7));
		std::cout << "Bobble owned sections: 88 embedded states; maximum absolute error " << embedded_error << '\n';
		if (argc == 2) {
			const auto maximum_error = ReplaySectionFile(argv[1]);
			std::cout << "Bobble complete native step: 64 updates / 272 sections; maximum absolute error " << maximum_error << '\n';
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
