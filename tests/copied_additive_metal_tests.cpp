#include "sgre/render/sprite_pass.hpp"
#include "sgre/scene/motion_alternate_program.hpp"
#include "sgre/scene/motion_copied_region_owner.hpp"
#include "sgre/scene/motion_texture_binding.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireError(const std::function<void()>& operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());
		return;
	}

	throw std::runtime_error("invalid copied-additive Metal operation accepted");
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 4;
	result.Height = 4;
	result.MaxVertices = 16;
	result.MaxIndices = 16;
	result.MaxDraws = 4;
	result.MaxTextures = 8;
	result.MaxPipelines = 8;
	result.MaxTextureBytes = 8192;

	return result;
}

static sgre::scene::MutableTextureSpec Spec(std::uint32_t width, std::uint32_t height, std::uint32_t format = 28) {
	sgre::scene::MutableTextureSpec result;
	result.Description.Width = width;
	result.Description.Height = height;
	result.Description.OriginalFormat = format;

	return result;
}

static constexpr std::array<float, 16> kProjection{1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, .5f, 0, 0, 0, 1};
static constexpr std::array<std::uint8_t, 4> kSource{211, 117, 73, 153};
static constexpr std::uint32_t kColor = 0x8090c060;
static constexpr std::array<std::uint32_t, 4> kIndices{0, 1, 2, 3};

static std::array<std::uint8_t, 4> BasePixel(std::size_t x, std::size_t y) {
	return {static_cast<std::uint8_t>(30 + x * 50 + y * 11), static_cast<std::uint8_t>(17 + x * 9 + y * 40), static_cast<std::uint8_t>(41 + x * 24 + y * 21), static_cast<std::uint8_t>(50 + x * 27 + y * 19)};
}

static void Fill(sgre::scene::MutableTextureOwner& owner, bool base) {
	const auto mapped = owner.Map();
	Require(mapped.Succeeded, "copied-additive CPU shadow mapping failed");
	const auto& description = owner.State().Description;
	for (std::size_t y = 0; y < description.Height; ++y) {
		for (std::size_t x = 0; x < description.Width; ++x) {
			const auto pixel = base ? BasePixel(x, y) : kSource;
			for (std::size_t channel = 0; channel < 4; ++channel) {
				mapped.Bytes[y * mapped.RowPitch + x * 4 + channel] = static_cast<std::byte>(pixel[channel]);
			}
		}
	}
	owner.Unmap();
	owner.SetAddress(0, 0);
	owner.Bind(0, sgre::scene::MutableTextureStage::Pixel);
}

static sgre::scene::MotionCopyRegionPlan CopyPlan(sgre::scene::MotionBounds bounds) {
	sgre::scene::MotionCopyRegionPlan result;
	result.Action = sgre::scene::MotionCopyRegionAction::Copy;
	result.Rectangle = bounds;

	return result;
}

static sgre::scene::SpritePipeline Pipeline(bool copied, std::uint32_t vertex = 4, bool preserve_alpha = true) {
	sgre::scene::MotionAlternateProgramInput input;
	input.State.Layout = vertex == 4 ? sgre::scene::MotionVertexLayout::XyUv : sgre::scene::MotionVertexLayout::XyColorUv;
	input.State.BlendMode = 0x100;
	const auto selected = sgre::scene::SelectAlternateMotionShaderProgram(input);
	Require(selected.Program.VertexIndex == vertex && selected.Program.PixelIndex == 1205, "native alternate selector differs from the certified PS1205 pair");
	sgre::scene::SpritePipeline result;
	result.NativeVertexShaderIndex = vertex;
	result.NativePixelShaderIndex = copied ? selected.Program.PixelIndex : 240;
	result.VertexColor = sgre::scene::SpriteVertexColor::Multiply;
	result.NativeBlendStateIndex = preserve_alpha && copied ? 10u : 7u;
	result.Blend.Enabled = preserve_alpha && copied;
	result.Blend.SourceRgb = sgre::scene::SpriteBlendFactor::One;
	result.Blend.DestinationRgb = sgre::scene::SpriteBlendFactor::Zero;
	result.Blend.SourceAlpha = sgre::scene::SpriteBlendFactor::Zero;
	result.Blend.DestinationAlpha = sgre::scene::SpriteBlendFactor::One;

	return result;
}

static sgre::render::SpritePassDraw RawDraw(sgre::render::SpritePassRenderer& renderer, const sgre::scene::RawTextureDrawPacket& packet, const sgre::scene::SpritePipeline& pipeline) {
	sgre::render::SpritePassDraw draw;
	draw.Texture = renderer.MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel);
	draw.Pipeline = renderer.PreparePipeline(pipeline);
	draw.RawTextureVertices = packet.Vertices;
	draw.RawTextureColor = packet.Color;

	return draw;
}

