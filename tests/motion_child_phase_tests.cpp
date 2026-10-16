#include "sgre/scene/motion_child_control.hpp"
#include "sgre/scene/motion_player_update.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unsupported motion update was accepted");
}

template <typename Type>
static sgre::resource::PsbValue Value(Type value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

struct Phase {
	std::uint8_t Kind = 0;
	std::uint32_t Time = 0;
	bool operator==(const Phase&) const = default;
};

struct Trace {
	float Delta = 1;
	std::vector<Phase> Phases;
	std::vector<std::pair<sgre::scene::MotionPlayerEventKind, std::string>> Events;
	std::size_t DrawCalls = 0;
	bool EnqueueDuringDelivery = false;
	sgre::scene::MotionPlayerUpdate* Update = nullptr;

	Trace() {
		Phases.reserve(64);
		Events.reserve(64);
	}
};

static void Record(Trace& trace, std::uint8_t kind, float time) {
	Require(trace.Phases.size() < 64, "motion callback trace exceeds its bound");
	Phase phase;
	phase.Kind = kind;
	phase.Time = std::bit_cast<std::uint32_t>(time);
	trace.Phases.push_back(phase);
}

static sgre::scene::MotionPlayerUpdateServices Services(Trace& trace) {
	sgre::scene::MotionPlayerUpdateServices services;
	services.ReadFrameDelta = [&trace] {
		Record(trace, 0, 0);

		return trace.Delta;
	};
	services.PrepareFrames = [&trace](const auto traversal, const auto& context) {
		Require(context.Timeline != nullptr, "frame preparation must borrow an actual selected timeline");
		const auto kind = traversal == sgre::scene::MotionFrameTraversal::Seek ? 6 : traversal == sgre::scene::MotionFrameTraversal::Forward ? 7
		                                                                                                                                     : 8;
		Record(trace, static_cast<std::uint8_t>(kind), context.Clock.EvaluationTime);
	};
	services.ComposeLayers = [&trace](const auto& context) {
		Record(trace, 4, context.Clock.EvaluationTime);
	};
	services.PublishDrawPackets = [&trace](const auto& context) {
		Require(!context.Clock.TickAssigned, "native composition must clear first evaluation before draw packet publication");
		Record(trace, 5, context.Clock.EvaluationTime);
	};
	services.Draw = [&trace] {
		++trace.DrawCalls;
	};
	services.DeliverEvent = [&trace](const auto& event) {
		Require(trace.Events.size() < 64, "motion event trace exceeds its bound");
		trace.Events.emplace_back(event.Kind, std::string(event.Action));
		if (trace.EnqueueDuringDelivery) {
			trace.EnqueueDuringDelivery = false;
			Require(trace.Update != nullptr && trace.Update->PendingEventCount() == 0, "native event delivery must detach its queue before invoking listeners");
			trace.Update->EnqueueAction("layer", "later");
		}
	};

	return services;
}

static std::shared_ptr<sgre::resource::MotionResource> Model(float last, float loop, std::array<std::uint8_t, 3> options = {9, 9, 9}) {
	auto model = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionObject character;
	character.Name = "a";
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "idle";
	timeline.LastTime = last;
	timeline.LoopTime = loop;
	const std::array<float, 4> times{0, last / 3, (last * 2) / 3, last + 1000};
	for (std::size_t index = 0; index < times.size(); ++index) {
		sgre::resource::MotionFrame tag;
		tag.Time = times[index];
		const auto flags = index < options.size() ? options[index] : 0;
		tag.Type = (flags & 1) != 0 ? 1 : 0;
		tag.Content = Value(sgre::resource::PsbObject{{"align", Value((flags & 2) != 0)}, {"sync", Value((flags & 4) != 0)}, {"action", Value(index < options.size() && (flags & 8) != 0 ? "act" + std::to_string(index) : std::string{})}});
		timeline.Tags.push_back(std::move(tag));
	}

	sgre::resource::MotionFrame priority;
	priority.Type = 1;
	priority.Content = Value(sgre::resource::PsbArray{});
	timeline.Priority.push_back(priority);
	priority.Time = last + 1000;
	priority.Content.Data = std::monostate{};
	timeline.Priority.push_back(std::move(priority));

	character.Timelines.push_back(std::move(timeline));
	model->Objects.push_back(std::move(character));

	return model;
}

static sgre::scene::TextureCache EmptyTextureCache() {
	return sgre::scene::TextureCache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("source-free update fixture attempted texture upload");
	},
	    [](auto) {
		    throw std::runtime_error("source-free update fixture attempted texture release");
	    });
}

