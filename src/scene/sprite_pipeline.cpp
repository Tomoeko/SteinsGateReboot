#include "sgre/scene/sprite_pipeline.hpp"

#include "sgre/scene/motion_alternate_program.hpp"

#include <array>
#include <stdexcept>

namespace sgre::scene {

	static SpriteBlendFactor ReadBlendFactor(std::uint32_t value) {
		switch (value) {
		case 1:
			return SpriteBlendFactor::Zero;
		case 2:
			return SpriteBlendFactor::One;
		case 5:
			return SpriteBlendFactor::SourceAlpha;
		case 6:
			return SpriteBlendFactor::InverseSourceAlpha;
		case 9:
			return SpriteBlendFactor::DestinationColor;
		case 10:
			return SpriteBlendFactor::InverseDestinationColor;
		default:
			throw std::runtime_error("native sprite blend table contains an unsupported factor");
		}
	}

	static SpriteBlendState ReadBlendState(std::uint32_t index, bool write_color) {
		// Original six-word rows at 0x140653350: RGB operation/source/destination, then alpha equivalents.
		static constexpr std::array<std::array<std::uint32_t, 6>, 12> kBlendRows = {{
		    {1, 5, 6, 1, 2, 6},  // Native mode 0
		    {1, 5, 2, 1, 1, 2},  // Native mode 1
		    {3, 5, 2, 1, 1, 2},  // Native mode 2
		    {1, 9, 6, 1, 1, 2},  // Native mode 3
		    {1, 10, 2, 1, 1, 2}, // Native mode 4
		    {3, 5, 2, 1, 1, 2},  // Native mode 5
		    {1, 5, 6, 1, 2, 6},  // Native mode 6
		    {1, 2, 1, 1, 1, 2},  // Native mode 7
		    {1, 5, 6, 1, 2, 6},  // Native mode 8
		    {1, 1, 6, 1, 1, 6},  // Native mode 9
		    {1, 2, 1, 1, 1, 2},  // Native mode 10
		    {1, 2, 1, 1, 1, 2},  // Native mode 11
		}};
		if (index >= kBlendRows.size()) {
			throw std::runtime_error("sprite blend mode exceeds the native blend-state table");
		}

		const auto& row = kBlendRows[index];
		SpriteBlendState state;
		state.Enabled = index != 7;
		state.RgbOperation = row[0] == 3 ? SpriteBlendOperation::ReverseSubtract : SpriteBlendOperation::Add;
		state.SourceRgb = ReadBlendFactor(row[1]);
		state.DestinationRgb = ReadBlendFactor(row[2]);
		state.AlphaOperation = SpriteBlendOperation::Add;
		state.SourceAlpha = ReadBlendFactor(row[4]);
		state.DestinationAlpha = ReadBlendFactor(row[5]);
		state.WriteMask = write_color ? 15 : 0;

		return state;
	}

	static std::uint32_t ReadTextureKind(resource::PixelFormat format) {
		switch (format) {
		case resource::PixelFormat::Bc7:
			return 1;
		case resource::PixelFormat::Alpha8:
			return 2;
		case resource::PixelFormat::Luminance8:
			return 3;
		default:
			throw std::runtime_error("sprite texture format has no recovered shader interpretation");
		}
	}

	static void ValidateAddress(TextureAddress address) {
		if (address != TextureAddress::Clamp && address != TextureAddress::Repeat && address != TextureAddress::Mirror) {
			throw std::runtime_error("sprite sampler address mode is unsupported");
		}
	}

	static SpriteSamplerState ReadSamplerState(const SpritePipelineOptions& options) {
		if (options.Sampling != TextureSampling::Nearest && options.Sampling != TextureSampling::Linear) {
			throw std::runtime_error("sprite sampler filtering is unsupported");
		}

		ValidateAddress(options.AddressU);
		ValidateAddress(options.AddressV);
		SpriteSamplerState state;
		state.Minification = options.Sampling;
		state.Magnification = options.Sampling;
		state.MipFilter = options.Sampling;
		state.AddressU = options.AddressU;
		state.AddressV = options.AddressV;
		state.NativeFilterCode = options.Sampling == TextureSampling::Linear ? 0x15 : 0;

		return state;
	}

