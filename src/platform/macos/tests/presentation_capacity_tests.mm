#include "sgre/render/sprite_presentation.hpp"

#include "sgre/core/thread_task_frame.hpp"
#include "sgre/render/frame_host_heap.hpp"
#include "sgre/render/sprite_frame_encoder.hpp"
#include "sgre/render/sprite_host_inputs.hpp"

#include "sprite_presentation_fixture.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(fragment) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("missing actual capacity-owner rejection");
}

static sgre::render::SpritePassDescription RendererDescription() {
	sgre::render::SpritePassDescription result;
	result.Width = 4;
	result.Height = 4;
	result.MaxVertices = 8;
	result.MaxIndices = 8;
	result.MaxDraws = 2;
	result.MaxTextures = 2;
	result.MaxPipelines = 2;
	result.MaxTextureBytes = 8;

	return result;
}

static void RequireNoDrawable(const sgre::render::SpritePassPresentation& presentation) {
	const auto state = sgre::render::SpritePassPresentationFixtureBridge::Observe(presentation);
	Require(!state.ApplicationActive && !state.WindowVisible && !state.WindowKey && !state.WindowMain && state.ViewAttached && state.LayerAttached, "actual capacity owner did not remain attached and hidden");
	Require(state.DrawableRequests == 0 && state.RetainedDrawables == 0 && state.RetainedTransfers == 0 && !state.Presenting, "capacity metadata requested a drawable or created a transfer");
}

static void TestPolicies(sgre::render::SpritePassRenderer& renderer, const std::filesystem::path& shader) {
	const auto application_before = NSApp;
	const auto windows_before = NSApp.windows.count;
	for (const auto invalid : {0u, 1u, 4u}) {
		sgre::render::SpritePresentationDescription description;
		description.BufferCount = invalid;
		RequireFailure([&] {
			sgre::render::SpritePassPresentation rejected(renderer, shader, description);
		},
		    "capacity must be two or three");
		Require(NSApp == application_before && NSApp.windows.count == windows_before, "invalid capacity published an actual application/window owner");
	}
	{
		sgre::render::SpritePassPresentation diagnostic(renderer, shader);
		Require(diagnostic.DrawableCapacity() == 3, "existing diagnostic constructor changed its actual layer capacity");
		RequireNoDrawable(diagnostic);
	}
	{
		sgre::render::SpritePresentationDescription description;
		description.BufferCount = 3;
		sgre::render::SpritePassPresentation explicit_three(renderer, shader, description);
		Require(explicit_three.DrawableCapacity() == 3, "explicit nondefault layer capacity was lost");
		RequireNoDrawable(explicit_three);
	}
	Require(NSApp.windows.count == 0, "capacity policy owners retained an actual hidden window");
}

static void TestMetadata(sgre::render::SpritePassPresentation& presentation) {
	Require(presentation.DrawableCapacity() == 2, "native configuration was not read from the actual layer");
	RequireFailure([&] {
		static_cast<void>(presentation.CurrentSubmission());
	},
	    "actual initialized Submit");
	RequireNoDrawable(presentation);

	const auto objects = sgre::render::SpritePassPresentationFixtureBridge::BorrowObjects(presentation);
	objects.Layer.maximumDrawableCount = 3;
	RequireFailure([&] {
		static_cast<void>(presentation.DrawableCapacity());
	},
	    "drawable capacity changed");
	objects.Layer.maximumDrawableCount = 2;
	Require(presentation.DrawableCapacity() == 2, "correctable metadata tampering faulted the actual owner");

	id<MTLDevice> actual_device = objects.Layer.device;
	objects.Layer.device = nil;
	RequireFailure([&] {
		static_cast<void>(presentation.DrawableCapacity());
	},
	    "layer device, format or physical geometry changed");
	objects.Layer.device = actual_device;
	Require(presentation.DrawableCapacity() == 2, "actual device restoration failed its metadata query");

	std::exception_ptr worker_failure;
	std::thread worker([&] {
		try {
			static_cast<void>(presentation.DrawableCapacity());
		} catch (...) {
			worker_failure = std::current_exception();
		}
	});
	worker.join();
	Require(worker_failure != nullptr, "foreign-thread capacity query was admitted");
	RequireFailure([&] {
		std::rethrow_exception(worker_failure);
	},
	    "requires the main thread");
	RequireNoDrawable(presentation);
}