static std::array<sgre::scene::MotionSpriteVertex, 4> OrdinaryVertices(const sgre::scene::RawTextureDrawPacket& packet) {
	std::array<sgre::scene::MotionSpriteVertex, 4> result;
	for (std::size_t index = 0; index < result.size(); ++index) {
		result[index].Position = packet.Vertices[index].Position;
		result[index].Uv = packet.Vertices[index].Uv;
		result[index].Color = (packet.Color << 8) | (packet.Color >> 24);
	}

	return result;
}

static void SubmitBase(sgre::render::SpritePassRenderer& renderer, sgre::scene::MutableTextureOwner& base) {
	base.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	const auto packet = sgre::scene::PrepareRawTextureDrawPacket({-1, -1}, {2, 2}, 0xffffffff);
	const auto draw = RawDraw(renderer, packet, Pipeline(false));
	static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
}

static void BindCopied(sgre::render::SpritePassRenderer& renderer, sgre::scene::MutableTextureOwner& source, sgre::scene::MotionCopiedRegionOwner& copied, sgre::render::SpritePassDraw& draw, std::uint32_t blend_mode = 0x100) {
	sgre::scene::MotionTextureBindingState state;
	state.Textures[0] = &source;
	state.CopiedTarget = copied.SelectedTexture();
	state.BlendMode = blend_mode;
	sgre::scene::MotionTextureBindingScope scope(state);
	scope.Bind();
	draw.Texture = renderer.MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel);
	draw.CopiedDestination = renderer.MutableTextureBinding(2, sgre::scene::MutableTextureStage::Pixel);
	draw.CopiedUvAffine = copied.State().UvAffine;
	scope.Restore();
	Require(state.Textures[2] == nullptr && renderer.MutableTextureBinding(2, sgre::scene::MutableTextureStage::Pixel) == draw.CopiedDestination, "native nullable row restoration incorrectly cleared physical slot two");
}

static void BindPreviousCopied(sgre::render::SpritePassRenderer& renderer, sgre::scene::MutableTextureOwner& source, sgre::render::SpritePassDraw& draw, const std::array<float, 4>& affine) {
	sgre::scene::MotionTextureBindingState state;
	state.Textures[0] = &source;
	state.BlendMode = 0x100;
	sgre::scene::MotionTextureBindingScope scope(state);
	scope.Bind();
	draw.Texture = renderer.MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel);
	draw.CopiedDestination = renderer.MutableTextureBinding(2, sgre::scene::MutableTextureStage::Pixel);
	draw.CopiedUvAffine = affine;
	scope.Restore();
	Require(state.Textures[2] == nullptr, "native absent copied override changed the null prepared row");
}

static std::array<std::uint8_t, 4> AdditivePixel(std::array<std::uint8_t, 4> destination, std::uint32_t color, std::uint8_t target_alpha, bool preserve_alpha) {
	const std::array<std::uint32_t, 4> factors{(color >> 16) & 255, (color >> 8) & 255, color & 255, color >> 24};
	const double opacity = (static_cast<double>(kSource[3]) / 255) * (static_cast<double>(factors[3]) / 255);
	std::array<std::uint8_t, 4> result;
	for (std::size_t channel = 0; channel < 3; ++channel) {
		const double dest = static_cast<double>(destination[channel]) / 255;
		const double source = (static_cast<double>(kSource[channel]) / 255) * (static_cast<double>(factors[channel]) / 255);
		const double combined = std::min(source + dest, 1.0);
		result[channel] = static_cast<std::uint8_t>(std::lround((opacity * (combined - dest) + dest) * 255));
	}
	result[3] = preserve_alpha ? target_alpha : static_cast<std::uint8_t>(std::lround(opacity * 255));

	return result;
}

static void RequirePixel(const sgre::render::ColorPassPixels& pixels, std::size_t x, std::size_t y, const std::array<std::uint8_t, 4>& expected, int tolerance = 0) {
	for (std::size_t channel = 0; channel < 4; ++channel) {
		const auto actual = pixels.Rgba[(y * 4 + x) * 4 + channel];
		const auto difference = std::abs(static_cast<int>(actual) - static_cast<int>(expected[channel]));
		Require(difference <= tolerance, "copied additive pixel differs at " + std::to_string(x) + "/" + std::to_string(y) + "/" + std::to_string(channel) + " got " + std::to_string(actual) + " expected " + std::to_string(expected[channel]));
	}
}

