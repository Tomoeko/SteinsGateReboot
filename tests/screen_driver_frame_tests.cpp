#include "sgre/render/sprite_pass.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/scene/screens.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view fragment) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(fragment) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected actual Screen owner rejection: " + std::string(fragment));
}

struct TargetRetirement {
	sgre::scene::LayerTree* Tree = nullptr;
	sgre::scene::LayerScreenRootHandle Root;
	std::weak_ptr<sgre::render::SpritePassRenderer> Allocated;
	std::size_t Releases = 0;
	bool RootLiveAtRelease = false;
};

struct ActualTargetOwner {
	std::shared_ptr<sgre::render::SpritePassRenderer> Target;
	TargetRetirement& Observation;

	~ActualTargetOwner() noexcept {
		++Observation.Releases;
		Observation.RootLiveAtRelease = Observation.Tree && Observation.Tree->ContainsScreenRoot(Observation.Root);
	}
};

static sgre::scene::ScreenTargetAllocation MakeTarget(sgre::render::SpriteGraphicsTargets& graphics, const sgre::scene::ScreenDescription& description, TargetRetirement& observation) {
	auto extent = graphics.CurrentTarget()->Description();
	extent.Width = description.Width;
	extent.Height = description.Height;
	extent.MaxVertices = 3;
	extent.MaxIndices = 3;
	extent.MaxDraws = 1;
	auto renderer = graphics.AllocateTarget(extent);
	observation.Allocated = renderer;
	auto owner = std::make_shared<ActualTargetOwner>(std::move(renderer), observation);
	sgre::scene::ScreenTargetAllocation target;
	target.Width = description.Width;
	target.Height = description.Height;
	target.Renderer = std::shared_ptr<sgre::render::SpritePassRenderer>(owner, owner->Target.get());

	return target;
}