static void TestActualHostCopy(sgre::render::SpritePassRenderer& renderer, sgre::render::SpritePassPresentation& presentation) {
	sgre::render::FrameHostHeap heap(presentation.DrawableCapacity());
	heap.Begin();
	std::array<sgre::scene::MotionSpriteVertex, 4> vertices;
	vertices[0].Position = {-2, -2};
	vertices[1].Position = {2, -2};
	vertices[2].Position = {-2, 2};
	vertices[3].Position = {2, 2};
	for (auto& vertex : vertices) {
		vertex.Color = 0xffffffff;
		vertex.Uv = {0, 0};
	}
	const std::array<std::uint32_t, 4> indices{0, 2, 1, 3};
	std::unique_ptr<sgre::render::SpriteHostInputs> inputs;
	std::thread::id writer_thread;
	// Actual host time supplies only this fixture's worker-cost service; no native clock units are claimed.
	sgre::core::ThreadTaskFrame workers([] {
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	},
	    1, 1);
	sgre::core::ThreadTaskReceiver receiver(workers, [&](auto&) {
		writer_thread = std::this_thread::get_id();
		inputs = std::make_unique<sgre::render::SpriteHostInputs>(heap, vertices, indices);
	},
	    [](auto&) {
		    throw std::runtime_error("capacity fixture did not queue a Post callback");
	    });
	receiver.QueueUpdate();
	const auto queued_writer = receiver.State();
	workers.RunFrame();
	Require(queued_writer.UpdatePending, "actual writer did not enter its owner's pending queue");
	Require(inputs && writer_thread != std::this_thread::get_id() && !receiver.State().UpdatePending && !receiver.State().Executing, "actual writer did not join before its draw-copy borrow");

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
	sgre::render::SpritePassDraw draw;
	draw.Texture = texture;
	draw.Pipeline = pipeline;
	draw.HostInputs = inputs.get();
	const std::array<float, 16> projection{.5f, 0, 0, 0, 0, .5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	sgre::render::SpriteFrameEncoder encoder(renderer);
	encoder.Begin({}, sgre::render::SpritePassLoadAction::Clear);
	RequireFailure([&] {
		static_cast<void>(presentation.DrawableCapacity());
	},
	    "quiescent successful live encoding");
	encoder.Append(draw, projection);
	const auto submission = encoder.End();
	heap.End();
	Require(heap.ResourceEntryByte() == 1 && presentation.DrawableCapacity() == 2, "completed CPU phases changed metadata or the native entry byte");

	// Real writer completion and Append's synchronous copies precede recycling; GPU slots retain independent commands.
	heap.Begin();
	heap.Begin();
	RequireFailure([&] {
		static_cast<void>(inputs->VertexBytes());
	},
	    "physical page has recycled");
	const auto pixels = renderer.Capture(submission);
	Require(pixels.Width == 4 && pixels.Height == 4 && pixels.Rgba.size() == 64, "actual copied target dimensions changed");
	for (std::size_t pixel = 0; pixel < 16; ++pixel) {
		Require(pixels.Rgba[pixel * 4] == 255 && pixels.Rgba[pixel * 4 + 1] == 0 && pixels.Rgba[pixel * 4 + 2] == 0 && pixels.Rgba[pixel * 4 + 3] == 255, "actual upload copy changed after host-page recycling");
	}
	RequireNoDrawable(presentation);
	inputs.reset();
	receiver.Close();
	workers.Close();
	heap.Close();
	renderer.ReleaseTexture(texture);
}

static void TestExpiredSource(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	auto renderer = std::make_unique<sgre::render::SpritePassRenderer>(RendererDescription(), shader);
	auto presentation = std::make_unique<sgre::render::SpritePassPresentation>(*renderer, transfer, sgre::render::SpritePresentationDescription{});
	Require(presentation->DrawableCapacity() == 2, "fresh weak source failed its uninitialized metadata query");
	renderer.reset();
	RequireFailure([&] {
		static_cast<void>(presentation->DrawableCapacity());
	},
	    "renderer has closed");
	RequireNoDrawable(*presentation);
	presentation.reset();
	Require(NSApp.windows.count == 0, "expired renderer left its presentation window attached");
}

int main(int argc, char** argv) {
	@try {
		try {
			Require(argc == 3, "usage: presentation-drawable-capacity SPRITE_SHADER TRANSFER_SHADER");
			@autoreleasepool {
				Require([NSThread isMainThread], "capacity fixture requires its actual main-thread owner");
				const auto foreground_pid = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
				Require(foreground_pid > 0, "capacity fixture has no actual foreground witness");
				sgre::render::SpritePassRenderer renderer(RendererDescription(), argv[1]);
				TestPolicies(renderer, argv[2]);
				{
					sgre::render::SpritePassPresentation presentation(renderer, argv[2], sgre::render::SpritePresentationDescription{});
					TestMetadata(presentation);
					TestActualHostCopy(renderer, presentation);
					RequireNoDrawable(presentation);
				}
				TestExpiredSource(argv[1], argv[2]);
				Require(NSApp.windows.count == 0 && !NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground_pid, "actual hidden owner changed foreground or retained a window");
				std::cout << "{\"status\":\"PASS_ACTUAL_LAYER_CAPACITY_AND_HOST_COPY\",\"native_policy_capacity\":2,\"diagnostic_capacity\":3,\"actual_worker_writes\":1,\"actual_offscreen_pixels\":16,\"drawable_requests\":0,\"present_calls\":0,\"full_frame_or_display_fidelity\":false}\n";
			}

			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';

			return 1;
		}
	} @catch (NSException* exception) {
		std::cerr << "capacity fixture Objective-C exception: " << exception.reason.UTF8String << '\n';

		return 1;
	}
}
