#include "sgre/scene/sprite_pipeline.hpp"

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
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, "sprite pipeline rejection omitted required context");

		return;
	}

	throw std::runtime_error("unsupported sprite pipeline was accepted");
}

static void TestShaderSlots() {
	const std::array<sgre::resource::PixelFormat, 3> formats = {sgre::resource::PixelFormat::Bc7, sgre::resource::PixelFormat::Alpha8, sgre::resource::PixelFormat::Luminance8};
	// Original shader dispatch at 0x14054d554 maps these texture/color cases to these slots.
	const std::array<std::array<std::uint32_t, 3>, 3> slots = {{{180, 240, 300}, {360, 420, 480}, {540, 600, 660}}};
	for (std::size_t format = 0; format < formats.size(); ++format) {
		for (std::size_t color = 0; color < slots[format].size(); ++color) {
			sgre::scene::MotionSpriteCommand command;
			command.TextureFormat = formats[format];
			command.TextureAttributes = color == 0 ? 4 : 0;
			command.BlendMode = color == 2 ? 0x10 : 0;
			const auto pipeline = sgre::scene::SelectSpritePipeline(command, {});
			Require(pipeline.NativeVertexShaderIndex == 1 && pipeline.NativePixelShaderIndex == slots[format][color], "ordinary sprite shader slot changed");
			Require(pipeline.TextureFormat == formats[format], "shader texture interpretation changed");
		}
	}

	sgre::scene::MotionSpriteCommand command;
	command.BlendMode = 0x15;
	const auto inverted = sgre::scene::SelectSpritePipeline(command, {});
	Require(inverted.NativePixelShaderIndex == 301 && inverted.PixelOperation == sgre::scene::SpritePixelOperation::InvertRgb, "RGB inversion specialization changed");
	command.BlendMode = 0x13;
	const auto premultiplied = sgre::scene::SelectSpritePipeline(command, {});
	Require(premultiplied.NativePixelShaderIndex == 302 && premultiplied.PixelOperation == sgre::scene::SpritePixelOperation::PremultiplyRgb, "RGB premultiplication specialization changed");
	sgre::scene::SpritePipelineOptions options;
	options.ColorFilter = sgre::scene::SpriteColorFilter::Negate;
	const auto filtered = sgre::scene::SelectSpritePipeline(command, options);
	Require(filtered.NativePixelShaderIndex == 314, "color filter/specialization shader ordering changed");
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = command.TextureFormat;
	material.TextureAttributes = command.TextureAttributes;
	material.BlendMode = command.BlendMode;
	Require(sgre::scene::SelectSpritePipeline(material, options).NativePixelShaderIndex == filtered.NativePixelShaderIndex, "grid material selected a different ordinary shader");
}

static void TestBlendCoupling() {
	sgre::scene::MotionSpriteCommand command;
	const auto ordinary = sgre::scene::SelectSpritePipeline(command, {});
	Require(ordinary.Blend.Enabled && ordinary.Blend.SourceRgb == sgre::scene::SpriteBlendFactor::SourceAlpha && ordinary.Blend.DestinationRgb == sgre::scene::SpriteBlendFactor::InverseSourceAlpha, "ordinary RGB blending changed");
	Require(ordinary.Blend.SourceAlpha == sgre::scene::SpriteBlendFactor::One && ordinary.Blend.DestinationAlpha == sgre::scene::SpriteBlendFactor::InverseSourceAlpha, "ordinary alpha blending changed");
	command.BlendMode = 0x12;
	const auto reverse_subtract = sgre::scene::SelectSpritePipeline(command, {});
	Require(reverse_subtract.Blend.RgbOperation == sgre::scene::SpriteBlendOperation::ReverseSubtract && reverse_subtract.Blend.SourceRgb == sgre::scene::SpriteBlendFactor::SourceAlpha && reverse_subtract.Blend.DestinationRgb == sgre::scene::SpriteBlendFactor::One, "reverse subtraction row changed");
	Require(reverse_subtract.Blend.SourceAlpha == sgre::scene::SpriteBlendFactor::Zero && reverse_subtract.Blend.DestinationAlpha == sgre::scene::SpriteBlendFactor::One, "reverse subtraction alpha preservation changed");
	command.BlendMode = 0x13;
	const auto destination = sgre::scene::SelectSpritePipeline(command, {});
	Require(destination.Blend.SourceRgb == sgre::scene::SpriteBlendFactor::DestinationColor && destination.PixelOperation == sgre::scene::SpritePixelOperation::PremultiplyRgb, "destination-color blend/shader coupling changed");
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	options.WriteColor = false;
	const auto disabled = sgre::scene::SelectSpritePipeline(command, options);
	Require(disabled.NativeBlendStateIndex == 7 && !disabled.Blend.Enabled && disabled.Blend.WriteMask == 0, "native disabled-blend/write-mask selection changed");
	Require(disabled.NativePixelShaderIndex == destination.NativePixelShaderIndex, "disabling blending unexpectedly changed shader selection");
}

