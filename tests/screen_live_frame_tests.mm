#include "motion_receiver_fixture.hpp"

#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/graphics_frame_cycle.hpp"
#include "sgre/scene/raw_texture_task.hpp"
#include "sgre/scene/screens.hpp"

#include "sprite_presentation_fixture.hpp"

#include <cmath>

static void TestScreenTargets(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	auto description = Description();
	// Keep the original Motion bindings plus the four added chromatic projection pipeline variants.
	description.MaxPipelines += 4;
	description.MaxTextureBytes = 65536;
	sgre::render::SpriteGraphicsTargets graphics(description, shader, 4);
	const auto primary = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*primary, transfer, sgre::render::SpritePresentationDescription{});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	SceneFixture scene(*primary);
	auto& red = scene.Add("red", {-2, -2});
	auto& blue = scene.Add("blue", {0, -4});
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, scene.Cache, {}, {});
	for (auto* player : {red.Player.get(), blue.Player.get()}) {
		const auto& row = *player->PublishedRows()[0];
		cycle.BindTexture(row.Slot.Index, row.Texture);
	}
	auto& backend = cycle.RawTextureBackend();
	sgre::scene::RawTextureTask green(backend, {1, 1});
	green.RestoreRgb(std::array<std::uint8_t, 3>{0, 255, 0});
	green.SetCoordinate({-2, -2});
	green.SetDrawPaused(false);
	Scratch scratch;
	sgre::scene::LayerTree tree(8);
	sgre::scene::ParallelTaskSet tasks(4);
	sgre::scene::ScreenRegistry screens(tree, [&](const auto& request) {
		auto extent = primary->Description();
		extent.Width = request.Width;
		extent.Height = request.Height;
		sgre::scene::ScreenTargetAllocation allocation;
		allocation.Renderer = graphics.AllocateTarget(extent);
		allocation.Width = allocation.Renderer->Description().Width;
		allocation.Height = allocation.Renderer->Description().Height;
		return allocation;
	});
	bool fail_after_draw = false;
	std::size_t child_calls = 0;
	sgre::scene::LayerHandle motion_child;
	sgre::scene::LayerHandle raw_child;
	sgre::scene::ScreenDriverServices services;
	services.ResolveTasks = [&]() -> sgre::scene::ParallelTaskSet& {
		return tasks;
	};
	services.Layers.State = [&](auto layer) {
		Require(tree.Contains(layer), "Screen manager resolved a retired actual Layer");
		return sgre::scene::LayerFrameProcessState{};
	};
	services.Layers.Update = [](auto) {
	};
	services.Layers.PrepareDraw = [&] {
		cycle.ResetRootCamera();
	};
	services.Layers.Draw = [&](auto layer, auto argument) {
		Require(argument == 0, "native Screen manager must receive zero independently of outer task argument");
		Require(graphics.CurrentTarget()->Description().Width == 4 && cycle.FrameState().Viewport.Width == 4, "Screen child did not receive the actual dynamic target projection");
		++child_calls;
		if (layer == motion_child) {
			red.Player->Draw();
			cycle.Draw(scene.Runtime->Context(), scratch.Borrow());
		} else {
			Require(layer == raw_child, "Screen fixture reached an unrelated Layer");
			cycle.DrawRawTexture(green, green.PrepareDrawPacket());
			if (fail_after_draw) {
				throw std::runtime_error("Screen actual child failure after real encoding");
			}
		}
	};
	services.Draw = [&](auto, auto& target, auto& manager, const auto& state, auto) {
		cycle.DrawScreen(target, manager, state);
	};
	screens.ConfigureDriver(std::move(services));
	const auto screen = screens.Create({4, 4, false});
	screens.SetBackgroundColor(screen, {0, 0, 255, 255});
	const auto screen_root = screens.State(screen).LayerRoot;
	motion_child = tree.CreateMotionInScreen(screen_root);
	raw_child = tree.CreateRawTextureInScreen(screen_root);
	tree.SetPriority(motion_child, 0);
	tree.SetPriority(raw_child, 1);
	const auto target = graphics.RequireRegisteredTarget(screens.Renderer(screen));
	const auto clear_screen = screens.Create({2, 2, false});
	screens.SetBackgroundColor(clear_screen, {64, 128, 192, 255});
	const auto clear_target = graphics.RequireRegisteredTarget(screens.Renderer(clear_screen));
	try {
		for (std::size_t iteration = 0; iteration < 2; ++iteration) {
			cycle.PreFrame();
			cycle.BeginDrawing();
			blue.Player->Draw();
			cycle.Draw(scene.Runtime->Context(), scratch.Borrow());
			tasks.Draw(0xaabbccdd);
			Require(graphics.CurrentTarget() == primary && cycle.FrameState().Viewport.Width == 8 && cycle.CameraScope().Depth() == 1, "Screen scope did not restore prior target/default projection/model depth");
			cycle.PostFrame();
			const auto submission = cycle.LastSubmission();
			Require(submission && submission->Owner == primary.get(), "Screen added a separate submission or lost the real outer primary owner");
			const auto primary_pixels = primary->Capture(submission->Identity);
			RequirePixel(primary_pixels, 1, 1, {0, 0, 0, 255});
			RequirePixel(primary_pixels, 5, 1, {0, 0, 255, 255});
			const auto screen_pixels = target->CaptureAfterFrame(*primary, submission->Identity);
			Require(screen_pixels.Width == 4 && screen_pixels.Height == 4, "Screen pixels came from a substituted primary framebuffer");
			RequirePixel(screen_pixels, 0, 0, {0, 255, 0, 255});
			RequirePixel(screen_pixels, 2, 2, {255, 0, 0, 255});
			const auto clear_pixels = clear_target->CaptureAfterFrame(*primary, submission->Identity);
			Require(clear_pixels.Width == 2 && clear_pixels.Height == 2, "empty Screen used the wrong target extent");
			RequirePixel(clear_pixels, 1, 1, {64, 128, 192, 255});
			Require(cycle.CurrentCounters().DrawOperations == 3 && cycle.CurrentCounters().SubmittedElements == 12, "Screen and primary did not share the actual outer draw/input counters");
		}
		const auto last_visible = cycle.LastSubmission();
		screens.SetVisible(screen, false);
		screens.SetBackgroundColor(screen, {255, 0, 255, 255});
		screens.SetBackgroundColor(clear_screen, {192, 64, 128, 255});
		cycle.PreFrame();
		cycle.BeginDrawing();
		tasks.Draw(23);
		cycle.PostFrame();
		const auto hidden_frame = cycle.LastSubmission();
		Require(hidden_frame.has_value() && child_calls == 4 && cycle.CurrentCounters() == sgre::render::RenderFrameCounters{}, "outer hidden Screen entered its driver Draw or suppressed the distinct empty Screen task");
		RequirePixel(target->CaptureAfterFrame(*primary, last_visible->Identity), 0, 0, {0, 255, 0, 255});
		RequirePixel(clear_target->CaptureAfterFrame(*primary, hidden_frame->Identity), 1, 1, {192, 64, 128, 255});
		screens.SetVisible(screen, true);
		fail_after_draw = true;
		cycle.PreFrame();
		cycle.BeginDrawing();
		RequireFailure([&] {
			tasks.Draw(19);
		},
		    "actual child failure");
		Require(!cycle.LastSubmission() && graphics.CurrentTarget() == primary && cycle.CameraScope().Depth() == 1, "Screen failure committed output or retained the wrong CPU target/model scope");
		scene.Runtime->Context().Clear();
		cycle.AbortFrame();
		fail_after_draw = false;
		cycle.PreFrame();
		cycle.BeginDrawing();
		tasks.Draw(21);
		cycle.PostFrame();
		const auto replay = cycle.LastSubmission();
		Require(replay.has_value(), "Screen failed to replay after genuine whole-buffer abort");
		RequirePixel(target->CaptureAfterFrame(*primary, replay->Identity), 0, 0, {0, 255, 0, 255});
		Require(child_calls == 8, "Screen fixture changed genuine per-task child callback count");
	} catch (...) {
		const auto failure = std::current_exception();
		scene.Runtime->Context().Clear();
		try {
			cycle.AbortFrame();
		} catch (...) {
		}
		screens.Close();
		std::rethrow_exception(failure);
	}
	screens.Close();
	green.Close();
	cycle.Close();
	workers.Close();
	const auto observed = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!observed.WindowVisible && !observed.WindowKey && !observed.Presenting && observed.DrawableRequests == 0, "Screen diagnostic requested a drawable or visible window");
}