static void TestRegionBlendAndLifetime(const std::filesystem::path& shader) {
	for (const std::uint32_t vertex : std::array<std::uint32_t, 2>{1, 4}) {
		for (const bool preserve_alpha : std::array<bool, 2>{true, false}) {
			sgre::render::SpritePassRenderer renderer(Description(), shader);
			auto backend = renderer.CreateMutableTextureBackend();
			sgre::scene::MutableTextureOwner base(*backend, Spec(4, 4));
			sgre::scene::MutableTextureOwner source(*backend, Spec(1, 1));
			Fill(base, true);
			Fill(source, false);
			SubmitBase(renderer, base);
			sgre::scene::MotionCopiedRegionOwner copied(*backend, dynamic_cast<sgre::scene::MotionCopiedTargetBackend&>(*backend), dynamic_cast<sgre::scene::MotionRegionCopyBackend&>(*backend));
			Require(copied.Apply(CopyPlan({1, 1, 3, 3}), 28), "copied additive region did not copy");
			Require(copied.State().UvAffine == std::array<float, 4>{.5f, .5f, -.5f, -.5f}, "copied region pixel cb0 affine differs");
			const auto packet = sgre::scene::PrepareRawTextureDrawPacket({-.5f, -.5f}, {1, 1}, kColor);
			const auto ordinary = OrdinaryVertices(packet);
			auto draw = RawDraw(renderer, packet, Pipeline(true, vertex, preserve_alpha));
			if (vertex == 1) {
				draw.RawTextureVertices = {};
				draw.Vertices = ordinary;
				draw.Indices = kIndices;
			}
			BindCopied(renderer, source, copied, draw);
			const auto submission = renderer.Submit(std::span(&draw, 1), kProjection, {}, sgre::render::SpritePassLoadAction::Preserve);
			copied.Close();
			const auto pixels = renderer.Capture(submission);
			for (std::size_t y = 0; y < 4; ++y) {
				for (std::size_t x = 0; x < 4; ++x) {
					const auto base_pixel = BasePixel(x, y);
					const bool inside = x >= 1 && x <= 2 && y >= 1 && y <= 2;
					const auto expected = inside ? AdditivePixel(base_pixel, kColor, base_pixel[3], preserve_alpha) : base_pixel;
					RequirePixel(pixels, x, y, expected, inside ? 1 : 0);
					if (preserve_alpha) {
						Require(pixels.Rgba[(y * 4 + x) * 4 + 3] == base_pixel[3], "native blend ten changed destination alpha");
					}
				}
			}
			RequireError([&] {
				static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
			},
			    "stale");
			RequireError([&] {
				static_cast<void>(renderer.MutableTextureBinding(2, sgre::scene::MutableTextureStage::Pixel));
			},
			    "stale");
		}
	}
}

static void TestPerDrawConstantsAndCache(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	auto backend = renderer.CreateMutableTextureBackend();
	sgre::scene::MutableTextureOwner base(*backend, Spec(4, 4));
	sgre::scene::MutableTextureOwner source(*backend, Spec(1, 1));
	Fill(base, true);
	Fill(source, false);
	SubmitBase(renderer, base);
	sgre::scene::MotionCopiedRegionOwner copied(*backend, dynamic_cast<sgre::scene::MotionCopiedTargetBackend&>(*backend), dynamic_cast<sgre::scene::MotionRegionCopyBackend&>(*backend));
	Require(copied.Apply(CopyPlan({0, 0, 4, 4}), 28), "copied additive whole source failed");
	const auto left = sgre::scene::PrepareRawTextureDrawPacket({-1, -1}, {1, 2}, kColor);
	const auto right = sgre::scene::PrepareRawTextureDrawPacket({0, -1}, {1, 2}, 0xc040e020);
	std::array<sgre::render::SpritePassDraw, 2> draws{RawDraw(renderer, left, Pipeline(true)), RawDraw(renderer, right, Pipeline(true))};
	BindCopied(renderer, source, copied, draws[0]);
	BindPreviousCopied(renderer, source, draws[1], copied.State().UvAffine);
	Require(draws[0].CopiedDestination == draws[1].CopiedDestination, "absent copied override lost the previous physical slot two");
	draws[1].CopiedUvAffine[2] = -.5f;
	Require(draws[0].Pipeline == draws[1].Pipeline && draws[0].Pipeline != renderer.PreparePipeline(Pipeline(false)), "copied PS1205 and ordinary PS240 collided in the pipeline cache");
	const auto pixels = renderer.Capture(renderer.Submit(draws, kProjection, {}, sgre::render::SpritePassLoadAction::Preserve));
	for (std::size_t y = 0; y < 4; ++y) {
		for (std::size_t x = 0; x < 4; ++x) {
			const auto color = x < 2 ? kColor : 0xc040e020;
			const auto expected = AdditivePixel(BasePixel(x % 2, y), color, BasePixel(x, y)[3], true);
			RequirePixel(pixels, x, y, expected, 1);
			Require(pixels.Rgba[(y * 4 + x) * 4 + 3] == BasePixel(x, y)[3], "copied reference alpha replaced current target alpha");
		}
	}

	auto masked_pipeline = Pipeline(true);
	masked_pipeline.Blend.WriteMask = 0;
	for (auto& draw : draws) {
		draw.Pipeline = renderer.PreparePipeline(masked_pipeline);
	}
	const auto masked = renderer.Capture(renderer.Submit(draws, kProjection, {}, sgre::render::SpritePassLoadAction::Preserve));
	Require(masked.Rgba == pixels.Rgba, "copied additive zero write mask changed the preserved target");
}

