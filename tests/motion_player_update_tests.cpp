#include "sgre/scene/motion_player_update.hpp"

#include "sgre/scene/motion_parameter_values.hpp"

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
		    : traversal == sgre::scene::MotionFrameTraversal::Reverse                                                                        ? 8
		                                                                                                                                     : 9;
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

static void TestCurrentTagsDoNotAccumulateHistoricalTimelines() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	auto model = Model(12, -1, {9, 9, 9});
	const auto original = model->Objects.front().Timelines.front();
	for (std::size_t index = 0; index != 300; ++index) {
		auto timeline = original;
		timeline.Name = "selected" + std::to_string(index);
		model->Objects.front().Timelines.push_back(std::move(timeline));
	}
	static_cast<void>(manager.Register(Identity(), model, {}));
	sgre::scene::MotionPlayerCommands commands(manager);
	Trace trace;
	sgre::scene::MotionPlayerUpdate update(commands, Services(trace));
	commands.SetCharacter("a");
	for (std::size_t index = 0; index != 300; ++index) {
		trace.Phases.clear();
		trace.Events.clear();
		const auto& timeline = model->Objects.front().Timelines[index + 1];
		commands.Play(timeline.Name, sgre::scene::MotionPlayFlags::Force);
		update.PrepareCurrentTimeline(timeline);
		update.Progress(0);
		Require(commands.CachedTimeline() == &timeline && trace.Events.size() == 1 && trace.Events.front().second == "act0", "each genuine current tag selection must retain its real first action beyond256 historical timelines");
	}
}

static void TestClockAndEvents() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto model = Model(12, -1);
	static_cast<void>(manager.Register(Identity(), model, {}));
	sgre::scene::MotionPlayerCommands commands(manager);
	Trace trace;
	sgre::scene::MotionPlayerUpdate update(commands, Services(trace));
	trace.Update = &update;
	update.PrepareTimeline(model->Objects[0].Timelines[0]);
	commands.SetCharacter("a");
	commands.Play("idle");
	update.Update();
	Require(commands.ClockState().TickCount == 0 && !commands.ClockState().TickAssigned && !commands.ClockState().InitialTagDispatchPending && trace.Events.size() == 1 && trace.Events[0].second == "act0", "first native update must seek and dispatch the initial action without advancing assigned ticks");
	Require(trace.Phases.size() == 4 && trace.Phases[0].Kind == 0 && trace.Phases[1].Kind == 6 && trace.Phases[2].Kind == 4 && trace.Phases[3].Kind == 5, "ordinary update callbacks must preserve delta, frame preparation, composition and publication order");
	trace.Phases.clear();
	trace.Events.clear();
	trace.Delta = 5;
	update.Update();
	Require(commands.ClockState().TickCount == 5 && commands.ClockState().EvaluationTime == 5 && trace.Events.size() == 1 && trace.Events[0].second == "act1", "forward update must advance and dispatch a crossed tag action");
	trace.Delta = 20;
	update.Update();
	Require(!commands.ClockState().Playing && commands.ClockState().TickCount == 25 && commands.ClockState().EvaluationTime == 12 && trace.Events.back().second == "act2", "nonlooping end must retain the unclamped tick count and publish its terminal pose");
	const auto delivered = trace.Events.size();
	update.PostUpdate();
	Require(trace.Events.size() == delivered, "task PostUpdate must not redeliver a queue already drained by synchronous Update completion");
	update.Draw();
	Require(trace.DrawCalls == 1, "actual task Draw must invoke its required current-packet draw service");
	commands.Play("idle", sgre::scene::MotionPlayFlags::Force | sgre::scene::MotionPlayFlags::Delay2);
	trace.Phases.clear();
	update.Update();
	Require(trace.Phases.size() == 1 && trace.Phases[0].Kind == 0 && commands.ClockState().InitialTagDispatchPending && commands.ClockState().TickAssigned, "Delay2 must return after reading owner timing and selecting, before timeline/composition/publication phases");
	trace.EnqueueDuringDelivery = true;
	update.Update();
	Require(update.PendingEventCount() == 1, "events enqueued by a listener must survive detached current delivery");
	update.PostUpdate();
	Require(update.PendingEventCount() == 0 && trace.Events.back().second == "later", "next native event delivery must consume only the newly queued event");
	trace.Delta = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		update.Update();
	},
	    "finite frame delta");
	RequireFailure([&] {
		sgre::scene::MotionPlayerUpdate rejected(commands, {});
	},
	    "actual manager");
}

