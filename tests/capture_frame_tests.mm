#include "sgre/render/graphics_frame_cycle.hpp"

#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/sprite_frame_encoder.hpp"
#include "sgre/scene/capture_task.hpp"
#include "sgre/scene/layer_manager_frame.hpp"
#include "sgre/scene/raw_texture_packet.hpp"
#include "sgre/scene/screens.hpp"
#include "sgre/scene/sprite_pipeline.hpp"

#include "mutable_texture_backend.hpp"
#include "sprite_presentation_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected real Capture frame rejection: " + std::string(expected));
}

static sgre::render::SpritePassDescription Description(std::uint32_t extent = 8) {
	sgre::render::SpritePassDescription description;
	description.Width = extent;
	description.Height = extent;
	description.MaxVertices = 256;
	description.MaxIndices = 512;
	description.MaxDraws = 16;
	description.MaxTextures = 32;
	description.MaxPipelines = 128;
	description.MaxTextureBytes = 65536;

	return description;
}

static void RequirePixel(const sgre::render::ColorPassPixels& pixels, std::uint32_t x, std::uint32_t y, const std::array<std::uint8_t, 4>& expected) {
	Require(x < pixels.Width && y < pixels.Height, "Capture pixel observation exceeds its actual readback");
	const auto start = (static_cast<std::size_t>(y) * pixels.Width + x) * 4;
	for (std::size_t channel = 0; channel < expected.size(); ++channel) {
		if (pixels.Rgba[start + channel] != expected[channel]) {
			throw std::runtime_error("Capture pixel x=" + std::to_string(x) + " y=" + std::to_string(y) + " channel=" + std::to_string(channel) + " expected=" + std::to_string(expected[channel]) + " actual=" + std::to_string(pixels.Rgba[start + channel]));
		}
	}
}

static void RequireAllPixels(const sgre::render::ColorPassPixels& pixels, const std::array<std::uint8_t, 4>& expected) {
	for (std::uint32_t y = 0; y < pixels.Height; ++y) {
		for (std::uint32_t x = 0; x < pixels.Width; ++x) {
			RequirePixel(pixels, x, y, expected);
		}
	}
}

static sgre::scene::FillRectangleDrawPacket Rectangle(float left, float top, float right, float bottom, std::uint32_t color) {
	sgre::scene::FillRectangleDrawPacket packet;
	packet.Vertices = {{{left, top}, {left, bottom}, {right, top}, {right, bottom}}};
	packet.Color = color;

	return packet;
}

static sgre::render::SpritePipelineHandle PrepareSamplePipeline(sgre::render::SpritePassRenderer& renderer) {
	sgre::scene::MotionSpriteMaterial material;
	material.BlendMode = 0;
	material.TextureAttributes = 4;
	auto pipeline = sgre::scene::SelectSpritePipeline(material, {});
	pipeline.NativeVertexShaderIndex = 4;

	return renderer.PreparePipeline(pipeline);
}

static constexpr std::array<float, 16> kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static sgre::render::ColorPassPixels SampleHandle(sgre::render::SpritePassRenderer& destination, sgre::render::SpritePipelineHandle pipeline, sgre::render::SpriteTextureHandle actual_companion) {
	const auto packet = sgre::scene::PrepareRawTextureDrawPacket({-1, -1}, {2, 2}, 0xffffffff);
	sgre::render::SpritePassDraw draw;
	draw.Pipeline = pipeline;
	draw.Texture = actual_companion;
	draw.RawTextureVertices = packet.Vertices;
	draw.RawTextureColor = packet.Color;
	const auto submission = destination.Submit(std::span(&draw, 1), kIdentity);

	return destination.Capture(submission);
}

static sgre::render::SpriteTextureHandle BindActualCompanion(sgre::render::SpritePassRenderer& primary, sgre::scene::CaptureTask& task) {
	auto& texture = task.Texture();
	Require(texture.State().Storage == sgre::scene::MutableTextureStorage::StagingCompanion && texture.State().Dirty && !texture.State().HasView && texture.Companion() != nullptr, "Capture must write its actual nonsampled staging MAIN and leave the sampled companion dirty until Bind");
	texture.Bind(0, sgre::scene::MutableTextureStage::Pixel);
	const auto handle = primary.MutableTextureBinding(0, sgre::scene::MutableTextureStage::Pixel);
	Require(handle == sgre::render::SpriteMutableTextureHandle(texture.Companion()->Resource()) && !texture.State().Dirty, "actual Bind must copy MAIN to this owner's genuine sampled companion");

	return handle;
}