	static SpritePipeline SelectCopiedMaterialPipeline(resource::PixelFormat format, std::uint32_t attributes, std::uint32_t blend_mode, const SpritePipelineOptions& options) {
		if (format != resource::PixelFormat::Bc7 || (blend_mode != 0x110 && blend_mode != 0x112 && blend_mode != 0x118) || options.HasAuxiliaryTarget || options.BackbufferCopyPhase || options.ColorFilter != SpriteColorFilter::None || (options.NativeRenderMode != 0 && options.NativeRenderMode != 1 && options.NativeRenderMode != 6)) {
			throw std::runtime_error("sprite copied pipeline requires measured RGBA copied programs in an ordinary render phase");
		}

		MotionAlternateProgramInput input;
		input.State.Texture = MotionShaderTexture::Rgba;
		input.State.TextureAttributes = attributes;
		input.State.BlendMode = blend_mode;
		input.State.NativeRenderPhase = options.NativeRenderMode;
		const auto selected = SelectAlternateMotionShaderProgram(input);
		SpritePipeline pipeline;
		pipeline.TextureFormat = format;
		pipeline.VertexColor = (attributes & 4) != 0 ? SpriteVertexColor::Ignore : SpriteVertexColor::MultiplyAndDoubleRgb;
		pipeline.NativeVertexShaderIndex = selected.Program.VertexIndex;
		pipeline.NativePixelShaderIndex = selected.Program.PixelIndex;
		// Native5316c9 selects row10 for family100 while record blending is enabled.
		pipeline.NativeBlendStateIndex = options.EnableBlending ? 10 : 7;
		pipeline.Blend = ReadBlendState(pipeline.NativeBlendStateIndex, options.WriteColor);
		pipeline.Sampler = ReadSamplerState(options);

		return pipeline;
	}

	static SpritePipeline SelectMaterialPipeline(resource::PixelFormat format, std::uint32_t attributes, std::uint32_t blend_mode, const SpritePipelineOptions& options) {
		if ((blend_mode & 0xf00) == 0x100) {
			return SelectCopiedMaterialPipeline(format, attributes, blend_mode, options);
		}

		if (options.HasAuxiliaryTarget || options.BackbufferCopyPhase || (options.NativeRenderMode != 0 && options.NativeRenderMode != 1 && options.NativeRenderMode != 6)) {
			throw std::runtime_error("sprite pipeline requires an ordinary render phase without auxiliary targets");
		}

		const auto filter = static_cast<std::uint32_t>(options.ColorFilter);
		if (filter > 4) {
			throw std::runtime_error("sprite color filter is unsupported");
		}

		const auto blend_operation = blend_mode & 0xff0f;
		if (blend_operation >= 12) {
			throw std::runtime_error("sprite blend mode exceeds the native blend-state table");
		}

		const auto texture_kind = ReadTextureKind(format);
		const auto color_mode = (attributes & 4) != 0 ? 0u : (blend_mode & 0x10) != 0 ? 8u
		                                                                              : 4u;
		const auto specialization = blend_operation == 3 || blend_operation == 4 ? 2u : blend_operation == 5 ? 1u
		                                                                                                     : 0u;
		SpritePipeline pipeline;
		pipeline.TextureFormat = format;
		pipeline.VertexColor = color_mode == 0 ? SpriteVertexColor::Ignore : color_mode == 8 ? SpriteVertexColor::MultiplyAndDoubleRgb
		                                                                                     : SpriteVertexColor::Multiply;
		pipeline.PixelOperation = specialization == 2 ? SpritePixelOperation::PremultiplyRgb : specialization == 1 ? SpritePixelOperation::InvertRgb
		                                                                                                           : SpritePixelOperation::None;
		pipeline.ColorFilter = options.ColorFilter;
		pipeline.NativePixelShaderIndex = (12 * texture_kind + color_mode) * 15 + 3 * filter + specialization;
		pipeline.NativeBlendStateIndex = options.EnableBlending ? blend_operation : 7;
		pipeline.Blend = ReadBlendState(pipeline.NativeBlendStateIndex, options.WriteColor);
		pipeline.Sampler = ReadSamplerState(options);

		return pipeline;
	}

	SpritePipeline SelectSpritePipeline(const MotionSpriteCommand& command, const SpritePipelineOptions& options) {
		return SelectMaterialPipeline(command.TextureFormat, command.TextureAttributes, command.BlendMode, options);
	}

	SpritePipeline SelectSpritePipeline(const MotionSpriteMaterial& material, const SpritePipelineOptions& options) {
		return SelectMaterialPipeline(material.TextureFormat, material.TextureAttributes, material.BlendMode, options);
	}

} // namespace sgre::scene