static void TestActualGroupAndDeferredPayload(const std::filesystem::path& shader) {
	sgre::render::SpritePassDescription primary;
	primary.Width = 8;
	primary.Height = 8;
	sgre::render::SpriteGraphicsTargets graphics(primary, shader);
	static_cast<void>(graphics.SelectTarget(graphics.PrimaryTarget()));
	sgre::scene::LayerTree tree(16);
	sgre::scene::ParallelTaskSet actual_driver_group(4);
	bool sibling_borrowed = false;
	std::size_t sibling_updates = 0;
	std::size_t sibling_draws = 0;
	std::size_t sibling_releases = 0;
	sgre::scene::ParallelTaskCallbacks sibling;
	sibling.Update = [&](auto) {
		++sibling_updates;
	};
	sibling.Draw = [&](auto, auto) {
		++sibling_draws;
	};
	sibling.CanRelease = [&](auto) {
		return !sibling_borrowed;
	};
	sibling.Release = [&](auto) {
		++sibling_releases;
	};
	const auto sibling_task = actual_driver_group.Append(std::move(sibling));
	std::array<TargetRetirement, 4> target_observations;
	std::size_t target_count = 0;
	std::size_t resolutions = 0;
	bool driver_available = false;
	std::vector<sgre::scene::LayerHandle> updated;
	std::vector<sgre::scene::LayerHandle> drawn;
	std::size_t preparations = 0;
	std::size_t target_draws = 0;
	sgre::scene::ScreenRegistryLimits limits;
	limits.MaxScreens = 3;
	sgre::scene::ScreenRegistry screens(tree, [&](const auto& description) {
		Require(target_count < target_observations.size(), "bounded fixture target allocation count exceeded");
		auto& observation = target_observations[target_count++];
		observation.Tree = &tree;

		return MakeTarget(graphics, description, observation);
	},
	    limits);
	sgre::scene::ScreenHandle current;
	sgre::scene::LayerHandle guarded_layer;
	sgre::scene::ScreenHandle appended;
	sgre::scene::LayerHandle appended_child;
	sgre::scene::ScreenDriverServices services;
	services.ResolveTasks = [&]() -> sgre::scene::ParallelTaskSet& {
		++resolutions;
		if (!driver_available) {
			throw std::runtime_error("actual containing SQDriver setup has not published its +130 group");
		}

		return actual_driver_group;
	};
	services.Layers.State = [&](auto layer) {
		Require(tree.Contains(layer), "actual Screen frame must resolve its live Layer owner");

		return sgre::scene::LayerFrameProcessState{};
	};
	services.Layers.Update = [&](auto layer) {
		updated.push_back(layer);
		if (current.Owner && layer == guarded_layer) {
			RequireError([&] {
				screens.Release(current);
			},
			    "current frame");
			Require(!screens.CanClose(), "actual current Screen frame must exclude containing destruction");
			const auto allocations = target_count;
			appended = screens.Create({5, 3, false});
			target_observations[1].Root = screens.State(appended).LayerRoot;
			appended_child = tree.CreateMotionInScreen(target_observations[1].Root);
			Require(target_count == allocations + 1, "actual native tail insertion during Screen update must retain one genuine new target");
		}
	};
	services.Layers.Draw = [&](auto layer, auto) {
		drawn.push_back(layer);
	};
	services.Layers.PrepareDraw = [&] {
		++preparations;
	};
	services.Draw = [&](auto screen, auto& target, auto&, const auto& state, auto argument) {
		++target_draws;
		Require(screen == current && &target == &screens.Renderer(screen) && state.LayerRoot == screens.State(screen).LayerRoot && argument == 0xfedcba98, "actual Screen draw must lend the exact target/manager/state and complete argument");
		Require(!screens.CanClose(), "actual Screen target callback must retain its payload through exception unwinding");
		throw std::runtime_error("native4fa250 target-switch drawing remains an explicit containing graphics boundary");
	};
	screens.ConfigureDriver(std::move(services));
	RequireError([&] {
		static_cast<void>(screens.Create({4, 4, false}));
	},
	    "+130 group");
	Require(target_count == 0 && screens.Count() == 0 && actual_driver_group.Count() == 1, "premature provider failure must precede every target/root/task mutation");
	driver_available = true;
	const auto first = screens.Create({4, 4, false});
	target_observations[0].Root = screens.State(first).LayerRoot;
	{
		const auto actual_target = target_observations[0].Allocated.lock();
		Require(actual_target && actual_target.get() == &screens.Renderer(first), "Screen target must be the actual shared graphics allocation");
		const auto previous = graphics.SelectTarget(actual_target);
		const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
		const auto submitted = graphics.Submit({}, identity, {0.5f, 0.25f, 0, 1});
		const auto pixels = actual_target->Capture(submitted.Identity);
		Require(submitted.Owner == actual_target.get() && pixels.Width == 4 && pixels.Height == 4 && pixels.Rgba.size() == 64, "registered Screen target must produce its own actual offscreen submission");
		for (std::size_t offset = 0; offset < pixels.Rgba.size(); offset += 4) {
			Require(pixels.Rgba[offset] == 128 && pixels.Rgba[offset + 1] == 64 && pixels.Rgba[offset + 2] == 0 && pixels.Rgba[offset + 3] == 255, "registered Screen allocation did not retain actual rendered clear pixels");
		}
		static_cast<void>(graphics.SelectTarget(previous.lock()));
	}
	const auto first_child = tree.CreateMotionInScreen(target_observations[0].Root);
	current = first;
	guarded_layer = first_child;
	actual_driver_group.Update();
	Require(updated == std::vector{first_child, appended_child} && sibling_updates == 1, "actual Screen tail appended by current Layer update must be visited in the same native group pass");
	current = {};
	guarded_layer = {};
	updated.clear();
	std::array<sgre::scene::ParallelTaskHandle, 4> actual_order;
	static_cast<void>(actual_driver_group.WriteOrder(actual_order));
	const auto first_task = actual_order[1];
	const auto second = appended;
	const auto second_child = appended_child;
	static_cast<void>(actual_driver_group.WriteOrder(actual_order));
	const auto second_task = actual_order[2];
	const auto third = screens.Create({3, 5, true});
	target_observations[2].Root = screens.State(third).LayerRoot;
	const auto third_child = tree.CreateMotionInScreen(target_observations[2].Root);
	static_cast<void>(actual_driver_group.WriteOrder(actual_order));
	Require(actual_order[1] == sibling_task && actual_order[2] == first_task && actual_order[3] == second_task, "native nonzero insertion must prepend the actual +130 task ahead of existing siblings");
	std::array<sgre::scene::ScreenHandle, 3> visible_order;
	static_cast<void>(screens.WriteOrder(visible_order));
	Require(visible_order == std::array{third, first, second}, "native wrapper pair order must match Screen front/tail insertion");
	actual_driver_group.Update();
	Require(updated == std::vector{third_child, first_child, second_child} && sibling_updates == 2, "actual group Update must visit each separate live Screen Layer manager");
	current = first;
	screens.SetVisible(second, false);
	screens.SetVisible(third, false);
	Require(actual_driver_group.State(second_task).DrawPaused && !screens.State(second).Visible, "native inverse hidden byte must reach actual task DrawPaused");
	RequireError([&] {
		actual_driver_group.Draw(0xfedcba98);
	},
	    "target-switch");
	Require(target_draws == 1 && drawn.empty() && preparations == 0 && screens.CanClose(), "actual unsupported target switch must unwind guards without inventing inner manager drawing");
	current = {};
	screens.Release(first);
	Require(!screens.Contains(first) && screens.Count() == 2 && actual_driver_group.Contains(first_task) && actual_driver_group.State(first_task).Status == sgre::scene::TaskStatus::Retire, "native23a770 must erase wrapper identity and mark the actual task Retire without physical deletion");
	Require(target_observations[0].Releases == 0 && tree.ContainsScreenRoot(target_observations[0].Root), "pending native task must retain its actual target and manager");
	RequireError([&] {
		static_cast<void>(screens.Create({4, 4, false}));
	},
	    "pending native task");
	actual_driver_group.Update();
	Require(target_observations[0].Releases == 1 && target_observations[0].RootLiveAtRelease && !tree.ContainsScreenRoot(target_observations[0].Root) && !actual_driver_group.Contains(first_task), "physical group retirement must release target before manager/root exactly once");
	Require(tree.Contains(first_child) && !tree.State(first_child).Attached, "native manager retirement must detach a surviving separately owned Layer");
	const auto replacement = screens.Create({4, 4, false});
	target_observations[3].Root = screens.State(replacement).LayerRoot;
	Require(replacement.Index == first.Index && replacement.Generation != first.Generation && !screens.Contains(first), "Screen slot may be reused only after actual task retirement with a fresh identity");
	sibling_borrowed = true;
	Require(!actual_driver_group.CanClear() && screens.CanClose(), "unrelated sibling release veto must not prevent exact Screen containing cleanup");
	screens.Close();
	Require(actual_driver_group.Count() == 1 && actual_driver_group.Contains(sibling_task) && sibling_releases == 0, "Screen close must extract only exact own registrations");
	for (const auto& observation : target_observations) {
		Require(observation.Releases == 1 && observation.RootLiveAtRelease && !tree.ContainsScreenRoot(observation.Root), "every real target must retire once before its actual root");
	}
	RequireError([&] {
		static_cast<void>(screens.Create({4, 4, false}));
	},
	    "retired");
	sibling_borrowed = false;
	actual_driver_group.Clear();
	Require(sibling_releases == 1 && resolutions == 5, "own cleanup must leave unrelated callback retirement and actual resolver calls intact");
}

