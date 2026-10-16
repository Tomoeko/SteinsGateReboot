#include "sgre/core/application_event_control.hpp"

#include "sgre/io/file_reader.hpp"

#include <array>
#include <bit>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using Control = sgre::core::ApplicationEventControl;
using Receiver = sgre::core::ApplicationEventCallbacks;
using Operations = sgre::core::ApplicationEventCallbackOperations;
using Slot = sgre::core::ApplicationEventSlot;
using Resize = sgre::core::ApplicationResizeNotice;
using Event = std::array<std::uint32_t, 11>;

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

	throw std::runtime_error("unsupported application event operation was admitted");
}

static std::array<std::uint32_t, 7> StateWords(const Control& control) {
	const auto state = control.State();
	const std::array<bool, 11> flags = {state.SuspendPending, state.ResumePending, state.ClosePending, state.WaitBreakPending, state.QuitPending, state.Suspended, state.Resumed, state.CloseAccepted, state.WaitBreak, state.Quit, state.ResizePending};
	std::uint32_t bits = 0;
	for (std::size_t index = 0; index < flags.size(); ++index) {
		bits |= static_cast<std::uint32_t>(flags[index]) << index;
	}

	return {bits, state.CloseCode, state.ResumeRequests, state.Resize.Code, std::bit_cast<std::uint32_t>(state.Resize.Width), std::bit_cast<std::uint32_t>(state.Resize.Height), state.ResizeRequests};
}

static Resize Notice(std::uint32_t code, std::uint32_t width_bits, std::uint32_t height_bits) {
	Resize notice;
	notice.Code = code;
	notice.Width = std::bit_cast<float>(width_bits);
	notice.Height = std::bit_cast<float>(height_bits);

	return notice;
}

static Operations CountingOperations(std::array<std::uint32_t, 4>& counts) {
	Operations operations;
	operations.Suspend = [&] {
		++counts[0];
	};
	operations.Resume = [&] {
		++counts[1];
	};
	operations.Close = [&] {
		++counts[2];

		return true;
	};
	operations.Resize = [&](Resize) {
		++counts[3];
	};

	return operations;
}

static void TestNativeNullAndReset() {
	Control control;
	Require(StateWords(control) == std::array<std::uint32_t, 7>{0, 1, 0, 0, 0, 0, 0}, "actual initialized native state must not install callbacks");
	control.RequestSuspend();
	control.RequestResume();
	control.RequestClose(0);
	control.RequestClose(2);
	control.RequestResize(Notice(3, 0x80000000u, 0x7fc01234u));
	control.Flush();
	Require(!control.State().Suspended && control.State().Resumed && control.State().CloseAccepted && control.State().CloseCode == 0, "null callbacks preserve ordered resume and accepted sticky zero-code close");
	control.RequestResize(Notice(9, 0x3f800000u, 0x40000000u));
	control.Reset();
	Require(control.State().ResizePending && control.State().Resize.Code == 9 && control.State().ResizeRequests == 2 && control.State().ResumeRequests == 1 && control.State().CloseCode == 1, "native reset retains actual resize state and both counters");
	control.RequestQuit();
	control.Flush();
	control.RequestSuspend();
	control.RequestResume();
	control.RequestClose(1);
	control.RequestResize(Notice(1, 0, 0));
	control.Flush();
	Require(control.State().Quit && control.State().SuspendPending && control.State().ResumePending && control.State().ClosePending && control.State().ResizePending, "committed quit gates the four ordinary stages without discarding their pending requests");
	control.Close();
}

static void TestActualCallbackLifetime() {
	Control control;
	std::array<std::uint32_t, 4> counts{};
	Receiver* active = nullptr;
	auto operations = CountingOperations(counts);
	operations.Suspend = [&] {
		++counts[0];
		Require(!active->CanClose() && !control.CanClose(), "actual active callback must enclose its receiver and control lifetimes");
		RequireFailure([&] {
			active->Close();
		},
		    "finish its actual callback");
		RequireFailure([&] {
			control.Flush();
		},
		    "cannot recursively deliver");
		RequireFailure([&] {
			control.Reset();
		},
		    "stable dispatch boundary");
		Require(control.ExchangeCallback(Slot::Suspend, nullptr) == active, "registration returns the exact borrowed previous receiver");
	};
	Receiver receiver(control, std::move(operations));
	active = &receiver;
	Require(control.ExchangeCallback(Slot::Suspend, &receiver) == nullptr, "actual initial native slot is null");
	Control unrelated;
	RequireFailure([&] {
		static_cast<void>(unrelated.ExchangeCallback(Slot::Resume, &receiver));
	},
	    "exact control owner");
	control.RequestSuspend();
	control.Flush();
	Require(counts[0] == 1 && receiver.CanClose(), "self unregistration cannot destroy an executing receiver");
	receiver.Close();
	control.Close();
	unrelated.Close();
	RequireFailure([&] {
		control.RequestResume();
	},
	    "actual live owner");
}

