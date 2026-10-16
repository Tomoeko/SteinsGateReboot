#include "prepared_driver_leaf_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void RequireUnchangedDriver(const PreparedLeafFixture& fixture, std::size_t task_count) {
	Require(fixture.Driver.Close->Phase() == sgre::script::PreparedDriverClosePhase::Threads && !fixture.Driver.Vm.IsClosed() && fixture.Driver.Logs.IsInitialized() && !fixture.Driver.Manager.IsClosed() && !fixture.Driver.Meter.IsClosed() && fixture.Driver.Controller.Parallel().Count() == task_count && fixture.Driver.Events.empty(), "leaf admission must reject before any sibling Thread/VM/history/scene mutation");
}

static void TestExactOwnerAdmission(const std::filesystem::path& directory) {
	PreparedLeafFixture fixture(directory);
	const auto count = fixture.Driver.Controller.Parallel().Count();
	RequireFailure([&] {
		static_cast<void>(fixture.Attach());
	});
	Require(fixture.Second().Count() == 0 && fixture.Runtime->PlayerCount() == 0 && fixture.Runtime->CanClose(), "unregistered actual Layer runtime must reject before installing a player or advancing actual random ownership");
	RequireUnchangedDriver(fixture, count);
	sgre::scene::MotionResourceManager foreign(fixture.Driver.Textures);
	auto mismatched = fixture.MakeRuntime(foreign);
	RequireFailure([&] {
		fixture.Driver.Layers.AttachMotionRuntime(fixture.Leaf, *mismatched);
	});
	Require(mismatched->CanClose() && fixture.Runtime->CanClose() && fixture.Second().Count() == 0, "mismatched manager identity must reject without borrowing either runtime");
	fixture.Bind();
	RequireFailure([&] {
		fixture.Bind();
	});
	const std::array<sgre::script::SquirrelValue, 1> receiver{fixture.Driver.Vm.RootTable()};
	const auto other = fixture.Driver.Vm.Invoke(fixture.Driver.Layers.LayerClass(), receiver).Value;
	RequireFailure([&] {
		static_cast<void>(sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Driver.Layers.MotionTasks(other), false, "", [](const auto&) {
			throw std::runtime_error("cross-owner rejection unexpectedly delivered an event");
		}));
	});
	Require(fixture.Runtime->PlayerCount() == 0 && fixture.Second().Count() == 0 && fixture.Driver.Layers.MotionTasks(other).Group(sgre::scene::MotionTaskGroup::Second).Count() == 0, "bound runtime must reject another real leaf before any group mutation");
	sgre::scene::LayerTree other_tree;
	RequireFailure([&] {
		fixture.Driver.Layers.RequireCanReleaseAll(fixture.Driver.Vm, fixture.Driver.Objects, other_tree);
	});
	fixture.Driver.Layers.DetachMotionRuntime(fixture.Leaf, *fixture.Runtime);
	Require(fixture.Runtime->CanClose() && fixture.Runtime->PlayerCount() == 0, "explicit fresh-runtime detach must expire the concrete borrowed association");
	fixture.Runtime.reset();
	RequireUnchangedDriver(fixture, count);
	fixture.Driver.Close->Close();
}

