#include "motion_actual_scene_fixture.hpp"

#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/render/graphics_frame_cycle.hpp"

#include "sprite_presentation_fixture.hpp"

#include <filesystem>
#include <iostream>

struct ActualScratch {
	std::array<sgre::resource::MotionPoint, 64> Positions;
	std::array<sgre::resource::MotionPoint, 64> Uvs;
	std::array<std::uint32_t, 64> Colors;
	std::array<std::uint32_t, 128> Indices;
	std::array<sgre::scene::MotionMaskOperation, 8> Masks;
	std::array<sgre::scene::MotionMaskedBlendPass, 8> Blends;

	sgre::scene::MotionFrameScratch Borrow() {
		sgre::scene::MotionFrameScratch result;
		result.Batch = {Positions, Uvs, Colors, Indices};
		result.MaskOperations = Masks;
		result.BlendPasses = Blends;

		return result;
	}
};

static sgre::render::SpritePassDescription Description(std::size_t draws = 16) {
	sgre::render::SpritePassDescription result;
	result.Width = 16;
	result.Height = 16;
	result.MaxVertices = 128;
	result.MaxIndices = 256;
	result.MaxDraws = draws;
	result.MaxTextures = 16;
	result.MaxPipelines = 64;
	result.MaxTextureBytes = 64 * 1024;
	result.MaxTargetBytes = 64 * 1024;

	return result;
}

struct CycleCleanup {
	sgre::render::GraphicsFrameCycle& Cycle;
	ActualSceneFixture& Fixture;

	~CycleCleanup() noexcept {
		try {
			Cycle.AbortFrame();
			if (Fixture.Runtime) {
				Fixture.Runtime->SceneContext().Clear();
			}
		} catch (...) {
			// Preserve the first fixture failure; ordinary owner destructors still enforce quiescence.
		}
	}
};

static std::vector<sgre::scene::CachedTextureHandle> ActualSlots(ActualSceneFixture& fixture) {
	std::vector<sgre::scene::CachedTextureHandle> slots;
	const auto source = fixture.Manager.FindSource(fixture.Project, "atlas");
	Require(source.has_value() && source->Slot.Index < 16, "actual synthetic source must own a bounded local texture slot");
	slots.resize(static_cast<std::size_t>(source->Slot.Index) + 1);
	slots[source->Slot.Index] = source->Texture;

	return slots;
}

static std::vector<std::uint32_t> GeometryWords(const sgre::scene::MotionSceneEvaluation& scene) {
	std::vector<std::uint32_t> words;
	for (const auto& node : scene.Packets().Nodes()) {
		if (node.Command.Geometry == nullptr) {
			continue;
		}
		for (const auto point : node.Command.Geometry->Positions) {
			words.push_back(std::bit_cast<std::uint32_t>(point.X));
			words.push_back(std::bit_cast<std::uint32_t>(point.Y));
		}
	}
	Require(!words.empty(), "genuine containing scene must publish completed geometry before Draw");

	return words;
}

static void RequirePendingGuards(ActualSceneFixture& fixture, sgre::scene::MotionPlayerTask& task) {
	auto& context = fixture.Runtime->SceneContext();
	const auto* scene = task.GenericPlayer()->Scene();
	Require(scene && context.Size() == 1 && context.IsBorrowing(*scene), "actual task Draw must queue its real selected scene");
	RequireFailure([&] {
		context.Append(*scene);
	},
	    "distinct actual task Draw");
	RequireFailure([&] {
		fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(73);
	},
	    "queued scene borrows");
	RequireFailure([&] {
		task.Progress(1);
	},
	    "queued row borrows");
	RequireFailure([&] {
		task.Play("main", sgre::scene::MotionPlayFlags::Force);
	},
	    "queued row borrows");
	RequireFailure([&] {
		task.Clear();
	},
	    "queued row borrows");
	RequireFailure([&] {
		fixture.Runtime->RequireCanRetirePlayers();
	},
	    "consumed");
	Require(!fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).CanClear(), "actual queued native task must retain its retirement barrier");
}

static void RetireCpuScene(ActualSceneFixture& fixture, sgre::scene::MotionPlayerTask& task) {
	task.Clear();
	fixture.Tasks.Clear();
	Require(!task.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0 && fixture.Runtime->PlayerCount() == 0, "consumed source must retire its real scene, UID listeners and task");
	fixture.Runtime->Close();
	fixture.Runtime.reset();
	const std::weak_ptr<const sgre::scene::LoadedMotionAsset> weak = fixture.Asset;
	fixture.Asset.reset();
	fixture.Catalog.Prune();
	Require(weak.expired() && fixture.Catalog.DocumentCount() == 0 && fixture.Manager.Count() == 1 && fixture.Cache.Count() == 1, "CPU receiving owners must expire while the explicit manager still owns the sampled texture");
}

