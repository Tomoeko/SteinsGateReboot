#include "sgre/render/sprite_pass.hpp"
#include "sgre/scene/motion_copied_region_owner.hpp"
#include "sgre/scene/motion_shader_program.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

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
	throw std::runtime_error("invalid Metal copied-region operation accepted");
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription value;
	value.Width = 4;
	value.Height = 4;
	value.MaxVertices = 16;
	value.MaxIndices = 16;
	value.MaxDraws = 4;
	value.MaxTextures = 4;
	value.MaxPipelines = 4;
	value.MaxTextureBytes = 8192;
	return value;
}

static sgre::scene::MutableTextureSpec Spec(std::uint32_t width, std::uint32_t height, std::uint32_t format = 28, sgre::scene::MutableTextureStorage kind = sgre::scene::MutableTextureStorage::CpuShadow) {
	sgre::scene::MutableTextureSpec result;
	result.Storage = kind;
	result.Description.Width = width;
	result.Description.Height = height;
	result.Description.OriginalFormat = format;
	return result;
}

static std::array<std::uint8_t, 4> Pixel(std::size_t x, std::size_t y) {
	return {static_cast<std::uint8_t>(17 + y * 40 + x * 3), static_cast<std::uint8_t>(23 + x * 50 + y * 5), static_cast<std::uint8_t>(31 + x * 7 + y * 20), 255};
}

static void Fill(sgre::scene::MutableTextureOwner& texture) {
	const auto mapped = texture.Map();
	Require(mapped.Succeeded && mapped.RowPitch == 16 && mapped.Bytes.size() == 64, "Metal copy source shadow layout differs");
	for (std::size_t y = 0; y < 4; ++y) {
		for (std::size_t x = 0; x < 4; ++x) {
			const auto pixel = Pixel(x, y);
			for (std::size_t channel = 0; channel < 4; ++channel) {
				mapped.Bytes[(y * 4 + x) * 4 + channel] = static_cast<std::byte>(pixel[channel]);
			}
		}
	}
	texture.Unmap();
	texture.Bind(0, sgre::scene::MutableTextureStage::Pixel);
}

static sgre::render::SpritePipelineHandle Pipeline(sgre::render::SpritePassRenderer& renderer) {
	sgre::scene::MotionShaderProgramInput input;
	input.Layout = sgre::scene::MotionVertexLayout::XyUv;
	input.Texture = sgre::scene::ClassifyMotionShaderTexture(true, 28);
	const auto selected = sgre::scene::SelectOrdinaryMotionShaderProgram(input);
	sgre::scene::SpritePipeline pipeline;
	pipeline.NativeVertexShaderIndex = selected.VertexIndex;
	pipeline.NativePixelShaderIndex = selected.PixelIndex;
	pipeline.VertexColor = sgre::scene::SpriteVertexColor::Multiply;
	pipeline.Blend.Enabled = false;
	return renderer.PreparePipeline(pipeline);
}

static sgre::render::SpritePassDraw Draw(sgre::render::SpritePassRenderer& renderer, sgre::render::SpritePipelineHandle pipeline, const sgre::scene::RawTextureDrawPacket& packet) {
	sgre::render::SpritePassDraw result;
	result.Texture = renderer.MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel);
	result.Pipeline = pipeline;
	result.RawTextureVertices = packet.Vertices;
	result.RawTextureColor = packet.Color;
	return result;
}

static sgre::scene::MotionCopyRegionPlan Plan(const sgre::scene::MotionBounds& rectangle) {
	sgre::scene::MotionCopyRegionPlan result;
	result.Action = sgre::scene::MotionCopyRegionAction::Copy;
	result.Rectangle = rectangle;
	return result;
}

static constexpr std::array<float, 16> kProjection{1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, .5f, 0, 0, 0, 1};

static void RequirePixels(const sgre::render::ColorPassPixels& pixels, const std::array<std::array<std::uint8_t, 4>, 4>& source) {
	Require(pixels.Width == 4 && pixels.Height == 4 && pixels.Rgba.size() == 64, "Metal copied-region capture dimensions differ");
	for (std::size_t y = 0; y < 4; ++y) {
		for (std::size_t x = 0; x < 4; ++x) {
			for (std::size_t channel = 0; channel < 4; ++channel) {
				Require(pixels.Rgba[(y * 4 + x) * 4 + channel] == source[(y / 2) * 2 + x / 2][channel], "Metal copied region source box/destination pixel differs");
			}
		}
	}
}

