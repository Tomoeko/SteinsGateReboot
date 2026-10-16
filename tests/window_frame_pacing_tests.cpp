#include "sgre/core/window_frame_pacing.hpp"

#include "sgre/io/file_reader.hpp"

#include <array>
#include <bit>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

using Control = sgre::core::ApplicationEventControl;
using Pacing = sgre::core::WindowFramePacing;
using Sample = sgre::core::WindowPacingSample;

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

	throw std::runtime_error("unsupported window pacing operation was admitted");
}

static std::array<std::uint64_t, 7> Words(const Pacing& pacing) {
	const auto state = pacing.State();

	return {std::bit_cast<std::uint64_t>(state.CarryTicks), std::bit_cast<std::uint64_t>(state.PreviousCounter), std::bit_cast<std::uint64_t>(state.IntervalTicks), std::bit_cast<std::uint64_t>(state.EarlyMarginTicks), state.ActiveCalls, std::bit_cast<std::uint64_t>(state.PreviousLatenessCounter), std::bit_cast<std::uint64_t>(state.AccumulatedLatenessTicks)};
}

static void TestDomainAndPhases() {
	Control control;
	for (const auto frequency : {std::int64_t{0}, std::int64_t{-1}, std::numeric_limits<std::int64_t>::max()}) {
		RequireFailure([&] {
			Pacing rejected(control, frequency, 0);
		},
		    "frequency");
	}
	RequireFailure([&] {
		Pacing rejected(control, 1000, -1);
	},
	    "counter");
	Pacing pacing(control, 1000, 100);
	Require(pacing.State().IntervalTicks == 17 && pacing.State().EarlyMarginTicks == 1, "native interval and early margin initialization differ");
	RequireFailure([&] {
		static_cast<void>(pacing.ObservePacingCounter(116));
	},
	    "phase");
	RequireFailure([&] {
		pacing.RecordAfterPresentationCounter(116);
	},
	    "phase");
	Require(pacing.BeginAfterEventDispatch(), "initial event gate must be active");
	const auto started = Words(pacing);
	Require(!pacing.CanClose(), "unfinished actual sample phase must retain its owner");
	RequireFailure([&] {
		pacing.Close();
	},
	    "finish");
	RequireFailure([&] {
		static_cast<void>(pacing.BeginAfterEventDispatch());
	},
	    "unfinished");
	RequireFailure([&] {
		pacing.SetEscapePressed(true);
	},
	    "dispatch");
	RequireFailure([&] {
		static_cast<void>(pacing.ForceUnsynchronizedPresent());
	},
	    "completed");
	RequireFailure([&] {
		static_cast<void>(pacing.ObservePacingCounter(-1));
	},
	    "counter");
	Require(pacing.ObservePacingCounter(115) == Sample::RequirePrecisionWaitOneMillisecond && Words(pacing) == started, "precision wait must preserve native carry and previous sample");
	Require(pacing.ObservePacingCounter(116) == Sample::ReadyForPresentation && pacing.State().CarryTicks == -1 && pacing.ForceUnsynchronizedPresent(), "early margin must retain signed negative carry");
	pacing.RecordAfterPresentationCounter(120);
	Require(pacing.State().AccumulatedLatenessTicks == 0, "first lateness sample must initialize previous interval");
	pacing.SetEscapePressed(true);
	Require(pacing.BeginAfterEventDispatch(), "Escape-held event gate differs");
	Require(pacing.ObservePacingCounter(132) == Sample::ReadyForPresentation && !pacing.ForceUnsynchronizedPresent(), "held Escape must preserve synchronized presentation when on schedule");
	pacing.RecordAfterPresentationCounter(139);
	Require(pacing.State().AccumulatedLatenessTicks == 2, "late sample must accumulate raw counter delta minus interval");
	pacing.Close();
	RequireFailure([&] {
		static_cast<void>(pacing.State());
	},
	    "live");
	control.Close();
}