static sgre::scene::SpritePipeline SkipColorPipeline(std::uint32_t pixel, std::uint32_t vertex, bool preserve_alpha) {
	sgre::scene::MotionAlternateProgramInput input;
	input.State.Layout = vertex == 4 ? sgre::scene::MotionVertexLayout::XyUv : sgre::scene::MotionVertexLayout::XyColorUv;
	input.State.BlendMode = pixel == 1129 ? 0x110 : 0x112;
	input.State.TextureAttributes = 4;
	const auto selected = sgre::scene::SelectAlternateMotionShaderProgram(input);
	Require(selected.Program.VertexIndex == vertex && selected.Program.PixelIndex == pixel, "native selector differs from the original skip-color copied program");
	auto result = Pipeline(true, vertex, preserve_alpha);
	result.NativePixelShaderIndex = selected.Program.PixelIndex;
	result.VertexColor = sgre::scene::SpriteVertexColor::Ignore;
	result.PixelOperation = sgre::scene::SpritePixelOperation::None;

	return result;
}

static void FillSourceAlpha(sgre::scene::MutableTextureOwner& owner, std::uint8_t alpha) {
	const auto mapped = owner.Map();
	Require(mapped.Succeeded && owner.State().Description.Width == 1 && owner.State().Description.Height == 1, "skip-color fixture requires its actual one-pixel source owner");
	for (std::size_t channel = 0; channel < 4; ++channel) {
		mapped.Bytes[channel] = static_cast<std::byte>(channel == 3 ? alpha : kSource[channel]);
	}
	owner.Unmap();
	owner.SetAddress(0, 0);
	owner.Bind(0, sgre::scene::MutableTextureStage::Pixel);
}

static std::array<std::uint8_t, 4> SkipColorPixel(std::array<std::uint8_t, 4> destination, std::uint8_t alpha, bool overlay, bool preserve_alpha) {
	const double opacity = static_cast<double>(alpha) / 255;
	std::array<std::uint8_t, 4> result;
	for (std::size_t channel = 0; channel < 3; ++channel) {
		const double dest = static_cast<double>(destination[channel]) / 255;
		const double source = static_cast<double>(kSource[channel]) / 255;
		double combined = std::min(dest + source, 1.0);
		if (overlay) {
			combined = dest < 0.5 ? 2 * dest * source : 1 - (1 - dest) * (1 - source) * 2;
		}
		result[channel] = static_cast<std::uint8_t>(std::lround((opacity * (combined - dest) + dest) * 255));
	}
	result[3] = preserve_alpha ? destination[3] : alpha;

	return result;
}