static void TestConcurrentRequests() {
	Control control;
	std::array<std::thread, 2> workers;
	std::array<std::exception_ptr, 2> failures;
	try {
		for (std::size_t index = 0; index < workers.size(); ++index) {
			workers[index] = std::thread([&, index] {
				try {
					RequireFailure([&] {
						control.Flush();
					},
					    "serialized owner thread");
					for (std::size_t request = 0; request < 2048; ++request) {
						control.RequestResume();
					}
				} catch (...) {
					failures[index] = std::current_exception();
				}
			});
		}
	} catch (...) {
		for (auto& worker : workers) {
			if (worker.joinable()) {
				worker.join();
			}
		}
		throw;
	}
	for (auto& worker : workers) {
		worker.join();
	}
	for (const auto& failure : failures) {
		if (failure) {
			std::rethrow_exception(failure);
		}
	}
	Require(control.State().ResumeRequests == 4096 && control.State().ResumePending, "real lock must preserve every concurrent resume request count");
	control.Flush();
	Require(control.State().Resumed && !control.State().ResumePending, "owner flush consumes the actual coalesced resume request");
	control.Close();
}

static void TestFailureCompletion() {
	Control control;
	std::array<std::uint32_t, 4> counts{};
	auto operations = CountingOperations(counts);
	operations.Suspend = [&] {
		++counts[0];
		throw std::runtime_error("actual suspend callback failure");
	};
	operations.Close = [&]() -> bool {
		++counts[2];
		throw std::runtime_error("actual close callback failure");
	};
	Receiver receiver(control, std::move(operations));
	for (const auto slot : {Slot::Suspend, Slot::Resume, Slot::Close, Slot::Resize}) {
		static_cast<void>(control.ExchangeCallback(slot, &receiver));
	}
	control.RequestSuspend();
	control.RequestResume();
	control.RequestClose(1);
	control.RequestResize(Notice(1, 0, 0));
	control.RequestQuit();
	RequireFailure([&] {
		control.Flush();
	},
	    "actual suspend callback failure");
	Require(counts == std::array<std::uint32_t, 4>{1, 1, 1, 1} && control.State().Quit && !control.State().CloseAccepted, "authored failure policy finishes live dispatch, vetoes failed close and reports the first failure");
	Require(!control.State().SuspendPending && !control.State().ResumePending && !control.State().ClosePending && !control.State().ResizePending && !control.State().QuitPending, "failed concrete callbacks must not replay automatically");
	receiver.Close();
	control.Close();
}

