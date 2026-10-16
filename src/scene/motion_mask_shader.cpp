#include "sgre/scene/motion_mask_shader.hpp"

#include <stdexcept>

namespace sgre::scene {
	static std::uint32_t MaskTextureKind(MotionShaderTexture texture) {
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
			throw std::runtime_error("motion mask shader texture interpretation is invalid");
		}
	}
	MotionMaskShaderProgram SelectMotionMaskShaderProgram(const MotionMaskShaderInput& input) {
		MotionMaskShaderProgram result;
		MotionShaderProgramInput ordinary;
		ordinary.Layout = input.Layout;
		result.Program.VertexIndex = SelectOrdinaryMotionShaderProgram(ordinary).VertexIndex;
		const auto texture = MaskTextureKind(input.Texture);
		if (input.Phase == MotionMaskPhase::StencilWrite) {
			result.Program.PixelIndex = 2072 + texture;
			result.PixelMetadataIndex = 12 + texture;
			result.HasStencilScalar = true;
			return result;
		}
		const auto phase = static_cast<std::uint32_t>(input.Phase);
		if (phase < 3 || phase > 5) {
			throw std::runtime_error("motion mask shader requires stencil-write or texture-mask phases");
		}
		const auto variant = texture * 2 + static_cast<std::uint32_t>(input.UseAdditionalConstants);
		result.PixelMetadataIndex = variant * 3 + (phase - 3);
		result.Program.PixelIndex = 2042 + result.PixelMetadataIndex;
		result.HasTargetReciprocals = true;
		result.HasAdditionalConstants = input.UseAdditionalConstants;
		return result;
	}
	std::array<float, 4> PrepareMotionMaskShaderColor(std::uint32_t packed_argb) {
		const auto rgba = UnpackMotionShaderColor(packed_argb);
		return {rgba[3], rgba[0], rgba[1], rgba[2]};
	}
} // namespace sgre::scene
