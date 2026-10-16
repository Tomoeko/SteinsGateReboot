#include "sgre/scene/layers.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected layer rejection: " + std::string(message));
}

static void TestMotionLeafOwnership() {
	sgre::scene::LayerTree tree(4);
	const auto folder = tree.CreateFolder();
	const auto motion = tree.CreateMotion(folder);
	const auto peer = tree.CreateFolder();
	Require(tree.State(folder).Kind == sgre::scene::LayerKind::Folder && tree.State(motion).Kind == sgre::scene::LayerKind::Motion, "folder and motion leaf identities must remain distinct");
	Require(tree.State(motion).Owner == folder && tree.ChildCount(folder) == 1 && tree.State(motion).Visible && tree.State(motion).Transform.Scale == sgre::scene::LayerPoint{1, 1}, "motion leaf common native defaults or folder membership differ");
	RequireError([&] {
		tree.SetOwner(peer, motion);
	},
	    "folder child manager");
	Require(tree.State(peer).Attached && !tree.State(peer).Owner && tree.ChildCount() == 2 && tree.ChildCount(folder) == 1, "rejected motion owner must preserve previous membership");
	RequireError([&] {
		static_cast<void>(tree.CreateFolder(motion));
	},
	    "folder child manager");
	RequireError([&] {
		static_cast<void>(tree.CreateMotion(motion));
	},
	    "folder child manager");
	RequireError([&] {
		tree.Clear(motion);
	},
	    "folder child manager");
	RequireError([&] {
		static_cast<void>(tree.ChildCount(motion));
	},
	    "folder child manager");
	tree.SetZoom(folder, 2);
	tree.SetZoom(motion, 3);
	Require(tree.WorldTransform(motion).XX == 6, "motion leaf must inherit its folder camera transform");
	tree.Release(folder);
	Require(tree.Contains(motion) && !tree.State(motion).Attached && !tree.State(motion).Owner, "folder retirement must detach its surviving motion leaves");
	const auto screen = tree.CreateScreenRoot();
	tree.SetScreenOwner(motion, screen);
	const auto other = tree.CreateMotionInScreen(screen);
	Require(tree.ScreenChildCount(screen) == 2 && tree.State(other).Kind == sgre::scene::LayerKind::Motion, "screen manager must retain genuine motion leaves");
	tree.SetPriority(other, -1);
	tree.PrepareScreenChildren(screen);
	std::array<sgre::scene::LayerHandle, 2> order;
	Require(tree.WriteScreenChildren(screen, order) == 2 && order[0] == other && order[1] == motion, "motion leaves must participate in native manager priority ordering");
	tree.Release(motion);
	Require(!tree.Contains(motion) && tree.ScreenChildCount(screen) == 1 && tree.Contains(other), "motion leaf retirement must remove its own membership only");
	const auto reused = tree.CreateFolder();
	Require(reused.Index == motion.Index && reused.Generation != motion.Generation && tree.State(reused).Kind == sgre::scene::LayerKind::Folder, "reused motion slot must reset kind and generation");
	tree.ReleaseScreenRoot(screen);
	Require(tree.Contains(other) && !tree.State(other).Attached && !tree.State(other).ScreenOwner, "screen retirement must detach surviving motion leaves");
}