class CaptureObserver final : public sgre::scene::CaptureTaskObserver {
public:
	void CaptureResized(sgre::resource::MotionPoint size) noexcept override {
		++Resizes;
		Size = size;
	}
	void CaptureInvalidated() noexcept override {
		++Invalidations;
	}
	void CaptureRetired() noexcept override {
		++Retirements;
	}
	sgre::resource::MotionPoint Size;
	std::size_t Resizes = 0;
	std::size_t Invalidations = 0;
	std::size_t Retirements = 0;
};

static void DrawSplitSource(sgre::render::GraphicsFrameCycle& cycle) {
	cycle.DrawFillRectangle(Rectangle(-4, -4, 0, 4, 0xffff0000));
	cycle.DrawFillRectangle(Rectangle(0, -4, 4, 4, 0xff0000ff));
}

static void TestActualCycleCapture(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 3);
	const auto primary = graphics.PrimaryTarget();
	const auto sample_target = graphics.AllocateTarget(Description(4));
	const auto sample_pipeline = PrepareSamplePipeline(*primary);
	sgre::render::SpritePassPresentation surface(*primary, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("Capture fixture must use its real frame target, not an uploaded substitute image");
	},
	    [](auto) {
		    throw std::runtime_error("Capture fixture owns no immutable source image");
	    });
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, cache, {}, {});
	static_cast<void>(cycle.CaptureBackend());
	cycle.PrepareFillRectangle();
	CaptureObserver observer;
	CaptureObserver retained_observer;
	auto task = std::make_shared<sgre::scene::CaptureTask>(4, 4);
	task->SetOffset({4, 4});
	task->AddConsumer(observer);
	try {
		RequireFailure([&] {
			cycle.DrawCapture(task);
		},
		    "active primary encoder");
		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
		DrawSplitSource(cycle);
		cycle.DrawCapture(task);
		Require(task.use_count() == 2 && task->State().Captured && task->State().FramePending && !task->CanClose(), "actual Cycle retains the exact physical task before its first allocation/copy and through frame submission");
		RequireFailure([&] {
			task->Close();
		},
		    "End or Abort");
		cycle.DrawFillRectangle(Rectangle(-4, -4, 0, 0, 0xff00ff00));
		cycle.PostFrame();
		Require(task.use_count() == 1 && !task->State().FramePending && task->CanClose() && observer.Resizes == 1, "real Post releases only after the actual encoder End");
		Require(cycle.CurrentCounters().DrawOperations == 3 && cycle.CurrentCounters().SubmittedElements == 12, "Capture copy must not invent a materialized quad or extra logical draw record");
		RequirePixel(primary->Capture(cycle.LastSubmission()->Identity), 1, 1, {0, 255, 0, 255});
		const auto old_companion = BindActualCompanion(*primary, *task);
		RequireAllPixels(SampleHandle(*sample_target, sample_pipeline, old_companion), {255, 0, 0, 255});
		const auto* old_owner = &task->Texture();

		task->SetOffset({0, 4});
		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
		DrawSplitSource(cycle);
		cycle.DrawCapture(task);
		Require(observer.Resizes == 2 && &task->Texture() != old_owner && old_owner->State().HasCompanion && task->State().PhysicalBounds.MinimumX == 4, "changed rounded position recreates the actual owner despite identical dimensions and keeps displaced owner valid until Post");
		cycle.PostFrame();
		RequireFailure([&] {
			static_cast<void>(SampleHandle(*sample_target, sample_pipeline, old_companion));
		},
		    "stale");
		const auto right_companion = BindActualCompanion(*primary, *task);
		RequireAllPixels(SampleHandle(*sample_target, sample_pipeline, right_companion), {0, 0, 255, 255});

		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
		cycle.DrawFillRectangle(Rectangle(-4, -4, 4, 4, 0xff00ff00));
		cycle.DrawCapture(task);
		cycle.DrawCapture(task);
		Require(task.use_count() == 2 && observer.Resizes == 2, "repeated native Capture draw in one frame reuses one physical allocation and one containing retention");
		cycle.AbortFrame();
		Require(!cycle.LastSubmission() && !task->State().Captured && !task->State().FramePending && task.use_count() == 1 && observer.Invalidations == 1, "real Abort publishes no submission and invalidates uncommitted readiness before releasing the exact task");
		RequireFailure([&] {
			static_cast<void>(task->Texture());
		},
		    "preceding framebuffer copy");

		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
		cycle.DrawFillRectangle(Rectangle(-4, -4, 4, 4, 0xff00ff00));
		cycle.DrawCapture(task);
		cycle.PostFrame();
		Require(observer.Resizes == 2, "fresh copy after Abort reuses the exact bounded staging pair");
		RequireAllPixels(SampleHandle(*sample_target, sample_pipeline, BindActualCompanion(*primary, *task)), {0, 255, 0, 255});
		task->Close();
		Require(observer.Retirements == 1, "physical Capture retirement clears actual consumer backlink");
		task.reset();

		auto retained = std::make_shared<sgre::scene::CaptureTask>(4, 4);
		retained->SetOffset({4, 4});
		retained->AddConsumer(retained_observer);
		const std::weak_ptr<sgre::scene::CaptureTask> weak = retained;
		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
		DrawSplitSource(cycle);
		cycle.DrawCapture(retained);
		retained.reset();
		Require(!weak.expired(), "actual Cycle strongly retains a physical Capture after caller drops its last shared owner");
		cycle.PostFrame();
		Require(weak.expired() && retained_observer.Retirements == 1, "Post retires the final physical owner only after real retained Metal submission");
		static_cast<void>(primary->Capture(cycle.LastSubmission()->Identity));
	} catch (...) {
		cycle.AbortFrame();
		if (task) {
			task->Close();
		}
		throw;
	}
	cycle.Close();
	workers.Close();
	const auto observation = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observation.WindowVisible && !observation.WindowKey && !observation.Presenting && observation.DrawableRequests == 0, "Capture frame fixture requested a visible window or drawable");
}