static void TestQueuedCopyAndRetention(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	auto backend = renderer.CreateMutableTextureBackend();
	auto& targets = dynamic_cast<sgre::scene::MotionCopiedTargetBackend&>(*backend);
	auto& copies = dynamic_cast<sgre::scene::MotionRegionCopyBackend&>(*backend);
	sgre::scene::MutableTextureOwner source(*backend, Spec(4, 4));
	Fill(source);
	const auto pipeline = Pipeline(renderer);
	const auto packet = sgre::scene::PrepareRawTextureDrawPacket({-1, -1}, {2, 2}, 0xffffffff);
	const auto source_draw = Draw(renderer, pipeline, packet);
	static_cast<void>(renderer.Submit(std::span(&source_draw, 1), kProjection));

	sgre::scene::MotionCopiedRegionOwner copied(*backend, targets, copies);
	Require(copied.Apply(Plan({1, 1, 3, 3}), 28), "Metal copied region did not reach reset phase");
	Require(copied.State().UvAffine == std::array<float, 4>{.5f, .5f, -.5f, -.5f}, "Metal copied region affine differs");
	copied.SelectedTexture()->Bind(0, sgre::scene::MutableTextureStage::Pixel);
	const auto destination_draw = Draw(renderer, pipeline, packet);
	RequirePixels(renderer.Capture(renderer.Submit(std::span(&destination_draw, 1), kProjection)), {Pixel(1, 1), Pixel(2, 1), Pixel(1, 2), Pixel(2, 2)});
	const auto first_handle = destination_draw.Texture;

	static_cast<void>(renderer.Submit(std::span(&source_draw, 1), kProjection));
	Require(copied.Apply(Plan({0, 0, 1, 1}), 28) && copied.State().Target.Size.Width == 2 && copied.State().Target.Size.Height == 2, "Metal copied target half-size request did not retain the old resource");
	copied.SelectedTexture()->Bind(0, sgre::scene::MutableTextureStage::Pixel);
	const auto retained_draw = Draw(renderer, pipeline, packet);
	Require(retained_draw.Texture == first_handle, "Metal copied target retained-size update replaced its actual wrapper identity");
	const auto submitted = renderer.Submit(std::span(&retained_draw, 1), kProjection);
	copied.Close();
	RequirePixels(renderer.Capture(submitted), {Pixel(0, 0), Pixel(2, 1), Pixel(1, 2), Pixel(2, 2)});
	RequireError([&] {
		static_cast<void>(renderer.Submit(std::span(&retained_draw, 1), kProjection));
	},
	    "stale");
}

static void TestCapabilityAndBounds(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	// Bounds/capability probes require a real initialized physical copy source.
	static_cast<void>(renderer.Submit({}, kProjection, {0, 0, 0, 1}));
	auto backend = renderer.CreateMutableTextureBackend();
	auto other = renderer.CreateMutableTextureBackend();
	auto& targets = dynamic_cast<sgre::scene::MotionCopiedTargetBackend&>(*backend);
	auto& copies = dynamic_cast<sgre::scene::MotionRegionCopyBackend&>(*backend);
	sgre::scene::MutableTextureOwner sampled(*backend, Spec(2, 2));
	RequireError([&] {
		static_cast<void>(targets.CreateRenderTargetView(sampled.Resource()));
	},
	    "render-target texture");
	RequireError([&] {
		copies.CopyFromCurrentTarget(sampled.Resource(), {0, 0, 1, 1});
	},
	    "same-format render-target");

	sgre::scene::MutableTextureOwner foreign(*other, Spec(2, 2, 28, sgre::scene::MutableTextureStorage::RenderTarget));
	RequireError([&] {
		static_cast<void>(targets.CreateRenderTargetView(foreign.Resource()));
	},
	    "another backend");
	RequireError([&] {
		copies.CopyFromCurrentTarget(foreign.Resource(), {0, 0, 1, 1});
	},
	    "another backend");
	sgre::scene::MutableTextureOwner destination(*backend, Spec(2, 2, 28, sgre::scene::MutableTextureStorage::RenderTarget));
	auto view = targets.CreateRenderTargetView(destination.Resource());
	Require(static_cast<bool>(view) && !destination.Map().Succeeded, "Metal copied target has no view or became CPU mappable");
	RequireError([&] {
		copies.CopyFromCurrentTarget(destination.Resource(), {0, 0, 3, 2});
	},
	    "bounds");
	RequireError([&] {
		copies.CopyFromCurrentTarget(destination.Resource(), {3, 0, 5, 1});
	},
	    "bounds");
	RequireError([&] {
		copies.CopyFromCurrentTarget(destination.Resource(), {.5f, 0, 1, 1});
	},
	    "integer");
	RequireError([&] {
		backend->Upload(destination.Resource(), std::array<std::byte, 16>{}, 8);
	},
	    "CPU upload");
	sgre::scene::MutableTextureOwner bgra(*backend, Spec(2, 2, 87, sgre::scene::MutableTextureStorage::RenderTarget));
	RequireError([&] {
		copies.CopyFromCurrentTarget(bgra.Resource(), {0, 0, 1, 1});
	},
	    "same-format");
}

static void TestRendererClosure(const std::filesystem::path& shader) {
	auto renderer = std::make_unique<sgre::render::SpritePassRenderer>(Description(), shader);
	static_cast<void>(renderer->Submit({}, kProjection, {0, 0, 0, 1}));
	auto backend = renderer->CreateMutableTextureBackend();
	auto& targets = dynamic_cast<sgre::scene::MotionCopiedTargetBackend&>(*backend);
	auto& copies = dynamic_cast<sgre::scene::MotionRegionCopyBackend&>(*backend);
	sgre::scene::MotionCopiedRegionOwner copied(*backend, targets, copies);
	Require(copied.Apply(Plan({0, 0, 2, 2}), 28), "Metal closure fixture could not copy its prepared target");
	renderer.reset();
	RequireError([&] {
		static_cast<void>(copied.Apply(Plan({0, 0, 2, 2}), 28));
	},
	    "closed");
	copied.Close();
	Require(copied.State().Target.Closed && !copied.SelectedTexture(), "renderer-first closure left a stale borrowed copied texture");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "expected Metal copied-region shader source");
		const std::filesystem::path shader(argv[1]);
		TestQueuedCopyAndRetention(shader);
		TestCapabilityAndBounds(shader);
		TestRendererClosure(shader);
		std::cout << "Metal copied-region three queue/pixel/capability/lifetime groups passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
