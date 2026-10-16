#include "sgre/scene/layer_manager_frame.hpp"

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

	throw std::runtime_error("expected actual manager frame safety rejection");
}

static void TestStatusVisibilityPriorityAndArgument() {
	sgre::scene::LayerTree tree(8);
	const auto owner = tree.CreateFolder();
	const auto first = tree.CreateMotion(owner);
	const auto second = tree.CreateMotion(owner);
	const auto third = tree.CreateMotion(owner);
	tree.SetVisible(first, false);
	tree.SetPriority(second, -2);
	std::array<sgre::scene::LayerFrameProcessState, 8> states{};
	states[second.Index].Status = sgre::scene::TaskStatus::Complete;
	states[third.Index].Status = sgre::scene::TaskStatus::Retire;
	std::vector<sgre::scene::LayerHandle> updated;
	std::vector<sgre::scene::LayerHandle> drawn;
	std::size_t renderer_preparations = 0;
	sgre::scene::LayerManagerFrameOperations operations;
	operations.State = [&](auto layer) {
		return states[layer.Index];
	};
	operations.Update = [&](auto layer) {
		updated.push_back(layer);
	};
	operations.Draw = [&](auto layer, std::uint32_t argument) {
		Require(argument == 0xfedcba98 && renderer_preparations == 1, "actual manager forwards the complete DWORD after concrete renderer preparation");
		drawn.push_back(layer);
	};
	operations.PrepareDraw = [&] {
		++renderer_preparations;
		std::array<sgre::scene::LayerHandle, 3> order;
		static_cast<void>(tree.WriteChildren(owner, order));
		Require(order[0] == second, "native dirty priority sorting happens before actual renderer preparation");
	};
	sgre::scene::LayerManagerFrame frame(tree, owner, std::move(operations));
	frame.Update();
	Require(updated == std::vector<sgre::scene::LayerHandle>{first} && tree.ChildCount(owner) == 3, "native update ignores visibility but skips Complete/Retire without unlinking actual child owners");
	frame.Draw(0xfedcba98);
	Require(drawn == std::vector<sgre::scene::LayerHandle>{second}, "native draw visits visible Complete and suppresses hidden/Retire owners");
	states[second.Index].Status = sgre::scene::TaskStatus::Active;
	updated.clear();
	frame.Update();
	Require(updated == std::vector<sgre::scene::LayerHandle>{second, first}, "next update consumes the same actual list after the previous draw sort");
}

static void TestActualScreenLiveMembership() {
	sgre::scene::LayerTree tree(8);
	const auto screen = tree.CreateScreenRoot();
	const auto first = tree.CreateMotionInScreen(screen);
	const auto second = tree.CreateMotionInScreen(screen);
	sgre::scene::LayerHandle appended;
	std::vector<sgre::scene::LayerHandle> updated;
	std::vector<sgre::scene::LayerHandle> drawn;
	std::size_t preparations = 0;
	sgre::scene::LayerManagerFrameOperations operations;
	operations.State = [](auto) {
		return sgre::scene::LayerFrameProcessState{};
	};
	operations.Update = [&](auto layer) {
		updated.push_back(layer);
		if (layer == first) {
			tree.Detach(second);
			appended = tree.CreateMotionInScreen(screen);
		}
	};
	operations.Draw = [&](auto layer, auto) {
		drawn.push_back(layer);
	};
	operations.PrepareDraw = [&] {
		++preparations;
	};
	sgre::scene::LayerManagerFrame frame(tree, screen, std::move(operations));
	frame.Update();
	Require(updated == std::vector<sgre::scene::LayerHandle>{first, appended}, "actual Screen owner list supports native same-pass append and future detach without a copied owner registry");
	frame.Draw(0);
	Require(preparations == 1 && drawn == updated && tree.Contains(second), "detached future script owner remains alive while genuine attached Screen members draw");
}

static void TestConcreteServicesWorkersAndReentry() {
	sgre::scene::LayerTree tree(8);
	const auto screen = tree.CreateScreenRoot();
	const auto child = tree.CreateMotionInScreen(screen);
	RequireError([&] {
		sgre::scene::LayerManagerFrame frame(tree, screen, {});
	},
	    "concrete state");
	std::size_t calls = 0;
	sgre::scene::LayerFrameProcessState state;
	state.ThreadedUpdate = true;
	sgre::scene::LayerManagerFrame* current_frame = nullptr;
	sgre::scene::LayerManagerFrameOperations operations;
	operations.State = [&](auto layer) {
		Require(layer == child, "frame service must receive the actual existing owner handle");

		return state;
	};
	operations.Update = [&](auto) {
		++calls;
	};
	operations.Draw = [&](auto, auto) {
		++calls;
	};
	operations.PrepareDraw = [&] {
		RequireError([&] {
			current_frame->Draw(0);
		},
		    "cannot reenter");
	};
	sgre::scene::LayerManagerFrame frame(tree, screen, std::move(operations));
	current_frame = &frame;
	RequireError([&] {
		frame.Update();
	},
	    "verified worker dispatcher");
	Require(calls == 0 && tree.ScreenChildCount(screen) == 1, "unsupported native worker scheduling must not be replaced by synchronous callbacks or unlinking");
	state.Status = sgre::scene::TaskStatus::Complete;
	frame.Update();
	Require(calls == 0, "native Complete status gates threaded dispatch before any worker domain check");
	frame.Draw(0);
	Require(calls == 1, "draw remains independent from threaded-update flag and restores its concrete frame guard");
}

int main() {
	try {
		TestStatusVisibilityPriorityAndArgument();
		TestActualScreenLiveMembership();
		TestConcreteServicesWorkersAndReentry();
		std::cout << "3 actual Layer manager frame groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