static void TestQueuedRowsAndRealRetirement(const std::filesystem::path& directory) {
	PreparedLeafFixture fixture(directory);
	fixture.Bind();
	auto task = fixture.Attach();
	const auto before_references = fixture.Owner->ReferenceCount(fixture.Project);
	const auto listener = fixture.Driver.Vm.Retain(fixture.Driver.Listener());
	const auto thread = fixture.Driver.Vm.Retain(fixture.Driver.Threads.Spawn(fixture.Driver.DormantFunction()));
	fixture.Driver.Objects.Subscribe(thread.Value(), listener.Value(), [&](const auto&) {
		fixture.Driver.Events.emplace_back("thread");

		return false;
	});
	fixture.QueueRows(task);
	Require(fixture.Runtime->Context().CanClear() && !task.Player().CanMutate() && fixture.Runtime->Identifiers().LiveCount() == 1, "idle submission still retains genuine queued row/selected ID borrows");
	RequireFailure([&] {
		fixture.Runtime->RequireCanRetirePlayers();
	});
	const auto count = fixture.Driver.Controller.Parallel().Count();
	RequireFailure([&] {
		fixture.Driver.Close->Close();
	});
	RequireUnchangedDriver(fixture, count);
	Require(task.IsLive() && fixture.Runtime->Context().Size() == 1 && fixture.Owner->ReferenceCount(fixture.Project) == before_references, "rejected close must retain the same task, row and actual PSB/source owners");
	auto view = fixture.Driver.Layers.BorrowMotionView(fixture.Leaf, [](bool) {
		throw std::runtime_error("retirement fixture unexpectedly changed filtering");
	},
	    [&] {
		    Require(!task.IsLive() && fixture.Runtime->CanClose() && fixture.Runtime->PlayerCount() == 0 && fixture.Driver.Vm.ResolveWeakReference(fixture.WeakPsb) == sgre::script::SquirrelValue{}, "actual task/runtime retirement and source root drop must precede borrowed view detach");
		    RequireFailure([&] {
			    static_cast<void>(fixture.Runtime->Context());
		    });
		    fixture.Driver.Events.emplace_back("leaf");
	    });
	fixture.Runtime->Context().Clear();
	fixture.Driver.Close->Close();
	Require(fixture.Driver.Events == std::vector<std::string>{"thread", "leaf"} && !view.IsAttached() && fixture.Driver.Close->Phase() == sgre::script::PreparedDriverClosePhase::Closed && !task.IsLive() && fixture.Driver.Tree.ChildCount() == 0, "real notification then VM leaf task/runtime/source retirement must precede parent scene close");
	fixture.Runtime.reset();
}

static void TestActualLeafCallbacksAndPreparation(const std::filesystem::path& directory) {
	PreparedLeafFixture fixture(directory);
	fixture.Bind();
	const auto count = fixture.Driver.Controller.Parallel().Count();
	std::size_t preparation_rejections = 0;
	fixture.OnAlias = [&] {
		RequireFailure([&] {
			fixture.Driver.Close->Close();
		});
		RequireUnchangedDriver(fixture, count);
		++preparation_rejections;
	};
	const auto selected = fixture.Owner->Secondary().ResolveTimeline("character", "main");
	Require(selected.has_value(), "actual prepared project selection must exist");
	static_cast<void>(fixture.Runtime->Prepare(*selected));
	Require(preparation_rejections == 1 && fixture.Runtime->PlayerCount() == 0, "actual alias preparation must reject cleanup even outside active task-group traversal");
	fixture.OnAlias = {};
	std::size_t action_rejections = 0;
	fixture.OnAction = [&] {
		RequireFailure([&] {
			fixture.Driver.Close->Close();
		});
		RequireUnchangedDriver(fixture, count);
		++action_rejections;
	};
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	fixture.Second().Update();
	Require(action_rejections != 0 && fixture.Runtime->Context().Size() == 0 && task.IsLive(), "active real action callback must reject before parent mutation without fabricating a queued renderer row");
	fixture.OnAction = {};
	for (const auto group : {sgre::scene::MotionTaskGroup::CommonFirst, sgre::scene::MotionTaskGroup::CommonSecond, sgre::scene::MotionTaskGroup::First}) {
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [&](auto) {
			RequireFailure([&] {
				fixture.Driver.Close->Close();
			});
			RequireUnchangedDriver(fixture, count);
		};
		auto& actual = fixture.Driver.Layers.MotionTasks(fixture.Leaf).Group(group);
		static_cast<void>(actual.Append(std::move(callbacks)));
		actual.Update();
	}
	fixture.Driver.Close->Close();
	Require(!task.IsLive() && fixture.Runtime->CanClose(), "actual four-group cleanup must retire the supplied runtime after callbacks stop");
}