static sgre::scene::MotionProjectIdentity Identity(std::uint64_t document = 1) {
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = document;
	identity.RootOffset = 128;

	return identity;
}

static std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
	Require(offset <= bytes.size() && 4 <= bytes.size() - offset, "native child phase word exceeds record bounds");
	std::uint32_t value = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
	}

	return value;
}

template <std::size_t Size>
static std::array<std::uint8_t, Size> ReadRecord(std::ifstream& input) {
	std::array<char, Size> raw{};
	input.read(raw.data(), static_cast<std::streamsize>(raw.size()));
	Require(input.gcount() == static_cast<std::streamsize>(raw.size()), "truncated native child phase record");
	std::array<std::uint8_t, Size> result{};
	for (std::size_t index = 0; index < Size; ++index) {
		result[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(raw[index]));
	}

	return result;
}

static void RequireState(const sgre::scene::MotionPlayerCommands& commands, const sgre::scene::MotionPlayerUpdate& update, std::span<const std::uint8_t> bytes) {
	const auto& clock = commands.ClockState();
	const std::array<float, 6> actual{clock.TickCount, clock.EvaluationTime, clock.LastTime, clock.LoopTime, clock.Speed, clock.DeltaTicks};
	for (std::size_t index = 0; index < actual.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(actual[index]) == ReadU32(bytes, index * 4), "native child update clock float32 word differs");
	}

	Require(update.TagIndex() == ReadU32(bytes, 24), "native child update tag index differs");
	Require(clock.Playing == (bytes[28] != 0) && clock.TickAssigned == (bytes[29] != 0) && clock.InitialTagDispatchPending == (bytes[30] != 0) && clock.SyncWaiting == (bytes[31] != 0) && clock.AlignedThisUpdate == (bytes[32] != 0), "native child update control flags differ");
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open(), "cannot open native child phase fixture");
	const auto header = ReadRecord<12>(input);
	Require(header[0] == 'M' && header[1] == 'C' && header[2] == 'H' && header[3] == '1', "native child phase fixture signature differs");
	const auto cases = ReadU32(header, 4);
	const auto steps = ReadU32(header, 8);
	Require(cases > 0 && cases <= 512 && steps > 0 && steps <= 8, "native child phase fixture exceeds bounded work");
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		const auto parameters = ReadRecord<11>(input);
		const auto last = std::bit_cast<float>(ReadU32(parameters, 0));
		const auto loop = std::bit_cast<float>(ReadU32(parameters, 4));
		const auto model = Model(last, loop, {parameters[8], parameters[9], parameters[10]});
		auto cache = EmptyTextureCache();
		sgre::scene::MotionResourceManager manager(cache);
		static_cast<void>(manager.Register(Identity(), model, {}));
		sgre::scene::MotionPlayerCommands commands(manager);
		Trace trace;
		sgre::scene::MotionPlayerUpdate update(commands, Services(trace));
		sgre::scene::MotionPlayerCommands parent_commands(manager);
		Trace parent_trace;
		sgre::scene::MotionPlayerUpdate parent(parent_commands, Services(parent_trace));
		update.PrepareTimeline(model->Objects[0].Timelines[0]);
		commands.SetCharacter("a");
		commands.Play("idle");
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto record = ReadRecord<43>(input);
			trace.Delta = std::bit_cast<float>(ReadU32(record, 0));
			commands.SetSpeed(std::bit_cast<float>(ReadU32(record, 4)));
			trace.Phases.clear();
			trace.Events.clear();
			update.UpdateChild(trace.Delta);
			Require(trace.Events.empty() && trace.DrawCalls == 0 && update.PendingEventCount() == record[42], "child update must retain queued events without its own task/publication/delivery/draw phases");
			parent_trace.Events.clear();
			parent.PrependChildEvents(update);
			parent.PostUpdate();
			RequireState(commands, update, std::span<const std::uint8_t>(record).subspan(8, 33));
			Require(record[41] <= 64 && record[42] <= 64 && trace.Phases.size() == record[41] && parent_trace.Events.size() == record[42], "native child phase phase/event counts differ");
			for (std::size_t index = 0; index < trace.Phases.size(); ++index) {
				const auto phase = ReadRecord<5>(input);
				Require(trace.Phases[index].Kind == phase[0] && trace.Phases[index].Time == ReadU32(phase, 1), "native child phase preparation/composition/publication order or time differs");
			}

			for (std::size_t index = 0; index < parent_trace.Events.size(); ++index) {
				const auto event = ReadRecord<2>(input);
				Require(event[0] <= 1 && event[1] < 3, "native event fixture kind or action index exceeds its bounds");
				const auto kind = event[0] == 0 ? sgre::scene::MotionPlayerEventKind::Action : sgre::scene::MotionPlayerEventKind::Sync;
				Require(parent_trace.Events[index].first == kind && parent_trace.Events[index].second == (kind == sgre::scene::MotionPlayerEventKind::Action ? "act" + std::to_string(event[1]) : std::string{}), "native sync/action delivery order differs");
			}

			Require(update.PendingEventCount() == 0 && parent_trace.Events.size() == record[42], "child event transfer failed to clear its completed queue");
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native child phase fixture has trailing bytes");
}

