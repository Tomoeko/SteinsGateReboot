#include "sgre/scene/motion_shader_program.hpp"

#include <cstddef>
#include <stdexcept>

namespace sgre::scene {

	MotionShaderTexture ClassifyMotionShaderTexture(bool has_texture, std::uint32_t original_dxgi_format) {
		if (!has_texture) {
			return MotionShaderTexture::None;
		}

		switch (original_dxgi_format) {
		case 65:
			return MotionShaderTexture::Alpha;
		case 61:
			return MotionShaderTexture::Luminance;
		case 49:
			return MotionShaderTexture::RedGreen;
		default:
			return MotionShaderTexture::Rgba;
		}
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
			throw std::runtime_error("motion shader texture interpretation is invalid");
		}
	}

	MotionShaderProgram SelectOrdinaryMotionShaderProgram(const MotionShaderProgramInput& input) {
		const auto layout = static_cast<std::uint32_t>(input.Layout);
		if (layout >= 12) {
			throw std::runtime_error("motion shader vertex layout exceeds the recovered twelve formats");
		}
		if (input.NativeRenderPhase != 0 && input.NativeRenderPhase != 1 && input.NativeRenderPhase != 6) {
			throw std::runtime_error("motion shader render phase is outside ordinary dispatch");
		}
		if ((input.BlendMode & 0xf00) == 0x100) {
			throw std::runtime_error("motion shader blend requires the alternate program family");
		}

		const auto texture = TextureGroup(input.Texture);
		const auto group_layout = layout % 6;
		const bool has_color = group_layout == 1 || group_layout == 4 || group_layout == 5;
		std::uint32_t uv_count = 0;
		if (group_layout >= 2) {
			uv_count = group_layout == 2 || group_layout == 4 ? 1 : 2;
		}
		const std::uint32_t position_group = layout >= 6 ? 2 : 0;
		MotionShaderProgram program;
		program.VertexIndex = (position_group + (has_color ? 0 : 1)) * 3 + uv_count;
		if (input.BackbufferCopyPhase) {
			program.PixelIndex = 0;

			return program;
		}

		std::uint32_t color = 4;
		if ((input.TextureAttributes & 4) != 0) {
			color = 0;
		} else if ((input.BlendMode & 0x10) != 0) {
			color = 8;
		}
		std::uint32_t auxiliary = 0;
		if (input.HasAuxiliaryTarget) {
			auxiliary = 1;
			if (input.NativeRenderPhase == 0) {
				auxiliary = uv_count == 2 ? 3 : 2;
			}
		}
		const auto blend = input.BlendMode & 0xff0f;
		std::uint32_t specialization = 0;
		if (blend == 3 || blend == 4) {
			specialization = 2;
		} else if (blend == 5) {
			specialization = 1;
		}
		// The native unknown-filter branch explicitly selects the no-filter offset.
		const auto filter = input.ColorFilter <= 4 ? input.ColorFilter * 3 : 0;
		program.PixelIndex = (texture * 12 + color + auxiliary) * 15 + filter + specialization;

		return program;
	}

	std::array<float, 16> PrepareMotionShaderMatrix(const MotionShaderTransform& transform) {
		std::array<float, 16> matrix{};
		if (transform.UseIdentity) {
			matrix[0] = 1;
			matrix[5] = 1;
			matrix[10] = 1;
		} else if (transform.UseExplicitMatrix) {
			for (std::size_t index = 0; index < transform.Explicit.Rows.size(); ++index) {
				matrix[index] = transform.Explicit.Rows[index];
			}
		} else {
			const auto& graphics = transform.Graphics.Rows;
			const auto& current = transform.Current.Rows;
			for (std::size_t row = 0; row < 3; ++row) {
				for (std::size_t column = 0; column < 4; ++column) {
					const auto offset = row * 4;
					const float first = graphics[offset] * current[column];
					const float second = graphics[offset + 1] * current[column + 4];
					const float third = graphics[offset + 2] * current[column + 8];
					// Keep the original scalar multiply/add stages; contraction changes rounding.
					float value = (first + second) + third;
					if (column == 3) {
						value += graphics[offset + 3];
					}
					matrix[offset + column] = value;
				}
			}
		}
		matrix[15] = 1;

		return matrix;
	}

	std::array<float, 4> UnpackMotionShaderColor(std::uint32_t packed_argb) {
		return {static_cast<float>((packed_argb >> 16) & 255) / 255.0f, static_cast<float>((packed_argb >> 8) & 255) / 255.0f, static_cast<float>(packed_argb & 255) / 255.0f, static_cast<float>(packed_argb >> 24) / 255.0f};
	}

	std::array<float, 2> PrepareMotionTargetReciprocals(std::uint32_t width, std::uint32_t height) {
		if (width == 0 || height == 0) {
			throw std::runtime_error("motion shader target dimensions must be nonzero");
		}

		const float half_width = static_cast<float>(width) * 0.5f;
		const float half_height = static_cast<float>(height) * 0.5f;

		return {1.0f / (half_width - -half_width), 1.0f / (half_height - -half_height)};
	}

} // namespace sgre::scene