static void TestLoopSyncAndAlignment() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto model = Model(12, 3, {9, 11, 13});
	static_cast<void>(manager.Register(Identity(), model, {}));
	sgre::scene::MotionPlayerCommands commands(manager);
	Trace trace;
	sgre::scene::MotionPlayerUpdate update(commands, Services(trace));
	update.PrepareTimeline(model->Objects[0].Timelines[0]);
	commands.SetCharacter("a");
	commands.Play("idle");
	update.Update();
	trace.Delta = 10;
	update.Update();
	Require(commands.ClockState().TickCount == 4 && commands.ClockState().EvaluationTime == 4 && commands.ClockState().AlignedThisUpdate && !commands.ClockState().SyncWaiting, "align must stop traversal at the crossed tag's actual time");
	update.Update();
	Require(commands.ClockState().TickCount == 8 && commands.ClockState().SyncWaiting && trace.Events[trace.Events.size() - 2].first == sgre::scene::MotionPlayerEventKind::Sync && trace.Events.back().second == "act2", "sync must queue its event before action, retain Playing and block later advancement");
	const auto count = trace.Events.size();
	update.Update();
	Require(commands.ClockState().Playing && commands.ClockState().TickCount == 8 && !commands.ClockState().AlignedThisUpdate && trace.Events.size() == count, "sync waiting must retain its pose without redelivering tags");
	commands.Play("idle", sgre::scene::MotionPlayFlags::Force);
	Require(!commands.ClockState().SyncWaiting, "real incoming timeline preparation must clear the prior sync wait");
	const auto plain = Model(12, 3);
	static_cast<void>(manager.Register(Identity(2), plain, {}));
	update.PrepareTimeline(plain->Objects[0].Timelines[0]);
	RequireFailure([&] {
		auto unsupported = model->Objects[0].Timelines[0];
		unsupported.LoopTime = unsupported.LastTime;
		update.PrepareTimeline(unsupported);
	},
	    "positive loop span");
	RequireFailure([&] {
		auto unsupported = model->Objects[0].Timelines[0];
		unsupported.Parameterization = Value(std::int64_t{0});
		update.PrepareTimeline(unsupported);
	},
	    "actual ordered parameter definitions");
	RequireFailure([&] {
		auto incomplete = model->Objects[0].Timelines[0];
		incomplete.Tags.resize(1);
		update.PrepareTimeline(incomplete);
	},
	    "terminal frame");
}

static void TestActualRegisteredTimelineParameterClock() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto model = Model(12, -1, {0, 0, 0});
	auto& timeline = model->Objects[0].Timelines[0];
	timeline.Parameterization = Value(std::int64_t{0});
	timeline.Parameters = Value(sgre::resource::PsbArray{Value(sgre::resource::PsbObject{{"id", Value(std::string("clock"))}, {"discretization", Value(false)}, {"rangeBegin", Value(0.0)}, {"rangeEnd", Value(1.0)}, {"division", Value(24.0)}})});
	static_cast<void>(manager.Register(Identity(), model, {}));
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("a");
	commands.Play("idle");
	Trace trace;
	sgre::scene::MotionPlayerUpdate update(commands, Services(trace));
	update.PrepareTimeline(timeline);
	RequireFailure([&] {
		update.Update();
	},
	    "genuine registered ordered cell owner");

	{
		sgre::scene::MotionTimelineParameters parameters(commands.ParameterValues(), timeline);
		Require(parameters.TimelineParameter() == &parameters.Entries()[0] && commands.ParameterValues().RegisteredParameterCount() == 1, "whole-timeline clock must use the actual registered ordered parameter cell");
		commands.ParameterValues().WriteLocal("clock", 0.25f, 0);
		trace.Phases.clear();
		update.Update();
		Require(commands.ClockState().TickCount == 6 && commands.ClockState().EvaluationTime == 6 && trace.Phases.size() == 4 && trace.Phases[1].Kind == 6, "first registered clock update must seek its genuine parameter value");
		commands.ParameterValues().WriteLocal("clock", 1, 0);
		trace.Phases.clear();
		update.Update();
		Require(commands.ClockState().TickCount == 24 && commands.ClockState().EvaluationTime == 24 && commands.ClockState().Playing && trace.Phases.size() == 4 && trace.Phases[1].Kind == 7, "registered parameter position must advance without ordinary lastTime clamp/stop policy");
		commands.Stop();
		commands.ParameterValues().WriteLocal("clock", 0.5f, 0);
		trace.Phases.clear();
		update.Update();
		Require(commands.ClockState().TickCount == 12 && commands.ClockState().EvaluationTime == 12 && !commands.ClockState().Playing && trace.Phases.size() == 4 && trace.Phases[1].Kind == 8, "bound clock must reverse from its real setter before the ordinary stopped gate");
		trace.Phases.clear();
		update.Update();
		Require(commands.ClockState().TickCount == 12 && trace.Phases.size() == 4 && trace.Phases[1].Kind == 9 && trace.Events.empty(), "unchanged bound clock must run genuine ParameterOnly without replaying tags");
	}

	Require(commands.ParameterValues().RegisteredParameterCount() == 0, "actual registered clock owner must retire before its command/value maps");
}

