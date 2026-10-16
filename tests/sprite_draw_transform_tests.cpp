#include "sgre/render/sprite_graphics_camera.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/render/sprite_host_inputs.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 4;
	result.Height = 4;
	result.MaxVertices = 8;
	result.MaxIndices = 8;
	result.MaxDraws = 2;
	result.MaxTextures = 2;
	result.MaxPipelines = 1;
	result.MaxTextureBytes = 8;

	return result;
}

static sgre::render::SpriteTextureHandle Texture(sgre::render::SpritePassRenderer& renderer, std::array<std::uint8_t, 4> color) {
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels.assign(color.begin(), color.end());

	return renderer.UploadTexture(sgre::resource::PixelFormat::Bc7, image);
}

static void TestActualModels(const std::filesystem::path& shader) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader);
	auto renderer = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(renderer));
	sgre::scene::GraphicsModelStack models(4);
	models.Reset();
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	camera.ResetDefaultCamera();
	const auto parent = models.Current();
	const auto common = camera.OrdinaryShaderRegisters();
	const auto red = Texture(*renderer, {255, 0, 0, 255});
	const auto blue = Texture(*renderer, {0, 0, 255, 255});
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = renderer->PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-2, -2};
	vertices[1].Position = {2, -2};
	vertices[2].Position = {-2, 2};
	vertices[3].Position = {2, 2};
	for (auto& vertex : vertices) {
		vertex.Uv = {.5f, .5f};
		vertex.Color = 0xffffffff;
	}
	const std::array<std::uint32_t, 4> indices{0, 2, 1, 3};
	sgre::render::FrameHostHeap heap(1);
	heap.Begin();
	sgre::render::SpriteHostInputs host_inputs(heap, vertices, indices);
	std::array<sgre::render::SpritePassDraw, 2> draws;
	for (std::size_t index = 0; index < draws.size(); ++index) {
		auto model = parent;
		model.Coefficients[0] = .5f;
		model.Coefficients[3] = index == 0 ? -1.0f : 1.0f;
		models.SetCurrent(model);
		draws[index].Texture = index == 0 ? red : blue;
		draws[index].Pipeline = pipeline;
		draws[index].Vertices = vertices;
		draws[index].Indices = indices;
		draws[index].TransformRegisters = camera.OrdinaryShaderRegisters();
	}
	draws[1].HostInputs = &host_inputs;
	draws[1].Vertices = {};
	draws[1].Indices = {};
	const auto submission = graphics.Submit(draws, common);
	heap.End();
	heap.Begin();
	// Real upload-slot storage must retain both snapshots after the caller changes its current model and records.
	models.SetCurrent(parent);
	for (auto& draw : draws) {
		draw.TransformRegisters->fill(0);
	}
	const auto pixels = renderer->Capture(submission.Identity);
	Require(pixels.Rgba.size() == 64, "per-draw capture extent differs");
	for (std::size_t pixel = 0; pixel < 16; ++pixel) {
		const bool left = pixel % 4 < 2;
		const auto offset = pixel * 4;
		Require(pixels.Rgba[offset] == (left ? 255 : 0) && pixels.Rgba[offset + 1] == 0 && pixels.Rgba[offset + 2] == (left ? 0 : 255) && pixels.Rgba[offset + 3] == 255, "one real batch did not retain its distinct actual left/right camera matrices");
	}

	for (auto& draw : draws) {
		draw.TransformRegisters.reset();
		draw.HostInputs = nullptr;
		draw.Vertices = vertices;
		draw.Indices = indices;
	}
	const auto legacy = graphics.Submit(draws, common);
	const auto legacy_pixels = renderer->Capture(legacy.Identity);
	for (std::size_t offset = 0; offset < legacy_pixels.Rgba.size(); offset += 4) {
		Require(legacy_pixels.Rgba[offset] == 0 && legacy_pixels.Rgba[offset + 1] == 0 && legacy_pixels.Rgba[offset + 2] == 255 && legacy_pixels.Rgba[offset + 3] == 255, "legacy containing matrix fallback no longer covers both draws");
	}
	draws[0].TransformRegisters = common;
	(*draws[0].TransformRegisters)[7] = std::numeric_limits<float>::quiet_NaN();
	bool rejected = false;
	try {
		static_cast<void>(graphics.Submit(draws, common));
	} catch (const std::runtime_error& error) {
		rejected = std::string(error.what()).find("finite shader registers") != std::string::npos;
	}
	Require(rejected, "invalid per-draw registers must reject before real submission identity changes");
	Require(renderer->Capture(legacy.Identity).Rgba == legacy_pixels.Rgba, "invalid transform must preserve the actual latest target submission and pixels");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: sprite_draw_transform_tests shader");
		TestActualModels(argv[1]);
		std::cout << "Distinct actual per-draw model snapshots, upload retention, legacy fallback and finite rejection pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