static sgre::scene::CaptureFrameContext ActualFrameContext() {
	sgre::scene::CaptureFrameContext frame;
	frame.View = sgre::scene::PrepareRenderTargetLayout(8, 8, 1, 0).Projection;
	frame.Viewport = {8, 8};
	frame.NativeTargetType = 1;

	return frame;
}

static void TestSameBufferMainCompanionBinding(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	const auto sample_pipeline = PrepareSamplePipeline(renderer);
	sgre::scene::MotionSpriteMaterial material;
	material.BlendMode = 0;
	auto fill = sgre::scene::SelectSpritePipeline(material, {});
	fill.NativeVertexShaderIndex = 3;
	fill.NativePixelShaderIndex = 60;
	fill.VertexColor = sgre::scene::SpriteVertexColor::Multiply;
	const auto fill_pipeline = renderer.PreparePipeline(fill);
	auto backend = renderer.CreateMutableTextureBackend();
	sgre::render::SpriteFrameEncoder encoder(renderer);
	sgre::scene::CaptureTask task(4, 4);
	task.SetOffset({4, 4});
	const auto allocate = [&](const auto& spec) {
		return encoder.PrepareCaptureTexture(*backend, spec);
	};
	const auto copy = [&](auto& destination, const auto& bounds) {
		encoder.CopyCaptureCurrentTarget(destination, bounds);
	};
	const auto fill_packet = Rectangle(-1, -1, 1, 1, 0xff00ff00);
	sgre::render::SpritePassDraw green;
	green.Pipeline = fill_pipeline;
	green.FillRectangleVertices = fill_packet.Vertices;
	green.FillRectangleColor = fill_packet.Color;
	const auto sampled_packet = sgre::scene::PrepareRawTextureDrawPacket({0, -1}, {1, 1}, 0xffffffff);
	sgre::render::SpritePassDraw sampled;
	sampled.Pipeline = sample_pipeline;
	sampled.RawTextureVertices = sampled_packet.Vertices;
	sampled.RawTextureColor = sampled_packet.Color;
	try {
		encoder.Begin({1, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
		encoder.SetScissor({0, 0, 8, 8});
		task.Draw(*backend, ActualFrameContext(), allocate, copy);
		Require(task.Texture().State().Dirty, "same-buffer Capture main write must mark its actual companion copy pending");
		encoder.Append(green, kIdentity);
		sampled.Texture = encoder.PrepareMutableTexture(task.Texture(), 0, sgre::scene::MutableTextureStage::Pixel);
		Require(sampled.Texture == sgre::render::SpriteMutableTextureHandle(task.Texture().Companion()->Resource()) && !task.Texture().State().Dirty, "actual same-buffer PrepareMutableTexture must bind genuine copied companion");
		encoder.Append(sampled, kIdentity);
		const auto submission = encoder.End();
		task.ReleaseFrame(true);
		const auto pixels = renderer.Capture(submission);
		RequirePixel(pixels, 1, 1, {0, 255, 0, 255});
		RequirePixel(pixels, 6, 6, {255, 0, 0, 255});

		encoder.Begin({0, 0, 1, 1}, sgre::render::SpritePassLoadAction::Clear);
		encoder.SetScissor({0, 0, 8, 8});
		task.Draw(*backend, ActualFrameContext(), allocate, copy);
		auto& aborted_owner = task.Texture();
		static_cast<void>(encoder.PrepareMutableTexture(aborted_owner, 0, sgre::scene::MutableTextureStage::Pixel));
		encoder.Abort();
		task.ReleaseFrame(false);
		RequireFailure([&] {
			aborted_owner.Bind(0, sgre::scene::MutableTextureStage::Pixel);
		},
		    "fresh real GPU copy");
		RequireFailure([&] {
			static_cast<void>(task.Texture());
		},
		    "preceding framebuffer copy");

		encoder.Begin({0, 1, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
		encoder.SetScissor({0, 0, 8, 8});
		task.Draw(*backend, ActualFrameContext(), allocate, copy);
		sampled.Texture = encoder.PrepareMutableTexture(task.Texture(), 0, sgre::scene::MutableTextureStage::Pixel);
		encoder.Append(sampled, kIdentity);
		const auto recovered = encoder.End();
		task.ReleaseFrame(true);
		RequireAllPixels(renderer.Capture(recovered), {0, 255, 0, 255});
		task.Close();
	} catch (...) {
		encoder.Abort();
		task.ReleaseFrame(false);
		throw;
	}
}

static void TestActualCurrentScreenSource(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 4);
	const auto primary = graphics.PrimaryTarget();
	const auto screen = graphics.AllocateTarget(Description(4));
	const auto sample = graphics.AllocateTarget(Description(4));
	const auto sample_pipeline = PrepareSamplePipeline(*primary);
	sgre::render::SpritePassPresentation surface(*primary, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("Screen Capture must not replace the current target with an uploaded image");
	},
	    [](auto) {
	    });
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, cache, {}, {});
	static_cast<void>(cycle.CaptureBackend());
	cycle.PrepareFillRectangle();
	auto task = std::make_shared<sgre::scene::CaptureTask>(4, 4);
	task->SetOffset({2, 2});
	sgre::scene::LayerTree tree(4);
	const auto root = tree.CreateScreenRoot();
	const auto child = tree.CreateBaseInScreen(root);
	sgre::scene::LayerManagerFrameOperations operations;
	operations.State = [](auto) {
		return sgre::scene::LayerFrameProcessState{};
	};
	operations.Update = [](auto) {
	};
	operations.PrepareDraw = [&] {
		cycle.ResetRootCamera();
	};
	operations.Draw = [&](auto actual_child, auto argument) {
		Require(actual_child == child && argument == 0 && cycle.FrameState().Viewport.Width == 4, "actual Screen manager must lend its own child/camera/Draw0 scope");
		cycle.DrawCapture(task);
	};
	sgre::scene::LayerManagerFrame manager(tree, root, std::move(operations));
	sgre::scene::ScreenState state;
	state.Description.Width = 4;
	state.Description.Height = 4;
	state.LayerRoot = root;
	state.BackgroundColor = {0, 0, 255, 255};
	try {
		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
		cycle.DrawFillRectangle(Rectangle(-4, -4, 4, 4, 0xffff0000));
		cycle.DrawScreen(*screen, manager, state);
		Require(cycle.FrameState().Viewport.Width == 8 && task->State().PhysicalBounds.MaximumX == 4, "Capture inside Screen uses its actual four-pixel target then restores primary camera");
		cycle.PostFrame();
		RequireAllPixels(primary->Capture(cycle.LastSubmission()->Identity), {255, 0, 0, 255});
		RequireAllPixels(SampleHandle(*sample, sample_pipeline, BindActualCompanion(*primary, *task)), {0, 0, 255, 255});
		task->Close();
		task.reset();
	} catch (...) {
		cycle.AbortFrame();
		task->Close();
		throw;
	}
	tree.Release(child);
	tree.ReleaseScreenRoot(root);
	cycle.Close();
	workers.Close();
	const auto observation = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observation.WindowVisible && !observation.WindowKey && observation.DrawableRequests == 0, "nested Screen Capture must remain hidden without a drawable");
}