static void TestLateNotificationRecheck(const std::filesystem::path& directory) {
	PreparedLeafFixture fixture(directory);
	fixture.Bind();
	auto task = fixture.Attach();
	task.SetVisible(true);
	task.Play("main");
	fixture.Second().Update();
	const auto listener = fixture.Driver.Vm.Retain(fixture.Driver.Listener());
	const auto thread = fixture.Driver.Vm.Retain(fixture.Driver.Threads.Spawn(fixture.Driver.DormantFunction()));
	std::size_t notifications = 0;
	fixture.Driver.Objects.Subscribe(thread.Value(), listener.Value(), [&](const auto&) {
		fixture.Second().Draw(0x1234);
		++notifications;

		return false;
	});
	RequireFailure([&] {
		fixture.Driver.Close->Close();
	});
	Require(notifications == 1 && fixture.Driver.Close->Phase() == sgre::script::PreparedDriverClosePhase::Vm && !fixture.Driver.Vm.IsClosed() && fixture.Driver.Logs.IsInitialized() && !fixture.Driver.Manager.IsClosed() && fixture.Runtime->Context().Size() == 1 && task.IsLive(), "notification-created actual row must stop the live recheck before VM/source mutation");
	fixture.Runtime->Context().Clear();
	fixture.Driver.Close->Close();
	Require(notifications == 1 && fixture.Driver.Vm.IsClosed() && !task.IsLive() && fixture.Runtime->CanClose(), "late corrected borrow must resume actual VM retirement without replaying notifications");
}

static void TestLayerEndsFirstAndDetach(const std::filesystem::path& directory) {
	PreparedLeafFixture fixture(directory);
	fixture.Bind();
	auto task = fixture.Attach();
	fixture.Driver.Layers.ReleaseInstance(fixture.Leaf);
	Require(!task.IsLive() && fixture.Runtime->CanClose() && fixture.Runtime->PlayerCount() == 0 && fixture.Driver.Tree.ChildCount() == 0, "explicit actual Layer retirement must expire runtime borrowing before source-owner fields drop");
	RequireFailure([&] {
		static_cast<void>(fixture.Runtime->Manager());
	});
	fixture.Runtime.reset();
	fixture.Driver.Close->Close();
}

[[noreturn]] static void ExitAfterExpectedRuntimeDestruction() noexcept {
	std::_Exit(0);
}

[[noreturn]] static void ExitAfterIsolatedRuntimeDestruction() noexcept {
	std::_Exit(86);
}

static int DestroyRegisteredRuntimeFirst(const std::filesystem::path& directory, bool verify) {
	PreparedLeafFixture fixture(directory);
	fixture.Bind();
	Require(fixture.Runtime->CanClose() && fixture.Second().Count() == 0 && fixture.Driver.Events.empty(), "early-runtime fixture must isolate an attached real borrow before any other cleanup");
	std::set_terminate(verify ? ExitAfterExpectedRuntimeDestruction : ExitAfterIsolatedRuntimeDestruction);
	fixture.Runtime.reset();

	return 1;
}

int main(int argc, char** argv) {
	try {
		if (argc < 2 || argc > 3) {
			throw std::runtime_error("usage: prepared_driver_leaf_preflight_tests <fixture-directory> [--destroy-runtime-first|--verify-destroy-runtime-first]");
		}
		const auto directory = std::filesystem::path(argv[1]);
		std::filesystem::create_directories(directory);
		if (argc == 3) {
			const auto mode = std::string_view(argv[2]);
			if (mode == "--destroy-runtime-first" || mode == "--verify-destroy-runtime-first") {
				return DestroyRegisteredRuntimeFirst(directory, mode == "--verify-destroy-runtime-first");
			}
			throw std::runtime_error("unknown prepared leaf lifetime mode");
		}

		TestExactOwnerAdmission(directory);
		TestQueuedRowsAndRealRetirement(directory);
		TestActualLeafCallbacksAndPreparation(directory);
		TestLateNotificationRecheck(directory);
		TestLayerEndsFirstAndDetach(directory);
		std::cout << "PASS 5 prepared Driver actual leaf/runtime preflight groups\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
