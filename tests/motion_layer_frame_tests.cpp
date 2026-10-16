#include "sgre/scene/motion_layer_frame.hpp"

#include <array>
#include <cstdint>
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
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected actual Motion frame safety rejection");
}

static sgre::scene::LayerCameraFrameState FrameState() {
	sgre::scene::LayerCameraFrameState state;
	state.View.Coefficients[0] = 2.0f / 1280;
	state.View.Coefficients[3] = -1;
	state.View.Coefficients[5] = -2.0f / 720;
	state.View.Coefficients[7] = 1;
	state.Viewport = {1280, 720};
	state.RawScissor = {0, 0, 1280, 720};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;

	return state;
}

static void TestActualFourGroupsAndSourceDrawOrder() {
	sgre::scene::CommonProcessTasks process(8);
	sgre::scene::MotionLayerTasks groups(8);
	std::vector<std::int32_t> events;
	std::vector<sgre::scene::MotionIntegerScissor> submissions;
	const std::array<sgre::scene::MotionScissorMatrix, 1> models{};
	sgre::scene::LayerCameraDrawScope renderer(FrameState(), models, [&](const auto& scissor) {
		submissions.push_back(scissor);
	});
	sgre::scene::MotionLayerFrameState state;
	state.Camera.Offset = {20, 30};
	state.ObjectClip = {0, 0, 100, 100};
	std::size_t releases = 0;
	for (const auto group : {sgre::scene::MotionTaskGroup::CommonFirst, sgre::scene::MotionTaskGroup::CommonSecond, sgre::scene::MotionTaskGroup::Second, sgre::scene::MotionTaskGroup::First}) {
		const auto identifier = static_cast<std::int32_t>(group);
		sgre::scene::ParallelTaskCallbacks child;
		child.Update = [&, identifier](auto) {
			events.push_back(identifier);
		};
		child.Draw = [&, identifier](auto, std::uint32_t argument) {
			Require(argument == 0xfedcba98, "actual group draw preserves the complete common DWORD argument");
			const bool common = identifier == static_cast<std::int32_t>(sgre::scene::MotionTaskGroup::CommonFirst) || identifier == static_cast<std::int32_t>(sgre::scene::MotionTaskGroup::CommonSecond);
			Require(renderer.Depth() == (common ? 1 : 2), "common tasks draw outside the actual Camera while concrete tasks observe its pushed model");
			events.push_back(10 + identifier);
		};
		child.Release = [&](auto) {
			++releases;
		};
		static_cast<void>(groups.Group(group).Append(std::move(child)));
	}
	sgre::scene::SerialTaskCallbacks serial;
	serial.Update = [&](auto&) {
		events.push_back(30);
	};
	serial.Draw = [&](auto&, std::uint32_t argument) {
		Require(argument == 0xfedcba98 && renderer.Depth() == 1 && renderer.State().RawScissor.MaximumX == 1280, "serial head draws after all real Camera/Clip scopes are restored");
		events.push_back(31);
	};
	static_cast<void>(process.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	sgre::scene::MotionLayerSourceDrawing drawing;
	drawing.Secondary = [&](std::uint32_t argument) {
		Require(argument == 0xfedcba98 && renderer.Depth() == 2, "secondary source publication occurs inside the actual camera before object clipping");
		events.push_back(20);
	};
	drawing.Primary = [&](std::uint32_t argument) {
		Require(argument == 0xfedcba98 && renderer.Depth() == 2 && renderer.State().RawScissor.MaximumX == 1280, "primary source publication observes restored object clipping inside Camera");
		events.push_back(21);
	};
	sgre::scene::MotionLayerFrame frame(
	    process, groups, renderer, [&] {
		    return state;
	    },
	    std::move(drawing));
	frame.Update([] {
		throw std::runtime_error("actual nonempty serial must suppress onProcess");
	});
	Require(events == std::vector<std::int32_t>{30, 0, 1, 3, 2}, "the existing actual groups update in serial/common-first/common-second/concrete-second/concrete-first order");
	events.clear();
	frame.Draw(0xfedcba98);
	Require(events == std::vector<std::int32_t>{10, 11, 13, 20, 12, 21, 31}, "actual group and source draw ordering matches the constructor graph");
	Require(submissions.size() == 2 && renderer.Depth() == 1, "object clip submission is applied/restored without leaking a real camera stack entry");
	frame.Close();
	Require(process.Parallel().Count() == 0 && process.Serial().Count() == 1 && releases == 0, "frame unregistration removes borrowed adapters while preserving actual Layer-owned tasks and serial owners");
	groups.Clear();
	Require(releases == 4, "actual Layer task owner controls retirement after its frame adapters detach");
}

static void TestClipSuppressionAndLiveStateReads() {
	sgre::scene::CommonProcessTasks process(8);
	sgre::scene::MotionLayerTasks groups(8);
	std::size_t concrete_draws = 0;
	std::size_t primary_draws = 0;
	std::size_t secondary_draws = 0;
	std::size_t common_draws = 0;
	std::size_t serial_draws = 0;
	std::vector<sgre::scene::MotionIntegerScissor> submissions;
	const std::array<sgre::scene::MotionScissorMatrix, 1> models{};
	sgre::scene::LayerCameraDrawScope renderer(FrameState(), models, [&](const auto& scissor) {
		submissions.push_back(scissor);
	});
	sgre::scene::MotionLayerFrameState state;
	state.BaseClip = {10000, 10000, 10010, 10010};
	sgre::scene::ParallelTaskCallbacks common;
	common.Draw = [&](auto, auto) {
		++common_draws;
	};
	static_cast<void>(groups.Group(sgre::scene::MotionTaskGroup::CommonFirst).Append(std::move(common)));
	for (const auto group : {sgre::scene::MotionTaskGroup::First, sgre::scene::MotionTaskGroup::Second}) {
		sgre::scene::ParallelTaskCallbacks concrete;
		concrete.Draw = [&](auto, auto) {
			++concrete_draws;
		};
		static_cast<void>(groups.Group(group).Append(std::move(concrete)));
	}
	sgre::scene::SerialTaskCallbacks serial;
	serial.Draw = [&](auto&, auto) {
		++serial_draws;
	};
	static_cast<void>(process.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	sgre::scene::MotionLayerSourceDrawing drawing;
	drawing.Secondary = [&](auto) {
		++secondary_draws;
		state.ObjectClip = {10000, 10000, 10010, 10010};
	};
	drawing.Primary = [&](auto) {
		++primary_draws;
	};
	sgre::scene::MotionLayerFrame frame(
	    process, groups, renderer, [&] {
		    return state;
	    },
	    std::move(drawing));
	frame.Draw(0);
	Require(common_draws == 1 && serial_draws == 1 && concrete_draws == 0 && primary_draws == 0 && secondary_draws == 0, "empty BaseClip suppresses Camera and sources while common groups and serial remain drawable");
	state.BaseClip = {1, 1, -1, -1};
	frame.Draw(0);
	Require(concrete_draws == 1 && primary_draws == 1 && secondary_draws == 1, "ObjectClip state is read after the actual secondary source callback and suppresses First only");
	Require(renderer.Depth() == 1 && submissions.empty(), "disabled base clip and empty object clip preserve the actual initial model and scissor");
}

static void TestFrameClosePreflightAndServices() {
	sgre::scene::CommonProcessTasks process(8);
	sgre::scene::MotionLayerTasks groups(8);
	sgre::scene::LayerCameraDrawScope renderer(FrameState(), {}, [](const auto&) {
		throw std::runtime_error("lifecycle fixture unexpectedly submitted renderer work");
	});
	sgre::scene::MotionLayerSourceDrawing absent;
	RequireError([&] {
		sgre::scene::MotionLayerFrame frame(
		    process, groups, renderer, [] {
			    return sgre::scene::MotionLayerFrameState{};
		    },
		    absent);
	},
	    "both source drawing operations");
	Require(process.Parallel().Count() == 0, "missing actual source services reject before inserting frame adapters");
	std::size_t source_calls = 0;
	sgre::scene::MotionLayerSourceDrawing drawing;
	drawing.Secondary = [&](auto) {
		++source_calls;
	};
	drawing.Primary = [&](auto) {
		++source_calls;
	};
	sgre::scene::MotionLayerFrame frame(
	    process, groups, renderer, [] {
		    return sgre::scene::MotionLayerFrameState{};
	    },
	    std::move(drawing));
	sgre::scene::ParallelTaskCallbacks child;
	child.Update = [&](auto) {
		Require(!frame.CanClose(), "actual common phase keeps borrowed frame adapters alive through its child callback");
		RequireError([&] {
			frame.Close();
		},
		    "active owned traversal");
	};
	static_cast<void>(groups.Group(sgre::scene::MotionTaskGroup::First).Append(std::move(child)));
	frame.Update([] {
	});
	Require(source_calls == 0 && frame.CanClose(), "source managers have no fabricated update callbacks and the frame guard restores after actual task update");
	frame.Close();
	RequireError([&] {
		frame.Draw(0);
	},
	    "attachment has retired");
}

static void TestFrameConstructorRollbackAndLostRegistration() {
	sgre::scene::MotionLayerTasks groups(8);
	sgre::scene::LayerCameraDrawScope renderer(FrameState(), {}, [](const auto&) {
		throw std::runtime_error("constructor fixture unexpectedly submitted renderer work");
	});
	std::size_t source_calls = 0;
	sgre::scene::MotionLayerSourceDrawing drawing;
	drawing.Secondary = [&](auto) {
		++source_calls;
	};
	drawing.Primary = [&](auto) {
		++source_calls;
	};
	const auto state = [] {
		return sgre::scene::MotionLayerFrameState{};
	};
	sgre::scene::CommonProcessTasks limited(1);
	RequireError([&] {
		sgre::scene::MotionLayerFrame frame(limited, groups, renderer, state, drawing);
	},
	    "capacity");
	Require(limited.Parallel().Count() == 0 && limited.CanClear(), "failed constructor must remove its already registered borrowed root before members retire");
	sgre::scene::CommonProcessTasks retired(8);
	retired.Parallel().SetRetireWhenEmpty(true);
	retired.Parallel().Update();
	RequireError([&] {
		sgre::scene::MotionLayerFrame frame(retired, groups, renderer, state, drawing);
	},
	    "unused common root");
	Require(retired.Parallel().Count() == 0 && source_calls == 0, "retired root rejects attachment without inserting or submitting");
	sgre::scene::CommonProcessTasks process(8);
	sgre::scene::MotionLayerFrame frame(process, groups, renderer, state, drawing);
	process.Parallel().Clear();
	RequireError([&] {
		frame.Update([] {
			throw std::runtime_error("lost root must reject before onProcess");
		});
	},
	    "remain registered");
	frame.Close();
}

int main() {
	try {
		TestActualFourGroupsAndSourceDrawOrder();
		TestClipSuppressionAndLiveStateReads();
		TestFrameClosePreflightAndServices();
		TestFrameConstructorRollbackAndLostRegistration();
		std::cout << "4 actual Motion Layer frame groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
