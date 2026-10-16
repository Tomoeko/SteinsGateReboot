#include "sgre/scene/layers.hpp"

#include <array>
#include <cstdint>
#include <iostream>
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
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected actual manager traversal safety rejection");
}

static void TestLiveAppendAndFutureRemoval() {
	sgre::scene::LayerTree tree(8);
	const auto owner = tree.CreateFolder();
	const auto first = tree.CreateMotion(owner);
	const auto second = tree.CreateMotion(owner);
	const auto third = tree.CreateMotion(owner);
	std::vector<sgre::scene::LayerHandle> visited;
	sgre::scene::LayerHandle appended;
	tree.VisitChildren(owner, [&](auto current) {
		visited.push_back(current);
		if (current == first) {
			tree.Detach(second);
			appended = tree.CreateMotion(owner);
		}
	});
	Require(visited == std::vector<sgre::scene::LayerHandle>{first, third, appended}, "future removal and tail creation must change the native live traversal in this pass");
	Require(tree.Contains(second) && !tree.State(second).Attached && tree.ChildCount(owner) == 3, "detached future owners retain their actual lifetime independently of manager membership");
}

static void TestPriorRemovalAndSameOwnerFutureReappend() {
	sgre::scene::LayerTree tree(8);
	const auto owner = tree.CreateFolder();
	const auto first = tree.CreateMotion(owner);
	const auto second = tree.CreateMotion(owner);
	const auto third = tree.CreateMotion(owner);
	std::vector<sgre::scene::LayerHandle> visited;
	tree.VisitChildren(owner, [&](auto current) {
		visited.push_back(current);
		if (current == second) {
			tree.Release(first);
		}
	});
	Require(visited == std::vector<sgre::scene::LayerHandle>{first, second, third} && !tree.Contains(first), "prior owner retirement must preserve the current next link");
	const auto fourth = tree.CreateMotion(owner);
	visited.clear();
	tree.VisitChildren(owner, [&](auto current) {
		visited.push_back(current);
		if (current == second) {
			tree.SetOwner(third, owner);
		}
	});
	Require(visited == std::vector<sgre::scene::LayerHandle>{second, fourth, third}, "same-owner future reappend takes its native tail position during the same pass");
}

static void TestCurrentMemberRejectionBeforeMutation() {
	sgre::scene::LayerTree tree(8);
	const auto owner = tree.CreateFolder();
	const auto destination = tree.CreateFolder();
	const auto first = tree.CreateMotion(owner);
	const auto second = tree.CreateMotion(owner);
	tree.VisitChildren(owner, [&](auto current) {
		const auto before = tree.State(current);
		RequireError([&] {
			tree.RequireCanRelease(current);
		},
		    "current traversal member");
		RequireError([&] {
			tree.Release(current);
		},
		    "current traversal member");
		RequireError([&] {
			tree.Detach(current);
		},
		    "current traversal member");
		RequireError([&] {
			tree.SetOwner(current, destination);
		},
		    "current traversal member");
		Require(tree.Contains(current) && tree.State(current).Owner == before.Owner && tree.State(current).Attached, "self-removal safety rejection must preserve its actual current owner and membership");
	});
	Require(tree.ChildCount(owner) == 2 && tree.ChildCount(destination) == 0, "failed self-reparent must not detach or append before the guard");
	tree.Release(first);
	tree.Release(second);
	Require(tree.ChildCount(owner) == 0, "current-member guard must expire after traversal");
}

static void TestNestedManagerOwnerProtection() {
	sgre::scene::LayerTree tree(8);
	const auto owner = tree.CreateFolder();
	const auto nested = tree.CreateFolder(owner);
	const auto child = tree.CreateMotion(nested);
	std::vector<sgre::scene::LayerHandle> visited;
	tree.VisitChildren(owner, [&](auto current) {
		Require(current == nested, "outer native manager must visit its real folder child");
		tree.VisitChildren(nested, [&](auto inner) {
			visited.push_back(inner);
			RequireError([&] {
				tree.Release(nested);
			},
			    "current traversal member");
			RequireError([&] {
				tree.Clear(nested);
			},
			    "clear during child traversal");
		});
	});
	Require(visited == std::vector<sgre::scene::LayerHandle>{child} && tree.Contains(child), "different native child managers may nest without copying a separate registry");
	tree.VisitChildren(nested, [&](auto) {
		RequireError([&] {
			tree.RequireCanRelease(nested);
		},
		    "owner cannot retire");
	});
	const auto screen = tree.CreateScreenRoot();
	static_cast<void>(tree.CreateMotionInScreen(screen));
	tree.VisitScreenChildren(screen, [&](auto) {
		RequireError([&] {
			tree.ReleaseScreenRoot(screen);
		},
		    "clear during child traversal");
	});
	Require(tree.ContainsScreenRoot(screen) && tree.ScreenChildCount(screen) == 1, "an actual Screen manager survives rejected destruction from its own child");
}

