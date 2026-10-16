#include "sgre/core/looper_frame.hpp"

#include "sgre/script/system_bindings.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

static void Require(bool condition, const std::string& message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static std::uint64_t ReadWord(const std::vector<std::uint8_t>& bytes, std::size_t& offset, std::size_t count) {
	Require(offset <= bytes.size() && count <= bytes.size() - offset && count <= 8, "Looper fixture word exceeds bounded bytes");
	std::uint64_t result = 0;
	for (std::size_t index = 0; index < count; ++index) {
		result |= static_cast<std::uint64_t>(bytes[offset++]) << (index * 8);
	}

	return result;
}

struct Case {
	std::uint32_t Iterations = 1;
	std::uint32_t Elapsed = 17;
	std::uint32_t Flags = 0;
};

struct Observation {
	std::vector<std::uint8_t> Events;
	std::uint32_t Frames = 0;
	std::uint32_t Remaining = 0;
	double Accumulated = 0;
	float Variable = 0;
	bool Skip = false;
};

static Observation Run(const Case& sample) {
	Observation result;
	sgre::core::LooperTaskState state;
	std::uint32_t pumps = 0;
	sgre::core::ThreadTaskFrame workers([] {
		return std::uint64_t{100};
	},
	    1);
	sgre::core::ThreadTaskReceiver post(
	    workers, [](auto&) {
		    throw std::runtime_error("phase-only witness must not invent a queued worker update");
	    },
	    [&](auto&) {
		    result.Events.push_back(3);
	    });
	sgre::core::LooperTaskOperations task;
	task.ReadState = [&] {
		return state;
	};
	task.Update = [&] {
		result.Events.push_back(2);
		if ((sample.Flags & 8) != 0) {
			state.Status = sgre::scene::TaskStatus::Retire;
		}
	};
	task.Draw = [&](std::uint32_t argument) {
		Require(argument == 0, "native Looper Draw argument changed");
		result.Events.push_back(4);
	};
	task.CanRetire = [] {
		return true;
	};
	task.Retire = [&] {
		result.Events.push_back(6);
	};
	sgre::core::LooperFrame looper(
	    workers, std::move(task), [&](auto& actual) {
		    Require(actual.InWait(), "native +32 wait phase must enclose the actual pump");
		    result.Events.push_back(7);
		    ++pumps;
		    static_cast<void>(actual.Clock().Advance(100 + pumps * sample.Elapsed));
		    if (pumps == sample.Iterations && (sample.Flags & 24) == 0) {
			    state.Status = sgre::scene::TaskStatus::Complete;
		    }
		    if ((sample.Flags & 1) != 0 && pumps == 1) {
			    actual.SkipNextIteration();
		    }
	    },
	    100);
	state.DrawPaused = (sample.Flags & 4) != 0;
	if ((sample.Flags & 2) != 0) {
		looper.SkipNextIteration();
	}
	std::array<sgre::scene::ParallelTaskSet*, 5> groups{&looper.Hooks().Group(sgre::scene::LooperHookPhase::PreLoop), &looper.Group(sgre::core::LooperFrameGroup::AfterUpdate), &looper.Hooks().Group(sgre::scene::LooperHookPhase::PostLoop), &looper.Group(sgre::core::LooperFrameGroup::VSync), &looper.Group(sgre::core::LooperFrameGroup::AfterWait)};
	constexpr std::array<std::uint8_t, 5> events{1, 8, 5, 9, 10};
	for (std::size_t index = 0; index < groups.size(); ++index) {
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [&, index](auto) {
			result.Events.push_back(events[index]);
			if (events[index] == 1) {
				post.QueuePost();
			}
			if (events[index] == 5 && (sample.Flags & 16) != 0) {
				state.Status = sgre::scene::TaskStatus::Retire;
			}
		};
		static_cast<void>(groups[index]->Append(std::move(callbacks)));
	}
	bool continuing = true;
	for (std::uint32_t iteration = 0; iteration < sample.Iterations; ++iteration) {
		Require(continuing, "native case stopped before its required final iteration");
		continuing = looper.RunIteration();
	}
	Require(!continuing, "native case remained active after its exact final iteration");
	result.Frames = looper.FrameNumber();
	result.Remaining = looper.RemainingFixedTicks();
	result.Accumulated = looper.AccumulatedTicks();
	result.Variable = looper.Clock().VariableStep();
	result.Skip = looper.SkipPending();
	const auto observed = result;
	for (auto* group : groups) {
		group->Clear();
	}
	looper.ReleaseTask();
	looper.Close();
	post.Close();
	workers.Close();

	return observed;
}

static void IndependentPhases() {
	const auto normal = Run(Case{});
	Require(normal.Events == std::vector<std::uint8_t>({1, 2, 3, 8, 4, 5, 7, 9, 10}), "native update/fence/draw/post/wait/VSync order changed");
	Require(normal.Frames == 1 && normal.Remaining == 1 && normal.Variable == 1 && normal.Accumulated == 1, "native frame counters changed");
	Case retire;
	retire.Flags = 8;
	const auto retiring = Run(retire);
	Require(retiring.Events == std::vector<std::uint8_t>({1, 2, 3, 8, 5, 6, 7, 9, 10}), "retiring task must skip Draw and retain final wait/VSync");
	Case skip;
	skip.Flags = 2;
	const auto skipped = Run(skip);
	Require(skipped.Events == std::vector<std::uint8_t>({7, 9, 10}), "initial skip must bypass actual update/fence/draw/post phases only");
	const auto native = sgre::core::FrameClock::NativeLooper(100);
	Require(native.VariableStep() == 0 && !native.VariableFrame() && native.ResidualTicks() == 0, "actual native Looper initial clock fields changed");
	Require(sgre::core::FrameClock(100).VariableStep() == 1, "prepared historical clock construction changed");
}

static void NativeCases(const char* filename) {
	std::ifstream stream(filename, std::ios::binary);
	Require(stream.good(), "native Looper fixture is unavailable");
	const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
	Require(bytes.size() >= 8 && bytes.size() < 16384 && std::string(bytes.begin(), bytes.begin() + 4) == "LPFR", "native Looper fixture header invalid");
	std::size_t offset = 4;
	const auto count = ReadWord(bytes, offset, 4);
	Require(count == 28, "native Looper fixture case count changed");
	for (std::size_t index = 0; index < count; ++index) {
		Case sample;
		sample.Iterations = static_cast<std::uint32_t>(ReadWord(bytes, offset, 4));
		sample.Elapsed = static_cast<std::uint32_t>(ReadWord(bytes, offset, 4));
		sample.Flags = static_cast<std::uint32_t>(ReadWord(bytes, offset, 4));
		const auto events = ReadWord(bytes, offset, 4);
		Require(events <= 64 && offset <= bytes.size() && events <= bytes.size() - offset, "native Looper event count invalid");
		const std::vector<std::uint8_t> expected(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(offset + events));
		offset += events;
		const auto fixed = ReadWord(bytes, offset, 4);
		const auto remaining = ReadWord(bytes, offset, 4);
		const auto frames = ReadWord(bytes, offset, 4);
		static_cast<void>(ReadWord(bytes, offset, 4));
		const auto accumulated = ReadWord(bytes, offset, 8);
		const auto variable = ReadWord(bytes, offset, 4);
		static_cast<void>(ReadWord(bytes, offset, 8));
		static_cast<void>(ReadWord(bytes, offset, 4));
		const auto flags = ReadWord(bytes, offset, 4);
		const auto actual = Run(sample);
		Require(fixed == 1 && actual.Remaining == remaining && actual.Frames == frames && std::bit_cast<std::uint64_t>(actual.Accumulated) == accumulated && std::bit_cast<std::uint32_t>(actual.Variable) == variable && actual.Events == expected && flags == (actual.Skip ? 0x100u : 0u), "original Looper case mismatch at index " + std::to_string(index));
	}
	Require(offset == bytes.size(), "native Looper fixture trailing bytes");
}

template <typename Operation>
static void Reject(Operation operation, const std::string& message) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error(message);
}

