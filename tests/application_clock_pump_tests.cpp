#include "sgre/core/application_clock_pump.hpp"

#include "sgre/core/looper_frame.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using Events = sgre::core::ApplicationEventControl;
using Pump = sgre::core::ApplicationClockPump;
using Looper = sgre::core::LooperFrame;
using Root = sgre::scene::SerialTask;
using Event = std::array<std::uint32_t, 7>;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unsupported application pump phase was admitted");
}

static std::uint32_t EventFlags(const Events& events) {
	const auto state = events.State();
	const std::array<bool, 11> fields = {state.SuspendPending, state.ResumePending, state.ClosePending, state.WaitBreakPending, state.QuitPending, state.Suspended, state.Resumed, state.CloseAccepted, state.WaitBreak, state.Quit, state.ResizePending};
	std::uint32_t flags = 0;
	for (std::size_t index = 0; index < fields.size(); ++index) {
		flags |= static_cast<std::uint32_t>(fields[index]) << index;
	}

	return flags;
}

class RecordingPlatform final : public sgre::core::ApplicationPumpPlatform {
public:
	explicit RecordingPlatform(Events& events, std::uint32_t mode, std::uint32_t delta) : m_Events(events), Mode(mode), Delta(delta) {
	}
	void RequireLive() const override {
		Require(Live, "actual test platform owner is closed");
	}
	std::int64_t Frequency() const override {
		RequireLive();

		return 1000;
	}
	std::int64_t ReadRawCounter() override {
		RequireLive();
		Counter += Delta;
		Record(4, static_cast<std::uint32_t>(Counter));

		return Counter;
	}
	void DispatchEvents(Events& events, sgre::core::WindowFramePacing& pacing) override {
		RequireLive();
		Require(&events == &m_Events && ActualPump != nullptr && &pacing == &ActualPump->Pacing(), "dispatch must receive its exact actual event and pacing owners");
		Record(3, 0);
		if (Reenter) {
			RequireFailure([&] {
				static_cast<void>(ActualLooper->RunIteration());
			},
			    "nonrecursive");
			RequireFailure([&] {
				ActualPump->ClearAuxiliary();
			},
			    "quiescent");
			RequireFailure([&] {
				ActualPump->Close();
			},
			    "retired");
		}
	}
	void PrecisionWaitOneMillisecond() override {
		RequireLive();
		Record(5, 1);
		if (FailPrecisionWait) {
			throw std::runtime_error("actual precision wait failure");
		}
	}
	void SuspendedWaitSixteenMilliseconds() override {
		RequireLive();
		Record(8, 16);
		++SuspendedWaits;
		if (Mode == 4) {
			m_Events.RequestQuit();
		} else if (Mode == 5 && SuspendedWaits == 1) {
			m_Events.RequestClose(1);
		} else {
			m_Events.RequestResume();
		}
	}
	bool HasBoundPresentationTarget() const override {
		RequireLive();

		return HasTarget;
	}
	void Present(bool force_unsynchronized) override {
		RequireLive();
		Require(HasTarget, "presentation requires its actual bound target");
		Record(6, static_cast<std::uint32_t>(force_unsynchronized));
	}
	void Record(std::uint32_t code, std::uint32_t argument) {
		if (!Recording) {
			return;
		}
		Require(ActualLooper != nullptr && ActualRoot != nullptr, "recording requires its actual containing root");
		const auto has_root = ActualLooper->HasTask();
		Trace.push_back(Event{code, argument, has_root ? static_cast<std::uint32_t>(ActualRoot->State().Status) : 0, EventFlags(m_Events), std::bit_cast<std::uint32_t>(ActualLooper->Clock().VariableStep()), static_cast<std::uint32_t>(ActualLooper->InWait()), static_cast<std::uint32_t>(has_root)});
	}
	Events& m_Events;
	Pump* ActualPump = nullptr;
	Looper* ActualLooper = nullptr;
	Root* ActualRoot = nullptr;
	std::vector<Event> Trace;
	std::uint32_t Mode = 0;
	std::int64_t Counter = 900;
	std::uint32_t Delta = 100;
	std::uint32_t SuspendedWaits = 0;
	bool Live = true;
	bool HasTarget = true;
	bool Recording = false;
	bool Reenter = false;
	bool FailPrecisionWait = false;
};

