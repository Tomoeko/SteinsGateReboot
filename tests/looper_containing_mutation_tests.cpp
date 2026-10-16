#include "sgre/core/looper_frame.hpp"

#include <array>
#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<sgre::core::LooperContainingMutation>);
static_assert(!std::is_move_constructible_v<sgre::core::LooperContainingMutation>);

static void Require(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "first mutation fixture failure: " << message << '\n';
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}
	Require(false, "missing actual containing mutation rejection");
}

static std::uint64_t Counter() {
	return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

static void TestMutation() {
	sgre::core::ThreadTaskFrame workers(Counter, 1, 2);
	sgre::core::ThreadTaskFrame wrong_workers(Counter, 1, 1);
	sgre::core::LooperFrame* actual = nullptr;
	std::size_t updates = 0;
	std::size_t draws = 0;
	std::size_t waits = 0;
	std::size_t retired = 0;
	std::size_t group_updates = 0;
	std::size_t group_releases = 0;
	std::size_t writer_updates = 0;
	sgre::core::ThreadTaskReceiver writer(workers, [&](auto&) {
		++writer_updates;
	},
	    [](auto&) {
	    });
	sgre::core::ThreadTaskReceiver quiescent(workers, [](auto&) {
	},
	    [](auto&) {
	    });
	sgre::core::LooperTaskState state;
	sgre::core::LooperTaskOperations task;
	task.ReadState = [&] {
		return state;
	};
	task.Update = [&] {
		++updates;
		RequireFailure([&] {
			sgre::core::LooperContainingMutation rejected(*actual, workers);
		});
	};
	task.Draw = [&](std::uint32_t argument) {
		Require(argument == 0, "actual root draw argument changed");
		++draws;
	};
	task.CanRetire = [&] {
		return writer.CanClose();
	};
	task.Retire = [&] {
		++retired;
	};
	sgre::core::LooperFrame looper(workers, std::move(task), [&](auto& frame) {
		Require(frame.InWait(), "actual Looper wait did not enclose its fixture pump");
		++waits;
		RequireFailure([&] {
			sgre::core::LooperContainingMutation rejected(frame, workers);
		});
	},
	    0, 3);
	actual = &looper;
	try {
		const std::array<sgre::scene::ParallelTaskSet*, 5> groups{&looper.Hooks().Group(sgre::scene::LooperHookPhase::PreLoop), &looper.Hooks().Group(sgre::scene::LooperHookPhase::PostLoop), &looper.Group(sgre::core::LooperFrameGroup::VSync), &looper.Group(sgre::core::LooperFrameGroup::AfterUpdate), &looper.Group(sgre::core::LooperFrameGroup::AfterWait)};
		for (auto* group : groups) {
			sgre::scene::ParallelTaskCallbacks fixture_clock;
			fixture_clock.Update = [&](auto) {
				++group_updates;
				RequireFailure([&] {
					sgre::core::LooperContainingMutation rejected(looper, workers);
				});
			};
			fixture_clock.Draw = [&](auto, std::uint32_t) {
				++group_updates;
			};
			fixture_clock.Release = [&](auto) {
				++group_releases;
			};
			static_cast<void>(group->Append(std::move(fixture_clock)));
		}
		RequireFailure([&] {
			sgre::core::LooperContainingMutation rejected(looper, wrong_workers);
		});
		std::exception_ptr foreign_error;
		std::thread foreign([&] {
			try {
				sgre::core::LooperContainingMutation rejected(looper, workers);
			} catch (...) {
				foreign_error = std::current_exception();
			}
		});
		foreign.join();
		Require(foreign_error != nullptr, "foreign thread acquired the actual containing mutation");

		writer.QueueUpdate();
		try {
			sgre::core::LooperContainingMutation mutation(looper, workers);
			mutation.RequireOwner(looper, workers);
			RequireFailure([&] {
				mutation.RequireOwner(looper, wrong_workers);
			});
			RequireFailure([&] {
				sgre::core::LooperContainingMutation nested(looper, workers);
			});
			RequireFailure([&] {
				static_cast<void>(looper.RunIteration());
			});
			RequireFailure([&] {
				looper.ReleaseTask();
			});
			RequireFailure([&] {
				looper.Close();
			});
			RequireFailure([&] {
				workers.RunFrame();
			});
			RequireFailure([&] {
				quiescent.QueueUpdate();
			});
			RequireFailure([&] {
				writer.QueuePost();
			});
			RequireFailure([&] {
				workers.Close();
			});
			RequireFailure([&] {
				looper.RequireIdleContainingPhase();
			});
			workers.RequireCanRunFrame();
			Require(writer.State().UpdatePending && !writer.CanClose() && !writer.State().PostPending && updates == 0 && draws == 0 && waits == 0 && retired == 0 && writer_updates == 0 && looper.FrameNumber() == 0, "scope consumed actual pending work or admitted containing callbacks");
			for (auto* group : groups) {
				RequireFailure([&] {
					group->Update();
				});
				RequireFailure([&] {
					group->Draw(0);
				});
			}
			Require(group_updates == 0, "direct actual list traversal bypassed containing mutation");
			Require(quiescent.CanClose(), "exact quiescent receiver retirement became circular");
			quiescent.Close();
			mutation.RequireOwner(looper, workers);
			throw std::runtime_error("authored dependent cleanup first cause");
		} catch (const std::runtime_error& error) {
			Require(std::string_view(error.what()) == "authored dependent cleanup first cause", "mutation unwinding replaced the first failure");
		}
		looper.RequireIdleContainingPhase();
		Require(writer.State().UpdatePending && writer_updates == 0, "scope destruction manufactured worker completion");
		workers.RunFrame();
		Require(writer_updates == 1 && writer.CanClose(), "released claim did not admit actual queued worker delivery");
		Require(looper.RunIteration() && updates == 1 && draws == 1 && waits == 1 && group_updates == 5, "released claim did not restore genuine iteration phase order");

		{
			sgre::core::LooperContainingMutation mutation(looper, workers);
			for (auto* group : groups) {
				group->Clear();
			}
			Require(group_releases == 5 && group_updates == 5, "scope blocked actual clock release or invoked a traversal");
			writer.Close();
			Require(writer.CanClose() && !workers.CanClose(), "scope did not preserve receiver retirement while retaining the actual controller");
			mutation.RequireOwner(looper, workers);
		}
		looper.ReleaseTask();
		Require(retired == 1 && !looper.HasTask(), "actual root retirement did not execute exactly once");
		Require(looper.CanClose() && workers.CanClose(), "actual released owners are not quiescent before final mutation");
		{
			sgre::core::LooperContainingMutation mutation(looper, workers);
			Require(!looper.CanClose() && !workers.CanClose(), "empty actual owners admitted shutdown while their scope retained identity");
			RequireFailure([&] {
				looper.Close();
			});
			RequireFailure([&] {
				workers.Close();
			});
			workers.RequireCanRunFrame();
		}
		looper.Close();
		workers.Close();
		wrong_workers.Close();
		RequireFailure([&] {
			sgre::core::LooperContainingMutation rejected(looper, workers);
		});
	} catch (const std::exception& error) {
		std::cerr << "original fixture cause before containing destruction: " << error.what() << '\n';
		throw;
	}
}

int main() {
	try {
		TestMutation();
		std::cout << "PASS exact Looper/controller/five-list mutation scope, reentry fences, pending work retained, real receiver retirement, first-cause unwind and actual delivery afterward; no native/OS/GPU execution\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