static void TestRepeatedLoopAndReverse() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto model = Model(12, 3);
	static_cast<void>(manager.Register(Identity(), model, {}));
	sgre::scene::MotionPlayerCommands commands(manager);
	Trace trace;
	sgre::scene::MotionPlayerUpdate update(commands, Services(trace));
	update.PrepareTimeline(model->Objects[0].Timelines[0]);
	commands.SetCharacter("a");
	commands.Play("idle");
	update.Update();
	trace.Delta = 30;
	trace.Events.clear();
	trace.Phases.clear();
	update.Update();
	Require(commands.ClockState().TickCount == 3 && commands.ClockState().EvaluationTime == 3 && commands.ClockState().Playing && trace.Events.size() == 3 && trace.Events[0].second == "act1" && trace.Events[1].second == "act2" && trace.Events[2].second == "act0", "native repeated forward wraps must traverse the outgoing end, seek the loop point, then wrap scalar ticks without replaying every intermediate whole loop");
	Require(trace.Phases.size() == 6 && trace.Phases[1].Kind == 7 && trace.Phases[2].Kind == 6 && trace.Phases[3].Kind == 7, "native forward wrap frame preparation order differs");
	commands.SetSpeed(-1);
	trace.Delta = 5;
	trace.Events.clear();
	trace.Phases.clear();
	update.Update();
	Require(commands.ClockState().TickCount == 7 && commands.ClockState().EvaluationTime == 7 && trace.Events.size() == 2 && trace.Events[0].second == "act2" && trace.Events[1].second == "act1", "native reverse wrap must seek the outgoing last frame before descending to the wrapped time");
	Require(trace.Phases.size() == 6 && trace.Phases[1].Kind == 8 && trace.Phases[2].Kind == 6 && trace.Phases[3].Kind == 8, "native reverse wrap frame preparation order differs");
}

static std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
	Require(offset <= bytes.size() && 4 <= bytes.size() - offset, "native update word exceeds record bounds");
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
	Require(input.gcount() == static_cast<std::streamsize>(raw.size()), "truncated native update record");
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
		Require(std::bit_cast<std::uint32_t>(actual[index]) == ReadU32(bytes, index * 4), "native ordinary update clock float32 word differs");
	}

	Require(update.TagIndex() == ReadU32(bytes, 24), "native ordinary update tag index differs");
	Require(clock.Playing == (bytes[28] != 0) && clock.TickAssigned == (bytes[29] != 0) && clock.InitialTagDispatchPending == (bytes[30] != 0) && clock.SyncWaiting == (bytes[31] != 0) && clock.AlignedThisUpdate == (bytes[32] != 0), "native ordinary update control flags differ");
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open(), "cannot open native update fixture");
	const auto header = ReadRecord<12>(input);
	Require(header[0] == 'M' && header[1] == 'U' && header[2] == 'P' && header[3] == '1', "native update fixture signature differs");
	const auto cases = ReadU32(header, 4);
	const auto steps = ReadU32(header, 8);
	Require(cases > 0 && cases <= 512 && steps > 0 && steps <= 8, "native update fixture exceeds bounded work");
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
		update.PrepareTimeline(model->Objects[0].Timelines[0]);
		commands.SetCharacter("a");
		commands.Play("idle");
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto record = ReadRecord<43>(input);
			trace.Delta = std::bit_cast<float>(ReadU32(record, 0));
			commands.SetSpeed(std::bit_cast<float>(ReadU32(record, 4)));
			trace.Phases.clear();
			trace.Events.clear();
			update.Update();
			RequireState(commands, update, std::span<const std::uint8_t>(record).subspan(8, 33));
			Require(record[41] <= 64 && record[42] <= 64 && trace.Phases.size() == record[41] && trace.Events.size() == record[42], "native update phase/event counts differ");
			for (std::size_t index = 0; index < trace.Phases.size(); ++index) {
				const auto phase = ReadRecord<5>(input);
				Require(trace.Phases[index].Kind == phase[0] && trace.Phases[index].Time == ReadU32(phase, 1), "native update preparation/composition/publication order or time differs");
			}

			for (std::size_t index = 0; index < trace.Events.size(); ++index) {
				const auto event = ReadRecord<2>(input);
				Require(event[0] <= 1 && event[1] < 3, "native event fixture kind or action index exceeds its bounds");
				const auto kind = event[0] == 0 ? sgre::scene::MotionPlayerEventKind::Action : sgre::scene::MotionPlayerEventKind::Sync;
				Require(trace.Events[index].first == kind && trace.Events[index].second == (kind == sgre::scene::MotionPlayerEventKind::Action ? "act" + std::to_string(event[1]) : std::string{}), "native sync/action delivery order differs");
			}

			update.PostUpdate();
			Require(update.PendingEventCount() == 0 && trace.Events.size() == record[42], "native task PostUpdate redelivered or retained events");
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native update fixture has trailing bytes");
}