static void TestChildPhase() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto model = Model(12, -1);
	static_cast<void>(manager.Register(Identity(), model, {}));
	sgre::scene::MotionPlayerCommands commands(manager);
	Trace trace;
	sgre::scene::MotionPlayerUpdate child(commands, Services(trace));
	child.PrepareTimeline(model->Objects[0].Timelines[0]);
	commands.SetCharacter("a");
	commands.Play("idle");
	commands.SetSpeed(2);
	trace.Delta = 999;
	child.UpdateChild(1);
	Require(trace.Phases.size() == 2 && trace.Phases[0].Kind == 6 && trace.Phases[1].Kind == 4 && trace.Events.empty() && trace.DrawCalls == 0 && child.PendingEventCount() == 1, "actual child phase seeks/composes without manager delta/publication/task delivery/draw");
	Require(commands.ClockState().TickCount == 0 && commands.ClockState().DeltaTicks == 2 && !commands.ClockState().TickAssigned, "child scales the parent delta by its own speed while honoring genuine initial assigned ticks");
	commands.Play("idle", sgre::scene::MotionPlayFlags::Force | sgre::scene::MotionPlayFlags::Delay2);
	trace.Phases.clear();
	child.UpdateChild(2);
	Require(commands.ClockState().TickCount == 4 && commands.Names().DelayedMotion == "idle" && trace.Phases.size() == 2 && trace.Phases[0].Kind == 7 && trace.Phases[1].Kind == 4, "child phase leaves delayed play intact and advances its already selected timeline");
	RequireFailure([&] {
		child.UpdateChild(std::numeric_limits<float>::infinity());
	},
	    "finite frame delta");
	child.UpdateChild(0);
	Require(child.PendingEventCount() >= 1, "invalid child delta must release the reentrancy guard without implicitly delivering queued events");
}

static void TestChildEventTransfer() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands parent_commands(manager);
	sgre::scene::MotionPlayerCommands first_commands(manager);
	sgre::scene::MotionPlayerCommands second_commands(manager);
	Trace parent_trace;
	Trace first_trace;
	Trace second_trace;
	sgre::scene::MotionPlayerUpdate parent(parent_commands, Services(parent_trace));
	sgre::scene::MotionPlayerUpdate first(first_commands, Services(first_trace));
	sgre::scene::MotionPlayerUpdate second(second_commands, Services(second_trace));
	parent.EnqueueAction("p", "parent");
	first.EnqueueAction("a", "a0");
	first.EnqueueAction("a", "a1");
	second.EnqueueAction("b", "b0");
	second.EnqueueAction("b", "b1");
	parent.PrependChildEvents(first);
	parent.PrependChildEvents(second);
	Require(parent.PendingEventCount() == 5 && first.PendingEventCount() == 0 && second.PendingEventCount() == 0 && first_trace.Events.empty() && second_trace.Events.empty(), "child transfer clears child queues and performs no child listener dispatch");
	RequireFailure([&] {
		parent.PrependChildEvents(parent);
	},
	    "distinct idle child");
	parent.PostUpdate();
	const std::array<std::string_view, 5> expected{"b0", "b1", "a0", "a1", "parent"};
	Require(parent_trace.Events.size() == expected.size(), "parent detached event count differs");
	for (std::size_t index = 0; index < expected.size(); ++index) {
		Require(parent_trace.Events[index].second == expected[index], "native child-list reverse grouping or within-child order differs");
	}

	sgre::scene::MotionPlayerUpdateLimits limits;
	limits.MaxPendingEvents = 2;
	Trace bounded_trace;
	sgre::scene::MotionPlayerUpdate bounded(parent_commands, Services(bounded_trace), limits);
	bounded.EnqueueAction("p", "one");
	first.EnqueueAction("a", "two");
	first.EnqueueAction("a", "three");
	RequireFailure([&] {
		bounded.PrependChildEvents(first);
	},
	    "bounded queue");
	Require(bounded.PendingEventCount() == 1 && first.PendingEventCount() == 2, "failed child queue preflight must preserve both actual queues");
}

