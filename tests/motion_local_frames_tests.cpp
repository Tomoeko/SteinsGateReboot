#include "sgre/scene/motion_local_frames.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view context) {
	if (!condition) {
		throw std::runtime_error(std::string(context));
	}
}

template <typename Type>
static sgre::resource::PsbValue Value(Type value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

struct Trace {
	std::vector<std::pair<std::size_t, std::size_t>> Bindings;
	std::vector<std::string> Actions;
};

static sgre::scene::MotionLocalFrameServices Services(Trace& trace, const sgre::scene::PreparedMotionTimeline& prepared) {
	sgre::scene::MotionLocalFrameServices services;
	services.BindSource = [&trace, &prepared](std::uint32_t layer, const auto& current, const auto& following) {
		Require(layer == 0, "fixture source binding layer differs");
		const auto& frames = prepared.Layers[layer].Frames;
		for (std::size_t index = 0; index + 1 < frames.size(); ++index) {
			if (&frames[index] == &current) {
				Require(&frames[index + 1] == &following, "source binding must receive actual selected adjacent frames");
				trace.Bindings.emplace_back(index, trace.Actions.size());

				return;
			}
		}

		throw std::runtime_error("source binding borrowed a frame outside its preparation");
	};
	services.EnqueueAction = [&trace](std::string_view label, std::string_view action) {
		Require(label == "label", "native frame action must use the selected node's label");
		trace.Actions.emplace_back(action);
	};

	return services;
}

static sgre::resource::MotionTimeline Timeline(const std::vector<float>& times, const std::vector<std::uint8_t>& options) {
	sgre::resource::MotionTimeline timeline;
	timeline.LastTime = times.back();
	sgre::resource::MotionLayer layer;
	layer.Label = "label";
	layer.TransformOrder = {0, 3, 2, 1};
	for (std::size_t index = 0; index < times.size(); ++index) {
		sgre::resource::MotionFrame frame;
		frame.Time = times[index];
		frame.Type = options[index] == 0 || index + 1 == times.size() ? 0 : 2;
		const auto action = options[index] == 3 ? std::string{} : std::string{"act"};
		const auto mask = options[index] >= 2 && frame.Type != 0 ? 0x40000 : 0;
		frame.Content = Value(sgre::resource::PsbObject{{"mask", Value(static_cast<std::int64_t>(mask))}, {"act", Value(action)}, {"src", Value(std::string{"source"})}, {"icon", Value(std::string{"icon"})}});
		layer.Frames.push_back(std::move(frame));
	}

	timeline.Layers.push_back(std::move(layer));
	sgre::resource::MotionFrame priority;
	priority.Type = 1;
	priority.Content = Value(sgre::resource::PsbArray{Value(std::int64_t{0})});
	timeline.Priority.push_back(priority);
	priority.Time = times.back();
	priority.Content.Data = std::monostate{};
	timeline.Priority.push_back(std::move(priority));

	return timeline;
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error(std::string(context));
}

static void TestSelectedCursors() {
	sgre::resource::MotionResource resource;
	const auto timeline = Timeline({0, 1, 1, 2, 3}, {2, 3, 2, 0, 0});
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, resource);
	Trace trace;
	sgre::scene::MotionLocalFrames local(timeline, prepared, Services(trace, prepared));
	RequireFailure([&] {
		local.Traverse(sgre::scene::MotionFrameTraversal::Forward, 0);
	},
	    "advancing without seek must reject");
	Require(local.Samples().empty(), "failed initial traversal must publish nothing");
	local.Traverse(sgre::scene::MotionFrameTraversal::Seek, 1);
	Require(local.State(0).CurrentIndex == 1 && trace.Actions == std::vector<std::string>{""} && trace.Bindings == std::vector<std::pair<std::size_t, std::size_t>>{{1, 0}}, "seek must select first exact duplicate and bind before its empty action");

	trace.Actions.clear();
	trace.Bindings.clear();
	local.Traverse(sgre::scene::MotionFrameTraversal::Forward, 1);
	Require(local.State(0).CurrentIndex == 2 && trace.Actions == std::vector<std::string>{"act"} && trace.Bindings == std::vector<std::pair<std::size_t, std::size_t>>{{2, 1}}, "forward equality crosses duplicate and queues before source binding");
	trace.Actions.clear();
	trace.Bindings.clear();
	local.Traverse(sgre::scene::MotionFrameTraversal::Reverse, 1);
	Require(local.State(0).CurrentIndex == 2 && trace.Actions.empty() && trace.Bindings.empty(), "reverse equality retains current duplicate");
	local.Traverse(sgre::scene::MotionFrameTraversal::Reverse, 0.5f);
	Require(local.State(0).CurrentIndex == 0 && trace.Actions == std::vector<std::string>{"", "act"} && trace.Bindings == std::vector<std::pair<std::size_t, std::size_t>>{{0, 2}}, "reverse crossing queues every entered action before final binding");

	trace.Actions.clear();
	trace.Bindings.clear();
	local.Traverse(sgre::scene::MotionFrameTraversal::Seek, 3);
	Require(local.State(0).CurrentIndex == 3 && trace.Actions.empty() && local.Samples().size() == 1 && local.Samples()[0].Hidden, "terminal seek clamps to the penultimate hidden frame without terminal action");
	RequireFailure([&] {
		local.Traverse(sgre::scene::MotionFrameTraversal::Seek, std::numeric_limits<float>::quiet_NaN());
	},
	    "nonfinite traversal must reject");
	Require(local.Samples().empty(), "failed traversal invalidates borrowed sample publication");
}

