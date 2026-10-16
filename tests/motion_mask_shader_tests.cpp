#include "sgre/scene/motion_mask_shader.hpp"

#include <array>
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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "mask shader failure lost its context");
		return;
	}
	throw std::runtime_error("invalid mask shader state was accepted");
}
static void TestStencilPrograms() {
	sgre::scene::MotionMaskShaderInput input;
	input.Layout = sgre::scene::MotionVertexLayout::XyUv;
	for (const auto texture : std::array<sgre::scene::MotionShaderTexture, 5>{sgre::scene::MotionShaderTexture::None, sgre::scene::MotionShaderTexture::Rgba, sgre::scene::MotionShaderTexture::Alpha, sgre::scene::MotionShaderTexture::Luminance, sgre::scene::MotionShaderTexture::RedGreen}) {
		input.Texture = texture;
		const auto kind = static_cast<std::uint32_t>(texture);
		for (const auto variant : {false, true}) {
			input.UseAdditionalConstants = variant;
			const auto result = sgre::scene::SelectMotionMaskShaderProgram(input);
			Require(result.Program.VertexIndex == 4 && result.Program.PixelIndex == 2072 + kind && result.PixelMetadataIndex == 12 + kind, "stencil texture/program/scalar metadata selection changed");
			Require(result.HasStencilScalar && !result.HasTargetReciprocals && !result.HasAdditionalConstants, "stencil mode incorrectly consumed texture-mask additional parameters");
		}
	}
}
static void TestTextureMaskPrograms() {
	sgre::scene::MotionMaskShaderInput input;
	input.Layout = sgre::scene::MotionVertexLayout::XyzColorUv;
	for (std::uint32_t kind = 0; kind < 5; ++kind) {
		input.Texture = static_cast<sgre::scene::MotionShaderTexture>(kind);
		for (std::uint32_t phase = 3; phase <= 5; ++phase) {
			input.Phase = static_cast<sgre::scene::MotionMaskPhase>(phase);
			for (const auto variant : {false, true}) {
				input.UseAdditionalConstants = variant;
				const auto result = sgre::scene::SelectMotionMaskShaderProgram(input);
				const auto relative = (kind * 2 + static_cast<std::uint32_t>(variant)) * 3 + phase - 3;
				Require(result.Program.VertexIndex == 7 && result.Program.PixelIndex == 2042 + relative && result.PixelMetadataIndex == relative, "texture-mask phase/variant program index changed");
				Require(!result.HasStencilScalar && result.HasTargetReciprocals && result.HasAdditionalConstants == variant, "texture-mask constant requests changed");
			}
		}
	}
}
static void TestColorAndInvalidDomains() {
	Require(sgre::scene::PrepareMotionMaskShaderColor(0x80402010) == std::array<float, 4>{128.0f / 255, 64.0f / 255, 32.0f / 255, 16.0f / 255}, "mask vertex color did not preserve alpha/red/green/blue order");
	Require(sgre::scene::PrepareMotionMaskShaderColor(0xffffffff) == std::array<float, 4>{1, 1, 1, 1}, "white mask constant changed");
	sgre::scene::MotionMaskShaderInput input;
	input.Phase = sgre::scene::MotionMaskPhase::Idle;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectMotionMaskShaderProgram(input));
	},
	    "phases");
	input.Phase = sgre::scene::MotionMaskPhase::Resolved;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectMotionMaskShaderProgram(input));
	},
	    "phases");
	input.Phase = sgre::scene::MotionMaskPhase::StencilWrite;
	input.Texture = static_cast<sgre::scene::MotionShaderTexture>(5);
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectMotionMaskShaderProgram(input));
	},
	    "texture interpretation");
	input.Texture = sgre::scene::MotionShaderTexture::Rgba;
	input.Layout = static_cast<sgre::scene::MotionVertexLayout>(12);
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectMotionMaskShaderProgram(input));
	},
	    "layout");
}
int main() {
	try {
		TestStencilPrograms();
		TestTextureMaskPrograms();
		TestColorAndInvalidDomains();
		std::cout << "Stencil/texture mask programs, relative metadata and alpha-first color fixtures passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