static void TestChildTransferDuringParentComposition() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands parent_commands(manager);
	sgre::scene::MotionPlayerCommands child_commands(manager);
	Trace parent_trace;
	Trace child_trace;
	sgre::scene::MotionPlayerUpdate child(child_commands, Services(child_trace));
	child.EnqueueAction("child", "during-compose");
	auto services = Services(parent_trace);
	const auto compose = services.ComposeLayers;
	const auto deliver = services.DeliverEvent;
	sgre::scene::MotionPlayerUpdate* parent_pointer = nullptr;
	services.ComposeLayers = [&](const auto& context) {
		Require(parent_pointer != nullptr, "actual parent composition requires its owner");
		parent_pointer->PrependChildEvents(child);
		compose(context);
	};
	services.DeliverEvent = [&](const auto& event) {
		RequireFailure([&] {
			parent_pointer->PrependChildEvents(child);
		},
		    "outside delivery");
		deliver(event);
	};
	sgre::scene::MotionPlayerUpdate parent(parent_commands, std::move(services));
	parent_pointer = &parent;
	parent.Update();
	Require(child.PendingEventCount() == 0 && child_trace.Events.empty() && parent_trace.Events.size() == 1 && parent_trace.Events[0].second == "during-compose", "native parent composition accepts a completed child queue before actual parent delivery");
}

static sgre::resource::MotionFrame ControlFrame(std::int64_t mask, sgre::resource::PsbObject motion = {}) {
	sgre::resource::MotionFrame frame;
	frame.Type = 2;
	frame.Content = Value(sgre::resource::PsbObject{{"mask", Value(mask)}, {"motion", Value(std::move(motion))}});

	return frame;
}

static void TestControlMetadata() {
	sgre::resource::MotionLayer layer;
	layer.Type = 3;
	auto absent = ControlFrame(0);
	const auto zero = sgre::scene::PrepareMotionChildFrameControl(layer, absent);
	Require(!zero.has_value(), "absent motion mask performs no write to a genuine reused child control slot");
	auto frame = ControlFrame(0x80000, {{"mask", Value(std::int64_t{0})}, {"timeOffset", Value(1.25)}});
	const auto defaults = sgre::scene::PrepareMotionChildFrameControl(layer, frame);
	Require(defaults->Specified && defaults->DirectionMode == 1 && defaults->TimeOffset == 1.25f && defaults->DirectionTarget.empty(), "present motion metadata defaults to layer direction and requires actual timeOffset");
	frame = ControlFrame(0x80000, {{"mask", Value(std::int64_t{31})}, {"flags", Value(std::int64_t{3})}, {"dt", Value(2.75)}, {"dofst", Value(-45.5)}, {"docmpl", Value(std::int64_t{256})}, {"dtgt", Value(std::string{"target\0ignored", 14})}, {"timeOffset", Value(-0.5)}});
	const auto control = sgre::scene::PrepareMotionChildFrameControl(layer, frame);
	Require(static_cast<std::uint32_t>(control->Flags) == 3 && control->DirectionMode == 2 && control->DirectionOffset == -45.5f && control->CompleteDirection && control->DirectionTarget == "target" && control->TimeOffset == -0.5f, "native child control flags, truncating mode, float fields, full integer boolean and target string differ");
	sgre::scene::MotionChildFrameControl slot;
	sgre::scene::ApplyMotionChildFrameControl(control, slot);
	sgre::scene::ApplyMotionChildFrameControl(zero, slot);
	Require(slot.DirectionMode == 2 && slot.DirectionTarget == "target" && slot.TimeOffset == -0.5f, "absent control update retains the selected slot's previously prepared fields and actual target owner");
	auto hidden = absent;
	hidden.Type = 0;
	sgre::scene::ApplyMotionChildFrameControl(sgre::scene::PrepareMotionChildFrameControl(layer, hidden), slot);
	Require(slot.DirectionMode == 2 && slot.DirectionTarget == "target", "hidden control update retains the native reused slot");

	frame = ControlFrame(0x80000, {{"mask", Value(std::int64_t{256})}, {"timeOffset", Value(0.0)}});
	Require(sgre::scene::PrepareMotionChildFrameControl(layer, frame)->DirectionMode == 1, "native child property mask uses its low byte");
	RequireFailure([&] {
		const auto incomplete = ControlFrame(0x80000, {{"mask", Value(std::int64_t{0})}});
		static_cast<void>(sgre::scene::PrepareMotionChildFrameControl(layer, incomplete));
	},
	    "timeOffset");
	RequireFailure([&] {
		const auto incomplete = ControlFrame(0x80000, {{"mask", Value(std::int64_t{16})}, {"timeOffset", Value(0.0)}});
		static_cast<void>(sgre::scene::PrepareMotionChildFrameControl(layer, incomplete));
	},
	    "dtgt");
	absent.Type = 0;
	absent.Content.Data = std::monostate{};
	Require(!sgre::scene::PrepareMotionChildFrameControl(layer, absent).has_value(), "hidden native child frame bypasses all control content decoding");
	layer.Type = 2;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::PrepareMotionChildFrameControl(layer, frame));
	},
	    "genuine kind3");
}

