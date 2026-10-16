#include "sgre/scene/motion_layer_frame.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("expected frame lifetime preflight rejection");
}

static sgre::scene::LayerCameraFrameState State() {
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {1280, 720};
	state.RawScissor = {0, 0, 1280, 720};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;

	return state;
}

static sgre::scene::MotionLayerSourceDrawing SourceObservers(std::size_t& calls) {
	sgre::scene::MotionLayerSourceDrawing sources;
	sources.Secondary = [&](auto) {
		++calls;
	};
	sources.Primary = [&](auto) {
		++calls;
	};

	return sources;
}

static void TestDetachedAndTransferredAdapterLease() {
	sgre::scene::CommonProcessTasks process(8);
	sgre::scene::MotionLayerTasks groups(8);
	sgre::scene::LayerCameraDrawScope renderer(State(), {}, [](const auto&) {
		throw std::runtime_error("lifetime fixture unexpectedly submitted renderer work");
	});
	std::size_t source_calls = 0;
	sgre::scene::MotionLayerFrame frame(
	    process, groups, renderer, [] {
		    return sgre::scene::MotionLayerFrameState{};
	    },
	    SourceObservers(source_calls));
	std::array<sgre::scene::ParallelTaskHandle, 3> order;
	static_cast<void>(process.Parallel().WriteOrder(order));
	auto detached = process.Parallel().Extract(order[0]);
	Require(!frame.CanClose(), "a detached root lease still owns callbacks that borrow this attachment");
	RequireError([&] {
		frame.Close();
	});
	sgre::scene::ParallelTaskSet destination(8);
	const auto transferred = destination.AppendLease(std::move(detached));
	Require(!frame.CanClose() && destination.Contains(transferred), "transferring adapter ownership must not allow attachment destruction");
	destination.Clear();
	Require(frame.CanClose(), "actual adapter release must record that no detached borrowed callbacks remain");
	frame.Close();
	Require(process.Parallel().Count() == 0 && source_calls == 0, "borrowed adapter retirement must not fabricate source drawing");
}

static void TestClearedAdaptersAndDestructor() {
	sgre::scene::CommonProcessTasks process(8);
	sgre::scene::MotionLayerTasks groups(8);
	sgre::scene::LayerCameraDrawScope renderer(State(), {}, [](const auto&) {
		throw std::runtime_error("cleared fixture unexpectedly submitted renderer work");
	});
	std::size_t calls = 0;
	{
		sgre::scene::MotionLayerFrame frame(
		    process, groups, renderer, [] {
			    return sgre::scene::MotionLayerFrameState{};
		    },
		    SourceObservers(calls));
		process.Parallel().Clear();
		Require(frame.CanClose(), "completed adapter cleanup is distinct from an outstanding detached lease");
	}
	Require(process.Parallel().Count() == 0 && process.CanClear() && calls == 0, "stable destruction removes no actual child/source owner");
}

[[noreturn]] static void ExitAfterExpectedFrameTermination() noexcept {
	std::_Exit(0);
}

[[noreturn]] static void ExitAfterIsolatedFrameTermination() noexcept {
	std::_Exit(86);
}

static int DestroyDuringCallback(bool verify) {
	sgre::scene::CommonProcessTasks process(8);
	sgre::scene::MotionLayerTasks groups(8);
	sgre::scene::LayerCameraDrawScope renderer(State(), {}, [](const auto&) {
		throw std::runtime_error("death fixture unexpectedly submitted renderer work");
	});
	std::size_t calls = 0;
	auto frame = std::make_unique<sgre::scene::MotionLayerFrame>(
	    process, groups, renderer, [] {
		    return sgre::scene::MotionLayerFrameState{};
	    },
	    SourceObservers(calls));
	sgre::scene::ParallelTaskCallbacks child;
	child.Update = [&](auto) {
		std::set_terminate(verify ? ExitAfterExpectedFrameTermination : ExitAfterIsolatedFrameTermination);
		frame.reset();
	};
	static_cast<void>(groups.Group(sgre::scene::MotionTaskGroup::First).Append(std::move(child)));
	frame->Update([&] {
		++calls;
	});

	return 1;
}

static int DestroyWithDetachedLease(bool verify) {
	sgre::scene::CommonProcessTasks process(8);
	sgre::scene::MotionLayerTasks groups(8);
	sgre::scene::LayerCameraDrawScope renderer(State(), {}, [](const auto&) {
		throw std::runtime_error("lease death fixture unexpectedly submitted renderer work");
	});
	std::size_t calls = 0;
	auto frame = std::make_unique<sgre::scene::MotionLayerFrame>(
	    process, groups, renderer, [] {
		    return sgre::scene::MotionLayerFrameState{};
	    },
	    SourceObservers(calls));
	std::array<sgre::scene::ParallelTaskHandle, 3> order;
	static_cast<void>(process.Parallel().WriteOrder(order));
	const auto outstanding = process.Parallel().Extract(order[0]);
	std::set_terminate(verify ? ExitAfterExpectedFrameTermination : ExitAfterIsolatedFrameTermination);
	frame.reset();

	return 1;
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--destroy-in-callback") {
			return DestroyDuringCallback(false);
		}
		if (argc == 2 && std::string_view(argv[1]) == "--destroy-with-lease") {
			return DestroyWithDetachedLease(false);
		}
		if (argc == 2 && std::string_view(argv[1]) == "--verify-in-callback") {
			return DestroyDuringCallback(true);
		}
		if (argc == 2 && std::string_view(argv[1]) == "--verify-with-lease") {
			return DestroyWithDetachedLease(true);
		}
		Require(argc == 1, "unsupported frame lifetime fixture arguments");
		TestDetachedAndTransferredAdapterLease();
		TestClearedAdaptersAndDestructor();
		std::cout << "2 frame lifetime groups passed; isolated death mode expects exit86\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
