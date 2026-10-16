#include "sgre/scene/motion_alternate_program.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "alternate shader rejection lost its context");

		return;
	}

	throw std::runtime_error("unsupported alternate shader state was accepted");
}

static void TestPrograms() {
	sgre::scene::MotionAlternateProgramInput input;
	for (std::uint32_t operation = 0; operation < 13; ++operation) {
		input.State.BlendMode = 0x100 + operation;
		const auto result = sgre::scene::SelectAlternateMotionShaderProgram(input);
		Require(result.Program.VertexIndex == 1 && result.Program.PixelIndex == 1205 + operation && result.PixelMetadataIndex == 305 + operation, "alternate operation selected another program or metadata index");
		Require(result.HasPixelationConstant == (operation == 12) && !result.HasBlurConstant, "alternate operation changed its reflected constant request");
	}
	input.State.BlendMode = 0x10d;
	input.BlendParameter = 4.875f;
	const auto horizontal = sgre::scene::SelectAlternateMotionShaderProgram(input);
	Require(horizontal.Program.PixelIndex == 1222 && horizontal.HasBlurConstant, "blur fractional step did not truncate before program selection");
	input.BlendParameter = 9.875f;
	Require(sgre::scene::SelectAlternateMotionShaderProgram(input).Program.PixelIndex == horizontal.Program.PixelIndex, "paired blur axes did not share the same program");
	input.State.Layout = sgre::scene::MotionVertexLayout::XyzColorTwoUv;
	input.State.HasAuxiliaryTarget = true;
	input.State.Texture = sgre::scene::MotionShaderTexture::Alpha;
	input.State.TextureAttributes = 4;
	Require(sgre::scene::SelectAlternateMotionShaderProgram(input).Program.PixelIndex == 1431, "alternate texture/auxiliary layout group changed");
	input.State.BlendMode = 0x10e;
	const auto fallback = sgre::scene::SelectAlternateMotionShaderProgram(input);
	Require(fallback.Program.PixelIndex == 1413 && !fallback.HasPixelationConstant && !fallback.HasBlurConstant, "native unknown-operation default program changed");
	input.State.BackbufferCopyPhase = true;
	Require(sgre::scene::SelectAlternateMotionShaderProgram(input).Program.PixelIndex == 900 && sgre::scene::SelectAlternateMotionShaderProgram(input).PixelMetadataIndex == 0, "alternate copy phase did not reset relative metadata selection");
}

static void TestConstants() {
	const auto pixelation = sgre::scene::PrepareMotionPixelationConstants(8, 4, 4, 2, 3);
	Require(pixelation == std::array<float, 4>{4.0f / 3, 2.0f / 3, 0.5f, 1}, "pixelation extent quantization or half-cell offset changed");
	const auto negative = sgre::scene::PrepareMotionPixelationConstants(100, 80, 100, 80, -8);
	Require(negative == std::array<float, 4>{100.0f / -12, 80.0f / -12, -5.5f / 100, -5.5f / 80}, "negative shader amount was substituted with the region's absolute cell size");
	const auto negative_zero = sgre::scene::PrepareMotionPixelationConstants(1, 1, 8, 4, -2);
	Require(std::isinf(negative_zero[0]) && std::signbit(negative_zero[0]) && std::isinf(negative_zero[1]) && std::signbit(negative_zero[1]) && negative_zero[2] == 0.0625f && negative_zero[3] == 0.125f, "signed-zero quantization did not preserve the original negative infinities and half-texel origins");
	Require(sgre::scene::PrepareMotionBlurConstants(8, 4, 4.875f) == std::array<float, 4>{0.125f, 0, 0, 0}, "horizontal blur reciprocal step changed");
	Require(sgre::scene::PrepareMotionBlurConstants(8, 4, 5) == std::array<float, 4>{0, 0.25f, 0, 0}, "vertical blur reciprocal step changed");
	RequireFailure([]() {
		static_cast<void>(sgre::scene::PrepareMotionPixelationConstants(8, 4, 4, 2, 0));
	},
	    "nonzero finite amount");
	RequireFailure([]() {
		static_cast<void>(sgre::scene::PrepareMotionPixelationConstants(8, 4, 4, 2, std::numeric_limits<float>::denorm_min()));
	},
	    "quantization extent");
	RequireFailure([]() {
		static_cast<void>(sgre::scene::PrepareMotionBlurConstants(0, 4, 1));
	},
	    "positive signed integers");
	for (const auto step : std::array<float, 4>{-1, 10, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
		RequireFailure([&]() {
			static_cast<void>(sgre::scene::PrepareMotionBlurConstants(8, 4, step));
		},
		    "finite");
	}
}

static void TestUnsupportedFamily() {
	sgre::scene::MotionAlternateProgramInput input;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectAlternateMotionShaderProgram(input));
	},
	    "blend-family flags");
	input.State.BlendMode = 0x10d;
	input.BlendParameter = -1;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectAlternateMotionShaderProgram(input));
	},
	    "finite");
	input.BlendParameter = 0;
	input.State.NativeRenderPhase = 2;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectAlternateMotionShaderProgram(input));
	},
	    "ordinary dispatch");
}

int main() {
	try {
		TestPrograms();
		TestConstants();
		TestUnsupportedFamily();
		std::cout << "Native alternate motion programs/relative metadata/pixelation/blur fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