static void TestConstructorManagerOrderAndRollback(const std::filesystem::path& shader) {
	sgre::render::SpritePassDescription primary;
	primary.Width = 4;
	primary.Height = 4;
	sgre::render::SpriteGraphicsTargets graphics(primary, shader);
	static_cast<void>(graphics.SelectTarget(graphics.PrimaryTarget()));
	sgre::scene::LayerTree tree(1);
	std::array<sgre::scene::LayerScreenRootHandle, 63> existing;
	for (auto& root : existing) {
		root = tree.CreateScreenRoot();
	}
	bool fail_factory = true;
	TargetRetirement retirement;
	retirement.Tree = &tree;
	sgre::scene::ScreenRegistry screens(tree, [&](const auto& description) {
		RequireError([&] {
			static_cast<void>(tree.CreateScreenRoot());
		},
		    "screen root capacity");
		if (fail_factory) {
			throw std::runtime_error("actual target factory failed after manager creation");
		}

		return MakeTarget(graphics, description, retirement);
	});
	RequireError([&] {
		static_cast<void>(screens.Create({4, 4, false}));
	},
	    "after manager creation");
	Require(screens.Count() == 0, "failed Screen target allocation published a wrapper or retained pixel storage");
	const auto available = tree.CreateScreenRoot();
	tree.ReleaseScreenRoot(available);
	fail_factory = false;
	const auto actual = screens.Create({4, 4, false});
	retirement.Root = screens.State(actual).LayerRoot;
	screens.Release(actual);
	Require(retirement.Releases == 1 && retirement.RootLiveAtRelease, "Screen rollback/retry must retain target-before-manager destruction");
	screens.Close();
	for (const auto root : existing) {
		tree.ReleaseScreenRoot(root);
	}
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: screen_driver_frame_tests SPRITE_SHADER");
		TestConstructorManagerOrderAndRollback(argv[1]);
		TestActualGroupAndDeferredPayload(argv[1]);
		std::cout << "actual Screen task insertion/update/deferred target-before-manager retirement and honest target-draw boundary passed; no native target-switch fidelity claim\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
