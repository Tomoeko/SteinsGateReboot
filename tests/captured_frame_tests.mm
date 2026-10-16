#include "sgre/render/graphics_frame_cycle.hpp"

#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/sprite_frame_encoder.hpp"
#include "sgre/scene/capture_task.hpp"
#include "sgre/scene/captured_task.hpp"
#include "sgre/scene/layer_manager_frame.hpp"
#include "sgre/scene/parallel_task_set.hpp"
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

class CaptureCompletionObserver final : public sgre::scene::CaptureTaskObserver {
public:
	void CaptureResized(sgre::resource::MotionPoint size) noexcept override {
		++Resizes;
		Size = size;
	}
	void CaptureInvalidated() noexcept override {
		++Invalidations;
	}
	void CaptureCompleted() noexcept override {
		++Completions;
	}
	void CaptureRetired() noexcept override {
		++Retirements;
	}
	sgre::resource::MotionPoint Size;
	std::size_t Resizes = 0;
	std::size_t Invalidations = 0;
	std::size_t Completions = 0;
	std::size_t Retirements = 0;
};

static void TestActualCapturedConsumer(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto primary = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*primary, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("Captured fixture must sample its actual framebuffer source, not substitute decoded/uploaded pixels");
	},
	    [](auto) {
	    });
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, cache, {}, {});
	static_cast<void>(cycle.CaptureBackend());
	cycle.PrepareFillRectangle();
	cycle.PrepareCaptured();
	CaptureCompletionObserver observer;
	auto source = std::make_shared<sgre::scene::CaptureTask>(4, 4);
	source->SetOffset({4, 4});
	source->AddConsumer(observer);
	auto consumer = std::make_shared<sgre::scene::CapturedTask>(source);
	sgre::scene::ParallelTaskSet producers(1);
	sgre::scene::ParallelTaskCallbacks producer;
	producer.Draw = [&](auto, auto) {
		cycle.DrawCapture(source);
	};
	producer.Release = [&](auto) {
		source->Close();
	};
	producer.CanRelease = [&](auto) {
		return source->CanClose();
	};
	const auto producer_handle = producers.Append(std::move(producer));
	sgre::scene::ParallelTaskSet camera_children(1);
	sgre::scene::ParallelTaskCallbacks child;
	std::size_t received_packets = 0;
	child.Draw = [&](auto, auto) {
		consumer->Draw([&](auto& actual_source, const auto& packet) {
			Require(&actual_source == &source->Texture() && cycle.CameraScope().Depth() == 2, "Captured callback must borrow the exact source under its actual camera-child model");
			cycle.DrawCaptured(actual_source, packet);
			++received_packets;
		});
	};
	child.Release = [&](auto) {
		consumer->Close();
	};
	child.CanRelease = [&](auto) {
		return consumer->CanClose();
	};
	static_cast<void>(camera_children.Append(std::move(child)));
	const auto begin = [&] {
		cycle.PreFrame();
		cycle.BeginDrawing();
		cycle.ResetRootCamera();
	};
	const auto draw_consumer = [&] {
		cycle.CameraScope().Draw(sgre::scene::LayerCameraParameters{}, camera_children, 0);
	};
	const auto fill = [&](std::uint32_t color) {
		cycle.DrawFillRectangle(Rectangle(-4, -4, 4, 4, color));
	};
	try {
		Require(!consumer->State().Ready, "Captured must not invent an initial framebuffer or ready state");
		begin();
		fill(0xffff0000);
		producers.Draw(0);
		Require(observer.Resizes == 1 && observer.Completions == 1 && consumer->State().Ready && source->Texture().State().Dirty, "real Capture must publish completion only after same-buffer MAIN write");
		fill(0xff00ff00);
		draw_consumer();
		Require(!source->Texture().State().Dirty && received_packets == 1, "actual same-buffer Captured binding must consume the genuine dirty main-to-companion copy");
		RequireFailure([&] {
			consumer->Close();
		},
		    "source frame End or Abort");
		cycle.PostFrame();
		const auto opaque = primary->Capture(cycle.LastSubmission()->Identity);
		RequirePixel(opaque, 1, 1, {0, 255, 0, 255});
		RequirePixel(opaque, 5, 5, {255, 0, 0, 255});
		Require(cycle.CurrentCounters().DrawOperations == 3 && cycle.CurrentCounters().SubmittedElements == 12, "Capture copy itself adds no draw; actual Captured emits exactly one native four-element strip");
		{
			sgre::scene::CapturedTask late_consumer(source);
			Require(late_consumer.State().Ready && late_consumer.State().FixedSize.X == 4, "native constructor created after actual Capture reads its cached physical size/readiness");
			late_consumer.Close();
		}

		// Pause the actual physical producer task: the consumer must retain the preceding completed image.
		producers.SetDrawPaused(producer_handle, true);
		consumer->SetOpacity(128);
		begin();
		fill(0xff00ff00);
		producers.Draw(0);
		Require(!source->State().FramePending && observer.Completions == 1, "actual paused Capture must not perform a hidden new copy");
		draw_consumer();
		cycle.PostFrame();
		const auto translucent = primary->Capture(cycle.LastSubmission()->Identity);
		RequirePixel(translucent, 1, 1, {0, 255, 0, 255});
		RequirePixel(translucent, 5, 5, {128, 127, 0, 255});
		Require(cycle.CurrentCounters().DrawOperations == 2 && consumer->State().Opacity == 128, "ordinary128 opacity uses the actual nonwhite-attribute pipeline, not a copied-target approximation");

		producers.SetDrawPaused(producer_handle, false);
		consumer->SetOpacity(255);
		begin();
		fill(0xff0000ff);
		producers.Draw(0);
		draw_consumer();
		Require(consumer->State().Ready && observer.Resizes == 1 && observer.Completions == 2, "same-bounds copy uses the existing actual source owner");
		cycle.AbortFrame();
		Require(!consumer->State().Ready && !source->State().Captured && !cycle.LastSubmission() && observer.Invalidations == 1, "actual Abort invalidates uncommitted source and consumer readiness without publishing pixels");

		begin();
		fill(0xff00ff00);
		const auto before_retry = received_packets;
		draw_consumer();
		Require(received_packets == before_retry, "aborted capture must skip all materialized consumer drawing until a fresh real copy");
		producers.Draw(0);
		Require(consumer->State().Ready && observer.Resizes == 1 && observer.Completions == 3, "successful same-bounds retry restores readiness without a synthetic resize");
		fill(0xff0000ff);
		draw_consumer();
		cycle.PostFrame();
		const auto recovered = primary->Capture(cycle.LastSubmission()->Identity);
		RequirePixel(recovered, 1, 1, {0, 0, 255, 255});
		RequirePixel(recovered, 5, 5, {0, 255, 0, 255});

		// Actual task retirement clears consumer backlinks before deleting the real texture pair.
		producers.SetStatus(producer_handle, sgre::scene::TaskStatus::Retire);
		producers.Update();
		Require(source->State().Closed && consumer->State().SourceRetired && observer.Retirements == 1, "actual source task retirement must notify surviving consumer before GPU registration retirement");
		begin();
		fill(0xff0000ff);
		const auto before_retired = received_packets;
		draw_consumer();
		cycle.PostFrame();
		Require(received_packets == before_retired && cycle.CurrentCounters().DrawOperations == 1, "retired source Captured is a native no-op without fabricated empty texture draw");
		RequireAllPixels(primary->Capture(cycle.LastSubmission()->Identity), {0, 0, 255, 255});
	} catch (...) {
		cycle.AbortFrame();
		producers.Clear();
		camera_children.Clear();
		throw;
	}
	producers.Clear();
	camera_children.Clear();
	consumer.reset();
	source.reset();
	cycle.Close();
	workers.Close();
	const auto observation = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observation.WindowVisible && !observation.WindowKey && !observation.Presenting && observation.DrawableRequests == 0, "Captured physical fixture requested a visible surface/drawable");
}

int main(int argc, char** argv) {
	@try {
		try {
			Require(argc == 3, "usage: captured_frame_tests sprite.metal sprite_present.metal");
			@autoreleasepool {
				const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
				TestActualCapturedConsumer(argv[1], argv[2]);
				Require(NSApp.windows.count == 0 && !NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "Captured fixture changed foreground/activation or retained a window");
			}
			std::cout << "PASS actual Metal Captured same-buffer source/opacity255+128, physical source pause/retirement and Abort/same-bounds retry\n";
			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	} @catch (NSException* error) {
		std::cerr << "Captured fixture Objective-C failure: " << error.reason.UTF8String << '\n';
		return 1;
	}
}
