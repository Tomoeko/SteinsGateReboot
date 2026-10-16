#include "sgre/scene/motion_shader_program.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "motion shader rejection lost its context");

		return;
	}

	throw std::runtime_error("unsupported motion shader state was accepted");
}

static void TestVertexLayouts() {
	const std::array<std::uint32_t, 12> expected{3, 0, 4, 5, 1, 2, 9, 6, 10, 11, 7, 8};
	sgre::scene::MotionShaderProgramInput input;
	for (std::size_t index = 0; index < expected.size(); ++index) {
		input.Layout = static_cast<sgre::scene::MotionVertexLayout>(index);
		Require(sgre::scene::SelectOrdinaryMotionShaderProgram(input).VertexIndex == expected[index], "native position/color/UV layout selected another vertex program");
	}
}

static void TestPixelRouting() {
	sgre::scene::MotionShaderProgramInput input;
	Require(sgre::scene::SelectOrdinaryMotionShaderProgram(input).PixelIndex == 240, "default RGBA vertex-color program changed");
	input.BlendMode = 3;
	input.HasAuxiliaryTarget = true;
	Require(sgre::scene::SelectOrdinaryMotionShaderProgram(input).PixelIndex == 272, "phase-zero single-UV auxiliary program changed");
	input.Layout = sgre::scene::MotionVertexLayout::XyColorTwoUv;
	Require(sgre::scene::SelectOrdinaryMotionShaderProgram(input).PixelIndex == 287, "phase-zero second UV did not select its auxiliary group");
	input.NativeRenderPhase = 6;
	Require(sgre::scene::SelectOrdinaryMotionShaderProgram(input).PixelIndex == 257, "nonzero ordinary phase auxiliary group changed");
	input.Texture = sgre::scene::MotionShaderTexture::RedGreen;
	input.TextureAttributes = 4;
	input.ColorFilter = 4;
	Require(sgre::scene::SelectOrdinaryMotionShaderProgram(input).PixelIndex == 749, "two-channel texture/filter specialization changed");
	input.ColorFilter = 0xffffffff;
	Require(sgre::scene::SelectOrdinaryMotionShaderProgram(input).PixelIndex == 737, "native unknown-filter offset was not preserved");
	input.BackbufferCopyPhase = true;
	Require(sgre::scene::SelectOrdinaryMotionShaderProgram(input).PixelIndex == 0, "copy phase did not force pixel program zero");
	Require(sgre::scene::ClassifyMotionShaderTexture(false, 65) == sgre::scene::MotionShaderTexture::None, "absent texture retained a format group");
	Require(sgre::scene::ClassifyMotionShaderTexture(true, 65) == sgre::scene::MotionShaderTexture::Alpha, "native alpha format interpretation changed");
	Require(sgre::scene::ClassifyMotionShaderTexture(true, 61) == sgre::scene::MotionShaderTexture::Luminance, "native luminance format interpretation changed");
	Require(sgre::scene::ClassifyMotionShaderTexture(true, 49) == sgre::scene::MotionShaderTexture::RedGreen, "native two-channel format interpretation changed");
	Require(sgre::scene::ClassifyMotionShaderTexture(true, 28) == sgre::scene::MotionShaderTexture::Rgba, "native ordinary color format interpretation changed");
}

static void TestTransformOrder() {
	sgre::scene::MotionShaderTransform transform;
	transform.Graphics.Rows = {2, 0, 0, 10, 0, 3, 0, 20, 0, 0, 4, 30};
	transform.Current.Rows = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
	const std::array<float, 16> expected{2, 4, 6, 18, 15, 18, 21, 44, 36, 40, 44, 78, 0, 0, 0, 1};
	Require(sgre::scene::PrepareMotionShaderMatrix(transform) == expected, "graphics/current affine composition or appended row changed");
	transform.UseExplicitMatrix = true;
	transform.Explicit.Rows = {12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
	const auto explicit_matrix = sgre::scene::PrepareMotionShaderMatrix(transform);
	Require(explicit_matrix[0] == 12 && explicit_matrix[3] == 9 && explicit_matrix[11] == 1 && explicit_matrix[15] == 1, "explicit matrix was composed or transposed");
	transform.UseIdentity = true;
	const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	Require(sgre::scene::PrepareMotionShaderMatrix(transform) == identity, "identity control did not take precedence over the explicit matrix");
}

static void TestConstantPreparation() {
	const auto color = sgre::scene::UnpackMotionShaderColor(0x80402010);
	Require(color == std::array<float, 4>{64.0f / 255, 32.0f / 255, 16.0f / 255, 128.0f / 255}, "packed ARGB color did not become semantic RGBA");
	Require(sgre::scene::PrepareMotionTargetReciprocals(8, 4) == std::array<float, 2>{0.125f, 0.25f}, "centered-target reciprocal extent changed");
	Require(std::bit_cast<std::uint32_t>(sgre::scene::PrepareMotionTargetReciprocals(0xffffffff, 1)[0]) == 0x2f800000, "unsigned large target dimension conversion changed");
	RequireFailure([]() {
		static_cast<void>(sgre::scene::PrepareMotionTargetReciprocals(0, 1));
	},
	    "nonzero");
}

static void TestUnsupportedFamilies() {
	sgre::scene::MotionShaderProgramInput input;
	input.Layout = static_cast<sgre::scene::MotionVertexLayout>(12);
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectOrdinaryMotionShaderProgram(input));
	},
	    "vertex layout");
	input.Layout = sgre::scene::MotionVertexLayout::XyColorUv;
	input.NativeRenderPhase = 2;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectOrdinaryMotionShaderProgram(input));
	},
	    "ordinary dispatch");
	input.NativeRenderPhase = 0;
	input.BlendMode = 0x10d;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectOrdinaryMotionShaderProgram(input));
	},
	    "alternate program family");
	input.BlendMode = 0;
	input.Texture = static_cast<sgre::scene::MotionShaderTexture>(99);
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectOrdinaryMotionShaderProgram(input));
	},
	    "texture interpretation");
}

int main() {
	try {
		TestVertexLayouts();
		TestPixelRouting();
		TestTransformOrder();
		TestConstantPreparation();
		TestUnsupportedFamilies();
		std::cout << "Native motion shader layout/routing/affine/color/target fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