int main() {
	try {
		TestMotionLeafOwnership();
		using sgre::scene::LayerHandle;
		sgre::scene::LayerTree tree(8);
		const auto folder = tree.CreateFolder();
		const auto first = tree.CreateFolder(folder);
		const auto second = tree.CreateFolder(folder);
		const auto third = tree.CreateFolder(folder);
		const auto grandchild = tree.CreateFolder(first);
		const auto initial = tree.State(first);
		Require(initial.Owner == folder && initial.Attached && initial.Visible && !initial.Smoothing && initial.Priority == 0, "native layer ownership/visibility defaults differ");
		Require(initial.Transform.Scale == sgre::scene::LayerPoint{1, 1} && initial.Transform.Offset == sgre::scene::LayerPoint{} && initial.Clip == sgre::scene::LayerClip{}, "native camera/clip defaults differ");
		std::array<LayerHandle, 8> children;
		Require(tree.WriteChildren(folder, children) == 3 && children[0] == first && children[1] == second && children[2] == third, "children lost tail registration order");
		tree.SetPriority(first, 7);
		tree.SetPriority(second, -2);
		tree.SetPriority(third, -2);
		Require(tree.WriteChildren(folder, children) == 3 && children[0] == first, "priority mutation sorted before manager preparation");
		tree.PrepareChildren(folder);
		Require(tree.WriteChildren(folder, children) == 3 && children[0] == second && children[1] == third && children[2] == first, "ascending priority or stable ties differ");
		tree.SetOwner(second, folder);
		tree.PrepareChildren(folder);
		Require(tree.WriteChildren(folder, children) == 3 && children[0] == third && children[1] == second && children[2] == first, "same-owner reassignment failed to reappend before stable sorting");
		tree.SetPriority(first, -2);
		tree.PrepareChildren(folder);
		Require(tree.WriteChildren(folder, children) == 3 && children[0] == third && children[1] == second && children[2] == first, "new tie lost previously sorted manager order");
		RequireError([&] {
			tree.SetOwner(folder, grandchild);
		},
		    "cycle");
		Require(tree.State(folder).Attached && tree.ChildCount() == 1, "rejected cycle mutated root ownership");
		tree.Clear(folder);
		Require(tree.ChildCount(folder) == 0 && tree.ChildCount() == 1 && !tree.State(first).Owner && !tree.State(first).Attached && tree.Contains(first), "clear moved or destroyed detached children");
		Require(tree.State(grandchild).Owner == first && tree.State(grandchild).Attached, "clear detached descendants beyond the direct child manager");
		tree.SetOwner(first, {});
		Require(tree.State(first).Attached && !tree.State(first).Owner && tree.ChildCount() == 2, "null owner failed to reattach the root manager");
		tree.SetOffset(first, {15, -9});
		tree.SetCenter(first, {8, 4});
		tree.SetScale(first, {-2, 3});
		tree.SetZoom(first, 5);
		tree.SetAngleDegrees(first, 180);
		Require(std::bit_cast<std::uint32_t>(tree.State(first).Transform.AngleRadians) == 0x40490fdb && tree.AngleDegrees(first) == 180, "native angle conversion lost float32 constants/order");
		tree.SetVisible(first, false);
		tree.SetSmoothing(first, true);
		Require(tree.State(first).Transform.Offset == sgre::scene::LayerPoint{15, -9} && tree.State(first).Transform.Center == sgre::scene::LayerPoint{8, 4} && tree.State(first).Transform.Scale == sgre::scene::LayerPoint{5, 5} && !tree.State(first).Visible && tree.State(first).Smoothing, "independent layer state setters interfered");
		tree.SetBaseClip(first, {8, 9}, {-2, 4});
		tree.SetClip(first, {3, 4}, {5, 6});
		Require(tree.State(first).BaseClip == sgre::scene::LayerClip{8, 9, 6, 13} && tree.State(first).Clip == sgre::scene::LayerClip{3, 4, 8, 10}, "native clip endpoints or negative extents differ");
		tree.ResetBaseClip(first);
		tree.ResetClip(first);
		Require(tree.State(first).BaseClip == sgre::scene::LayerClip{} && tree.State(first).Clip == sgre::scene::LayerClip{}, "clip reset lost native disabled sentinel");
		sgre::scene::LayerTransform transform;
		transform.Center = {8, 4};
		transform.Offset = {15, -9};
		transform.Scale = {2, 3};
		const auto affine = sgre::scene::PrepareLayerLocalTransform(transform);
		Require(affine.XX == 2 && affine.XY == 0 && affine.TranslationX == -23 && affine.YX == 0 && affine.YY == 3 && affine.TranslationY == 1, "center/scale/negative camera offset composition differs");
		const auto point = sgre::scene::TransformLayerPoint(affine, {12, 7});
		Require(point == sgre::scene::LayerPoint{1, 22} && sgre::scene::InverseTransformLayerPoint(affine, point) == sgre::scene::LayerPoint{12, 7}, "world/local coordinate arithmetic differs");
		tree.SetAngleRadians(first, 0);
		tree.SetOffset(first, {1, 2});
		tree.SetCenter(first, {});
		tree.SetZoom(first, 2);
		tree.SetOffset(folder, {3, 4});
		tree.SetZoom(folder, 3);
		tree.SetOwner(first, folder);
		const auto world = tree.WorldTransform(first);
		Require(world.XX == 6 && world.YY == 6 && world.TranslationX == -6 && world.TranslationY == -10, "folder ancestry composition reversed matrix order");
		tree.SetOwner(first, {});
		tree.SetZoom(first, 5);
		RequireError([&] {
			static_cast<void>(sgre::scene::InverseTransformLayerPoint({}, {std::numeric_limits<float>::quiet_NaN(), 0}));
		},
		    "nonfinite");
		sgre::scene::LayerAffineTransform singular;
		singular.XX = 0;
		RequireError([&] {
			static_cast<void>(sgre::scene::InverseTransformLayerPoint(singular, {}));
		},
		    "singular");
		RequireError([&] {
			tree.SetScale(first, {std::numeric_limits<float>::infinity(), 0});
		},
		    "finite");
		Require(tree.State(first).Transform.Scale == sgre::scene::LayerPoint{5, 5}, "nonfinite rejection partially mutated camera state");
		RequireError([&] {
			static_cast<void>(tree.WriteChildren({}, std::span<LayerHandle>{}));
		},
		    "too small");
		tree.Release(first);
		Require(!tree.Contains(first) && tree.Contains(grandchild) && !tree.State(grandchild).Attached, "release failed to invalidate one owner and retain detached children");
		const auto reused = tree.CreateFolder();
		Require(reused.Index == first.Index && reused.Generation != first.Generation, "reused layer slot lost generation identity");
		RequireError([&] {
			static_cast<void>(tree.State(first));
		},
		    "stale");
		const auto screen = tree.CreateScreenRoot();
		const auto screen_child = tree.CreateFolderInScreen(screen);
		const auto nested = tree.CreateFolder(screen_child);
		Require(tree.ScreenChildCount(screen) == 1 && tree.State(screen_child).ScreenOwner == screen && !tree.State(screen_child).Owner && tree.State(nested).Owner == screen_child, "screen manager ownership lost its distinct root identity");
		tree.SetScreenOwner(reused, screen);
		tree.SetPriority(reused, -3);
		tree.PrepareScreenChildren(screen);
		Require(tree.WriteScreenChildren(screen, children) == 2 && children[0] == reused && children[1] == screen_child, "screen manager lost stable priority preparation");
		tree.SetZoom(screen_child, 3);
		tree.SetZoom(nested, 2);
		Require(tree.WorldTransform(nested).XX == 6, "screen root unexpectedly added a folder transform");
		tree.ReleaseScreenRoot(screen);
		Require(!tree.ContainsScreenRoot(screen) && tree.Contains(screen_child) && !tree.State(screen_child).Attached && !tree.State(screen_child).ScreenOwner && tree.State(nested).Owner == screen_child, "screen root release moved or destroyed direct children or descendants");
		tree.SetOwner(reused, {});
		Require(tree.State(reused).Attached && !tree.State(reused).ScreenOwner, "screen-detached layer failed to rejoin the global manager");
		RequireError([&] {
			tree.SetScreenOwner(reused, screen);
		},
		    "stale");
		sgre::scene::LayerTree foreign(1);
		RequireError([&] {
			tree.SetOwner(reused, foreign.CreateFolder());
		},
		    "another tree");
		RequireError([&] {
			static_cast<void>(foreign.CreateFolder());
		},
		    "capacity");
		std::cout << "Layer manager ownership, stable priorities, camera state, clip sentinels, and bounded handles passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