static void LifetimeAndAdmission() {
	sgre::core::ThreadTaskFrame workers([] {
		return std::uint64_t{123};
	},
	    1);
	sgre::core::LooperTaskState state;
	sgre::core::LooperFrame* actual = nullptr;
	std::size_t updates = 0;
	std::size_t retirements = 0;
	std::size_t reentry_checks = 0;
	bool can_retire = false;
	bool report_retirement_failure = false;
	sgre::core::LooperTaskOperations task;
	task.ReadState = [&] {
		if (actual) {
			Reject([&] {
				static_cast<void>(actual->RunIteration());
			},
			    "actual root state read allowed recursive iteration");
			++reentry_checks;
		}

		return state;
	};
	task.Update = [&] {
		++updates;
	};
	task.Draw = [](auto) {
	};
	task.CanRetire = [&] {
		Reject([&] {
			actual->ReleaseTask();
		},
		    "actual retirement preflight allowed recursive root release");

		return can_retire;
	};
	task.Retire = [&] {
		Require(actual->HasTask(), "containing4e0620 root visibility must persist through actual deletion");
		++retirements;
		if (report_retirement_failure) {
			throw std::runtime_error("actual structural retirement completed with a reported failure");
		}
	};
	sgre::core::LooperFrame looper(
	    workers, std::move(task), [&](auto& owner) {
		    static_cast<void>(owner.Clock().Advance(117));
		    state.Status = sgre::scene::TaskStatus::Complete;
	    },
	    100);
	actual = &looper;
	state.Asynchronous = true;
	Reject([&] {
		static_cast<void>(looper.RunIteration());
	},
	    "unsupported asynchronous root was admitted");
	Require(updates == 0 && looper.FrameNumber() == 0 && looper.HasTask(), "asynchronous admission mutated the actual root/frame");
	state.Asynchronous = false;
	Require(!looper.RunIteration() && updates == 1 && reentry_checks >= 5 && !looper.CanClose(), "Complete root exit or whole-read callback guard changed");
	Reject([&] {
		looper.ReleaseTask();
	},
	    "borrowed actual root was released");
	Require(looper.HasTask() && retirements == 0, "rejected retirement detached the actual root");
	can_retire = true;
	report_retirement_failure = true;
	Reject([&] {
		looper.ReleaseTask();
	},
	    "completed retirement failure was suppressed");
	Require(!looper.HasTask() && retirements == 1 && looper.CanClose(), "completed deletion must clear root and never invoke it again");
	looper.ReleaseTask();
	looper.Close();
	workers.Close();
}