// A copied-family draw must sample this actual Screen, not the contrasting primary attachment.
static void TestCopiedScreenSource(const std::filesystem::path& shader) {
	auto description = Description(16);
	description.MaxPipelines = 64;
	description.MaxTextures = 8;
	description.MaxTextureBytes = 65536;
	sgre::render::SpriteGraphicsTargets graphics(description, shader, 3);
	const auto primary = graphics.PrimaryTarget();
	static_cast<void>(graphics.SelectTarget(primary));
	graphics.SetMaskMode(1);
	auto screen_description = description;
	screen_description.Width = 4;
	screen_description.Height = 4;
	const auto screen = graphics.AllocateTarget(screen_description);
	sgre::scene::GraphicsModelStack models(4);
	models.Reset();
	auto frame_state = ActualMonoFrameState(graphics);
	sgre::render::SpriteGraphicsCamera camera(graphics, models, frame_state);
	camera.ResetDefaultCamera();
	sgre::render::RenderFrameStatistics statistics({}, {});
	sgre::render::SpriteRenderRecords records(*primary, camera, statistics);
	sgre::render::FrameHostHeap heap(2);
	sgre::scene::TextureCache cache(Upload(*primary), Release(*primary));
	const std::array<std::uint8_t, 16> block{0x6d, 0x66, 0x4d, 0xf0, 0x44, 0x42, 0xa6, 0x8a, 0xde, 0x19, 0xa0, 0x53, 0xe7, 0xe9, 0xc7, 0x3f};
	const std::array<std::array<std::uint8_t, 3>, 16> source_pixels{{{49, 115, 82}, {49, 56, 82}, {75, 40, 217}, {61, 38, 222}, {49, 92, 82}, {49, 33, 82}, {90, 42, 212}, {132, 49, 198}, {134, 23, 171}, {111, 18, 222}, {99, 16, 247}, {169, 31, 91}, {111, 18, 222}, {99, 16, 247}, {99, 16, 247}, {181, 33, 66}}};
	sgre::resource::ImageTexture image;
	image.SerializedValueOffset = 4;
	image.Format = sgre::resource::PixelFormat::Bc7;
	image.Width = 4;
	image.Height = 4;
	image.TruncatedWidth = 4;
	image.TruncatedHeight = 4;
	image.PixelRange.Size = 16;
	const auto texture = cache.Acquire(cache.AllocateDocumentIdentity(), image, block);
	sgre::render::MotionSpriteRecordReceiver receiver(records, *primary, graphics, cache, heap, 2);
	std::array<sgre::resource::MotionPoint, 4> positions{{{-2, -2}, {2, -2}, {-2, 2}, {2, 2}}};
	std::array<sgre::scene::MotionClipGeometry, 2> geometry;
	std::array<sgre::scene::MotionClipCommand, 2> descriptors;
	std::array<sgre::scene::MotionPreparedFrameCommand, 2> commands;
	for (std::size_t index = 0; index < commands.size(); ++index) {
		geometry[index].Positions = positions;
		descriptors[index].Geometry = &geometry[index];
		descriptors[index].GeometryCount = 4;
		commands[index].Clip = &descriptors[index];
		commands[index].PixelUvs.Width = 4;
		commands[index].PixelUvs.Height = 4;
		commands[index].Colors.fill(0xffffffff);
		commands[index].Key = 4;
	}
	descriptors[0].ClipGroup = 1;
	commands[0].BlendMode = 0x110;
	std::array<sgre::scene::MotionClipNode, 1> nodes;
	nodes[0].Command.Hidden = true;
	nodes[0].ModeFlags = 5;
	nodes[0].Members = std::span<const sgre::scene::MotionClipCommand>(descriptors).subspan(1, 1);
	std::array<sgre::scene::MotionMaskSettings, 1> settings;
	std::array<sgre::scene::MotionFrameGroup, 1> groups{{{1, 0}}};
	std::array<sgre::scene::MotionFrameCandidateBinding, 1> candidates{{{&descriptors[1], &commands[1]}}};
	std::array<sgre::resource::MotionPoint, 1> sizes{{{4, 4}}};
	Scratch scratch;
	const auto reset_camera = [&] {
		frame_state = ActualMonoFrameState(graphics);
		camera.ResetDefaultCamera();
	};
	const auto prepared_frame = [&] {
		auto frame = records.OrdinaryFrameGlobals(sizes);
		frame.Queue = std::span<const sgre::scene::MotionPreparedFrameCommand>(commands).first(1);
		frame.ClipQueue = std::span<const sgre::scene::MotionClipCommand>(descriptors).first(1);
		frame.Nodes = nodes;
		frame.MaskSettings = settings;
		frame.Groups = groups;
		frame.Candidates = candidates;
		return frame;
	};
	bool heap_open = false;
	try {
		heap.Begin();
		heap_open = true;
		records.BeginFrame({1, 0, 0, 1}, sgre::render::SpritePassLoadAction::Clear);
		records.SelectRenderTarget(graphics, screen, std::array<float, 4>{0, 0, 1, 1});
		reset_camera();
		auto frame = prepared_frame();
		receiver.DrawActualFrame(frame, std::array{texture}, scratch.Borrow());
		records.SelectRenderTarget(graphics, primary, std::nullopt);
		reset_camera();
		const auto submission = records.EndFrame();
		RequirePixel(primary->Capture(submission), 1, 1, {255, 0, 0, 255});
		const auto pixels = screen->CaptureAfterFrame(*primary, submission);
		for (std::size_t index = 0; index < source_pixels.size(); ++index) {
			const auto& source = source_pixels[index];
			const auto offset = index * 4;
			Require(std::abs(static_cast<int>(pixels.Rgba[offset]) - source[0]) <= 2 && std::abs(static_cast<int>(pixels.Rgba[offset + 1]) - source[1]) <= 2 && pixels.Rgba[offset + 2] >= 253 && pixels.Rgba[offset + 3] == 255, "copied masked Screen packet sampled the primary instead of its actual Screen attachment");
		}
		receiver.ReleaseFrameInputs();
		heap.End();
		heap_open = false;

		// The old idle API cannot replay a copied plan prepared before an intervening target reset.
		heap.Begin();
		heap_open = true;
		records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
		auto stale = prepared_frame();
		records.AbortFrame();
		heap.End();
		heap_open = false;
		receiver.PrepareFrame(stale, scratch.Borrow());
		heap.Begin();
		heap_open = true;
		records.BeginFrame({}, sgre::render::SpritePassLoadAction::Clear);
		records.SelectRenderTarget(graphics, screen, std::array<float, 4>{0, 0, 1, 1});
		reset_camera();
		records.SelectRenderTarget(graphics, primary, std::nullopt);
		reset_camera();
		RequireFailure([&] {
			receiver.DrawPreparedFrame(scratch.Borrow());
		},
		    "target resource bindings changed");
		records.AbortFrame();
		receiver.ReleaseFrameInputs();
		heap.End();
		heap_open = false;
	} catch (...) {
		const auto failure = std::current_exception();
		records.AbortFrame();
		receiver.ReleaseFrameInputs();
		if (heap_open) {
			heap.End();
		}
		std::rethrow_exception(failure);
	}
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "usage: screen_live_frame_tests sprite.metal presentation.metal");
		@autoreleasepool {
			TestScreenTargets(argv[1], argv[2]);
			TestCopiedScreenSource(argv[1]);
		}
		std::cout << "actual Screen same-buffer clear/manager-zero argument, Motion/Raw dynamic target pixels, primary restoration/shared counters and abort replay passed; no visible window or native full-game fidelity claim\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
