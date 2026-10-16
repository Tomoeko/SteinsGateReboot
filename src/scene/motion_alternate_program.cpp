#include "sgre/scene/motion_alternate_program.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static std::uint32_t BlurStep(float parameter) {
		if (!std::isfinite(parameter) || parameter < 0 || parameter >= 10) {
			throw std::runtime_error("motion blur step must be finite and within the recovered [0,10) domain");
		}

		return static_cast<std::uint32_t>(parameter);
	}

	static std::uint32_t TextureGroup(MotionShaderTexture texture) {
		switch (texture) {
		case MotionShaderTexture::None:
			return 0;
		case MotionShaderTexture::Rgba:
			return 1;
		case MotionShaderTexture::Alpha:
			return 2;
		case MotionShaderTexture::Luminance:
			return 3;
		case MotionShaderTexture::RedGreen:
			return 4;
		default:
			throw std::runtime_error("alternate motion shader texture interpretation is invalid");
		}
	}

	MotionAlternateProgram SelectAlternateMotionShaderProgram(const MotionAlternateProgramInput& input) {
		const auto blend = input.State.BlendMode & 0xff0f;
		if ((blend & 0xf00) != 0x100) {
			throw std::runtime_error("alternate motion shader requires native blend-family flags 0x100");
		}

		auto ordinary_input = input.State;
		ordinary_input.BlendMode &= ~0xf00u;
		const auto ordinary = SelectOrdinaryMotionShaderProgram(ordinary_input);
		MotionAlternateProgram result;
		result.Program.VertexIndex = ordinary.VertexIndex;
		if (input.State.BackbufferCopyPhase) {
			result.Program.PixelIndex = 900;

			return result;
		}

		std::uint32_t operation = 0;
		if (blend >= 0x100 && blend <= 0x10c) {
			operation = blend - 0x100 + 1;
		} else if (blend == 0x10d) {
			operation = 14 + BlurStep(input.BlendParameter) % 5;
		}
		const auto layout = static_cast<std::uint32_t>(input.State.Layout);
		const bool two_uv = layout == 3 || layout == 5 || layout == 9 || layout == 11;
		std::uint32_t auxiliary = 0;
		if (input.State.HasAuxiliaryTarget) {
			auxiliary = 1;
			if (input.State.NativeRenderPhase == 0) {
				auxiliary = two_uv ? 3 : 2;
			}
		}
		std::uint32_t color = 4;
		if ((input.State.TextureAttributes & 4) != 0) {
			color = 0;
		} else if ((input.State.BlendMode & 0x10) != 0) {
			color = 8;
		}
		result.PixelMetadataIndex = (TextureGroup(input.State.Texture) * 12 + color + auxiliary) * 19 + operation;
		result.Program.PixelIndex = 900 + result.PixelMetadataIndex;
		result.HasPixelationConstant = blend == 0x10c;
		result.HasBlurConstant = blend == 0x10d;

		return result;
	}

	static void RequireCopiedDimensions(std::int32_t width, std::int32_t height) {
		if (width <= 0 || height <= 0) {
			throw std::runtime_error("motion copied-target dimensions must be positive signed integers");
		}
	}

	std::array<float, 4> PrepareMotionPixelationConstants(std::uint32_t target_width, std::uint32_t target_height, std::int32_t copied_width, std::int32_t copied_height, float amount) {
		RequireCopiedDimensions(copied_width, copied_height);
		if (target_width == 0 || target_height == 0 || !std::isfinite(amount) || amount == 0) {
			throw std::runtime_error("motion pixelation requires nonzero target dimensions and a nonzero finite amount");
		}

		const float half_width = static_cast<float>(target_width) * 0.5f;
		const float half_height = static_cast<float>(target_height) * 0.5f;
		const auto extent = std::max(half_width - -half_width, half_height - -half_height);
		const float size = std::ceil(extent / amount);
		if (!std::isfinite(size)) {
			throw std::runtime_error("motion pixelation quantization extent is not finite");
		}

		const float width = static_cast<float>(copied_width);
		const float height = static_cast<float>(copied_height);
		const float offset = size * 0.5f + 0.5f;

		return {width / size, height / size, offset / width, offset / height};
	}

	std::array<float, 4> PrepareMotionBlurConstants(std::int32_t copied_width, std::int32_t copied_height, float step) {
		RequireCopiedDimensions(copied_width, copied_height);
		if (BlurStep(step) >= 5) {
			return {0, 1.0f / static_cast<float>(copied_height), 0, 0};
		}

		return {1.0f / static_cast<float>(copied_width), 0, 0, 0};
	}

} // namespace sgre::scene