static void TestRequiredPreparation() {
	sgre::resource::MotionResource resource;
	auto timeline = Timeline({0, 1}, {2, 0});
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, resource);
	Trace trace;
	auto services = Services(trace, prepared);
	services.BindSource = {};
	RequireFailure([&] {
		sgre::scene::MotionLocalFrames local(timeline, prepared, services);
	},
	    "missing actual source service must reject");
	services = Services(trace, prepared);
	services.EnqueueAction = {};
	RequireFailure([&] {
		sgre::scene::MotionLocalFrames local(timeline, prepared, services);
	},
	    "missing actual queue service must reject");

	timeline.Layers[0].Properties.emplace_back("groundCorrection", Value(std::int64_t{256}));
	RequireFailure([&] {
		sgre::scene::MotionLocalFrames local(timeline, prepared, Services(trace, prepared));
	},
	    "full nonzero ground-correction metadata requires its real owner");
	timeline.Layers[0].Properties.clear();
	auto tween_prepared = prepared;
	tween_prepared.Layers[0].Frames[0].Tween = true;
	RequireFailure([&] {
		sgre::scene::MotionLocalFrames local(timeline, tween_prepared, Services(trace, tween_prepared));
	},
	    "unrecovered tween cannot enter static preparation");
	timeline.Parameterization = Value(std::int64_t{1});
	RequireFailure([&] {
		sgre::scene::MotionLocalFrames local(timeline, prepared, Services(trace, prepared));
	},
	    "parameterized timeline must retain unsupported boundary");
}

static void TestServiceFailureAndReentry() {
	sgre::resource::MotionResource resource;
	const auto timeline = Timeline({0, 1}, {2, 0});
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, resource);
	Trace trace;
	auto services = Services(trace, prepared);
	const auto binding = services.BindSource;
	sgre::scene::MotionLocalFrames* current = nullptr;
	bool fail_binding = true;
	services.BindSource = [&](std::uint32_t layer, const auto& frame, const auto& next) {
		Require(current->Samples().empty(), "samples are unpublished during actual required services");
		RequireFailure([&] {
			current->Traverse(sgre::scene::MotionFrameTraversal::Seek, 0);
		},
		    "same local owner cannot reenter its services");
		if (fail_binding) {
			throw std::runtime_error("bounded required-service failure");
		}

		binding(layer, frame, next);
	};
	sgre::scene::MotionLocalFrames local(timeline, prepared, std::move(services));
	current = &local;
	RequireFailure([&] {
		local.Traverse(sgre::scene::MotionFrameTraversal::Seek, 0);
	},
	    "required-service failure must propagate");
	Require(local.Samples().empty() && trace.Actions.empty(), "failed seek service publishes and queues nothing");
	fail_binding = false;
	local.Traverse(sgre::scene::MotionFrameTraversal::Seek, 0);
	Require(local.Samples().size() == 1 && trace.Actions == std::vector<std::string>{"act"}, "scope guards restore after service failure");
}