static void TestActualGateAndOwner() {
	Control control;
	Control unrelated;
	Pacing pacing(control, 60, 100);
	unrelated.RequestSuspend();
	unrelated.Flush();
	Require(pacing.BeginAfterEventDispatch(), "unrelated event state must not replace the exact control owner");
	Require(pacing.ObservePacingCounter(101) == Sample::ReadyForPresentation, "one-tick interval differs");
	pacing.RecordAfterPresentationCounter(102);
	control.RequestSuspend();
	control.Flush();
	const auto previous = Words(pacing);
	Require(!pacing.BeginAfterEventDispatch(), "committed suspension must skip pacing");
	RequireFailure([&] {
		static_cast<void>(pacing.ObservePacingCounter(103));
	},
	    "phase");
	pacing.RecordAfterPresentationCounter(104);
	Require(pacing.State().ActiveCalls == previous[4] && pacing.State().PreviousCounter == 101 && pacing.State().PreviousLatenessCounter == 104, "inactive gate must retain pacing but update lateness");
	control.RequestClose(1);
	Require(!pacing.BeginAfterEventDispatch(), "pending close must not count as committed close");
	pacing.RecordAfterPresentationCounter(105);
	control.Flush();
	Require(pacing.BeginAfterEventDispatch(), "accepted close must pace even while suspended");
	Require(pacing.ObservePacingCounter(106) == Sample::ReadyForPresentation, "accepted close sample differs");
	pacing.RecordAfterPresentationCounter(107);
	control.ClearClose();
	control.RequestQuit();
	control.Flush();
	Require(pacing.BeginAfterEventDispatch(), "committed quit must pace while suspended");
	Require(pacing.ObservePacingCounter(108) == Sample::ReadyForPresentation, "committed quit sample differs");
	pacing.RecordAfterPresentationCounter(109);
	std::exception_ptr failure;
	std::thread foreign([&] {
		try {
			static_cast<void>(pacing.BeginAfterEventDispatch());
		} catch (...) {
			failure = std::current_exception();
		}
	});
	foreign.join();
	Require(static_cast<bool>(failure), "foreign owner thread must reject before pacing mutation");
	control.Close();
	RequireFailure([&] {
		static_cast<void>(pacing.BeginAfterEventDispatch());
	},
	    "live");
	pacing.Close();
	unrelated.Close();
}

static void TestContainingDispatchPreflight() {
	Control control;
	std::exception_ptr failure;
	std::thread foreign([&] {
		try {
			Pacing rejected(control, 1000, 100);
		} catch (...) {
			failure = std::current_exception();
		}
	});
	foreign.join();
	Require(static_cast<bool>(failure), "foreign actual control owner must reject pacing construction");
	Pacing pacing(control, 1000, 100);
	std::array<std::uint32_t, 4> visits{};
	sgre::core::ApplicationEventCallbackOperations operations;
	operations.Suspend = [&] {
		++visits[0];
		RequireFailure([&] {
			static_cast<void>(pacing.BeginAfterEventDispatch());
		},
		    "recursively");
		RequireFailure([&] {
			Pacing rejected(control, 1000, 100);
		},
		    "recursively");
	};
	operations.Resume = [&] {
		++visits[1];
	};
	operations.Close = [&] {
		++visits[2];

		return false;
	};
	operations.Resize = [&](sgre::core::ApplicationResizeNotice notice) {
		visits[3] += notice.Code;
	};
	sgre::core::ApplicationEventCallbacks actual_receiver(control, operations);
	Require(control.ExchangeCallback(sgre::core::ApplicationEventSlot::Suspend, &actual_receiver) == nullptr, "actual suspend slot initialization differs");
	Require(control.ExchangeCallback(sgre::core::ApplicationEventSlot::Close, &actual_receiver) == nullptr, "actual close slot initialization differs");
	const auto initial = Words(pacing);
	control.RequestSuspend();
	control.RequestClose(1);
	control.Flush();
	Require(visits[0] == 1 && visits[2] == 1 && Words(pacing) == initial, "actual callback preflight must reject before pacing mutation");
	Require(!pacing.BeginAfterEventDispatch(), "real close callback veto must preserve suspended inactive gate");
	pacing.RecordAfterPresentationCounter(110);
	actual_receiver.Close();
	pacing.Close();
	control.Close();
}