static void ReplayControl(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open(), "cannot open native child control fixture");
	const auto header = ReadRecord<8>(input);
	Require(header[0] == 'M' && header[1] == 'C' && header[2] == 'C' && header[3] == '1' && ReadU32(header, 4) == 128, "native child control fixture header differs");
	sgre::resource::MotionLayer layer;
	layer.Type = 3;
	for (std::size_t index = 0; index < 128; ++index) {
		const auto parameters = ReadRecord<27>(input);
		const auto expected = ReadRecord<19>(input);
		const auto property_mask = ReadU32(parameters, 2);
		const auto flags = ReadU32(parameters, 6);
		const auto direction = ReadU32(parameters, 10);
		const auto offset = std::bit_cast<float>(ReadU32(parameters, 14));
		const auto time_offset = std::bit_cast<float>(ReadU32(parameters, 18));
		const auto complete = ReadU32(parameters, 22);
		const auto target = parameters[26] != 0 ? std::string{"target"} : std::string{"m"};
		auto frame = ControlFrame(parameters[1] != 0 ? 0x80000 : 0, {{"mask", Value(static_cast<std::int64_t>(property_mask))}, {"flags", Value(static_cast<std::int64_t>(flags))}, {"dt", Value(static_cast<std::int64_t>(direction))}, {"dofst", Value(static_cast<double>(offset))}, {"docmpl", Value(static_cast<std::int64_t>(complete))}, {"dtgt", Value(target)}, {"timeOffset", Value(static_cast<double>(time_offset))}});
		frame.Type = parameters[0];
		const auto control = sgre::scene::PrepareMotionChildFrameControl(layer, frame);
		const auto actual = control.value_or(sgre::scene::MotionChildFrameControl{});
		Require(static_cast<std::uint32_t>(actual.Flags) == ReadU32(expected, 0) && actual.DirectionMode == ReadU32(expected, 4) && std::bit_cast<std::uint32_t>(actual.DirectionOffset) == ReadU32(expected, 8) && actual.CompleteDirection == (expected[12] != 0) && std::bit_cast<std::uint32_t>(actual.TimeOffset) == ReadU32(expected, 13), "native child control flags/mode/offset/boolean/time words differ");
		Require((actual.DirectionTarget == "target") == (expected[17] != 0) && (actual.DirectionTarget == "m") == (expected[18] != 0) && actual.Specified == (frame.Type != 0 && parameters[1] != 0), "native child control target or zero/present ownership differs");
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native child control fixture has trailing bytes");
	std::cout << "native child control replay: 128 cases passed\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc >= 1 && argc <= 3, "usage: motion_child_phase_tests [native-child-phase-fixture [native-child-control-fixture]]");
		TestChildPhase();
		TestChildEventTransfer();
		TestChildTransferDuringParentComposition();
		TestControlMetadata();
		if (argc >= 2) {
			ReplayNative(argv[1]);
		}

		if (argc == 3) {
			ReplayControl(argv[2]);
		}

		std::cout << "motion child phase tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