static void RequireQuiet(const sgre::render::SpritePassPresentation& surface) {
	const auto state = sgre::render::SpritePassPresentationFixtureBridge::Observe(surface);
	Require(!state.WindowVisible && !state.WindowKey && !state.WindowMain && !state.Presenting && state.DrawableRequests == 0, "repeat-draw receiving requested a visible window, activation, drawable or Present");
}

static void TestRepeatedActualOccurrences(const std::filesystem::path& shader, const std::filesystem::path& transfer, bool copied) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*renderer, transfer, {});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	ActualSceneFixture fixture(*renderer, copied);
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(1);
	const auto* scene = task.GenericPlayer()->Scene();
	Require(scene && scene->PacketRows().size() == 3 && fixture.Runtime->Identifiers().LiveCount() == 3, "genuine root and selected child must finish before repeated receiving");
	const auto geometry = GeometryWords(*scene);
	const auto tick = task.Commands().ClockState().TickCount;
	const auto serial = task.Commands().SelectionSerial();
	const auto random = task.Random().State();
	const auto events = fixture.Events.size();
	const auto rows = std::vector<const sgre::scene::MotionStencilRowState*>(scene->PacketRows().begin(), scene->PacketRows().end());
	const auto slots = ActualSlots(fixture);
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, fixture.Cache, {}, {});
	CycleCleanup cleanup{cycle, fixture};
	ActualScratch scratch;
	cycle.PreFrame();
	cycle.BeginDrawing();
	cycle.ResetRootCamera();

	sgre::render::RenderFrameCounters first_encoded;
	for (std::size_t occurrence = 0; occurrence != 2; ++occurrence) {
		fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0xabcdef01);
		RequirePendingGuards(fixture, task);
		cycle.DrawScene(fixture.Runtime->SceneContext(), slots, scratch.Borrow());
		Require(fixture.Runtime->SceneContext().Size() == 0 && !fixture.Runtime->SceneContext().IsBorrowing(*scene) && task.GenericPlayer()->CanMutate(), "successful synchronous receiver must consume only its actual CPU occurrence");
		Require(task.GenericPlayer()->Scene() == scene && task.Commands().ClockState().TickCount == tick && task.Commands().SelectionSerial() == serial && task.Random().State() == random && fixture.Events.size() == events && fixture.Runtime->Identifiers().LiveCount() == 3 && GeometryWords(*scene) == geometry && std::equal(rows.begin(), rows.end(), scene->PacketRows().begin()), "immutable repeated Draw changed clock, selected/prepared rows, geometry, UID, events or RNG");
		const auto encoded = cycle.CurrentCounters();
		if (occurrence == 0) {
			first_encoded = encoded;
			Require(first_encoded.DrawOperations > 0 && first_encoded.SubmittedElements > 0, "first actual occurrence must encode real host inputs");
		} else {
			Require(encoded.DrawOperations == first_encoded.DrawOperations * 2 && encoded.SubmittedElements == first_encoded.SubmittedElements * 2, "identical second occurrence must submit its actual packet records again");
		}
	}
	const auto repeated = cycle.CurrentCounters();
	Require(repeated.DrawOperations >= 2 && repeated.SubmittedElements > 0 && !cycle.LastSubmission(), "two actual occurrences must encode before any outer completion");

	// A third real publication grows the copied region, exercising replacement ownership until End.
	task.SetZoom(2, 2);
	task.Progress(0);
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(7);
	cycle.DrawScene(fixture.Runtime->SceneContext(), slots, scratch.Borrow());
	Require(cycle.CurrentCounters().DrawOperations > repeated.DrawOperations, "post-consumption mutation failed to encode its genuinely recomposed scene");
	RetireCpuScene(fixture, task);
	cycle.PostFrame();
	const auto submission = cycle.LastSubmission();
	Require(submission.has_value(), "retired source CPU owners prevented actual outer End");
	const auto pixels = renderer->Capture(submission->Identity);
	bool colored = false;
	for (std::size_t offset = 0; offset != pixels.Rgba.size(); offset += 4) {
		colored = colored || pixels.Rgba[offset] != 0 || pixels.Rgba[offset + 1] != 0 || pixels.Rgba[offset + 2] != 0;
	}
	Require(colored, "actual encoded host inputs and copied-target owners did not survive source-first retirement to completion");
	Require(fixture.Manager.Unregister(fixture.Project) && fixture.Cache.Count() == 0 && fixture.Uploads == 1 && fixture.Releases == 1, "actual source texture retirement must balance after outer completion");
	cycle.Close();
	workers.Close();
	RequireQuiet(surface);
}