static sgre::scene::SerialTaskCallbacks ActualRootCallbacks(RecordingPlatform& platform, std::unique_ptr<std::uint32_t>& payload, std::uint32_t& releases) {
	sgre::scene::SerialTaskCallbacks callbacks;
	callbacks.Update = [&](Root& root) {
		Require(payload != nullptr, "actual root payload must survive Update");
		++*payload;
		platform.Record(1, 0);
		root.SetStatus(platform.Mode == 9 ? sgre::scene::TaskStatus::Retire : sgre::scene::TaskStatus::Complete);
	};
	callbacks.Draw = [&](Root&, std::uint32_t argument) {
		Require(payload != nullptr, "actual root payload must survive Draw");
		platform.Record(2, argument);
	};
	callbacks.Release = [&] {
		Require(payload != nullptr, "actual root retirement must be single");
		platform.Record(9, 0);
		payload.reset();
		++releases;
	};
	callbacks.CanRelease = [&] {
		return payload != nullptr;
	};

	return callbacks;
}

static sgre::core::ApplicationEventCallbackOperations ActualEventCallbacks(RecordingPlatform& platform) {
	sgre::core::ApplicationEventCallbackOperations callbacks;
	callbacks.Suspend = [&] {
		platform.Record(10, 0);
	};
	callbacks.Resume = [&] {
		platform.Record(11, 0);
	};
	callbacks.Close = [&] {
		platform.Record(12, 0);

		return platform.Mode != 5;
	};
	callbacks.Resize = [&](sgre::core::ApplicationResizeNotice notice) {
		platform.Record(13, notice.Code);
	};

	return callbacks;
}

static std::array<std::uint64_t, 23> StateWords(Events& events, Pump& pump, Looper& looper, Root& root, std::uint32_t releases) {
	const auto event_state = events.State();
	const auto pacing = pump.Pacing().State();

	return {EventFlags(events), event_state.CloseCode, event_state.ResumeRequests, looper.HasTask() ? static_cast<std::uint32_t>(root.State().Status) : 0, static_cast<std::uint64_t>(looper.HasTask()), looper.FrameNumber(), looper.RemainingFixedTicks(), std::bit_cast<std::uint32_t>(looper.Clock().VariableStep()), std::bit_cast<std::uint32_t>(looper.Clock().ResidualTicks()), std::bit_cast<std::uint64_t>(looper.AccumulatedTicks()), static_cast<std::uint64_t>(looper.Clock().VariableFrame()), static_cast<std::uint64_t>(looper.SkipPending()), static_cast<std::uint64_t>(looper.InWait()), std::bit_cast<std::uint64_t>(pacing.CarryTicks), std::bit_cast<std::uint64_t>(pacing.PreviousCounter), std::bit_cast<std::uint64_t>(pacing.IntervalTicks), std::bit_cast<std::uint64_t>(pacing.EarlyMarginTicks), pacing.ActiveCalls, std::bit_cast<std::uint64_t>(pacing.PreviousLatenessCounter), std::bit_cast<std::uint64_t>(pacing.AccumulatedLatenessTicks), static_cast<std::uint64_t>(pacing.EscapePressed), releases, event_state.ResizeRequests};
}

struct CaseOutput {
	std::vector<Event> Trace;
	std::array<std::uint64_t, 23> State{};
};