static void TestExactTerminalAndNumericBooleans() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto model = Model(12, -1);
	auto& timeline = model->Objects[0].Timelines[0];
	timeline.Priority.back().Time = timeline.LastTime;
	timeline.Tags.back().Time = timeline.LastTime;
	timeline.Tags.back().Type = 1;
	timeline.Tags.back().Content = Value(sgre::resource::PsbObject{{"align", Value(false)}, {"sync", Value(true)}, {"action", Value(std::string{"terminal-must-not-dispatch"})}});
	static_cast<void>(manager.Register(Identity(), model, {}));
	sgre::scene::MotionPlayerCommands commands(manager);
	Trace trace;
	sgre::scene::MotionPlayerUpdate update(commands, Services(trace));
	update.PrepareTimeline(timeline);
	commands.SetCharacter("a");
	commands.Play("idle");
	commands.SetTickCount(timeline.LastTime);
	update.Update();
	Require(commands.ClockState().EvaluationTime == 12 && !commands.ClockState().SyncWaiting && trace.Events.size() == 1 && trace.Events.front().second == "act2", "exact terminal seek must clamp to the penultimate tag and leave final content inactive");

	const std::array<sgre::resource::PsbValue, 8> booleans{Value(std::int64_t{0}), Value(std::int64_t{256}), Value(std::int64_t{-256}), Value(std::int64_t{4294967296}), Value(-0.0), Value(std::numeric_limits<double>::denorm_min()), Value(std::numeric_limits<double>::infinity()), Value(std::numeric_limits<double>::quiet_NaN())};
	const std::array<bool, 8> expected{false, true, true, true, false, true, true, true};
	for (std::size_t index = 0; index < booleans.size(); ++index) {
		auto numeric_model = Model(12, -1, {1, 1, 1});
		auto& numeric_timeline = numeric_model->Objects[0].Timelines[0];
		auto* content = std::get_if<sgre::resource::PsbObject>(&numeric_timeline.Tags.front().Content.Data);
		Require(content != nullptr, "numeric tag fixture requires an object");
		for (auto& [name, value] : *content) {
			if (name == "sync") {
				value = booleans[index];
			}
		}

		sgre::scene::MotionResourceManager numeric_manager(cache);
		static_cast<void>(numeric_manager.Register(Identity(), numeric_model, {}));
		sgre::scene::MotionPlayerCommands numeric_commands(numeric_manager);
		Trace numeric_trace;
		sgre::scene::MotionPlayerUpdate numeric_update(numeric_commands, Services(numeric_trace));
		numeric_update.PrepareTimeline(numeric_timeline);
		numeric_commands.SetCharacter("a");
		numeric_commands.Play("idle");
		numeric_update.Update();
		Require(numeric_commands.ClockState().SyncWaiting == expected[index], "native tag boolean conversion differs for full integers or IEEE values");
		Require(numeric_trace.Events.size() == (expected[index] ? 1U : 0U), "native numeric sync tag delivery count differs");
	}
}

