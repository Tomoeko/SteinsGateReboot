#include "sgre/render/sprite_pass.hpp"

#include <algorithm>
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
	throw std::runtime_error("unsupported mutable Metal operation accepted");
}
static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription description;
	description.Width = 2;
	description.Height = 2;
	description.MaxVertices = 16;
	description.MaxIndices = 16;
	description.MaxDraws = 4;
	description.MaxTextures = 4;
	description.MaxPipelines = 4;
	description.MaxTextureBytes = 4096;
	return description;
}
static sgre::scene::MutableTextureSpec Spec(std::uint32_t format = 28) {
	sgre::scene::MutableTextureSpec spec;
	spec.Storage = sgre::scene::MutableTextureStorage::CpuShadow;
	spec.Description.Width = 2;
	spec.Description.Height = 2;
	spec.Description.OriginalFormat = format;
	return spec;
}
static constexpr std::array<float, 16> kProjection{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.5f, 0, 0, 0, 1};
static constexpr std::array<std::uint32_t, 4> kIndices{0, 2, 1, 3};
static std::array<sgre::scene::MotionSpriteVertex, 4> Quad(float u = 0.5f, float v = 0.5f) {
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-1, 1};
	vertices[1].Position = {1, 1};
	vertices[2].Position = {-1, -1};
	vertices[3].Position = {1, -1};
	for (auto& vertex : vertices) {
		vertex.Color = 0xffffffff;
		vertex.Uv = {u, v};
	}
	return vertices;
}
static sgre::render::SpritePipelineHandle Pipeline(sgre::render::SpritePassRenderer& renderer) {
	sgre::scene::SpritePipeline pipeline;
	pipeline.VertexColor = sgre::scene::SpriteVertexColor::Ignore;
	pipeline.Blend.Enabled = false;
	pipeline.NativePixelShaderIndex = 180;
	return renderer.PreparePipeline(pipeline);
}
static sgre::render::SpritePassDraw Draw(sgre::render::SpritePassRenderer& renderer, sgre::render::SpritePipelineHandle pipeline, std::span<const sgre::scene::MotionSpriteVertex> vertices) {
	sgre::render::SpritePassDraw draw;
	draw.Texture = renderer.MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel);
	draw.Pipeline = pipeline;
	draw.Vertices = vertices;
	draw.Indices = kIndices;
	return draw;
}
static sgre::render::ColorPassPixels Render(sgre::render::SpritePassRenderer& renderer, const sgre::render::SpritePassDraw& draw) {
	return renderer.Capture(renderer.Submit(std::span(&draw, 1), kProjection));
}
static void RequirePixel(const sgre::render::ColorPassPixels& pixels, std::size_t index, std::array<std::uint8_t, 4> expected, unsigned tolerance = 0) {
	Require(pixels.Rgba.size() == 16, "mutable Metal capture dimensions changed");
	for (std::size_t channel = 0; channel < 4; ++channel) {
		const auto actual = pixels.Rgba[index * 4 + channel];
		const auto difference = std::max(actual, expected[channel]) - std::min(actual, expected[channel]);
		Require(static_cast<unsigned>(difference) <= tolerance, "mutable Metal sample differs at pixel " + std::to_string(index) + " channel " + std::to_string(channel) + ": " + std::to_string(actual));
	}
}
static void Fill(sgre::scene::MutableTextureOwner& texture, std::array<std::uint8_t, 4> pixel) {
	const auto mapped = texture.Map();
	Require(mapped.Succeeded && mapped.Bytes.size() == 16 && mapped.RowPitch == 8, "mutable Metal CPU shadow mapping changed");
	for (std::size_t index = 0; index < 4; ++index) {
		for (std::size_t channel = 0; channel < 4; ++channel) {
			mapped.Bytes[index * 4 + channel] = static_cast<std::byte>(pixel[channel]);
		}
	}
	texture.Unmap();
	texture.Bind(0, sgre::scene::MutableTextureStage::Pixel);
}
static void TestChannelsAndQueuedUpdates(const std::filesystem::path& shader) {
	for (const std::uint32_t format : std::array<std::uint32_t, 3>{28, 87, 88}) {
		sgre::render::SpritePassRenderer renderer(Description(), shader);
		auto backend = renderer.CreateMutableTextureBackend();
		sgre::scene::MutableTextureOwner texture(*backend, Spec(format));
		const auto vertices = Quad();
		const auto pipeline = Pipeline(renderer);
		Fill(texture, format == 28 ? std::array<std::uint8_t, 4>{17, 73, 191, 101} : std::array<std::uint8_t, 4>{191, 73, 17, 101});
		const auto draw = Draw(renderer, pipeline, vertices);
		const auto submission = renderer.Submit(std::span(&draw, 1), kProjection);
		Fill(texture, format == 28 ? std::array<std::uint8_t, 4>{211, 29, 83, 53} : std::array<std::uint8_t, 4>{83, 29, 211, 53});
		const auto first = renderer.Capture(submission);
		const auto second = Render(renderer, draw);
		for (std::size_t index = 0; index < 4; ++index) {
			RequirePixel(first, index, {17, 73, 191, static_cast<std::uint8_t>(format == 88 ? 255 : 101)});
			RequirePixel(second, index, {211, 29, 83, static_cast<std::uint8_t>(format == 88 ? 255 : 53)});
		}
		Require(texture.State().Description.OriginalFormat == format, "mutable Metal resource identity was relabeled as a compressed asset");
	}
}
static void TestCachedSampler(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	auto backend = renderer.CreateMutableTextureBackend();
	sgre::scene::MutableTextureOwner texture(*backend, Spec());
	const auto mapped = texture.Map();
	for (std::size_t row = 0; row < 2; ++row) {
		mapped.Bytes[row * 8] = std::byte{255};
		mapped.Bytes[row * 8 + 3] = std::byte{255};
		mapped.Bytes[row * 8 + 6] = std::byte{255};
		mapped.Bytes[row * 8 + 7] = std::byte{255};
	}
	texture.Unmap();
	texture.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	const auto center = Quad();
	const auto pipeline = Pipeline(renderer);
	const auto draw = Draw(renderer, pipeline, center);
	RequirePixel(Render(renderer, draw), 0, {0, 0, 255, 255});
	texture.SetFiltering(1, 1);
	texture.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	RequirePixel(Render(renderer, draw), 0, {0, 0, 255, 255});
	texture.SetAddress(0, 0);
	texture.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	RequirePixel(Render(renderer, draw), 0, {128, 0, 128, 255}, 1);
	const auto repeated = Quad(1.25f);
	const auto repeated_draw = Draw(renderer, pipeline, repeated);
	texture.SetFiltering(0, 0);
	texture.SetAddress(1, 0);
	texture.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	RequirePixel(Render(renderer, repeated_draw), 0, {255, 0, 0, 255});
}
static void TestInitialRowsAndAggregateLimit(const std::filesystem::path& shader) {
	for (const std::uint32_t format : std::array<std::uint32_t, 3>{28, 87, 88}) {
		sgre::render::SpritePassRenderer renderer(Description(), shader);
		auto backend = renderer.CreateMutableTextureBackend();
		const std::array<std::array<std::uint8_t, 4>, 4> expected{{{255, 0, 0, 127}, {0, 255, 0, 63}, {0, 0, 255, 191}, {255, 255, 0, 31}}};
		std::array<std::byte, 24> source{};
		for (std::size_t index = 0; index < 4; ++index) {
			const auto offset = (index / 2) * 12 + (index % 2) * 4;
			for (std::size_t channel = 0; channel < 4; ++channel) {
				const auto lane = format != 28 && channel != 1 && channel != 3 ? 2 - channel : channel;
				source[offset + channel] = static_cast<std::byte>(expected[index][lane]);
			}
		}
		sgre::scene::MutableTextureOwner texture(*backend, Spec(format), {source, 12});
		texture.Bind(0, sgre::scene::MutableTextureStage::Pixel);
		auto vertices = Quad();
		vertices[0].Uv = {0, 0};
		vertices[1].Uv = {1, 0};
		vertices[2].Uv = {0, 1};
		vertices[3].Uv = {1, 1};
		const auto draw = Draw(renderer, Pipeline(renderer), vertices);
		const auto pixels = Render(renderer, draw);
		for (std::size_t index = 0; index < 4; ++index) {
			auto pixel = expected[index];
			if (format == 88) {
				pixel[3] = 255;
			}
			RequirePixel(pixels, index, pixel);
		}
	}
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	auto backend = renderer.CreateMutableTextureBackend();
	auto first = std::make_unique<sgre::scene::MutableTextureOwner>(*backend, Spec());
	auto second = std::make_unique<sgre::scene::MutableTextureOwner>(*backend, Spec());
	RequireError([&]() {
		sgre::scene::MutableTextureOwner third(*backend, Spec());
	},
	    "aggregate allocation");
	first.reset();
	sgre::scene::MutableTextureOwner replacement(*backend, Spec());
}
static void TestLifetimeAndGeneration(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	auto backend = renderer.CreateMutableTextureBackend();
	auto texture = std::make_unique<sgre::scene::MutableTextureOwner>(*backend, Spec());
	Fill(*texture, {31, 97, 167, 255});
	const auto vertices = Quad();
	const auto pipeline = Pipeline(renderer);
	const auto draw = Draw(renderer, pipeline, vertices);
	const auto submission = renderer.Submit(std::span(&draw, 1), kProjection);
	RequireError([&]() {
		renderer.ReleaseTexture(draw.Texture);
	},
	    "actual owner");
	texture.reset();
	RequireError([&]() {
		static_cast<void>(renderer.MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel));
	},
	    "stale");
	RequireError([&]() {
		static_cast<void>(renderer.Submit(std::span(&draw, 1), kProjection));
	},
	    "stale");
	RequirePixel(renderer.Capture(submission), 0, {31, 97, 167, 255});
	auto replacement = std::make_unique<sgre::scene::MutableTextureOwner>(*backend, Spec());
	Fill(*replacement, {73, 149, 223, 255});
	const auto replacement_draw = Draw(renderer, pipeline, vertices);
	Require(replacement_draw.Texture.Index == draw.Texture.Index && replacement_draw.Texture.Generation != draw.Texture.Generation, "mutable Metal replacement failed to advance the borrowed handle generation");
	RequirePixel(Render(renderer, replacement_draw), 0, {73, 149, 223, 255});
	sgre::render::SpritePassRenderer other(Description(), shader);
	RequireError([&]() {
		static_cast<void>(other.Submit(std::span(&replacement_draw, 1), kProjection));
	},
	    "another renderer");
	RequireError([&]() {
		replacement->Bind(256, sgre::scene::MutableTextureStage::Pixel);
	},
	    "slot exceeds");
	RequireError([&]() {
		static_cast<void>(renderer.MutableTextureBinding(0, static_cast<sgre::scene::MutableTextureStage>(2)));
	},
	    "shader stage");
}
static void TestBackendLimitsAndClosedRenderer(const std::filesystem::path& shader) {
	auto description = Description();
	description.MaxTextureBytes = 512;
	sgre::render::SpritePassRenderer limited(description, shader);
	auto limited_backend = limited.CreateMutableTextureBackend();
	RequireError([&]() {
		sgre::scene::MutableTextureOwner texture(*limited_backend, Spec());
	},
	    "allocation limit");
	description = Description();
	description.MaxTextures = 1;
	auto renderer = std::make_unique<sgre::render::SpritePassRenderer>(description, shader);
	auto backend = renderer->CreateMutableTextureBackend();
	auto texture = std::make_unique<sgre::scene::MutableTextureOwner>(*backend, Spec());
	RequireError([&]() {
		sgre::scene::MutableTextureOwner another(*backend, Spec());
	},
	    "texture count");
	auto unsupported = Spec();
	unsupported.Storage = sgre::scene::MutableTextureStorage::DynamicWrite;
	RequireError([&]() {
		sgre::scene::MutableTextureOwner another(*backend, unsupported);
	},
	    "default sampled");
	unsupported = Spec(61);
	RequireError([&]() {
		sgre::scene::MutableTextureOwner another(*backend, unsupported);
	},
	    "original RGBA8");
	unsupported = Spec();
	unsupported.Description.MipLevels = 2;
	RequireError([&]() {
		sgre::scene::MutableTextureOwner another(*backend, unsupported);
	},
	    "single-mip");
	renderer.reset();
	RequireError([&]() {
		texture->Bind(0, sgre::scene::MutableTextureStage::Pixel);
	},
	    "renderer has closed");
	RequireError([&]() {
		sgre::scene::MutableTextureOwner another(*backend, Spec());
	},
	    "renderer has closed");
	texture.reset();
}
int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("expected the readable sprite shader source path");
		}
		const std::filesystem::path shader(argv[1]);
		TestChannelsAndQueuedUpdates(shader);
		TestCachedSampler(shader);
		TestInitialRowsAndAggregateLimit(shader);
		TestLifetimeAndGeneration(shader);
		TestBackendLimitsAndClosedRenderer(shader);
		std::cout << "Mutable Metal format channels, queued updates, native sampler cache and exclusive lifetime passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
