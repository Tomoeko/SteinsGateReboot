#pragma once

#include "sgre/scene/motion_shader_program.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {

	struct MotionAlternateProgramInput {
		MotionShaderProgramInput State;
		float BlendParameter = 0;
	};

	struct MotionAlternateProgram {
		MotionShaderProgram Program;
		// Alternate reflected metadata is indexed relative to pixel program 900.
		std::uint32_t PixelMetadataIndex = 0;
		bool HasPixelationConstant = false;
		bool HasBlurConstant = false;
	};

	// The copied-target owner/region/UV affine must already be prepared by the caller.
	[[nodiscard]] MotionAlternateProgram SelectAlternateMotionShaderProgram(const MotionAlternateProgramInput& input);
	// Negative amounts retain signed shader quantization; a signed-zero quantum produces original negative infinities.
	[[nodiscard]] std::array<float, 4> PrepareMotionPixelationConstants(std::uint32_t target_width, std::uint32_t target_height, std::int32_t copied_width, std::int32_t copied_height, float amount);
	[[nodiscard]] std::array<float, 4> PrepareMotionBlurConstants(std::int32_t copied_width, std::int32_t copied_height, float step);

} // namespace sgre::scene