static void TestSkipColorPrograms(const std::filesystem::path& shader) {
	std::size_t pixel_comparisons = 0;
	for (const std::uint32_t vertex : std::array<std::uint32_t, 2>{1, 4}) {
		for (const bool preserve_alpha : std::array<bool, 2>{true, false}) {
			sgre::render::SpritePassRenderer renderer(Description(), shader);
			auto backend = renderer.CreateMutableTextureBackend();
			sgre::scene::MutableTextureOwner base(*backend, Spec(4, 4));
			sgre::scene::MutableTextureOwner source(*backend, Spec(1, 1));
			Fill(base, true);
			const auto additive = renderer.PreparePipeline(SkipColorPipeline(1129, vertex, preserve_alpha));
			const auto overlay = renderer.PreparePipeline(SkipColorPipeline(1131, vertex, preserve_alpha));
			Require(additive != overlay && additive != renderer.PreparePipeline(Pipeline(true, vertex, preserve_alpha)), "copied source-only and multiplied-color programs collided in the actual pipeline cache");
			for (const std::uint32_t pixel : std::array<std::uint32_t, 2>{1129, 1131}) {
				for (const std::uint8_t alpha : std::array<std::uint8_t, 4>{0, 64, 153, 255}) {
					FillSourceAlpha(source, alpha);
					SubmitBase(renderer, base);
					sgre::scene::MotionCopiedRegionOwner copied(*backend, dynamic_cast<sgre::scene::MotionCopiedTargetBackend&>(*backend), dynamic_cast<sgre::scene::MotionRegionCopyBackend&>(*backend));
					Require(copied.Apply(CopyPlan({0, 0, 4, 4}), 28), "skip-color original destination copy failed");
					const auto packet = sgre::scene::PrepareRawTextureDrawPacket({-1, -1}, {2, 2}, alpha == 0 ? 0xff03e711 : 0x052de109);
					const auto ordinary = OrdinaryVertices(packet);
					auto draw = RawDraw(renderer, packet, SkipColorPipeline(pixel, vertex, preserve_alpha));
					if (vertex == 1) {
						draw.RawTextureVertices = {};
						draw.Vertices = ordinary;
						draw.Indices = kIndices;
					}
					BindCopied(renderer, source, copied, draw, pixel == 1129 ? 0x110 : 0x112);
					const auto submission = renderer.Submit(std::span(&draw, 1), kProjection, {}, sgre::render::SpritePassLoadAction::Preserve);
					copied.Close();
					const auto pixels = renderer.Capture(submission);
					for (std::size_t y = 0; y < 4; ++y) {
						for (std::size_t x = 0; x < 4; ++x) {
							const auto expected = SkipColorPixel(BasePixel(x, y), alpha, pixel == 1131, preserve_alpha);
							RequirePixel(pixels, x, y, expected, 1);
							Require(pixels.Rgba[(y * 4 + x) * 4 + 3] == expected[3], "skip-color native blend state replaced the wrong alpha owner");
							++pixel_comparisons;
						}
					}
				}
			}
			for (const std::uint32_t pixel : std::array<std::uint32_t, 2>{1129, 1131}) {
				for (const std::uint32_t mutation : std::array<std::uint32_t, 4>{0, 1, 2, 3}) {
					auto invalid = SkipColorPipeline(pixel, vertex, preserve_alpha);
					if (mutation == 0) {
						invalid.VertexColor = sgre::scene::SpriteVertexColor::Multiply;
					} else if (mutation == 1) {
						invalid.NativeBlendStateIndex = 0;
					} else if (mutation == 2) {
						invalid.Blend.SourceAlpha = sgre::scene::SpriteBlendFactor::One;
					} else {
						invalid.PixelOperation = sgre::scene::SpritePixelOperation::InvertRgb;
					}
					RequireError([&] {
						static_cast<void>(renderer.PreparePipeline(invalid));
					},
					    "skip-color program requires");
				}
			}
		}
	}
	Require(pixel_comparisons == 512, "skip-color fixture did not compare every actual layout/blend/source-alpha/overlay-side pixel");
}

static sgre::scene::SpritePipeline DarkenPipeline(bool ignore, std::uint32_t vertex, bool preserve_alpha) {
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	material.TextureAttributes = ignore ? 4 : 0;
	material.BlendMode = 0x118;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = preserve_alpha;
	auto pipeline = sgre::scene::SelectSpritePipeline(material, options);
	pipeline.NativeVertexShaderIndex = vertex;

	return pipeline;
}

static std::array<std::uint8_t, 4> DarkenPixel(std::array<std::uint8_t, 4> destination, std::uint8_t alpha, bool ignore, bool preserve_alpha) {
	const std::array<std::uint32_t, 4> color{(kColor >> 16) & 255, (kColor >> 8) & 255, kColor & 255, kColor >> 24};
	const double opacity = (static_cast<double>(alpha) / 255) * (ignore ? 1.0 : static_cast<double>(color[3]) / 255);
	std::array<std::uint8_t, 4> result;
	for (std::size_t channel = 0; channel < 3; ++channel) {
		const double dest = static_cast<double>(destination[channel]) / 255;
		const double source = (static_cast<double>(kSource[channel]) / 255) * (ignore ? 1.0 : 2.0 * static_cast<double>(color[channel]) / 255);
		result[channel] = static_cast<std::uint8_t>(std::lround((dest + opacity * (std::min(source, dest) - dest)) * 255));
	}
	result[3] = preserve_alpha ? destination[3] : static_cast<std::uint8_t>(std::lround(opacity * 255));

	return result;
}

static sgre::scene::SpritePipeline DoubleColorPipeline(bool overlay, std::uint32_t vertex, bool preserve_alpha) {
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	material.TextureAttributes = 0;
	material.BlendMode = overlay ? 0x112 : 0x110;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = preserve_alpha;
	auto pipeline = sgre::scene::SelectSpritePipeline(material, options);
	pipeline.NativeVertexShaderIndex = vertex;
	Require(pipeline.NativePixelShaderIndex == (overlay ? 1283u : 1281u) && pipeline.VertexColor == sgre::scene::SpriteVertexColor::MultiplyAndDoubleRgb, "copied modulation must select the original double-color program");
	return pipeline;
}