static void TestActualThumbnailReadback(const std::filesystem::path& shader) {
	sgre::render::SpritePassRenderer renderer(Description(), shader);
	sgre::scene::MotionSpriteMaterial material;
	auto pipeline = sgre::scene::SelectSpritePipeline(material, {});
	pipeline.NativeVertexShaderIndex = 3;
	pipeline.NativePixelShaderIndex = 60;
	pipeline.VertexColor = sgre::scene::SpriteVertexColor::Multiply;
	const auto prepared = renderer.PreparePipeline(pipeline);
	auto backend = renderer.CreateMutableTextureBackend();
	sgre::render::SpriteFrameEncoder encoder(renderer);
	sgre::scene::CaptureTask task(8, 8);
	task.SetOffset({4, 4});
	try {
		encoder.Begin({}, sgre::render::SpritePassLoadAction::Clear);
		const std::array rectangles{Rectangle(-1, 1, 0, 0, 0xffff0000), Rectangle(0, 1, 1, 0, 0xff00ff00), Rectangle(-1, 0, 0, -1, 0xff0000ff), Rectangle(0, 0, 1, -1, 0xffffff00)};
		for (const auto& rectangle : rectangles) {
			sgre::render::SpritePassDraw draw;
			draw.Pipeline = prepared;
			draw.FillRectangleVertices = rectangle.Vertices;
			draw.FillRectangleColor = rectangle.Color;
			encoder.Append(draw, kIdentity);
		}
		task.Draw(*backend, ActualFrameContext(), [&](const auto& spec) {
			return encoder.PrepareCaptureTexture(*backend, spec);
		},
		    [&](auto& destination, const auto& bounds) {
			    encoder.CopyCaptureCurrentTarget(destination, bounds);
		    });
		RequireFailure([&] {
			static_cast<void>(renderer.CaptureThumbnail(task.Texture(), 2, 2));
		},
		    "encoding");
		static_cast<void>(encoder.End());
		task.ReleaseFrame(true);
		// Overwriting the primary must not change the earlier actual Capture snapshot.
		static_cast<void>(renderer.Submit({}, kIdentity, {1, 1, 1, 1}));
		const auto thumbnail = renderer.CaptureThumbnail(task.Texture(), 2, 2);
		RequirePixel(thumbnail, 0, 0, {255, 0, 0, 255});
		RequirePixel(thumbnail, 1, 0, {0, 255, 0, 255});
		RequirePixel(thumbnail, 0, 1, {0, 0, 255, 255});
		RequirePixel(thumbnail, 1, 1, {255, 255, 0, 255});
		const auto center = renderer.CaptureThumbnail(task.Texture(), 1, 1);
		RequirePixel(center, 0, 0, {128, 128, 64, 255});
		RequireFailure([&] {
			static_cast<void>(renderer.CaptureThumbnail(task.Texture(), 0, 2));
		},
		    "positive dimensions");
		sgre::render::SpritePassRenderer foreign(Description(), shader);
		RequireFailure([&] {
			static_cast<void>(foreign.CaptureThumbnail(task.Texture(), 2, 2));
		},
		    "resource domain");
		task.Close();
	} catch (...) {
		encoder.Abort();
		task.ReleaseFrame(false);
		throw;
	}
}

int main(int argc, char** argv) {
	@try {
		try {
			Require(argc == 3, "usage: capture_frame_tests sprite.metal sprite_present.metal");
			@autoreleasepool {
				const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
				TestActualCycleCapture(argv[1], argv[2]);
				TestActualThumbnailReadback(argv[1]);
				TestSameBufferMainCompanionBinding(argv[1]);
				TestActualCurrentScreenSource(argv[1], argv[2]);
				Require(NSApp.windows.count == 0 && !NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "Capture verification changed foreground/activation or retained a window");
			}
			std::cout << "PASS actual Cycle/Metal Capture MAIN-companion snapshot pixels, exact task/Post/Abort retention, position replacement, same-buffer Bind and current Screen source\n";
			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	} @catch (NSException* error) {
		std::cerr << "Capture fixture Objective-C failure: " << error.reason.UTF8String << '\n';
		return 1;
	}
}