static void TestSortPointAndCallbackPriorityChanges() {
	sgre::scene::LayerTree tree(8);
	const auto owner = tree.CreateFolder();
	const auto first = tree.CreateMotion(owner);
	const auto second = tree.CreateMotion(owner);
	const auto third = tree.CreateMotion(owner);
	tree.SetPriority(first, 7);
	tree.SetPriority(second, -2);
	tree.SetPriority(third, -2);
	std::vector<sgre::scene::LayerHandle> visited;
	tree.VisitChildren(owner, [&](auto current) {
		visited.push_back(current);
	});
	Require(visited == std::vector<sgre::scene::LayerHandle>{first, second, third}, "update traversal must not sort a dirty actual manager before native draw preparation");
	tree.PrepareChildren(owner);
	visited.clear();
	tree.VisitChildren(owner, [&](auto current) {
		visited.push_back(current);
		if (current == second) {
			tree.SetPriority(first, -5);
			RequireError([&] {
				tree.PrepareChildren(owner);
			},
			    "sort during child traversal");
		}
	});
	Require(visited == std::vector<sgre::scene::LayerHandle>{second, third, first}, "priority changes during draw preserve the prepared list and equal-priority history for this pass");
	visited.clear();
	tree.VisitChildren(owner, [&](auto current) {
		visited.push_back(current);
	});
	Require(visited == std::vector<sgre::scene::LayerHandle>{second, third, first}, "the next update uses the previous draw order before the next dirty preparation");
	tree.PrepareChildren(owner);
	std::array<sgre::scene::LayerHandle, 3> order;
	static_cast<void>(tree.WriteChildren(owner, order));
	Require(order == std::array<sgre::scene::LayerHandle, 3>{first, second, third}, "the following draw preparation consumes callback priority changes");
}

static void TestFailureVisitBoundsAndReentrancy() {
	sgre::scene::LayerTree tree(8);
	const auto owner = tree.CreateFolder();
	static_cast<void>(tree.CreateMotion(owner));
	static_cast<void>(tree.CreateMotion(owner));
	RequireError([&] {
		tree.VisitChildren(owner, {}, 1);
	},
	    "concrete visitor");
	RequireError([&] {
		tree.VisitChildren(
		    owner, [](auto) {
		    },
		    0);
	},
	    "bounded positive");
	tree.VisitChildren(owner, [&](auto) {
		RequireError([&] {
			tree.VisitChildren(owner, [](auto) {
			});
		},
		    "cannot reenter");
	});
	std::size_t visited = 0;
	RequireError([&] {
		tree.VisitChildren(
		    owner, [&](auto) {
			    ++visited;
		    },
		    1);
	},
	    "visit limit exceeded");
	Require(visited == 1, "bounded live manager traversal must stop before another native callback");
	RequireError([&] {
		tree.VisitChildren(owner, [](auto) {
			throw std::runtime_error("actual layer callback failure");
		});
	},
	    "actual layer callback failure");
	tree.Clear(owner);
	Require(tree.ChildCount(owner) == 0, "failed/rejected manager callbacks restore guards before actual owner cleanup");
}

int main() {
	try {
		TestLiveAppendAndFutureRemoval();
		TestPriorRemovalAndSameOwnerFutureReappend();
		TestCurrentMemberRejectionBeforeMutation();
		TestNestedManagerOwnerProtection();
		TestSortPointAndCallbackPriorityChanges();
		TestFailureVisitBoundsAndReentrancy();
		std::cout << "6 actual Layer manager traversal groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