static void RetiredGroupGate() {
	sgre::core::ThreadTaskFrame workers([] {
		return std::uint64_t{42};
	},
	    1);
	sgre::core::LooperTaskState state;
	sgre::core::LooperTaskOperations task;
	task.ReadState = [&] {
		return state;
	};
	task.Update = [&] {
		state.Status = sgre::scene::TaskStatus::Complete;
	};
	task.Draw = [](auto) {
	};
	task.CanRetire = [] {
		return true;
	};
	task.Retire = [] {
	};
	sgre::core::LooperFrame looper(
	    workers, std::move(task), [](auto& owner) {
		    static_cast<void>(owner.Clock().Advance(17));
	    },
	    0);
	auto& group = looper.Group(sgre::core::LooperFrameGroup::AfterUpdate);
	group.SetRetireWhenEmpty(true);
	group.Update();
	Require(group.Status() == sgre::scene::TaskStatus::Retire, "group fixture did not establish actual own retired status");
	std::size_t calls = 0;
	sgre::scene::ParallelTaskCallbacks callbacks;
	callbacks.Update = [&](auto) {
		++calls;
	};
	static_cast<void>(group.Append(std::move(callbacks)));
	Require(!looper.RunIteration() && calls == 0, "native own-group status gate admitted retired group children");
	group.Clear();
	looper.ReleaseTask();
	looper.Close();
	workers.Close();
}

static void ActualWorkerPreflight() {
	sgre::core::ThreadTaskFrame workers([] {
		return std::uint64_t{42};
	},
	    1);
	std::size_t task_reads = 0;
	std::size_t mutations = 0;
	sgre::core::LooperTaskOperations task;
	task.ReadState = [&] {
		++task_reads;

		return sgre::core::LooperTaskState{};
	};
	task.Update = [&] {
		++mutations;
	};
	task.Draw = [&](auto) {
		++mutations;
	};
	task.CanRetire = [] {
		return true;
	};
	task.Retire = [&] {
		++mutations;
	};
	sgre::core::LooperFrame looper(
	    workers, std::move(task), [&](auto&) {
		    ++mutations;
	    },
	    0);
	sgre::core::ThreadTaskReceiver receiver(
	    workers, [](auto&) {
		    throw std::runtime_error("preflight Post-only fixture must not invent a worker update");
	    },
	    [&](auto&) {
		    Reject([&] {
			    static_cast<void>(looper.RunIteration());
		    },
		        "containing Looper admitted the actual worker Post phase");
	    });
	receiver.QueuePost();
	workers.RunFrame();
	Require(task_reads == 0 && mutations == 0 && looper.FrameNumber() == 0, "actual busy worker preflight occurred after containing task mutation");
	receiver.Close();
	workers.Close();
	Reject([&] {
		static_cast<void>(looper.RunIteration());
	},
	    "containing Looper admitted its closed worker owner");
	Require(task_reads == 0 && mutations == 0 && looper.HasTask(), "closed worker preflight changed actual task ownership");
	looper.ReleaseTask();
	Require(mutations == 1, "actual containing release did not retire its task once");
	looper.Close();
}