static void TestPartialFrameAbortAndRecovery(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	sgre::render::SpriteGraphicsTargets graphics(Description(), shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*renderer, transfer, {});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	ActualSceneFixture fixture(*renderer, true);
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(1);
	const auto slots = ActualSlots(fixture);
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, fixture.Cache, {}, {});
	CycleCleanup cleanup{cycle, fixture};
	ActualScratch scratch;
	cycle.PreFrame();
	cycle.BeginDrawing();
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	cycle.DrawScene(fixture.Runtime->SceneContext(), slots, scratch.Borrow());
	Require(cycle.CurrentCounters().DrawOperations > 0 && fixture.Runtime->SceneContext().Size() == 0, "failure fixture needs an actual encoded first occurrence");
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	auto undersized = scratch.Borrow();
	undersized.Batch.Positions = {};
	RequireFailure([&] {
		cycle.DrawScene(fixture.Runtime->SceneContext(), slots, undersized);
	},
	    "bounded prepared streams that fit one quad");
	Require(fixture.Runtime->SceneContext().Size() == 0 && task.GenericPlayer()->CanMutate() && !cycle.LastSubmission(), "failed second occurrence must Abort prior copied inputs and clear pending CPU publication without submission");
	cycle.AbortFrame();
	task.Progress(0);
	cycle.PreFrame();
	cycle.BeginDrawing();
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	cycle.DrawScene(fixture.Runtime->SceneContext(), slots, scratch.Borrow());
	cycle.PostFrame();
	Require(cycle.LastSubmission().has_value(), "actual cycle could not recover after copied occurrence failure");
	RetireCpuScene(fixture, task);
	Require(fixture.Manager.Unregister(fixture.Project) && fixture.Cache.Count() == 0 && fixture.Uploads == fixture.Releases, "Abort recovery lost a real source texture owner");
	cycle.Close();
	workers.Close();
	RequireQuiet(surface);
}

static void TestCompletedOccurrenceBudget(const std::filesystem::path& shader, const std::filesystem::path& transfer) {
	// A genuine single-layer scene makes the two-entry draw and CPU-occurrence bounds coincide.
	sgre::render::SpriteGraphicsTargets graphics(Description(2), shader, 2);
	const auto renderer = graphics.PrimaryTarget();
	sgre::render::SpritePassPresentation surface(*renderer, transfer, {});
	sgre::platform::HostPerformanceCounter counter;
	sgre::core::ThreadTaskFrame workers([&] {
		return counter.ReadRawCounter();
	},
	    1, 4);
	ActualSceneFixture fixture(*renderer, false, true);
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	task.Progress(1);
	const auto slots = ActualSlots(fixture);
	sgre::render::GraphicsFrameCycle cycle(graphics, surface, workers, fixture.Cache, {}, {});
	CycleCleanup cleanup{cycle, fixture};
	ActualScratch scratch;
	cycle.PreFrame();
	cycle.BeginDrawing();
	for (std::size_t index = 0; index != 2; ++index) {
		fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
		cycle.DrawScene(fixture.Runtime->SceneContext(), slots, scratch.Borrow());
		Require(fixture.Runtime->SceneContext().Size() == 0, "completed occurrences must release CPU contexts without recycling bounded outer slots");
	}
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Draw(0);
	RequireFailure([&] {
		cycle.DrawScene(fixture.Runtime->SceneContext(), slots, scratch.Borrow());
	},
	    "bounded distinct actual source-manager occurrences");
	Require(fixture.Runtime->SceneContext().Size() == 1 && !cycle.LastSubmission(), "capacity rejection must leave the not-received publication explicitly owned by its caller");
	fixture.Runtime->SceneContext().Clear();
	cycle.AbortFrame();
	RetireCpuScene(fixture, task);
	Require(fixture.Manager.Unregister(fixture.Project) && fixture.Cache.Count() == 0, "bounded occurrence Abort did not release actual source owners");
	cycle.Close();
	workers.Close();
	RequireQuiet(surface);
}

int main(int argc, char** argv) {
	@try {
		try {
			Require(argc == 3, "repeat-draw fixture requires actual sprite and transfer shader sources");
			@autoreleasepool {
				const auto foreground = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
				TestRepeatedActualOccurrences(argv[1], argv[2], false);
				TestRepeatedActualOccurrences(argv[1], argv[2], true);
				TestPartialFrameAbortAndRecovery(argv[1], argv[2]);
				TestCompletedOccurrenceBudget(argv[1], argv[2]);
				Require(NSApp.windows.count == 0 && !NSApp.active && NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier == foreground, "quiet repeated receiving retained a window or changed foreground application");
				std::cout << "PASS four actual containing-scene/Metal receiving groups: immutable repeat, copied-target replacement, source-first CPU retirement, Abort and occurrence bounds; no drawable or Present\n";
			}
			return 0;
		} catch (const std::exception& error) {
			std::cerr << error.what() << '\n';
			return 1;
		}
	} @catch (NSException* error) {
		std::cerr << "repeat-draw Objective-C exception: " << error.reason.UTF8String << '\n';
		return 1;
	}
}
