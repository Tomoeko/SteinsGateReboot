#include "sgre/render/sprite_frame_encoder.hpp"

#include "metal_support.hpp"
#include "sprite_presentation_bridge.hpp"

#import <dispatch/dispatch.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static constexpr std::array<std::uint32_t, 4> kIndices{0, 2, 1, 3};
static constexpr std::array<std::uint32_t, 4> kColors{0xff0000ff, 0x00ff00ff, 0x0000ffff, 0x808080ff};
static constexpr std::array<float, 16> kProjection{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0.5f, 0, 0, 0, 1};

struct QueueGate {
	id<MTLSharedEvent> Event = nil;

	~QueueGate() noexcept {
		@try {
			// An assertion must never leave a genuine GPU command waiting on this fixture's event.
			Event.signaledValue = 1;
		} @catch (NSException*) {
		}
	}
};

static void TestActualSlotPressure(const std::filesystem::path& shader, bool initial_encoder_frames) {
	sgre::render::SpritePassDescription description;
	description.Width = 8;
	description.Height = 8;
	description.MaxVertices = 4;
	description.MaxIndices = 4;
	description.MaxDraws = 1;
	description.MaxTextures = 1;
	description.MaxPipelines = 1;
	sgre::render::SpritePassRenderer renderer(description, shader);
	sgre::render::SpriteFrameEncoder encoder(renderer);
	const auto source = sgre::render::SpritePassPresentationBridge::CreateSource(renderer);
	sgre::resource::RgbaImage image;
	image.Width = 1;
	image.Height = 1;
	image.Pixels = {255, 255, 255, 255};
	const auto texture = renderer.UploadTexture(sgre::resource::PixelFormat::Luminance8, image);
	sgre::scene::MotionSpriteMaterial material;
	material.TextureFormat = sgre::resource::PixelFormat::Luminance8;
	material.TextureAttributes = 0;
	// The default0x10 doubles vertex RGB; these authored exact-color checks exercise ordinary mode0.
	material.BlendMode = 0;
	sgre::scene::SpritePipelineOptions options;
	options.EnableBlending = false;
	const auto pipeline = renderer.PreparePipeline(sgre::scene::SelectSpritePipeline(material, options));
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-1, 1};
	vertices[1].Position = {1, 1};
	vertices[2].Position = {-1, -1};
	vertices[3].Position = {1, -1};
	for (auto& vertex : vertices) {
		vertex.Uv = {.5f, .5f};
	}
	sgre::render::SpritePassDraw draw;
	draw.Texture = texture;
	draw.Pipeline = pipeline;
	draw.Vertices = vertices;
	draw.Indices = kIndices;
	std::array<id<MTLBuffer>, 4> readbacks;
	for (auto& readback : readbacks) {
		readback = [source.Device newBufferWithLength:256 * description.Height options:MTLResourceStorageModeShared];
		Require(readback != nil, "real slot fixture readback allocation failed");
	}
	QueueGate gate{[source.Device newSharedEvent]};
	Require(gate.Event != nil && gate.Event.signaledValue == 0, "real shared GPU gate allocation failed");
	id<MTLCommandBuffer> blocker = [source.Queue commandBuffer];
	Require(blocker != nil, "real shared GPU gate command allocation failed");
	[blocker encodeWaitForEvent:gate.Event value:1];
	[blocker commit];
	dispatch_semaphore_t last_readback = dispatch_semaphore_create(0);
	dispatch_semaphore_t released = dispatch_semaphore_create(0);
	Require(last_readback != nil && released != nil, "real slot fixture OS completion allocation failed");
	std::array<id<MTLCommandBuffer>, 4> producers;
	std::array<id<MTLCommandBuffer>, 4> copies;
	for (std::size_t frame = 0; frame < 4; ++frame) {
		for (auto& vertex : vertices) {
			vertex.Color = kColors[frame];
		}
		if (frame == 3) {
			Require(gate.Event.signaledValue == 0, "fixture GPU gate opened before filling all three actual upload slots");
			Require(!renderer.CanSubmit(), "all three genuine upload slots must be occupied before the fourth-frame wait");
			for (const auto producer : producers) {
				if (producer != nil) {
					Require(producer.status != MTLCommandBufferStatusCompleted && producer.status != MTLCommandBufferStatusError, "real producer completed before the deliberate fourth-frame pressure");
				}
			}
			id<MTLSharedEvent> actual_gate = gate.Event;
			// A test-only OS deadline releases real GPU work; production pacing never sleeps or uses this timer.
			dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100'000'000), dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
			  @try {
				  actual_gate.signaledValue = 1;
			  } @catch (NSException*) {
			  }
			  dispatch_semaphore_signal(released);
			});
		}
		const bool use_encoder = frame < 3 ? initial_encoder_frames : !initial_encoder_frames;
		std::uint64_t submission = 0;
		if (use_encoder) {
			encoder.Begin({}, sgre::render::SpritePassLoadAction::Clear);
			encoder.Append(draw, kProjection);
			submission = encoder.End();
		} else {
			submission = renderer.Submit(std::span(&draw, 1), kProjection);
		}
		const auto actual = source.RequireLatest(submission);
		Require(actual.Target.pixelFormat == MTLPixelFormatRGBA8Unorm, "raw slot readback requires the renderer's actual RGBA8 target");
		producers[frame] = actual.Producer;
		id<MTLCommandBuffer> copy = [source.Queue commandBuffer];
		id<MTLBlitCommandEncoder> blit = [copy blitCommandEncoder];
		Require(copy != nil && blit != nil, "real slot readback command allocation failed");
		[blit copyFromTexture:actual.Target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(8, 8, 1) toBuffer:readbacks[frame] destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:2048];
		[blit endEncoding];
		if (frame == 3) {
			[copy addCompletedHandler:^(id<MTLCommandBuffer> completed) {
			  static_cast<void>(completed);
			  dispatch_semaphore_signal(last_readback);
			}];
		}
		copies[frame] = copy;
		[copy commit];
	}
	Require(dispatch_semaphore_wait(released, dispatch_time(DISPATCH_TIME_NOW, 5'000'000'000)) == 0 && gate.Event.signaledValue == 1, "actual shared GPU gate release failed");
	Require(dispatch_semaphore_wait(last_readback, dispatch_time(DISPATCH_TIME_NOW, 5'000'000'000)) == 0, "actual queued slot readbacks did not complete");
	for (std::size_t frame = 0; frame < 4; ++frame) {
		Require(producers[frame].status == MTLCommandBufferStatusCompleted && copies[frame].status == MTLCommandBufferStatusCompleted, "real gated slot commands failed");
		const auto* pixels = static_cast<const std::uint8_t*>(readbacks[frame].contents);
		Require(pixels != nullptr, "real slot readback bytes are absent");
		for (std::size_t y = 0; y < 8; ++y) {
			for (std::size_t x = 0; x < 8; ++x) {
				for (std::size_t channel = 0; channel < 4; ++channel) {
					const auto expected = static_cast<std::uint8_t>(kColors[frame] >> (24 - channel * 8));
					const auto actual = pixels[y * 256 + x * 4 + channel];
					if (actual != expected) {
						throw std::runtime_error("upload slot pixel mismatch: initialFrames=" + std::string(initial_encoder_frames ? "encoder" : "Submit") + " frame=" + std::to_string(frame) + " x=" + std::to_string(x) + " y=" + std::to_string(y) + " channel=" + std::to_string(channel) + " format=RGBA8Unorm expected=" + std::to_string(expected) + " actual=" + std::to_string(actual) + " producerStatus=" + std::to_string(static_cast<std::uint32_t>(producers[frame].status)) + " copyStatus=" + std::to_string(static_cast<std::uint32_t>(copies[frame].status)));
					}
				}
			}
		}
	}
	encoder.Begin({}, sgre::render::SpritePassLoadAction::Clear);
	encoder.Append(draw, kProjection);
	encoder.Abort();
	encoder.Begin({}, sgre::render::SpritePassLoadAction::Clear);
	encoder.Append(draw, kProjection);
	const auto pixels = renderer.Capture(encoder.End());
	Require(pixels.Rgba.size() == 256 && pixels.Rgba[0] == 128 && pixels.Rgba[1] == 128 && pixels.Rgba[2] == 128 && pixels.Rgba[3] == 255, "uncommitted abort consumed or fabricated a fixed-slot completion signal");
	renderer.ReleaseTexture(texture);
}

int main(int argc, char** argv) {
	@autoreleasepool {
		try {
			Require(argc == 2, "usage: sprite_upload_slot_pacing_tests sprite_shader");
			TestActualSlotPressure(argv[1], false);
			TestActualSlotPressure(argv[1], true);
			std::cout << "actual three-slot completion backpressure, mixed Submit/encoder fourth frame, per-submission contrasting readbacks and uncommitted abort reuse passed\n";

			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';

			return 1;
		}
	}
}