static std::uint8_t ReadByte(std::istream& input) {
	const auto value = input.get();
	if (value == std::char_traits<char>::eof()) {
		throw std::runtime_error("native local frame fixture is truncated");
	}

	return static_cast<std::uint8_t>(value);
}

static std::uint32_t ReadU32(std::istream& input) {
	std::uint32_t value = 0;
	for (std::uint32_t byte = 0; byte < 4; ++byte) {
		value |= static_cast<std::uint32_t>(ReadByte(input)) << (byte * 8);
	}

	return value;
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "native local frame fixture is unavailable");
	for (const auto byte : std::array<std::uint8_t, 4>{'M', 'L', 'F', '2'}) {
		Require(ReadByte(input) == byte, "native local frame fixture signature differs");
	}

	const auto cases = ReadU32(input);
	Require(cases > 0 && cases <= 4096, "native local frame case count exceeds its bound");
	sgre::resource::MotionResource resource;
	std::uint32_t transitions = 0;
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		const auto count = ReadU32(input);
		const auto steps = ReadU32(input);
		Require(count >= 2 && count <= 8 && steps > 0 && steps <= 4096, "native local frame or step count exceeds its bound");
		std::vector<float> times;
		std::vector<std::uint8_t> options;
		for (std::uint32_t index = 0; index < count; ++index) {
			times.push_back(std::bit_cast<float>(ReadU32(input)));
			options.push_back(ReadByte(input));
			Require(options.back() <= 3, "native local frame fixture option exceeds its domain");
		}

		const auto timeline = Timeline(times, options);
		const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, resource);
		Trace trace;
		sgre::scene::MotionLocalFrames local(timeline, prepared, Services(trace, prepared));
		Require(local.Samples().empty(), "actual local frames must remain unpublished before initial seek");
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto mode = ReadByte(input);
			const auto position = std::bit_cast<float>(ReadU32(input));
			const auto current = ReadU32(input);
			const auto following = ReadU32(input);
			const auto binding_count = ReadByte(input);
			const auto event_count = ReadByte(input);
			Require(mode <= 2 && binding_count <= 1 && event_count <= 8, "native local frame transition exceeds its bounded domain");
			trace.Bindings.clear();
			trace.Actions.clear();
			const auto traversal = mode == 0 ? sgre::scene::MotionFrameTraversal::Seek : mode == 1 ? sgre::scene::MotionFrameTraversal::Forward
			                                                                                       : sgre::scene::MotionFrameTraversal::Reverse;
			local.Traverse(traversal, position);
			Require(local.State(0).Initialized && local.State(0).CurrentIndex == current && following == current + 1, "native selected local frame interval differs");
			Require(trace.Bindings.size() == binding_count && trace.Actions.size() == event_count, "native source-binding or frame-action counts differ");
			for (const auto& binding : trace.Bindings) {
				Require(binding.first == ReadByte(input), "native selected source binding differs");
				Require(binding.second == ReadByte(input), "native source binding and action queue ordering differ");
			}

			for (const auto& action : trace.Actions) {
				const auto empty = ReadByte(input) != 0;
				Require(action == (empty ? "" : "act"), "native entered frame action differs, including empty action");
			}

			Require(local.Samples().size() == 1 && local.Samples().front().Hidden == prepared.Layers[0].Frames[current].Hidden && local.Samples().front().UnresolvedMask == 0, "actual prepared sample must retain selected absence and consume recovered action mask");
			++transitions;
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native local frame fixture has trailing bytes");
	std::cout << "Replayed " << transitions << " native local-frame transitions\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_local_frames_tests [native-local-frame-fixture]");
		TestSelectedCursors();
		TestRequiredPreparation();
		TestServiceFailureAndReentry();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}
		std::cout << "motion local frame tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
