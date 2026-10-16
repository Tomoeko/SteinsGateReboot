#pragma once

#include "sgre/scene/motion_draw.hpp"

#include <cstdint>
#include <limits>

namespace sgre::scene {

	enum class SpriteVertexColor {
		Ignore,
		Multiply,
		MultiplyAndDoubleRgb
	};

	enum class SpritePixelOperation {
		None,
		InvertRgb,
		PremultiplyRgb
	};

	enum class SpriteColorFilter : std::uint32_t {
		None = 0,
		Blend = 1,
		Add = 2,
		Subtract = 3,
		Negate = 4
	};

	enum class TextureSampling {
		Nearest,
		Linear
	};

	enum class TextureAddress {
		Clamp,
		Repeat,
		Mirror
	};

	enum class SpriteBlendFactor {
		Zero,
		One,
		SourceAlpha,
		InverseSourceAlpha,
		DestinationColor,
		InverseDestinationColor
	};

	enum class SpriteBlendOperation {
		Add,
		ReverseSubtract
	};

	struct SpriteBlendState {
		bool Enabled = true;
		SpriteBlendOperation RgbOperation = SpriteBlendOperation::Add;
		SpriteBlendFactor SourceRgb = SpriteBlendFactor::SourceAlpha;
		SpriteBlendFactor DestinationRgb = SpriteBlendFactor::InverseSourceAlpha;
		SpriteBlendOperation AlphaOperation = SpriteBlendOperation::Add;
		SpriteBlendFactor SourceAlpha = SpriteBlendFactor::One;
		SpriteBlendFactor DestinationAlpha = SpriteBlendFactor::InverseSourceAlpha;
		std::uint8_t WriteMask = 15;
	};

	struct SpriteSamplerState {
		TextureSampling Minification = TextureSampling::Nearest;
		TextureSampling Magnification = TextureSampling::Nearest;
		TextureSampling MipFilter = TextureSampling::Nearest;
		TextureAddress AddressU = TextureAddress::Clamp;
		TextureAddress AddressV = TextureAddress::Clamp;
		TextureAddress AddressW = TextureAddress::Repeat;
		float MipLodBias = 0;
		float MinLod = -std::numeric_limits<float>::max();
		float MaxLod = std::numeric_limits<float>::max();
		// Native MaxAnisotropy is 2, but both recovered filters leave anisotropic filtering disabled.
		std::uint32_t MaxAnisotropy = 2;
		std::uint32_t NativeFilterCode = 0;
	};

	struct SpritePipelineOptions {
		TextureSampling Sampling = TextureSampling::Nearest;
		TextureAddress AddressU = TextureAddress::Clamp;
		TextureAddress AddressV = TextureAddress::Clamp;
		SpriteColorFilter ColorFilter = SpriteColorFilter::None;
		bool EnableBlending = true;
		bool WriteColor = true;
		bool HasAuxiliaryTarget = false;
		bool BackbufferCopyPhase = false;
		std::uint32_t NativeRenderMode = 0;
	};

	struct SpritePipeline {
		resource::PixelFormat TextureFormat = resource::PixelFormat::Bc7;
		SpriteVertexColor VertexColor = SpriteVertexColor::MultiplyAndDoubleRgb;
		SpritePixelOperation PixelOperation = SpritePixelOperation::None;
		SpriteColorFilter ColorFilter = SpriteColorFilter::None;
		SpriteBlendState Blend;
		SpriteSamplerState Sampler;
		std::uint32_t NativeVertexShaderIndex = 1;
		std::uint32_t NativePixelShaderIndex = 300;
		std::uint32_t NativeBlendStateIndex = 0;
	};

	// Classifies ordinary XY/color/UV sprites and the measured RGBA copied additive/overlay/darken color branches.
	// Texture-mask phases derive their real target/program state in the platform encoder.
	[[nodiscard]] SpritePipeline SelectSpritePipeline(const MotionSpriteCommand& command, const SpritePipelineOptions& options);
	[[nodiscard]] SpritePipeline SelectSpritePipeline(const MotionSpriteMaterial& material, const SpritePipelineOptions& options);

} // namespace sgre::scene