static CaseOutput RunCase(std::uint32_t mode, std::uint32_t delta, bool escape, bool auxiliary, bool variable_frame, std::uint32_t frames) {
	Events events;
	RecordingPlatform platform(events, mode, delta);
	Pump pump(events, platform);
	platform.ActualPump = &pump;
	platform.HasTarget = mode != 8;
	pump.Pacing().SetEscapePressed(escape);
	sgre::core::ThreadTaskFrame workers([&] {
		return static_cast<std::uint64_t>(platform.ReadRawCounter());
	},
	    1, 4);
	std::uint32_t releases = 0;
	auto payload = std::make_unique<std::uint32_t>(0);
	Root root(ActualRootCallbacks(platform, payload, releases));
	Looper looper(workers, root, pump, 4);
	platform.ActualLooper = &looper;
	platform.ActualRoot = &root;
	looper.Clock().SetVariableFrame(variable_frame);
	sgre::core::ApplicationEventCallbacks receiver(events, ActualEventCallbacks(platform));
	for (const auto slot : {sgre::core::ApplicationEventSlot::Suspend, sgre::core::ApplicationEventSlot::Resume, sgre::core::ApplicationEventSlot::Close, sgre::core::ApplicationEventSlot::Resize}) {
		Require(events.ExchangeCallback(slot, &receiver) == nullptr, "actual script callback registration differs");
	}
	std::uint32_t auxiliary_calls = 0;
	auto auxiliary_payload = std::make_unique<std::uint32_t>(0);
	if (auxiliary) {
		pump.SetAuxiliary([&](bool suspended) {
			platform.Record(7, static_cast<std::uint32_t>(suspended));
			++auxiliary_calls;
			++*auxiliary_payload;
			if (mode == 6 && !suspended && auxiliary_calls == 1) {
				events.RequestClose(1);
			}
		},
		    [&] {
			    Require(platform.Live && auxiliary_payload != nullptr, "actual auxiliary receiver must remain live through callback");
		    });
	}
	if (mode == 1 || mode == 2 || mode == 4 || mode == 5) {
		events.RequestSuspend();
	}
	if (mode == 2) {
		events.RequestClose(1);
	}
	if (mode == 3) {
		events.RequestQuit();
	}
	events.Flush();
	platform.Recording = true;
	RequireFailure([&] {
		looper.MarkCurrentTaskRetire();
	},
	    "during Looper wait");
	for (std::uint32_t frame = 0; frame < frames; ++frame) {
		Require(!looper.RunIteration(), "bounded actual test root must complete or retire this invocation");
	}
	CaseOutput result;
	result.Trace = platform.Trace;
	result.State = StateWords(events, pump, looper, root, releases);
	platform.Recording = false;
	looper.ReleaseTask();
	Require(root.IsClosed() && payload == nullptr && releases == 1, "actual root payload must retire exactly once");
	looper.Close();
	Require(pump.CanClose(), "actual Looper close must release its pump borrower and owned callback wrapper");
	pump.Close();
	receiver.Close();
	events.Close();
	workers.Close();

	return result;
}

static void TestAdmissionAndFailureLifetime() {
	Events events;
	RecordingPlatform platform(events, 0, 1);
	RequireFailure([&] {
		Pump rejected(events, platform, 0);
	},
	    "budget");
	Require(platform.Counter == 900, "invalid work budget must reject before counter/event mutation");
	Pump pump(events, platform, 4);
	platform.ActualPump = &pump;
	sgre::core::ThreadTaskFrame workers([&] {
		return static_cast<std::uint64_t>(platform.ReadRawCounter());
	},
	    1, 4);
	Root unsupported({});
	RequireFailure([&] {
		Looper rejected(workers, unsupported, pump, 4);
	},
	    "concrete");
	std::uint32_t releases = 0;
	auto payload = std::make_unique<std::uint32_t>(0);
	Root root(ActualRootCallbacks(platform, payload, releases));
	Looper looper(workers, root, pump, 4);
	platform.ActualLooper = &looper;
	platform.ActualRoot = &root;
	platform.Recording = true;
	platform.Reenter = true;
	RequireFailure([&] {
		static_cast<void>(looper.RunIteration());
	},
	    "bounded actual platform work");
	Require(!looper.InWait() && looper.FrameNumber() == 0 && pump.Pacing().CanClose(), "failed bounded work must abandon its incomplete decision without publishing a completed frame");
	platform.Recording = false;
	looper.ReleaseTask();
	looper.Close();
	pump.Close();
	events.Close();
	workers.Close();
	unsupported.Close();
	Events other_events;
	RecordingPlatform other_platform(other_events, 0, 100);
	std::exception_ptr failure;
	std::thread foreign([&] {
		try {
			Pump rejected(other_events, other_platform);
		} catch (...) {
			failure = std::current_exception();
		}
	});
	foreign.join();
	Require(static_cast<bool>(failure) && other_platform.Counter == 900, "foreign actual event owner must reject initialization before platform mutation");
	other_events.Close();
}