static void TestCopiedBlendCoupling() {
	for (const auto mode : std::array<std::uint32_t, 2>{0x110, 0x112}) {
		for (const bool blending : std::array<bool, 2>{false, true}) {
			for (const bool write_color : std::array<bool, 2>{false, true}) {
				sgre::scene::MotionSpriteMaterial material;
				material.TextureFormat = sgre::resource::PixelFormat::Bc7;
				material.TextureAttributes = 5;
				material.BlendMode = mode;
				sgre::scene::SpritePipelineOptions options;
				options.EnableBlending = blending;
				options.WriteColor = write_color;
				const auto pipeline = sgre::scene::SelectSpritePipeline(material, options);
				Require(pipeline.NativePixelShaderIndex == (mode == 0x110 ? 1129u : 1131u) && pipeline.NativeVertexShaderIndex == 1 && pipeline.VertexColor == sgre::scene::SpriteVertexColor::Ignore && pipeline.PixelOperation == sgre::scene::SpritePixelOperation::None, "original copied source-only program selection differs");
				Require(pipeline.NativeBlendStateIndex == (blending ? 10u : 7u) && pipeline.Blend.Enabled == blending && pipeline.Blend.WriteMask == (write_color ? 15 : 0), "native copied blend enable/write controls differ");
				if (blending) {
					Require(pipeline.Blend.SourceRgb == sgre::scene::SpriteBlendFactor::One && pipeline.Blend.DestinationRgb == sgre::scene::SpriteBlendFactor::Zero && pipeline.Blend.SourceAlpha == sgre::scene::SpriteBlendFactor::Zero && pipeline.Blend.DestinationAlpha == sgre::scene::SpriteBlendFactor::One, "native copied blend row10 must replace RGB and preserve destination alpha");
				}
			}
		}
	}
}

static void TestCopiedDoubleColorCoupling() {
	for (const auto mode : {0x110u, 0x112u}) {
		for (std::uint32_t attributes = 0; attributes != 8; ++attributes) {
			for (const bool blending : {false, true}) {
				for (const bool write : {false, true}) {
					for (const auto phase : {0u, 1u, 6u}) {
						sgre::scene::MotionSpriteMaterial material;
						material.TextureFormat = sgre::resource::PixelFormat::Bc7;
						material.TextureAttributes = attributes;
						material.BlendMode = mode;
						sgre::scene::SpritePipelineOptions options;
						options.EnableBlending = blending;
						options.WriteColor = write;
						options.NativeRenderMode = phase;
						const auto pipeline = sgre::scene::SelectSpritePipeline(material, options);
						const bool ignore = (attributes & 4) != 0;
						const auto expected = mode == 0x110 ? (ignore ? 1129u : 1281u) : (ignore ? 1131u : 1283u);
						Require(pipeline.NativePixelShaderIndex == expected && pipeline.VertexColor == (ignore ? sgre::scene::SpriteVertexColor::Ignore : sgre::scene::SpriteVertexColor::MultiplyAndDoubleRgb), "native copied source attributes must choose actual ignored or doubled vertex color");
						Require(pipeline.NativeBlendStateIndex == (blending ? 10u : 7u) && pipeline.Blend.Enabled == blending && pipeline.Blend.WriteMask == (write ? 15 : 0), "copied double-color keeps native blend enable and write state");
						Require(pipeline.Blend.SourceAlpha == sgre::scene::SpriteBlendFactor::Zero && pipeline.Blend.DestinationAlpha == sgre::scene::SpriteBlendFactor::One, "enabled copied blend preserves destination alpha independently of source modulation");
					}
				}
			}
		}
	}
}

static void TestCopiedDarken() {
	for (std::uint32_t attributes = 0; attributes < 8; ++attributes) {
		for (const bool blending : {false, true}) {
			for (const bool write : {false, true}) {
				for (const std::uint32_t phase : {0u, 1u, 6u}) {
					sgre::scene::MotionSpriteMaterial material;
					material.TextureFormat = sgre::resource::PixelFormat::Bc7;
					material.TextureAttributes = attributes;
					material.BlendMode = 0x118;
					sgre::scene::SpritePipelineOptions options;
					options.EnableBlending = blending;
					options.WriteColor = write;
					options.NativeRenderMode = phase;
					const auto pipeline = sgre::scene::SelectSpritePipeline(material, options);
					const bool ignore = (attributes & 4) != 0;
					Require(pipeline.NativePixelShaderIndex == (ignore ? 1137u : 1289u) && pipeline.VertexColor == (ignore ? sgre::scene::SpriteVertexColor::Ignore : sgre::scene::SpriteVertexColor::MultiplyAndDoubleRgb), "native operation9 and exact attribute/color branch changed");
					Require(pipeline.NativeBlendStateIndex == (blending ? 10u : 7u) && pipeline.Blend.Enabled == blending && pipeline.Blend.WriteMask == (write ? 15 : 0), "darken copied blend-state or write mask changed");
					Require(pipeline.Blend.SourceRgb == sgre::scene::SpriteBlendFactor::One && pipeline.Blend.DestinationRgb == sgre::scene::SpriteBlendFactor::Zero && pipeline.Blend.SourceAlpha == sgre::scene::SpriteBlendFactor::Zero && pipeline.Blend.DestinationAlpha == sgre::scene::SpriteBlendFactor::One && pipeline.PixelOperation == sgre::scene::SpritePixelOperation::None, "darken must use real copied RGB replacement and target alpha preservation");
				}
			}
		}
	}
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	material.BlendMode = 0x108;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::SelectSpritePipeline(material, {}));
	},
	    "copied pipeline");
	material.BlendMode = 0x118;
	material.TextureFormat = sgre::resource::PixelFormat::Alpha8;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::SelectSpritePipeline(material, {}));
	},
	    "copied pipeline");
}