static void ActualSystemSeconds() {
	sgre::core::ThreadTaskFrame workers([] {
		return std::uint64_t{0};
	},
	    1);
	sgre::core::LooperTaskState state;
	sgre::core::LooperTaskOperations task;
	task.ReadState = [&] {
		return state;
	};
	task.Update = [] {
	};
	task.Draw = [](auto) {
	};
	task.CanRetire = [] {
		return true;
	};
	task.Retire = [] {
	};
	std::uint64_t milliseconds = 0;
	sgre::core::LooperFrame looper(
	    workers, std::move(task), [&](auto& actual) {
		    milliseconds += 500;
		    static_cast<void>(actual.Clock().Advance(milliseconds));
	    },
	    0);
	sgre::script::SquirrelVm vm;
	{
		sgre::script::ScriptSystemBindings bindings(vm, looper.Clock());
		const auto system = bindings.SystemClass();
		const auto method = vm.GetSlot(system, "getSystemSecond");
		const std::array<sgre::script::SquirrelValue, 2> arguments{system, true};
		Reject([&] {
			static_cast<void>(vm.Invoke(method, arguments));
		},
		    "system seconds admitted a missing actual Looper");
		bindings.ConfigureLooperTime(looper);
		for (const auto expected : std::array<std::int32_t, 5>{0, 0, 1, 1, 2}) {
			Require(std::get<std::int32_t>(vm.Invoke(method, arguments).Value) == expected, "native System second differs from actual accumulated half-second Looper steps");
			Require(looper.RunIteration(), "actual System fixture unexpectedly retired its root");
		}
		Reject([&] {
			bindings.ConfigureLooperTime(looper);
		},
		    "system time admitted rebinding its containing owner");
	}
	vm.Close();
	looper.ReleaseTask();
	looper.Close();
	workers.Close();
}

static void ActualPassedFrame() {
	sgre::core::ThreadTaskFrame workers([] {
		return std::uint64_t{0};
	},
	    1);
	sgre::core::LooperTaskState state;
	sgre::core::LooperTaskOperations task;
	task.ReadState = [&] {
		return state;
	};
	task.Update = [] {
	};
	task.Draw = [](auto) {
	};
	task.CanRetire = [] {
		return true;
	};
	task.Retire = [] {
	};
	std::uint64_t milliseconds = 0;
	sgre::core::LooperFrame looper(workers, std::move(task), [&](auto& actual) {
		milliseconds += 100;
		static_cast<void>(actual.Clock().Advance(milliseconds));
	},
	    0);
	sgre::script::SquirrelVm vm;
	auto bindings = std::make_unique<sgre::script::ScriptSystemBindings>(vm, looper.Clock());
	const auto system = bindings->SystemClass();
	const auto method = vm.GetSlot(system, "getPassedFrame");
	const std::array<sgre::script::SquirrelValue, 3> arguments{system, vm.CreateTable(), false};
	Reject([&] {
		static_cast<void>(vm.Invoke(method, arguments));
	},
	    "passed frame admitted a missing containing Looper");
	bindings->ConfigureLooperTime(looper);
	Require(!vm.HasSlot(vm.RootTable(), "getPassedFrame"), "passed frame was incorrectly registered as a root function");
	Require(std::get<float>(vm.Invoke(method, arguments).Value) == 1 && looper.Clock().VariableStep() == 0, "native fixed base is one even before the real Looper clock's first sample");
	looper.Clock().SetVariableFrame(true);
	Require(std::get<float>(vm.Invoke(method, arguments).Value) == 1 && looper.Clock().VariableStep() == 0, "variable elapsed frame clamps the constructor's real zero without changing it");
	Require(looper.RunIteration(), "passed-frame fixture unexpectedly retired its actual root");
	Require(looper.FrameNumber() == 1 && looper.Clock().VariableStep() == 6 && std::get<float>(vm.Invoke(method, arguments).Value) == 6, "native getter reads the actual six-tick sample rather than frame count or accumulated seconds");
	const auto frame_number = looper.FrameNumber();
	const auto accumulated = looper.AccumulatedTicks();
	const auto residual = looper.Clock().ResidualTicks();
	Require(std::get<float>(vm.Invoke(method, arguments).Value) == 6 && looper.FrameNumber() == frame_number && looper.AccumulatedTicks() == accumulated && looper.Clock().ResidualTicks() == residual, "observing elapsed frame must not advance or consume the actual Looper");
	looper.Clock().SetVariableFrame(false);
	Require(std::get<float>(vm.Invoke(method, arguments).Value) == 1 && looper.Clock().VariableStep() == 6, "fixed mode returns immutable base independently of the retained variable sample");
	looper.ReleaseTask();
	looper.Close();
	Reject([&] {
		static_cast<void>(vm.Invoke(method, arguments));
	},
	    "passed frame admitted its retired actual Looper");
	bindings.reset();
	Reject([&] {
		static_cast<void>(vm.Invoke(method, arguments));
	},
	    "passed frame admitted expired native bindings");
	vm.Close();
	workers.Close();
}

int main(int argc, char** argv) {
	try {
		ActualSystemSeconds();
		ActualPassedFrame();
		IndependentPhases();
		LifetimeAndAdmission();
		RetiredGroupGate();
		ActualWorkerPreflight();
		if (argc == 2) {
			NativeCases(argv[1]);
		} else {
			Require(argc == 1, "usage: looper_frame_tests [native-phase-fixture]");
		}
		std::cout << "Looper phase owner and native clock cases PASS\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