static std::array<std::uint8_t, 4> DoubleColorPixel(std::array<std::uint8_t, 4> destination, std::uint8_t alpha, std::uint32_t packed_color, bool overlay, bool preserve_alpha) {
	const std::array<std::uint32_t, 4> color{(packed_color >> 16) & 255, (packed_color >> 8) & 255, packed_color & 255, packed_color >> 24};
	const double opacity = (static_cast<double>(alpha) / 255) * (static_cast<double>(color[3]) / 255);
	std::array<std::uint8_t, 4> result;
	for (std::size_t channel = 0; channel != 3; ++channel) {
		const double dest = static_cast<double>(destination[channel]) / 255;
		const double source = ((static_cast<double>(kSource[channel]) / 255) * (static_cast<double>(color[channel]) / 255)) * 2;
		const double combined = overlay ? (dest < .5 ? (dest * source) * 2 : 1 - ((1 - source) * (1 - dest)) * 2) : std::min(source + dest, 1.0);
		// The actual target is RGBA8Unorm; its conversion clamps only the final shader output.
		result[channel] = static_cast<std::uint8_t>(std::lround(std::clamp(dest + opacity * (combined - dest), 0.0, 1.0) * 255));
	}
	result[3] = preserve_alpha ? destination[3] : static_cast<std::uint8_t>(std::lround(opacity * 255));
	return result;
}

static void TestCopiedDoubleColorPrograms(const std::filesystem::path& shader) {
	std::size_t compared = 0;
	for (const auto vertex : {1u, 4u}) {
		for (const bool preserve_alpha : {false, true}) {
			sgre::render::SpritePassRenderer renderer(Description(), shader);
			auto backend = renderer.CreateMutableTextureBackend();
			sgre::scene::MutableTextureOwner base(*backend, Spec(4, 4));
			sgre::scene::MutableTextureOwner source(*backend, Spec(1, 1));
			Fill(base, true);
			for (const bool overlay : {false, true}) {
				const auto configuration = DoubleColorPipeline(overlay, vertex, preserve_alpha);
				const auto pipeline = renderer.PreparePipeline(configuration);
				const auto skipped = renderer.PreparePipeline(SkipColorPipeline(overlay ? 1131 : 1129, vertex, preserve_alpha));
				Require(pipeline != skipped && pipeline != renderer.PreparePipeline(Pipeline(true, vertex, preserve_alpha)), "copied double, ignored and single color pipelines must retain distinct identities");
				for (const auto packed_color : {kColor, 0x00ffffffu, 0xffffffffu}) {
					for (const std::uint8_t alpha : std::array<std::uint8_t, 4>{0, 64, 153, 255}) {
						FillSourceAlpha(source, alpha);
						SubmitBase(renderer, base);
						sgre::scene::MotionCopiedRegionOwner copied(*backend, dynamic_cast<sgre::scene::MotionCopiedTargetBackend&>(*backend), dynamic_cast<sgre::scene::MotionRegionCopyBackend&>(*backend));
						Require(copied.Apply(CopyPlan({0, 0, 4, 4}), 28), "double-color must copy its actual current destination");
						const auto packet = sgre::scene::PrepareRawTextureDrawPacket({-1, -1}, {2, 2}, packed_color);
						const auto ordinary = OrdinaryVertices(packet);
						auto draw = RawDraw(renderer, packet, configuration);
						if (vertex == 1) {
							draw.RawTextureVertices = {};
							draw.Vertices = ordinary;
							draw.Indices = kIndices;
						}
						BindCopied(renderer, source, copied, draw, overlay ? 0x112 : 0x110);
						const auto submitted = renderer.Submit(std::span(&draw, 1), kProjection, {}, sgre::render::SpritePassLoadAction::Preserve);
						copied.Close();
						const auto pixels = renderer.Capture(submitted);
						for (std::size_t y = 0; y != 4; ++y) {
							for (std::size_t x = 0; x != 4; ++x) {
								const auto expected = DoubleColorPixel(BasePixel(x, y), alpha, packed_color, overlay, preserve_alpha);
								RequirePixel(pixels, x, y, expected, 1);
								Require(pixels.Rgba[(y * 4 + x) * 4 + 3] == expected[3], "copied double-color alpha must retain actual vertex opacity and native target alpha policy");
								++compared;
							}
						}
					}
				}
				for (std::uint32_t mutation = 0; mutation != 4; ++mutation) {
					auto invalid = configuration;
					if (mutation == 0) {
						invalid.VertexColor = sgre::scene::SpriteVertexColor::Ignore;
					} else if (mutation == 1) {
						invalid.NativeBlendStateIndex = 0;
					} else if (mutation == 2) {
						invalid.Blend.SourceAlpha = sgre::scene::SpriteBlendFactor::One;
					} else {
						invalid.PixelOperation = sgre::scene::SpritePixelOperation::InvertRgb;
					}
					RequireError([&] {
						static_cast<void>(renderer.PreparePipeline(invalid));
					},
					    "copied double-color program requires");
				}
			}
		}
	}
	Require(compared == 1536, "double-color fixture must retain both layouts/blends/operations and all alpha/color/destination vectors");
}

