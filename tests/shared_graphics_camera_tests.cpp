#include "sgre/render/sprite_graphics_camera.hpp"

#include "sgre/render/sprite_frame_encoder.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/scene/layer_camera_draw.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <utility>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestActualSharedView(const std::filesystem::path& shader) {
	sgre::render::SpritePassDescription description;
	description.Width = 4;
	description.Height = 4;
	description.MaxVertices = 8;
	description.MaxIndices = 8;
	description.MaxDraws = 2;
	description.MaxTextures = 2;
	description.MaxPipelines = 1;
	description.MaxTextureBytes = 8;
	sgre::render::SpriteGraphicsTargets graphics(description, shader);
	const auto target = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(target));
	sgre::scene::GraphicsModelStack models(4);
	models.Reset();
	sgre::scene::LayerCameraFrameState frame;
	frame.Viewport = {4, 4};
	frame.RawScissor = graphics.CurrentLayout().FullScissor;
	frame.RoundedScissor = frame.RawScissor;
	frame.AppliedRoundedScissor = frame.RawScissor;
	sgre::render::SpriteGraphicsCamera camera(graphics, models, frame);
	camera.ResetDefaultCamera();
	Require(frame.View.Coefficients[0] == .5f, "default reset did not publish the selected target's actual shared view");
	sgre::render::SpriteFrameEncoder encoder(*target);
	std::size_t scissor_calls = 0;
	sgre::scene::LayerCameraDrawScope scope(models, frame, [&](const auto& rectangle) {
		encoder.SetScissor(rectangle);
		++scissor_calls;
	});
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 0, 0, 255};
	const auto red = target->UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	image.Pixels = {0, 0, 255, 255};
	const auto blue = target->UploadTexture(sgre::resource::PixelFormat::Bc7, image);
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = target->PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
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
	sgre::render::SpritePassDraw draw;
	draw.Texture = red;
	draw.Pipeline = pipeline;
	draw.Vertices = vertices;
	draw.Indices = indices;
	sgre::scene::ParallelTaskSet children(1);
	sgre::scene::ParallelTaskCallbacks child;
	child.Draw = [&](auto, auto argument) {
		Require(argument == 91 && frame.RawScissor.MinimumX == 1 && frame.RawScissor.MaximumX == 3, "real clipping traversal did not consume the changed shared view");
		Require(camera.OrdinaryShaderRegisters()[0] == .25f, "shader registers did not consume the same changed shared view");
		draw.Texture = blue;
		encoder.Append(draw, camera.OrdinaryShaderRegisters());
	};
	static_cast<void>(children.Append(std::move(child)));
	encoder.Begin({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	encoder.Append(draw, camera.OrdinaryShaderRegisters());
	frame.View.Coefficients[0] = .25f;
	scope.DrawClip({-2, -2, 2, 2}, children, 91);
	Require(scissor_calls == 2 && models.Depth() == 1, "actual shared-view clipping did not restore its physical scissor and model owner");
	const auto identity = encoder.End();
	const auto pixels = target->Capture(identity);
	Require(pixels.Rgba.size() == 64, "shared camera fixture target extent differs");
	for (std::size_t pixel = 0; pixel < 16; ++pixel) {
		const auto offset = pixel * 4;
		const bool middle = pixel % 4 == 1 || pixel % 4 == 2;
		Require(pixels.Rgba[offset] == (middle ? 0 : 255) && pixels.Rgba[offset + 1] == 0 && pixels.Rgba[offset + 2] == (middle ? 255 : 0) && pixels.Rgba[offset + 3] == 255, "real GPU projection/clipping read different frame views");
	}
	camera.ResetDefaultCamera();
	Require(scope.State().View.Coefficients[0] == .5f, "genuine root projection reset did not reach its already constructed clip scope");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: shared_graphics_camera_tests shader");
		TestActualSharedView(argv[1]);
		std::cout << "Shared actual camera projection, live clip traversal and16 independent GPU pixels passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