static void TestSamplerState() {
	sgre::scene::MotionSpriteCommand command;
	const auto nearest = sgre::scene::SelectSpritePipeline(command, {});
	Require(nearest.Sampler.NativeFilterCode == 0 && nearest.Sampler.Minification == sgre::scene::TextureSampling::Nearest && nearest.Sampler.MipFilter == sgre::scene::TextureSampling::Nearest, "native nearest sampler changed");
	Require(nearest.Sampler.AddressU == sgre::scene::TextureAddress::Clamp && nearest.Sampler.AddressV == sgre::scene::TextureAddress::Clamp && nearest.Sampler.AddressW == sgre::scene::TextureAddress::Repeat, "native sampler address defaults changed");
	Require(nearest.Sampler.MinLod < 0 && nearest.Sampler.MaxLod > 0 && nearest.Sampler.MipLodBias == 0 && nearest.Sampler.MaxAnisotropy == 2, "native sampler descriptor constants changed");
	sgre::scene::SpritePipelineOptions options;
	options.Sampling = sgre::scene::TextureSampling::Linear;
	options.AddressU = sgre::scene::TextureAddress::Repeat;
	options.AddressV = sgre::scene::TextureAddress::Mirror;
	const auto linear = sgre::scene::SelectSpritePipeline(command, options);
	Require(linear.Sampler.NativeFilterCode == 0x15 && linear.Sampler.Magnification == sgre::scene::TextureSampling::Linear && linear.Sampler.MipFilter == sgre::scene::TextureSampling::Linear, "native linear sampler changed");
	Require(linear.Sampler.AddressU == sgre::scene::TextureAddress::Repeat && linear.Sampler.AddressV == sgre::scene::TextureAddress::Mirror, "native sampler address selection changed");
}

static void TestUnsupportedStates() {
	sgre::scene::MotionSpriteCommand command;
	sgre::scene::SpritePipelineOptions options;
	options.HasAuxiliaryTarget = true;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectSpritePipeline(command, options));
	},
	    "ordinary render phase");
	options.HasAuxiliaryTarget = false;
	options.BackbufferCopyPhase = true;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectSpritePipeline(command, options));
	},
	    "ordinary render phase");
	options.BackbufferCopyPhase = false;
	options.NativeRenderMode = 2;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectSpritePipeline(command, options));
	},
	    "ordinary render phase");
	options.NativeRenderMode = 0;
	options.ColorFilter = static_cast<sgre::scene::SpriteColorFilter>(5);
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectSpritePipeline(command, options));
	},
	    "color filter");
	options.ColorFilter = sgre::scene::SpriteColorFilter::None;
	options.AddressU = static_cast<sgre::scene::TextureAddress>(3);
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectSpritePipeline(command, options));
	},
	    "address mode");
	options.AddressU = sgre::scene::TextureAddress::Clamp;
	options.Sampling = static_cast<sgre::scene::TextureSampling>(2);
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectSpritePipeline(command, options));
	},
	    "filtering");
	options.Sampling = sgre::scene::TextureSampling::Nearest;
	options.NativeRenderMode = 2;
	command.BlendMode = 0x110;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectSpritePipeline(command, options));
	},
	    "ordinary render phase");
	options.NativeRenderMode = 0;
	command.BlendMode = 12;
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectSpritePipeline(command, options));
	},
	    "blend mode");
	command.BlendMode = 0x10;
	command.TextureFormat = static_cast<sgre::resource::PixelFormat>(9);
	RequireFailure([&]() {
		static_cast<void>(sgre::scene::SelectSpritePipeline(command, options));
	},
	    "texture format");
}

int main() {
	try {
		TestShaderSlots();
		TestBlendCoupling();
		TestCopiedBlendCoupling();
		TestCopiedDoubleColorCoupling();
		TestCopiedDarken();
		TestSamplerState();
		TestUnsupportedStates();
		std::cout << "Native sprite shader slots, blend/sampler coupling, and unsupported-state checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