static void TestExplicitProgressPhases() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto model = Model(12, -1);
	static_cast<void>(manager.Register(Identity(), model, {}));
	sgre::scene::MotionPlayerCommands commands(manager);
	Trace trace;
	std::size_t clock_reads = 0;
	std::size_t rejected_reentry = 0;
	sgre::scene::MotionPlayerUpdate* actual = nullptr;
	auto services = Services(trace);
	services.ReadFrameDelta = [&]() -> float {
		++clock_reads;
		throw std::runtime_error("explicit progress must not read the containing frame clock");
	};
	const auto compose = services.ComposeLayers;
	services.ComposeLayers = [&](const auto& frame) {
		Require(actual != nullptr, "explicit progress fixture requires its actual update owner");
		RequireFailure([&] {
			actual->Progress(0);
		},
		    "recursive");
		++rejected_reentry;
		compose(frame);
	};
	const auto deliver = services.DeliverEvent;
	services.DeliverEvent = [&](const auto& event) {
		RequireFailure([&] {
			actual->Progress(0);
		},
		    "recursive");
		++rejected_reentry;
		deliver(event);
	};
	sgre::scene::MotionPlayerUpdate update(commands, std::move(services));
	actual = &update;
	update.PrepareTimeline(model->Objects[0].Timelines[0]);
	commands.SetCharacter("a");
	commands.Play("idle");
	update.Progress(0);
	Require(trace.Phases.size() == 3 && trace.Phases[0].Kind == 6 && trace.Phases[1].Kind == 4 && trace.Phases[2].Kind == 5 && trace.Events.size() == 1 && trace.Events[0].second == "act0", "progress0 must perform genuine initial seek, composition, publication and synchronous event delivery without a clock read");
	trace.Phases.clear();
	update.Progress(2);
	Require(commands.ClockState().TickCount == 2 && commands.ClockState().DeltaTicks == 2 && trace.Phases.size() == 3 && trace.Phases[0].Kind == 7, "explicit ticks must advance the actual selected clock and forward cursor");
	commands.SetSpeed(2);
	update.Progress(1);
	Require(commands.ClockState().TickCount == 4 && commands.ClockState().DeltaTicks == 2 && trace.Events.size() == 2 && trace.Events[1].second == "act1" && update.PendingEventCount() == 0, "progress must use actual speed and finish Post delivery in the same call");
	trace.Phases.clear();
	update.Progress(-.5f);
	Require(commands.ClockState().TickCount == 3 && commands.ClockState().DeltaTicks == -1 && trace.Phases[0].Kind == 8, "negative explicit ticks must use genuine reverse traversal rather than an absolute time assignment");
	commands.Stop();
	trace.Phases.clear();
	update.Progress(7);
	Require(commands.ClockState().TickCount == 3 && trace.Phases.size() == 2 && trace.Phases[0].Kind == 4 && trace.Phases[1].Kind == 5, "stopped progress retains actual time while still composing and publishing");
	const auto phase_count = trace.Phases.size();
	RequireFailure([&] {
		update.Progress(std::numeric_limits<float>::quiet_NaN());
	},
	    "finite");
	Require(clock_reads == 0 && rejected_reentry >= 6 && trace.DrawCalls == 0 && trace.Phases.size() == phase_count && commands.ClockState().TickCount == 3, "explicit progress must preserve clock independence, reentry/error guards and no Draw scheduling");
}

static void TestEmptyLayerAction() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands commands(manager);
	Trace trace;
	sgre::scene::MotionPlayerUpdate update(commands, Services(trace));
	update.EnqueueAction("label", "");
	Require(update.PendingEventCount() == 1, "native frame action must queue an explicitly empty action string");
	update.PostUpdate();
	Require(trace.Events.size() == 1 && trace.Events.front().second.empty(), "native empty frame action must reach detached delivery");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_player_update_tests [native-update-fixture]");
		TestCurrentTagsDoNotAccumulateHistoricalTimelines();
		TestClockAndEvents();
		TestExplicitProgressPhases();
		TestEmptyLayerAction();
		TestLoopSyncAndAlignment();
		TestRepeatedLoopAndReverse();
		TestActualRegisteredTimelineParameterClock();
		TestExactTerminalAndNumericBooleans();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion player update tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
