#include "sgre/render/sprite_pass.hpp"
#include "sgre/scene/screens.hpp"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

struct TargetRetirementObservation {
	sgre::scene::LayerTree& Tree;
	sgre::scene::LayerScreenRootHandle Root{};
	std::size_t Releases = 0;
	bool RootLiveAtTargetRelease = false;
};

struct ObservedTargetOwner {
	std::unique_ptr<sgre::render::SpritePassRenderer> Renderer;
	TargetRetirementObservation& Observation;

	~ObservedTargetOwner() noexcept {
		++Observation.Releases;
		Observation.RootLiveAtTargetRelease = Observation.Tree.ContainsScreenRoot(Observation.Root);
	}
};

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::scene::ScreenTargetAllocation AllocateTarget(const sgre::scene::ScreenDescription& description, const std::filesystem::path& shader, TargetRetirementObservation& observation) {
	sgre::render::SpritePassDescription target;
	target.Width = description.Width;
	target.Height = description.Height;
	target.MaxVertices = 3;
	target.MaxIndices = 3;
	target.MaxDraws = 1;
	auto renderer = std::make_unique<sgre::render::SpritePassRenderer>(target, shader);
	auto owner = std::make_shared<ObservedTargetOwner>(std::move(renderer), observation);
	sgre::scene::ScreenTargetAllocation allocation;
	allocation.Width = description.Width;
	allocation.Height = description.Height;
	allocation.Renderer = std::shared_ptr<sgre::render::SpritePassRenderer>(owner, owner->Renderer.get());

	return allocation;
}

static void TestRetirement(const std::filesystem::path& shader, bool explicit_release) {
	sgre::scene::LayerTree tree(4);
	TargetRetirementObservation observation{tree};
	sgre::scene::ScreenHandle screen;
	{
		sgre::scene::ScreenRegistry screens(tree, [&](const sgre::scene::ScreenDescription& description) {
			return AllocateTarget(description, shader, observation);
		});
		sgre::scene::ScreenDescription description;
		description.Width = 4;
		description.Height = 4;
		screen = screens.Create(description);
		observation.Root = screens.State(screen).LayerRoot;
		Require(tree.ContainsScreenRoot(observation.Root), "actual screen root must enclose its target lifetime");
		if (explicit_release) {
			const auto child = tree.CreateFolderInScreen(observation.Root);
			tree.VisitScreenChildren(observation.Root, [&](auto actual_child) {
				Require(actual_child == child, "screen traversal must retain its actual child");
				bool rejected = false;
				try {
					screens.Release(screen);
				} catch (const std::runtime_error&) {
					rejected = true;
				}
				Require(rejected && observation.Releases == 0 && screens.Contains(screen) && tree.ContainsScreenRoot(observation.Root), "busy release must preserve target and manager before mutation");
			});
			screens.Release(screen);
			Require(!screens.Contains(screen), "released screen must retire its registry identity");
			Require(observation.Releases == 1 && !tree.ContainsScreenRoot(observation.Root), "explicit release must retire both concrete owners");
		}
	}

	Require(observation.Releases == 1, "real target must have exactly one last-owner release");
	Require(observation.RootLiveAtTargetRelease, "native target must retire before its layer manager");
	Require(!tree.ContainsScreenRoot(observation.Root), "layer manager must retire after the actual target");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: screen_retirement_order_tests sprite_shader");
		TestRetirement(argv[1], true);
		TestRetirement(argv[1], false);
		std::cout << "explicit and containing destruction release actual Metal target before layer root\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