static std::uint64_t Read(std::span<const std::uint8_t> bytes, std::size_t& cursor, std::size_t count) {
	Require(count <= 8 && cursor <= bytes.size() && count <= bytes.size() - cursor, "bounded window pacing fixture is truncated");
	std::uint64_t value = 0;
	for (std::size_t index = 0; index < count; ++index) {
		value |= static_cast<std::uint64_t>(bytes[cursor++]) << (index * 8);
	}

	return value;
}

static void CompareWords(std::span<const std::uint8_t> bytes, std::size_t& cursor, const Pacing& pacing) {
	for (const auto actual : Words(pacing)) {
		Require(Read(bytes, cursor, 8) == actual, "actual window pacing state differs from original bytes");
	}
}

static void Replay(const char* path) {
	sgre::io::FileReader input(path);
	Require(input.Size() <= 1024 * 1024, "native window pacing fixture exceeds storage bound");
	const auto bytes = input.Read(0, static_cast<std::size_t>(input.Size()));
	std::size_t cursor = 0;
	Require(Read(bytes, cursor, 4) == 0x31504657, "native window pacing fixture magic differs");
	const auto cases = Read(bytes, cursor, 4);
	const auto cycles = Read(bytes, cursor, 4);
	Require(cases > 0 && cases <= 256 && cycles > 0 && cycles <= 64, "native window pacing fixture work exceeds bound");
	std::uint64_t waits = 0;
	for (std::uint64_t index = 0; index < cases; ++index) {
		Control control;
		const auto frequency = std::bit_cast<std::int64_t>(Read(bytes, cursor, 8));
		const auto initial = std::bit_cast<std::int64_t>(Read(bytes, cursor, 8));
		Pacing pacing(control, frequency, initial);
		CompareWords(bytes, cursor, pacing);
		for (std::uint64_t cycle = 0; cycle < cycles; ++cycle) {
			const auto flags = Read(bytes, cursor, 4);
			const auto escape = Read(bytes, cursor, 4);
			const auto count = Read(bytes, cursor, 4);
			const auto expected_waits = Read(bytes, cursor, 4);
			Require(flags <= 7 && escape <= 1 && count >= 1 && count <= 4 && expected_waits <= 2, "native pacing cycle metadata exceeds bounds");
			control.Reset();
			if (flags & 1) {
				control.RequestSuspend();
			}
			if (flags & 2) {
				control.RequestClose(1);
			}
			if (flags & 4) {
				control.RequestQuit();
			}
			control.Flush();
			pacing.SetEscapePressed(escape != 0);
			const auto active = pacing.BeginAfterEventDispatch();
			std::uint64_t observed_waits = 0;
			std::uint64_t observed_present = 2;
			for (std::uint64_t sample = 0; sample < count; ++sample) {
				const auto counter = std::bit_cast<std::int64_t>(Read(bytes, cursor, 8));
				if (sample + 1 == count) {
					pacing.RecordAfterPresentationCounter(counter);
				} else {
					const auto result = pacing.ObservePacingCounter(counter);
					Require((result == Sample::ReadyForPresentation) == (sample + 2 == count), "native pacing sample decision differs");
					if (result == Sample::RequirePrecisionWaitOneMillisecond) {
						++observed_waits;
					} else {
						observed_present = static_cast<std::uint64_t>(pacing.ForceUnsynchronizedPresent());
					}
				}
			}
			const auto expected_present = Read(bytes, cursor, 4);
			Require(expected_present == observed_present && (expected_present == 2) == !active && observed_waits == expected_waits, "native active gate, actual presentation decision or precision wait count differs");
			CompareWords(bytes, cursor, pacing);
			waits += observed_waits;
		}
		pacing.Close();
		control.Close();
	}
	Require(cursor == bytes.size(), "native pacing fixture has trailing bytes");
	std::cout << cases * cycles << " original pacing cycles and " << waits << " precision waits match\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: window_frame_pacing_tests [native_fixture]");
		TestDomainAndPhases();
		TestActualGateAndOwner();
		TestContainingDispatchPreflight();
		if (argc == 2) {
			Replay(argv[1]);
		}

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
