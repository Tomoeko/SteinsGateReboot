#include "sgre/render/sprite_frame_encoder.hpp"

#include "sgre/render/sprite_graphics_camera.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/render/sprite_host_inputs.hpp"
#include "sgre/scene/layer_camera_draw.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

void TestActualPresentationFence(sgre::render::SpritePassRenderer& renderer, sgre::render::SpriteFrameEncoder& encoder, std::uint64_t accepted_submission);

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, const char* message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()).find(message) != std::string::npos, error.what());

		return;
	}

	throw std::runtime_error(std::string("required live-frame failure absent: ") + message);
}

static sgre::render::SpritePassDescription Description() {
	sgre::render::SpritePassDescription result;
	result.Width = 4;
	result.Height = 4;
	result.MaxVertices = 16;
	result.MaxIndices = 16;
	result.MaxDraws = 4;
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

static std::array<sgre::scene::MotionSpriteVertex, 4> Vertices() {
	std::array<sgre::scene::MotionSpriteVertex, 4> result;
	result[0].Position = {-2, -2};
	result[1].Position = {2, -2};
	result[2].Position = {-2, 2};
	result[3].Position = {2, 2};
	for (auto& vertex : result) {
		vertex.Uv = {.5f, .5f};
		vertex.Color = 0xffffffff;
	}

	return result;
}

static void TestActualLiveScissorCameraAndHeap(const std::filesystem::path& shader) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader);
	auto renderer = graphics.PrimaryTarget();
	auto other = graphics.AllocateTarget(Description());
	static_cast<void>(graphics.SelectTarget(renderer));
	sgre::scene::GraphicsModelStack models(4);
	models.Reset();
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	camera.ResetDefaultCamera();
	const auto transform = camera.OrdinaryShaderRegisters();
	const auto red = Texture(*renderer, {255, 0, 0, 255});
	const auto blue = Texture(*renderer, {0, 0, 255, 255});
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = renderer->PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	auto vertices = Vertices();
	const std::array<std::uint32_t, 4> indices{0, 2, 1, 3};
	sgre::render::FrameHostHeap heap(1);
	heap.Begin();
	sgre::render::SpriteHostInputs host(heap, vertices, indices);
	sgre::render::SpritePassDraw draw;
	draw.Texture = red;
	draw.Pipeline = pipeline;
	draw.Vertices = vertices;
	draw.Indices = indices;
	sgre::render::SpriteFrameEncoder encoder(*renderer);
	sgre::render::SpriteFrameEncoder duplicate(*renderer);
	sgre::scene::LayerCameraFrameState actual_state;
	actual_state.View = camera.Projection();
	actual_state.Viewport = {4, 4};
	actual_state.RawScissor = graphics.CurrentLayout().FullScissor;
	actual_state.RoundedScissor = actual_state.RawScissor;
	actual_state.AppliedRoundedScissor = actual_state.RawScissor;
	std::size_t physical_scissors = 0;
	sgre::scene::LayerCameraDrawScope scope(models, actual_state, [&](const auto& rectangle) {
		encoder.SetScissor(rectangle);
		++physical_scissors;
	});
	sgre::scene::ParallelTaskSet children(1);
	sgre::scene::ParallelTaskCallbacks child;
	child.Draw = [&](auto, auto argument) {
		Require(argument == 17 && actual_state.RawScissor.MinimumX == 2, "real camera child did not see the committed right-half scissor");
		draw.Texture = blue;
		draw.HostInputs = &host;
		draw.Vertices = {};
		draw.Indices = {};
		encoder.Append(draw, camera.OrdinaryShaderRegisters());
	};
	static_cast<void>(children.Append(std::move(child)));
	encoder.Begin({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	Require(encoder.IsActive() && !renderer->CanSubmit() && !other->CanSubmit(), "actual open encoder did not fence the shared graphics submission context");
	RequireFailure([&] {
		duplicate.Begin({}, sgre::render::SpritePassLoadAction::Clear);
	},
	    "quiescent successful live encoding");
	Require(encoder.IsActive(), "rejected competing encoder abandoned the actual owner");
	RequireFailure([&] {
		static_cast<void>(other->Submit({}, transform));
	},
	    "quiescent successful live encoding");
	for (const auto& target : {std::shared_ptr<sgre::render::SpritePassRenderer>{}, renderer, other}) {
		RequireFailure([&] {
			static_cast<void>(graphics.SelectTarget(target));
		},
		    "quiescent successful live encoding");
		Require(graphics.CurrentTarget() == renderer, "rejected live target selection published a different current owner");
	}
	RequireFailure([&] {
		static_cast<void>(graphics.AllocateTarget(Description()));
	},
	    "quiescent successful live encoding");
	RequireFailure([&] {
		sgre::render::SpritePassRenderer shared(Description(), *renderer);
	},
	    "quiescent successful live encoding");
	bool foreign_rejected = false;
	std::thread foreign([&] {
		try {
			encoder.SetScissor({0, 0, 4, 4});
		} catch (const std::runtime_error&) {
			foreign_rejected = true;
		}
	});
	foreign.join();
	Require(foreign_rejected, "foreign thread reached the live system encoder");
	encoder.Append(draw, transform);
	sgre::scene::LayerCameraParameters clip;
	clip.Clip = {0, -2, 2, 2};
	scope.Draw(clip, children, 17);
	Require(physical_scissors == 2 && actual_state.RawScissor.MinimumX == 0 && models.Depth() == 1, "real encoder camera apply/restore and model restoration differ");
	heap.End();
	heap.Begin();
	vertices.fill({});
	const auto identity = encoder.End();
	Require(!encoder.IsActive() && renderer->CanSubmit(), "completed command encoding kept the shared context locked");
	const auto pixels = renderer->Capture(identity);
	Require(pixels.Rgba.size() == 64, "live encoder capture extent differs");
	for (std::size_t pixel = 0; pixel < 16; ++pixel) {
		const auto offset = pixel * 4;
		const bool left = pixel % 4 < 2;
		Require(pixels.Rgba[offset] == (left ? 255 : 0) && pixels.Rgba[offset + 1] == 0 && pixels.Rgba[offset + 2] == (left ? 0 : 255) && pixels.Rgba[offset + 3] == 255, "actual live encoder lost camera clipping, immediate heap ownership or draw order");
	}
	// Encoding is uncommitted until End; a rejected later draw must leave the accepted physical target unchanged.
	vertices = Vertices();
	draw.HostInputs = nullptr;
	draw.Vertices = vertices;
	draw.Indices = indices;
	encoder.Begin({}, sgre::render::SpritePassLoadAction::Preserve);
	encoder.Append(draw, transform);
	draw.Scissor = sgre::render::SpritePassScissor{0, 0, 4, 4};
	RequireFailure([&] {
		encoder.Append(draw, transform);
	},
	    "without a per-draw override");
	RequireFailure([&] {
		static_cast<void>(encoder.End());
	},
	    "without a per-draw override");
	encoder.Abort();
	Require(renderer->Capture(identity).Rgba == pixels.Rgba, "abandoned encoding published an unaccepted target or identity");
	draw.Scissor.reset();
	TestActualPresentationFence(*renderer, encoder, identity);

	actual_state.RawScissor = {0, 0, 4, 4};
	actual_state.RoundedScissor = actual_state.RawScissor;
	actual_state.AppliedRoundedScissor = actual_state.RawScissor;
	sgre::scene::LayerCameraDrawScope second(models, actual_state, [&](const auto& rectangle) {
		encoder.SetScissor(rectangle);
		++physical_scissors;
	});
	sgre::scene::ParallelTaskSet inner(1);
	sgre::scene::ParallelTaskSet outer(1);
	child = {};
	child.Draw = [&](auto, auto) {
		Require(actual_state.RawScissor.MinimumX > 2 && physical_scissors == 3, "rounded cache did not suppress the second fractional native integer binding");
		encoder.Append(draw, transform);
	};
	static_cast<void>(inner.Append(std::move(child)));
	child = {};
	child.Draw = [&](auto, auto) {
		second.DrawClip({.1f, -2, 2, 2}, inner, 0);
	};
	static_cast<void>(outer.Append(std::move(child)));
	encoder.Begin({0, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
	scope.DrawClip({-.1f, -2, 2, 2}, outer, 0);
	Require(physical_scissors == 4, "native apply/cache suppression/restore issued the wrong number of real system scissor binds");
	const auto fractional = encoder.End();
	const auto fractional_pixels = renderer->Capture(fractional);
	for (std::size_t pixel = 0; pixel < 16; ++pixel) {
		const auto offset = pixel * 4;
		Require(fractional_pixels.Rgba[offset] == 0 && fractional_pixels.Rgba[offset + 1] == 0 && fractional_pixels.Rgba[offset + 2] == (pixel % 4 == 0 ? 0 : 255) && fractional_pixels.Rgba[offset + 3] == 255, "GPU scissors followed rounded host coordinates instead of the retained native truncated device state");
	}
}

static void TestEmptyScissorCoverage(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	const auto red = Texture(renderer, {255, 0, 0, 255});
	const auto blue = Texture(renderer, {0, 0, 255, 255});
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Bc7;

	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = renderer.PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	const auto vertices = Vertices();
	const std::array<std::uint32_t, 4> indices{0, 2, 1, 3};
	const std::array<float, 16> transform{.5f, 0, 0, 0, 0, .5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	sgre::render::SpritePassDraw draw;
	draw.Pipeline = pipeline;
	draw.Vertices = vertices;
	draw.Indices = indices;
	sgre::render::SpriteFrameEncoder encoder(renderer);
	const std::array<sgre::scene::MotionIntegerScissor, 7> empty_rectangles{{{0, 0, 0, 0}, {4, 4, 4, 4}, {2, 1, 2, 3}, {0, 4, 4, 4}, {-8, -8, -1, -1}, {10, 1, 13, 3}, {3, 3, 1, 1}}};
	for (const auto& rectangle : empty_rectangles) {
		encoder.Begin({0, 1, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
		encoder.SetScissor(rectangle);
		const auto retained = encoder.SubmittedDeviceScissor();
		Require(retained.MinimumX == rectangle.MinimumX && retained.MinimumY == rectangle.MinimumY && retained.MaximumX == rectangle.MaximumX && retained.MaximumY == rectangle.MaximumY, "empty intersection changed the original submitted rectangle");
		draw.Texture = red;
		encoder.Append(draw, transform);
		const auto empty = renderer.Capture(encoder.End());
		for (std::size_t pixel = 0; pixel < 16; ++pixel) {
			const auto offset = pixel * 4;
			Require(empty.Rgba[offset] == 0 && empty.Rgba[offset + 1] == 255 && empty.Rgba[offset + 2] == 0 && empty.Rgba[offset + 3] == 255, "empty scissors emitted pixels or suppressed the render-pass clear");
		}

		encoder.Begin({}, sgre::render::SpritePassLoadAction::Preserve);
		encoder.SetScissor(rectangle);
		encoder.Append(draw, transform);
		encoder.SetScissor({1, 1, 3, 3});
		draw.Texture = blue;
		encoder.Append(draw, transform);
		const auto restored = renderer.Capture(encoder.End());
		for (std::size_t pixel = 0; pixel < 16; ++pixel) {
			const auto offset = pixel * 4;
			const bool inside = pixel % 4 >= 1 && pixel % 4 < 3 && pixel / 4 >= 1 && pixel / 4 < 3;
			Require(restored.Rgba[offset] == 0 && restored.Rgba[offset + 1] == (inside ? 0 : 255) && restored.Rgba[offset + 2] == (inside ? 255 : 0) && restored.Rgba[offset + 3] == 255, "positive scissor following zero coverage lost clipping or preserved target content");
		}
	}

	encoder.Begin({}, sgre::render::SpritePassLoadAction::Clear);
	draw.Texture = red;
	encoder.Append(draw, transform);
	const auto reset = renderer.Capture(encoder.End());
	for (std::size_t pixel = 0; pixel < 16; ++pixel) {
		const auto offset = pixel * 4;
		Require(reset.Rgba[offset] == 255 && reset.Rgba[offset + 1] == 0 && reset.Rgba[offset + 2] == 0 && reset.Rgba[offset + 3] == 255, "new frame retained an earlier empty scissor");
	}
}

static void TestRendererRetirementBeforeUncommittedEncoder(const std::filesystem::path& shader) {
	auto renderer = std::make_unique<sgre::render::SpritePassRenderer>(Description(), shader);
	sgre::render::SpriteFrameEncoder encoder(*renderer);
	encoder.Begin({}, sgre::render::SpritePassLoadAction::Clear);
	renderer.reset();
	RequireFailure([&] {
		encoder.SetScissor({0, 0, 4, 4});
	},
	    "renderer has retired");
	encoder.Abort();
	Require(!encoder.IsActive(), "retired receiver left an unsafe dangling active frame slot");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: live_metal_frame_tests shader");
		TestActualLiveScissorCameraAndHeap(argv[1]);
		TestEmptyScissorCoverage(argv[1]);
		TestRendererRetirementBeforeUncommittedEncoder(argv[1]);
		std::cout << "Actual live Metal scissor/camera/heap encoding, native rounded-cache GPU behavior, rejected-frame preservation and exclusive/expired owner guards passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