static void TestPlatformFailureLifetime() {
	Events events;
	RecordingPlatform platform(events, 0, 1);
	Pump pump(events, platform);
	platform.ActualPump = &pump;
	sgre::core::ThreadTaskFrame workers([&] {
		return static_cast<std::uint64_t>(platform.ReadRawCounter());
	},
	    1, 4);
	std::uint32_t releases = 0;
	auto payload = std::make_unique<std::uint32_t>(0);
	Root root(ActualRootCallbacks(platform, payload, releases));
	Looper looper(workers, root, pump, 4);
	platform.ActualLooper = &looper;
	platform.ActualRoot = &root;
	RequireFailure([&] {
		Looper duplicate(workers, root, pump, 4);
	},
	    "unbound");
	Require(!root.IsClosed() && *payload == 0, "duplicate containing admission must preserve the actual root");
	platform.FailPrecisionWait = true;
	RequireFailure([&] {
		static_cast<void>(looper.RunIteration());
	},
	    "actual precision wait failure");
	Require(!looper.InWait() && looper.FrameNumber() == 0 && pump.Pacing().CanClose() && releases == 0, "platform failure must retain the real root and permit explicit containing cleanup");
	looper.ReleaseTask();
	looper.Close();
	pump.Close();
	Require(root.IsClosed() && payload == nullptr && releases == 1, "failed actual platform frame must release its real root exactly once");
	events.Close();
	workers.Close();
	Events failing_events;
	RecordingPlatform failing_platform(failing_events, 0, 100);
	Pump failing_pump(failing_events, failing_platform);
	failing_platform.ActualPump = &failing_pump;
	sgre::core::ThreadTaskFrame failing_workers([&] {
		return static_cast<std::uint64_t>(failing_platform.ReadRawCounter());
	},
	    1, 4);
	std::uint32_t failing_releases = 0;
	auto failing_payload = std::make_unique<std::uint32_t>(0);
	Root preserved(ActualRootCallbacks(failing_platform, failing_payload, failing_releases));
	failing_platform.Live = false;
	RequireFailure([&] {
		Looper rejected(failing_workers, preserved, failing_pump, 4);
	},
	    "actual test platform owner is closed");
	Require(!preserved.IsClosed() && *failing_payload == 0 && failing_releases == 0 && failing_pump.CanClose(), "throwing actual platform attachment must discard only its failed borrow");
	preserved.Close();
	failing_pump.Close();
	failing_events.Close();
	failing_workers.Close();
}

static std::uint64_t Read(std::istream& input, std::size_t bytes) {
	std::uint64_t word = 0;
	Require(bytes <= 8, "native pump fixture word exceeds bound");
	for (std::size_t index = 0; index < bytes; ++index) {
		const auto value = input.get();
		Require(value != std::char_traits<char>::eof(), "native pump fixture is truncated");
		word |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(value)) << (index * 8);
	}

	return word;
}

static void Replay(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open() && Read(input, 4) == 0x31435041, "native pump fixture cannot open or has wrong magic");
	const auto cases = Read(input, 4);
	Require(cases > 0 && cases <= 256, "native pump fixture exceeds its case bound");
	std::size_t comparisons = 0;
	for (std::uint64_t index = 0; index < cases; ++index) {
		std::array<std::uint32_t, 6> inputs{};
		for (auto& value : inputs) {
			value = static_cast<std::uint32_t>(Read(input, 4));
		}
		Require(inputs[0] <= 9 && inputs[1] >= 1 && inputs[1] <= 100 && inputs[2] <= 1 && inputs[3] <= 1 && inputs[4] <= 1 && inputs[5] >= 1 && inputs[5] <= 3, "native pump fixture input exceeds its bounded scope");
		const auto actual = RunCase(inputs[0], inputs[1], inputs[2] != 0, inputs[3] != 0, inputs[4] != 0, inputs[5]);
		const auto events = Read(input, 4);
		Require(events == actual.Trace.size() && events <= 1024, "actual pump callback count differs");
		for (const auto& event : actual.Trace) {
			for (const auto value : event) {
				Require(Read(input, 4) == value, "actual pump callback entry differs case " + std::to_string(index));
				++comparisons;
			}
		}
		std::size_t state_index = 0;
		for (const auto value : actual.State) {
			const auto expected = Read(input, 8);
			Require(expected == value, "actual pump/root/event/clock state differs case " + std::to_string(index) + " field " + std::to_string(state_index) + " expected " + std::to_string(expected) + " actual " + std::to_string(value));
			++state_index;
			++comparisons;
		}
	}
	Require(input.peek() == std::char_traits<char>::eof(), "native pump fixture has trailing bytes");
	std::cout << cases << " original containing pump cases / " << comparisons << " entry and state words match\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: application_clock_pump_tests [native_fixture]");
		TestAdmissionAndFailureLifetime();
		TestPlatformFailureLifetime();
		if (argc == 2) {
			Replay(argv[1]);
		}

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
