#include "sgre/render/sprite_pass.hpp"
#include "sgre/scene/default_render_model.hpp"
#include "sgre/scene/motion_shader_program.hpp"
#include "sgre/scene/render_target_layout.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static std::array<float, 16> NativeResetProjection() {
	const auto model = sgre::scene::PrepareDefaultRenderModel();
	const std::array<std::uint32_t, 12> expected{0x3f800000, 0, 0, 0x80000000, 0, 0xbf800000, 0, 0x80000000, 0, 0, 0xbf800000, 0xbf800000};
	for (std::size_t index = 0; index < expected.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(model.Coefficients[index]) == expected[index], "default reset model differs from original words");
	}
	const auto layout = sgre::scene::PrepareRenderTargetLayout(8, 8, 0, 0);
	sgre::scene::MotionShaderTransform transform;
	transform.Graphics.Rows = layout.Projection.Coefficients;
	transform.Current.Rows = model.Coefficients;
	const auto projection = sgre::scene::PrepareMotionShaderMatrix(transform);
	Require(projection[11] < 0 && projection[15] == 1, "actual default reset must discriminate clip-depth state");

	return projection;
}

static void TestPixels(const std::filesystem::path& shader_source) {
	sgre::render::SpritePassDescription description;
	description.Width = 8;
	description.Height = 8;
	description.MaxVertices = 4;
	description.MaxIndices = 4;
	description.MaxDraws = 1;
	sgre::render::SpritePassRenderer renderer(description, shader_source);
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 0, 0, 255};
	const auto texture = renderer.UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = renderer.PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-4, 4};
	vertices[1].Position = {4, 4};
	vertices[2].Position = {-4, -4};
	vertices[3].Position = {4, -4};
	for (auto& vertex : vertices) {
		vertex.Color = 0xffffffff;
		vertex.Uv = {0.5f, 0.5f};
	}
	const std::array<std::uint32_t, 4> indices{0, 2, 1, 3};
	sgre::render::SpritePassDraw draw;
	draw.Texture = texture;
	draw.Pipeline = pipeline;
	draw.Vertices = vertices;
	draw.Indices = indices;
	const auto native_projection = NativeResetProjection();
	for (const auto depth : {native_projection[11], -2.0f, 0.5f, 2.0f}) {
		auto projection = native_projection;
		projection[11] = depth;
		const auto submission = renderer.Submit(std::span(&draw, 1), projection, {0, 1, 0, 1});
		const auto pixels = renderer.Capture(submission);
		Require(pixels.Rgba.size() == 8 * 8 * 4, "depth-clip pixel extent differs");
		for (std::size_t offset = 0; offset < pixels.Rgba.size(); offset += 4) {
			Require(pixels.Rgba[offset] == 255 && pixels.Rgba[offset + 1] == 0 && pixels.Rgba[offset + 2] == 0 && pixels.Rgba[offset + 3] == 255, "native depth-clip-disabled sprite must cover the target");
		}
	}
	renderer.ReleaseTexture(texture);
	std::cout << "four actual Metal depth-clamp projections retain sprite coverage\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: sprite_depth_clip_tests shader_source");
		TestPixels(argv[1]);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
