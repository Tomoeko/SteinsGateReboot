#pragma once

#include "sgre/scene/motion_shader_program.hpp"
#include "sgre/scene/motion_stencil_state.hpp"

namespace sgre::scene {
	struct MotionMaskShaderInput {
		MotionVertexLayout Layout = MotionVertexLayout::XyColorUv;
		MotionShaderTexture Texture = MotionShaderTexture::Rgba;
		MotionMaskPhase Phase = MotionMaskPhase::StencilWrite;
		// Original graphics byte +0x160 selects the extra two-float payload variant for texture masks.
		bool UseAdditionalConstants = false;
	};
	struct MotionMaskShaderProgram {
		MotionShaderProgram Program;
		// Stencil scalar metadata uses texture kind +12; texture-mask metadata uses pixel index -2042.
		std::uint32_t PixelMetadataIndex = 0;
		bool HasStencilScalar = false;
		bool HasTargetReciprocals = false;
		bool HasAdditionalConstants = false;
	};
	[[nodiscard]] MotionMaskShaderProgram SelectMotionMaskShaderProgram(const MotionMaskShaderInput& input);
	// Both native mask selectors publish alpha first, followed by red/green/blue.
	[[nodiscard]] std::array<float, 4> PrepareMotionMaskShaderColor(std::uint32_t packed_argb);
} // namespace sgre::scene
