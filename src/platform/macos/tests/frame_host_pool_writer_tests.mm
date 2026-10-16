#include "sgre/render/frame_host_pool.hpp"

#include "sgre/render/sprite_graphics_targets.hpp"

#include "sprite_presentation_fixture.hpp"

#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

static void Require(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "first writer-admission fixture failure: " << message << '\n';
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}
	Require(false, "missing actual read-only writer admission rejection");
}

static void TestActualAdmission(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpritePassDescription description;
	description.Width = 4;
	description.Height = 4;
	description.MaxVertices = 8;
	description.MaxIndices = 8;
	description.MaxDraws = 2;
	sgre::render::SpriteGraphicsTargets graphics(description, shader, 1);
	const auto renderer = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(renderer));
	sgre::scene::GraphicsModelStack models(8);
	models.Reset();
	sgre::render::SpriteGraphicsCamera camera(graphics, models);
	camera.ResetDefaultCamera();
	sgre::render::RenderFrameStatistics statistics({}, {});
	sgre::render::SpriteRenderRecords records(*renderer, camera, statistics);
	sgre::render::SpritePassPresentation surface(*renderer, transfer, sgre::render::SpritePresentationDescription{});
	sgre::core::ThreadTaskFrame workers([] {
		// Actual host-counter input is authored fixture timing, not a native clock claim.
		return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	},
	    1, 2);
	sgre::render::FrameHostPool pool(surface, workers, records);
	std::optional<sgre::render::SpriteHostInputs> input;
	std::optional<sgre::render::FrameHostInputBorrow> borrow;
	std::optional<sgre::render::FrameHostWriterBinding> binding;
	std::array<sgre::scene::MotionSpriteVertex, 3> vertices;
	const std::array<std::uint32_t, 3> indices{0, 1, 2};
	std::size_t actual_posts = 0;
	bool actual_unbound_updated = false;
	sgre::core::ThreadTaskReceiver writer(workers, [&](auto&) {
		binding->EmplaceInputs(input, borrow, vertices, indices);
	},
	    [&](auto& actual_receiver) {
		    ++actual_posts;
		    Require(!actual_receiver.State().PostPending && records.IsActive(), "logical Post flag or genuine active record frame changed");
		    RequireFailure([&] {
			    pool.RequireCompletedWriters();
		    });
	    });
	binding.emplace(pool, writer);
	sgre::core::ThreadTaskReceiver unbound(workers, [&](auto&) {
		actual_unbound_updated = true;
	},
	    [](auto&) {
	    });
	pool.RequireCompletedWriters();
	pool.Begin();
	const auto frame_counter = pool.FrameCounter();
	records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
	unbound.QueueUpdate();
	pool.RequireCompletedWriters();
	Require(records.IsActive() && !input && pool.FrameCounter() == frame_counter, "unbound pending queue changed genuine encoding/input/page state");
	writer.QueueUpdate();
	writer.QueuePost();
	writer.QueuePost();
	RequireFailure([&] {
		pool.RequireCompletedWriters();
	});
	Require(writer.State().UpdatePending && writer.State().PostPending && records.IsActive() && !input && pool.FrameCounter() == frame_counter, "read-only rejection consumed pending work or actual encoding");
	workers.RunFrame();
	Require(actual_posts == 2 && actual_unbound_updated && input && borrow && pool.RetainedInputs() == 1, "actual worker input or duplicate Post completion is absent");
	pool.RequireCompletedWriters();
	Require(records.IsActive() && pool.RetainedInputs() == 1 && pool.FrameCounter() == frame_counter, "completed-writer admission changed active encoding or actual retained input");
	std::exception_ptr foreign_error;
	std::thread foreign([&] {
		try {
			pool.RequireCompletedWriters();
		} catch (...) {
			foreign_error = std::current_exception();
		}
	});
	foreign.join();
	Require(foreign_error != nullptr && records.IsActive(), "foreign writer observation admitted or mutated actual record frame");
	records.AbortFrame();
	borrow.reset();
	input.reset();
	pool.End();
	binding->Close();
	binding.reset();
	pool.Close();
	RequireFailure([&] {
		pool.RequireCompletedWriters();
	});
	const auto observed = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observed.WindowVisible && !observed.WindowKey && observed.DrawableRequests == 0 && !observed.Presenting, "writer admission requested a drawable or activated presentation");
	writer.Close();
	unbound.Close();
	workers.Close();
}

int main(int argc, char** argv) {
	@try {
		try {
			Require(argc == 3, "writer-admission fixture requires sprite/transfer shaders");
			@autoreleasepool {
				const auto foreground_pid = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
				Require(foreground_pid > 0, "actual foreground witness absent");
				TestActualAdmission(argv[1], argv[2]);
				Require(NSApp.windows.count == 0 && !NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground_pid, "writer admission retained a window or changed foreground");
				std::cout << "PASS read-only exact writer admission before/while real encoding, genuine input Update and duplicate Posts, unrelated queue admitted, foreign/closed owner rejected; no GPU submission/drawable/Present\n";
			}
			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	} @catch (NSException* exception) {
		std::cerr << "writer fixture Objective-C exception: " << exception.reason.UTF8String << '\n';
		return 1;
	}
}
