#include "motion_receiver_fixture.hpp"

#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/graphics_frame_cycle.hpp"
#include "sgre/scene/parallel_task_set.hpp"

#include "sprite_presentation_fixture.hpp"

struct CycleTestCleanup {
	sgre::render::GraphicsFrameCycle& Cycle;
	sgre::scene::MotionSpriteDrawContext& Context;
	~CycleTestCleanup() noexcept {
		Context.Clear();
		try {
			Cycle.AbortFrame();
		} catch (...) {
			// A deliberately closed owner needs no second cleanup; failures remain visible to the test boundary.
		}
	}
};

static void TestActualCycle(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*renderer, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&counter] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	SceneFixture scene(*renderer);
	auto& red = scene.Add("red", {-4, -4});
	auto& blue = scene.Add("blue", {0, -4});
	auto& unsupported = scene.Add("red", {-4, -4}, 1);
	sgre::render::RenderFrameCounters initial;
	initial.DrawOperations = 11;
	initial.SubmittedElements = 29;
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, scene.Cache, initial, {});
	CycleTestCleanup cleanup{cycle, scene.Runtime->Context()};
	auto& camera = cycle.CameraScope();
	Scratch scratch;
	sgre::scene::ParallelTaskSet children(2);
	std::size_t child_calls = 0;
	sgre::scene::ParallelTaskCallbacks callback;
	callback.Draw = [&](auto, auto argument) {
		Require(argument == 73 && camera.IsDrawing(), "actual disabled Clip failed to enclose its forwarded child draw");
		++child_calls;
	};
	static_cast<void>(children.Append(std::move(callback)));
	const sgre::scene::MotionBounds disabled{1, 1, -1, -1};
	RequireFailure([&] {
		camera.DrawClip(disabled, children, 73);
	},
	    "active primary");
	Require(child_calls == 0 && !camera.IsDrawing(), "startup scope borrow admitted a draw before real frame preparation");
	const auto& row = *red.Player->PublishedRows()[0];
	cycle.BindTexture(row.Slot.Index, row.Texture);
	cycle.PreFrame();
	Require(cycle.PreviousCounters() == initial && cycle.CurrentCounters() == sgre::render::RenderFrameCounters{} && !cycle.LastSubmission(), "real Pre failed to copy/clear actual statistics before drawing");
	RequireFailure([&] {
		camera.DrawClip(disabled, children, 73);
	},
	    "active primary");
	cycle.BeginDrawing();
	cycle.ResetRootCamera();
	const auto mono = ActualMonoFrameState(graphics);
	Require(cycle.FrameState().Viewport.Width == mono.Viewport.Width && cycle.FrameState().Viewport.Height == mono.Viewport.Height, "contained actual viewport differs from the genuine selected layout");
	camera.DrawClip(disabled, children, 73);
	Require(child_calls == 1 && !camera.IsDrawing() && camera.Depth() == 1, "actual Clip traversal did not balance its containing activity");
	red.Player->Draw();
	cycle.Draw(scene.Runtime->Context(), scratch.Borrow());
	blue.Player->Draw();
	cycle.Draw(scene.Runtime->Context(), scratch.Borrow());
	cycle.PostFrame();
	const auto drawn = cycle.LastSubmission();
	Require(drawn && drawn->Owner == renderer.get(), "actual record/pool End did not publish its genuine submitted identity");
	const auto pixels = renderer->Capture(drawn->Identity);
	RequirePixel(pixels, 1, 1, {255, 0, 0, 255});
	RequirePixel(pixels, 5, 1, {0, 0, 255, 255});
	const auto completed = cycle.CurrentCounters();
	Require(completed.DrawOperations == 2 && completed.SubmittedElements == 8, "real packet record counters differ from two concrete indexed strips");

	cycle.PreFrame();
	Require(cycle.PreviousCounters() == completed && !cycle.LastSubmission(), "next Pre did not consume real completed counters");
	cycle.PostFrame();
	const auto paused = cycle.LastSubmission();
	Require(paused && paused->Identity > drawn->Identity, "draw-paused cycle failed to commit the actual native clear");
	const auto cleared = renderer->Capture(paused->Identity);
	RequirePixel(cleared, 1, 1, {0, 0, 0, 255});
	RequirePixel(cleared, 5, 1, {0, 0, 0, 255});
	Require(cycle.CurrentCounters() == sgre::render::RenderFrameCounters{}, "genuine clear invented a logical draw operation");

	// Every traversal remains a barrier through failure cleanup, including Clip's disabled fast path.
	for (const bool clip_only : {false, true}) {
		children.Clear();
		sgre::scene::ParallelTaskCallbacks failing;
		failing.Draw = [&](auto, auto) {
			Require(camera.IsDrawing() && camera.Depth() == (clip_only ? 1 : 2), "actual failing child lost its containing camera/model scope");
			unsupported.Player->Draw();
			try {
				cycle.Draw(scene.Runtime->Context(), scratch.Borrow());
			} catch (...) {
				const auto first = std::current_exception();
				RequireFailure([&] {
					cycle.PreFrame();
				},
				    "idle CPU");
				RequireFailure([&] {
					cycle.Close();
				},
				    "idle CPU");
				RequireFailure([&] {
					cycle.PostFrame();
				},
				    "prepared or drawing");
				Require(!cycle.LastSubmission(), "nested failure published a nominal frame completion");
				std::rethrow_exception(first);
			}
			throw std::runtime_error("actual unsupported filter was unexpectedly admitted");
		};
		static_cast<void>(children.Append(std::move(failing)));
		cycle.PreFrame();
		cycle.BeginDrawing();
		RequireFailure([&] {
			if (clip_only) {
				camera.DrawClip(disabled, children, 73);
			} else {
				camera.Draw(sgre::scene::LayerCameraParameters{}, children, 73);
			}
		},
		    "color-filter execution remains unsupported");
		Require(!camera.IsDrawing() && camera.Depth() == 1, "failed actual traversal did not finish model restoration");
		RequireFailure([&] {
			cycle.PreFrame();
		},
		    "idle CPU");
		scene.Runtime->Context().Clear();
		cycle.AbortFrame();
		Require(!cycle.LastSubmission(), "quiescent abort published a fabricated submission");
	}
	children.Clear();

	std::size_t writer_updates = 0;
	std::size_t writer_posts = 0;
	sgre::core::ThreadTaskReceiver writer(workers, [&](auto&) {
		++writer_updates;
	},
	    [&](auto&) {
		    ++writer_posts;
	    });
	auto binding = cycle.BindWriter(writer);
	try {
		for (const std::uint32_t gate : {0u, 1u, 2u}) {
			cycle.PreFrame();
			if (gate != 0) {
				cycle.BeginDrawing();
			}
			writer.QueueUpdate();
			writer.QueuePost();
			writer.QueuePost();
			RequireFailure([&] {
				if (gate == 0) {
					cycle.BeginDrawing();
				} else if (gate == 1) {
					red.Player->Draw();
					cycle.Draw(scene.Runtime->Context(), scratch.Borrow());
				} else {
					cycle.PostFrame();
				}
			},
			    "pending");
			Require(!cycle.LastSubmission() && writer.State().UpdatePending && writer.State().PostPending, "failed admission committed output or drained genuine pending writer work");
			RequireFailure([&] {
				cycle.PreFrame();
			},
			    "idle CPU");
			workers.RunFrame();
			scene.Runtime->Context().Clear();
			cycle.AbortFrame();
		}
	} catch (...) {
		const auto first = std::current_exception();
		// Authored fixture retirement drains only the bounded concrete test jobs, never game work.
		workers.RunFrame();
		scene.Runtime->Context().Clear();
		cycle.AbortFrame();
		binding.Close();
		writer.Close();
		std::rethrow_exception(first);
	}
	Require(writer_updates == 3 && writer_posts == 6, "genuine writer completion lost duplicate live Post occurrences");
	binding.Close();
	writer.Close();
	cycle.PreFrame();
	cycle.PostFrame();
	Require(cycle.LastSubmission().has_value(), "actual cycle could not recover after quiescent writer cleanup");
	cycle.Close();
	workers.Close();
	const auto observed = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observed.WindowVisible && !observed.WindowKey && !observed.Presenting && observed.DrawableRequests == 0, "actual cycle requested a drawable or activated the hidden surface");
}

int main(int argc, char** argv) {
	@try {
		try {
			Require(argc == 3, "frame cycle requires actual sprite and transfer shaders");
			@autoreleasepool {
				const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
				TestActualCycle(argv[1], argv[2]);
				Require(NSApp.windows.count == 0 && !NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "frame cycle changed foreground or retained a window");
				std::cout << "PASS genuine frame reset/pool/selected packet/Metal clear and contrasting pixels, nested camera and disabled Clip failure barriers, exact writer admission before Begin/Draw/commit; no drawable or Present\n";
			}
			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	} @catch (NSException* exception) {
		std::cerr << "frame cycle Objective-C exception: " << exception.reason.UTF8String << '\n';
		return 1;
	}
}