static void TestNativeFixture(const char* path) {
	sgre::io::FileReader file(path);
	Require(file.Size() >= 8 && file.Size() <= 1024 * 1024, "native event fixture exceeds its compact bound");
	const auto bytes = file.Read(0, static_cast<std::size_t>(file.Size()));
	std::size_t offset = 0;
	auto read = [&] {
		Require(offset <= bytes.size() && 4 <= bytes.size() - offset, "short native event word");
		std::uint32_t value = 0;
		for (std::size_t index = 0; index < 4; ++index) {
			value |= static_cast<std::uint32_t>(bytes[offset++]) << (index * 8);
		}

		return value;
	};
	Require(read() == 0x31434541u, "native event fixture magic must be AEC1");
	const auto cases = read();
	Require(cases > 0 && cases <= 64, "native event fixture exceeds its bounded case count");
	std::size_t total = 0;
	for (std::size_t case_index = 0; case_index < cases; ++case_index) {
		const auto mode = read();
		const auto accepted = read();
		const auto commands = read();
		Require(mode <= 63 && accepted <= 1 && commands <= 128, "native event callback/input controls exceed their bounded domain");
		Control control;
		std::array<std::unique_ptr<Receiver>, 2> receivers;
		std::array<Receiver*, 4> slots{};
		std::vector<Event> events;
		events.reserve(4);
		auto visit = [&](std::uint32_t owner, std::uint32_t slot, Resize notice) {
			Event event{};
			event[0] = owner * 4 + slot;
			const auto snapshot = StateWords(control);
			for (std::size_t index = 0; index < snapshot.size(); ++index) {
				event[index + 1] = snapshot[index];
			}
			if (slot == 3) {
				event[8] = notice.Code;
				event[9] = std::bit_cast<std::uint32_t>(notice.Width);
				event[10] = std::bit_cast<std::uint32_t>(notice.Height);
			}
			Require(events.size() < 4, "native flush exceeds its four concrete callback visits");
			events.push_back(event);
			if (slot == 0 && (mode & 1) != 0) {
				control.RequestResume();
			}
			if (slot == 1 && (mode & 2) != 0) {
				control.RequestSuspend();
			}
			if (slot == 2 && (mode & 4) != 0) {
				control.ClearClose();
			}
			if (slot == 2 && (mode & 8) != 0) {
				control.RequestQuit();
			}
			if (slot == 2 && (mode & 16) != 0) {
				static_cast<void>(control.ExchangeCallback(Slot::Resize, receivers[1].get()));
				slots[3] = receivers[1].get();
			}
			if (slot == 3 && (mode & 32) != 0) {
				control.RequestResize(Notice(42, 0x40800000u, 0x40a00000u));
			}
		};
		for (std::size_t index = 0; index < receivers.size(); ++index) {
			const auto owner = static_cast<std::uint32_t>(index + 1);
			Operations operations;
			operations.Suspend = [&, owner] {
				visit(owner, 0, {});
			};
			operations.Resume = [&, owner] {
				visit(owner, 1, {});
			};
			operations.Close = [&, owner] {
				visit(owner, 2, {});

				return accepted != 0;
			};
			operations.Resize = [&, owner](Resize notice) {
				visit(owner, 3, notice);
			};
			receivers[index] = std::make_unique<Receiver>(control, std::move(operations));
		}
		auto owner_id = [&](const Receiver* receiver) -> std::uint32_t {
			return receiver == nullptr ? 0u : (receiver == receivers[0].get() ? 1u : 2u);
		};
		for (std::size_t command = 0; command < commands; ++command) {
			const auto op = read();
			const auto first = read();
			const auto second = read();
			const auto third = read();
			std::uint32_t previous = 0;
			events.clear();
			switch (op) {
			case 0:
				control.Reset();
				slots.fill(nullptr);
				break;
			case 1:
				control.RequestSuspend();
				break;
			case 2:
				control.RequestResume();
				break;
			case 3:
				control.RequestClose(first);
				break;
			case 4:
				control.RequestResize(Notice(first, second, third));
				break;
			case 5:
				control.RequestQuit();
				break;
			case 6:
				control.ClearClose();
				break;
			case 7:
				control.ClearSuspend();
				break;
			case 8:
				control.Flush();
				break;
			case 9:
			case 10:
			case 11:
			case 12:
				Require(first <= 2, "native callback owner index exceeds its genuine receiver count");
				slots[op - 9] = first == 0 ? nullptr : receivers[first - 1].get();
				previous = owner_id(control.ExchangeCallback(static_cast<Slot>(op - 9), slots[op - 9]));
				break;
			default:
				throw std::runtime_error("native event operation exceeds its recovered domain");
			}
			for (const auto actual : StateWords(control)) {
				Require(read() == actual, "original event state word differs from concrete portable dispatch");
			}
			for (const auto* slot : slots) {
				Require(read() == owner_id(slot), "original callback registration differs from actual receiver identity");
			}
			Require(read() == previous, "original exchange did not return the actual previous callback");
			Require(read() == events.size(), "original live callback count differs");
			for (const auto& event : events) {
				for (const auto actual : event) {
					Require(read() == actual, "original callback entry state/argument word differs");
				}
			}
			++total;
		}
		for (auto& receiver : receivers) {
			receiver->Close();
		}
		control.Close();
	}
	Require(offset == bytes.size(), "native event fixture has trailing bytes");
	std::cout << total << " original event/control operations match\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: application_event_control_tests [event-control-native.bin]");
		TestNativeNullAndReset();
		TestActualCallbackLifetime();
		TestConcurrentRequests();
		TestFailureCompletion();
		if (argc == 2) {
			TestNativeFixture(argv[1]);
		}
		std::cout << "Four concrete event-control fixture groups PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
