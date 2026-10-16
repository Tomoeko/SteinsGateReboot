#include "sgre/render/sprite_graphics_camera.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/scene/default_render_model.hpp"
#include "sgre/scene/render_target_layout.hpp"

#include <array>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void RequireFailure(const auto& operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("expected actual camera owner rejection");
}

static sgre::render::SpritePassDescription Description(std::uint32_t size) {
	sgre::render::SpritePassDescription result;
	result.Width = size;
	result.Height = size;
	result.MaxVertices = 4;
	result.MaxIndices = 4;
	result.MaxDraws = 1;
	result.MaxTextures = 1;
	result.MaxPipelines = 1;
	result.MaxTextureBytes = 4;

	return result;
}

static void Draw(sgre::render::SpriteGraphicsTargets& graphics, sgre::render::SpriteGraphicsCamera& camera, sgre::render::SpriteTextureHandle texture, sgre::render::SpritePipelineHandle pipeline) {
	const auto target = graphics.CurrentTarget();
	const auto extent = target->Description();
	const auto width = static_cast<float>(extent.Width) * 0.5f;
	const auto height = static_cast<float>(extent.Height) * 0.5f;
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-width, -height};
	vertices[1].Position = {width, -height};
	vertices[2].Position = {-width, height};
	vertices[3].Position = {width, height};
	for (auto& vertex : vertices) {
		vertex.Color = 0xffffffff;
		vertex.Uv = {.5f, .5f};
	}
	const std::array<std::uint32_t, 4> indices{0, 2, 1, 3};
	sgre::render::SpritePassDraw draw;
	draw.Texture = texture;
	draw.Pipeline = pipeline;
	draw.Vertices = vertices;
	draw.Indices = indices;
	const auto submission = graphics.Submit(std::span(&draw, 1), camera.OrdinaryShaderRegisters());
	const auto pixels = target->Capture(submission.Identity);
	for (std::size_t offset = 0; offset < pixels.Rgba.size(); offset += 4) {
		Require(pixels.Rgba[offset] == 255 && pixels.Rgba[offset + 1] == 0 && pixels.Rgba[offset + 2] == 255 && pixels.Rgba[offset + 3] == 255, "real default camera did not cover the selected physical target");
	}
}

static void TestCamera(const std::filesystem::path& shader) {
	sgre::render::SpriteGraphicsTargets graphics(Description(4), shader, 2);
	sgre::scene::GraphicsModelStack models(8);
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	RequireFailure([&] {
		camera.ResetDefaultCamera();
	});
	auto root = graphics.PrimaryTarget();
	auto child = graphics.AllocateTarget(Description(3));
	static_cast<void>(graphics.SelectTarget(root));
	RequireFailure([&] {
		camera.ResetDefaultCamera();
	});
	models.Reset();
	RequireFailure([&] {
		static_cast<void>(camera.OrdinaryShaderRegisters());
	});
	camera.ResetDefaultCamera();
	Require(camera.Projection().Coefficients[0] == .5f && models.Current().Coefficients[5] == -1 && models.Current().Coefficients[11] == -1, "root reset did not consume actual target projection and default model");

	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 0, 255, 255};
	const auto texture = root->UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = root->PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	Draw(graphics, camera, texture, pipeline);
	static_cast<void>(graphics.SelectTarget(child));
	RequireFailure([&] {
		static_cast<void>(camera.Projection());
	});
	camera.ResetDefaultCamera();
	Draw(graphics, camera, texture, pipeline);
	static_cast<void>(graphics.SelectTarget(root));
	RequireFailure([&] {
		static_cast<void>(camera.OrdinaryShaderRegisters());
	});
	camera.ResetDefaultCamera();
	Draw(graphics, camera, texture, pipeline);
	static_cast<void>(graphics.SelectTarget({}));
	RequireFailure([&] {
		static_cast<void>(camera.Projection());
	});
}

static std::uint32_t ReadWord(std::istream& input) {
	std::array<unsigned char, 4> bytes;
	Require(static_cast<bool>(input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())), "native camera fixture is truncated");

	return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

static void TestNative(const std::filesystem::path& fixture) {
	std::ifstream input(fixture, std::ios::binary);
	Require(ReadWord(input) == 0x314d4143 && ReadWord(input) == 6, "native camera fixture header differs");
	for (std::size_t case_index = 0; case_index < 6; ++case_index) {
		const auto width = ReadWord(input);
		const auto height = ReadWord(input);
		const auto projection = sgre::scene::PrepareRenderTargetLayout(width, height, 1, 0).Projection;
		const auto model = sgre::scene::PrepareDefaultRenderModel();
		sgre::scene::MotionShaderTransform transform;
		transform.Graphics.Rows = projection.Coefficients;
		transform.Current.Rows = model.Coefficients;
		const auto combined = sgre::scene::PrepareMotionShaderMatrix(transform);
		for (const auto& expected : {projection.Coefficients, model.Coefficients}) {
			for (const auto value : expected) {
				Require(std::bit_cast<std::uint32_t>(value) == ReadWord(input), "default camera projection/model word differs from original instructions");
			}
		}
		for (std::size_t index = 0; index < 12; ++index) {
			Require(std::bit_cast<std::uint32_t>(combined[index]) == ReadWord(input), "ordinary camera shader composition word differs from original instructions");
		}
	}
	Require(input.peek() == std::char_traits<char>::eof(), "native camera fixture contains trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2 || argc == 3, "usage: camera_tests sprite_shader [native_fixture]");
		TestCamera(argv[1]);
		if (argc == 3) {
			TestNative(argv[2]);
		}
		std::cout << "actual target camera/default model/live stack and selected physical Metal pixels pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