static void TestCopiedDarkenPrograms(const std::filesystem::path& shader) {
	std::size_t pixels = 0;
	for (const std::uint32_t vertex : {1u, 4u}) {
		for (const bool preserve_alpha : {false, true}) {
			for (const bool ignore : {false, true}) {
				sgre::render::SpritePassRenderer renderer(Description(), shader);
				auto backend = renderer.CreateMutableTextureBackend();
				sgre::scene::MutableTextureOwner base(*backend, Spec(4, 4));
				sgre::scene::MutableTextureOwner source(*backend, Spec(1, 1));
				Fill(base, true);
				const auto configuration = DarkenPipeline(ignore, vertex, preserve_alpha);
				const auto pipeline = renderer.PreparePipeline(configuration);
				Require(pipeline != renderer.PreparePipeline(SkipColorPipeline(1129, vertex, preserve_alpha)), "copied darken must have its own genuine pipeline identity");
				for (const std::uint8_t alpha : std::array<std::uint8_t, 4>{0, 64, 153, 255}) {
					FillSourceAlpha(source, alpha);
					SubmitBase(renderer, base);
					sgre::scene::MotionCopiedRegionOwner copied(*backend, dynamic_cast<sgre::scene::MotionCopiedTargetBackend&>(*backend), dynamic_cast<sgre::scene::MotionRegionCopyBackend&>(*backend));
					Require(copied.Apply(CopyPlan({0, 0, 4, 4}), 28), "darken must copy its actual destination");
					const auto packet = sgre::scene::PrepareRawTextureDrawPacket({-1, -1}, {2, 2}, kColor);
					const auto ordinary = OrdinaryVertices(packet);
					auto draw = RawDraw(renderer, packet, configuration);
					if (vertex == 1) {
						draw.RawTextureVertices = {};
						draw.Vertices = ordinary;
						draw.Indices = kIndices;
					}
					BindCopied(renderer, source, copied, draw, 0x118);
					const auto submission = renderer.Submit(std::span(&draw, 1), kProjection, {}, sgre::render::SpritePassLoadAction::Preserve);
					copied.Close();
					const auto actual = renderer.Capture(submission);
					for (std::size_t y = 0; y < 4; ++y) {
						for (std::size_t x = 0; x < 4; ++x) {
							RequirePixel(actual, x, y, DarkenPixel(BasePixel(x, y), alpha, ignore, preserve_alpha), 1);
							++pixels;
						}
					}
				}
				for (std::uint32_t mutation = 0; mutation < 4; ++mutation) {
					auto invalid = configuration;
					if (mutation == 0) {
						invalid.VertexColor = sgre::scene::SpriteVertexColor::Multiply;
					} else if (mutation == 1) {
						invalid.NativeBlendStateIndex = 0;
					} else if (mutation == 2) {
						invalid.Blend.SourceAlpha = sgre::scene::SpriteBlendFactor::One;
					} else {
						invalid.PixelOperation = sgre::scene::SpritePixelOperation::InvertRgb;
					}
					RequireError([&] {
						static_cast<void>(renderer.PreparePipeline(invalid));
					},
					    "copied darken program requires");
				}
			}
		}
	}
	Require(pixels == 512, "darken fixture must compare both genuine color modes, VS1/4, blend7/10, source alpha and nonuniform copied destination");
}

static void TestInputAndPipelineGuards(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	RequireError([&] {
		static_cast<void>(renderer.Submit({}, kProjection, {}, sgre::render::SpritePassLoadAction::Preserve));
	},
	    "initialized");
	RequireError([&] {
		static_cast<void>(renderer.Submit({}, kProjection, {}, static_cast<sgre::render::SpritePassLoadAction>(55)));
	},
	    "load action");
	auto backend = renderer.CreateMutableTextureBackend();
	sgre::scene::MutableTextureOwner source(*backend, Spec(1, 1));
	Fill(source, false);
	RequireError([&] {
		source.Bind(1, sgre::scene::MutableTextureStage::Pixel);
	},
	    "slots zero and two");
	RequireError([&] {
		source.Bind(2, sgre::scene::MutableTextureStage::Vertex);
	},
	    "slots zero and two");
	RequireError([&] {
		static_cast<void>(renderer.MutableTextureBinding(1, sgre::scene::MutableTextureStage::Pixel));
	},
	    "slots zero and two");
	const auto packet = sgre::scene::PrepareRawTextureDrawPacket({-1, -1}, {2, 2}, kColor);
	auto draw = RawDraw(renderer, packet, Pipeline(true));
	RequireError([&] {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
	},
	    "stale");
	sgre::scene::MutableTextureOwner bgra(*backend, Spec(1, 1, 87));
	bgra.Bind(2, sgre::scene::MutableTextureStage::Pixel);
	draw.CopiedDestination = renderer.MutableTextureBinding(2, sgre::scene::MutableTextureStage::Pixel);
	RequireError([&] {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
	},
	    "bound RGBA");
	source.Bind(2, sgre::scene::MutableTextureStage::Pixel);
	draw.CopiedDestination = renderer.MutableTextureBinding(2, sgre::scene::MutableTextureStage::Pixel);
	draw.CopiedUvAffine[0] = std::numeric_limits<float>::quiet_NaN();
	RequireError([&] {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
	},
	    "finite pixel");
	for (const std::uint32_t mutation : std::array<std::uint32_t, 4>{0, 1, 2, 3}) {
		auto invalid = Pipeline(true);
		if (mutation == 0) {
			invalid.VertexColor = sgre::scene::SpriteVertexColor::Ignore;
		} else if (mutation == 1) {
			invalid.NativeBlendStateIndex = 0;
		} else if (mutation == 2) {
			invalid.Blend.SourceAlpha = sgre::scene::SpriteBlendFactor::One;
		} else {
			invalid.NativePixelShaderIndex = 1206;
		}
		RequireError([&] {
			static_cast<void>(renderer.PreparePipeline(invalid));
		},
		    mutation == 3 ? "shader identity" : "PS1205 requires");
	}
}

class ShaderFixture {
public:
	explicit ShaderFixture(std::filesystem::path path) : m_Path(std::move(path)) {
		Require(!std::filesystem::exists(m_Path), "copied shader fixture must not overwrite an existing file");
	}
	~ShaderFixture() {
		if (m_Created) {
			std::error_code error;
			std::filesystem::remove(m_Path, error);
		}
	}
	ShaderFixture(const ShaderFixture&) = delete;
	ShaderFixture& operator=(const ShaderFixture&) = delete;
	void WriteWithoutCopiedFragment(const std::filesystem::path& source) {
		std::ifstream input(source, std::ios::binary | std::ios::ate);
		const auto count = input.tellg();
		Require(input && count > 0 && count <= 64 * 1024, "copied shader fixture exceeds checked source size");
		std::string text(static_cast<std::size_t>(count), '\0');
		input.seekg(0);
		Require(static_cast<bool>(input.read(text.data(), static_cast<std::streamsize>(text.size()))), "copied shader fixture read failed");
		const auto start = text.find("struct CopiedAdditiveConstants");
		Require(start != std::string::npos, "copied shader fixture lacks separate fragment block");
		text.erase(start);
		std::ofstream output(m_Path, std::ios::binary);
		Require(static_cast<bool>(output), "copied shader fixture creation failed");
		m_Created = true;
		Require(static_cast<bool>(output.write(text.data(), static_cast<std::streamsize>(text.size()))), "copied shader fixture write failed");
	}

private:
	std::filesystem::path m_Path;
	bool m_Created = false;
};

static void TestLibraryGuard(const std::filesystem::path& shader, const std::filesystem::path& fixture_path) {
	ShaderFixture fixture(fixture_path);
	fixture.WriteWithoutCopiedFragment(shader);
	sgre::render::SpritePassRenderer renderer(Description(), fixture_path);
	static_cast<void>(renderer.PreparePipeline(Pipeline(false)));
	RequireError([&] {
		static_cast<void>(renderer.PreparePipeline(Pipeline(true)));
	},
	    "specialize Metal sprite shader");
	for (const std::uint32_t pixel : std::array<std::uint32_t, 2>{1129, 1131}) {
		RequireError([&] {
			static_cast<void>(renderer.PreparePipeline(SkipColorPipeline(pixel, 4, true)));
		},
		    "specialize Metal sprite shader");
	}
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "expected copied Metal shader and nonexistent ignored fixture output path");
		TestRegionBlendAndLifetime(argv[1]);
		TestPerDrawConstantsAndCache(argv[1]);
		TestInputAndPipelineGuards(argv[1]);
		TestSkipColorPrograms(argv[1]);
		TestCopiedDarkenPrograms(argv[1]);
		TestCopiedDoubleColorPrograms(argv[1]);
		TestLibraryGuard(argv[1], argv[2]);
		std::cout << "copied additive PS1205 regressions and512 PS1129/PS1131 plus512 copied darken and1536 modulated additive/overlay layout/blend/alpha pixels passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
