#include "prepared_driver_close_fixture.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

using sgre::script::PreparedDriverClosePhase;
using sgre::script::ScriptThreadStatus;
using sgre::script::SquirrelValue;

class ReleaseFixtureGate {
public:
	explicit ReleaseFixtureGate(std::promise<void>& release) : m_Release(release) {
	}
	~ReleaseFixtureGate() noexcept {
		Open();
	}
	void Open() noexcept {
		if (!m_Opened) {
			try {
				m_Release.set_value();
			} catch (...) {
				std::terminate();
			}
			m_Opened = true;
		}
	}
	ReleaseFixtureGate(const ReleaseFixtureGate&) = delete;
	ReleaseFixtureGate& operator=(const ReleaseFixtureGate&) = delete;

private:
	std::promise<void>& m_Release;
	bool m_Opened = false;
};

static void TestPendingBorrowBeforeSiblingMutation(const std::filesystem::path& directory) {
	PreparedCloseFixture fixture(directory);
	fixture.MountScript();
	const auto listener = fixture.Vm.Retain(fixture.Listener());
	const auto function = fixture.Vm.Retain(fixture.Threads.Spawn(fixture.DormantFunction()));
	const auto function_handle = fixture.Threads.ThreadHandle(function.Value());
	const auto file = fixture.Vm.Retain(fixture.Threads.Spawn(fixture.Vm.CreateString("script/item")));
	const auto file_handle = fixture.Threads.ThreadHandle(file.Value());
	for (const auto& instance : {function.Value(), file.Value()}) {
		fixture.Objects.Subscribe(instance, listener.Value(), [&](const auto&) {
			fixture.Events.emplace_back("thread");

			return false;
		});
	}
	std::promise<void> selected;
	auto selected_future = selected.get_future();
	std::promise<void> release;
	auto release_future = release.get_future();
	sgre::core::BackgroundCallTask blocker(fixture.Background, sgre::core::BackgroundCallLane::BelowNormal, [&]() {
		selected.set_value();
		release_future.wait();
	});
	ReleaseFixtureGate gate(release);
	blocker.Update();
	fixture.Background.PostLoop();
	selected_future.get();
	fixture.Controller.Parallel().Update();
	const auto count = fixture.Controller.Parallel().Count();
	const auto function_before = fixture.Scheduler.Info(function_handle);
	fixture.Vm.RequireCanClose();
	RequireFailure([&]() {
		fixture.Scheduler.RequireCanShutdownNativeThreads();
	});
	RequireFailure([&]() {
		fixture.Close->Close();
	});
	Require(fixture.Events.empty() && fixture.Close->Phase() == PreparedDriverClosePhase::Threads && !fixture.Vm.IsClosed() && fixture.Logs.IsInitialized() && !fixture.Manager.IsClosed() && !fixture.Meter.IsClosed(), "pending Thread receiver must reject before sibling notification, VM, history or scene mutation");
	Require(fixture.Controller.Parallel().Count() == count && fixture.Scheduler.Info(function_handle).Status == function_before.Status && fixture.Scheduler.Info(file_handle).HasScriptBuffer && fixture.Vm.Length(fixture.Scheduler.NativeThreadList()) == 2, "readonly admission changed sibling, receiving or real registration ownership");
	gate.Open();
	blocker.Close();
	fixture.PumpReaders();
	fixture.Scheduler.RequireCanShutdownNativeThreads();
	fixture.Close->Close();
	Require(fixture.Events == std::vector<std::string>{"thread", "thread"} && fixture.Close->Phase() == PreparedDriverClosePhase::Closed && std::holds_alternative<std::monostate>(fixture.Scheduler.NativeThreadList()), "actual finished readers must allow real ordered Thread retirement");
	bool closed_query_rejected = false;
	try {
		static_cast<void>(fixture.Scheduler.Info(file_handle));
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("script scheduler VM is closed") != std::string_view::npos, "closed scheduler query reported an unrelated error");
		closed_query_rejected = true;
	}
	Require(closed_query_rejected, "thread state queries must reject after their actual VM owner closes");
	Require(fixture.Mounts.RetainedPsbObjectCount() == 1 && fixture.Mounts.RetainedPsbObject(0).Bytes().size() != 0, "containing close must preserve actual received PSB bytes beyond processor retirement");
}

static void TestActualLoadedFileAndRetirementOrder(const std::filesystem::path& directory) {
	PreparedCloseFixture fixture(directory);
	const auto resource = fixture.CacheActualCompiled();
	const auto filename = fixture.Vm.CreateString("script/item");
	const auto listener = fixture.Vm.Retain(fixture.Listener());
	const auto instance = fixture.Vm.Retain(fixture.Threads.Spawn(filename));
	const auto handle = fixture.Threads.ThreadHandle(instance.Value());
	Require(fixture.Manager.Cache().Info(resource).References == 2 && fixture.Scheduler.Info(handle).HasScriptBuffer, "real Active ScriptBuffer hit requires its independent actual cache pin");
	fixture.Scheduler.SetExecutionStopped(handle, true);
	static_cast<void>(fixture.Scheduler.Advance(1));
	const auto loaded = fixture.Scheduler.Info(handle);
	Require(loaded.Context && loaded.Status == ScriptThreadStatus::Running && !loaded.HasScriptBuffer && loaded.Filename == filename && fixture.Manager.Cache().Info(resource).References == 1, "actual child compiled closure handoff must drop buffer before Running while filename remains");
	fixture.Objects.Subscribe(instance.Value(), listener.Value(), [&](const auto&) {
		Require(!fixture.Vm.IsClosed() && fixture.Logs.IsInitialized() && !fixture.Manager.IsClosed() && fixture.Scheduler.Info(handle).Filename == filename, "Thread notification must see concrete VM/history/cache/filename owners before cleanup");
		RequireFailure([&]() {
			fixture.Scheduler.RequireCanShutdownNativeThreads();
		});
		RequireFailure([&]() {
			fixture.Close->Close();
		});
		fixture.Events.emplace_back("thread");

		return false;
	});
	fixture.MonitorVmClose(false);
	fixture.MonitorActualSceneAndSerial(false);
	fixture.Close->Close();
	Require(fixture.Events == std::vector<std::string>{"thread", "vm", "priority", "font", "font", "serial"}, "visible real Thread/VM/Manager/group/Meter/Serial retirement order differs");
	Require(fixture.Close->Phase() == PreparedDriverClosePhase::Closed && fixture.Vm.IsClosed() && fixture.Manager.IsClosed() && fixture.Meter.IsClosed() && fixture.Controller.Parallel().Count() == 0 && fixture.Controller.Serial().Count() == 0 && fixture.Hooks.CanClose() && fixture.Background.CanClose(), "completed containing owner must retire actual callbacks without an extra background/hook root");
	fixture.Close->Close();
	fixture.Vm.RequireCanClose();
	RequireFailure([&]() {
		static_cast<void>(fixture.Close->Scene());
	});
}

static sgre::scene::ParallelTaskHandle FixtureOnlyClock(const sgre::scene::ParallelTaskSet& group) {
	std::array<sgre::scene::ParallelTaskHandle, 1> order{};
	Require(group.WriteOrder(order) == 1, "fixture requires actual singular Meter clock");

	return order.front();
}

static void TestLateOwnerRecheckAndResume(const std::filesystem::path& directory) {
	PreparedCloseFixture fixture(directory);
	const auto listener = fixture.Vm.Retain(fixture.Listener());
	const auto instance = fixture.Vm.Retain(fixture.Threads.Spawn(fixture.DormantFunction()));
	std::optional<sgre::scene::ParallelTaskLease> clock;
	fixture.Objects.Subscribe(instance.Value(), listener.Value(), [&](const auto&) {
		fixture.Events.emplace_back("thread");
		auto& group = fixture.Hooks.Group(sgre::scene::LooperHookPhase::PostLoop);
		clock.emplace(group.Extract(FixtureOnlyClock(group)));

		return false;
	});
	fixture.Close->RequireCanClose();
	RequireFailure([&]() {
		fixture.Close->Close();
	});
	Require(fixture.Events == std::vector<std::string>{"thread"} && fixture.Close->Phase() == PreparedDriverClosePhase::Vm && std::holds_alternative<std::monostate>(fixture.Scheduler.NativeThreadList()) && !fixture.Vm.IsClosed() && fixture.Logs.IsInitialized() && !fixture.Manager.IsClosed() && !fixture.Meter.IsClosed(), "notification-time hook transfer must reject VM/history/scene mutation after real Thread cleanup");
	clock->Release();
	clock.reset();
	fixture.Close->Close();
	Require(fixture.Events == std::vector<std::string>{"thread"} && fixture.Close->Phase() == PreparedDriverClosePhase::Closed, "phase resume must not replay already-completed actual Thread notifications");
}

static void TestActiveVmSceneMeterAndForeignGuards(const std::filesystem::path& directory) {
	PreparedCloseFixture fixture(directory);
	const auto listener = fixture.Vm.Retain(fixture.Listener());
	const auto instance = fixture.Vm.Retain(fixture.Threads.Spawn(fixture.DormantFunction()));
	fixture.Objects.Subscribe(instance.Value(), listener.Value(), [&](const auto&) {
		fixture.Events.emplace_back("thread");

		return false;
	});
	const auto direct_notify = fixture.Vm.Retain(fixture.Threads.Spawn(fixture.DormantFunction()));
	fixture.Objects.Subscribe(direct_notify.Value(), listener.Value(), [&](const auto&) {
		RequireFailure([&]() {
			fixture.Close->Close();
		});
		Require(fixture.Events.empty() && fixture.Vm.Length(fixture.Scheduler.NativeThreadList()) == 2, "active direct Object notification must reject before notifying its earlier sibling");

		return false;
	});
	fixture.Objects.Notify(direct_notify.Value(), true);
	const auto guard = fixture.Vm.CreateNativeClosure("actual-close-admission", [&](auto&, auto) {
		RequireFailure([&]() {
			fixture.Vm.RequireCanClose();
		});
		RequireFailure([&]() {
			fixture.Close->Close();
		});

		return sgre::script::SquirrelNativeResult{};
	});
	static_cast<void>(fixture.Vm.Invoke(guard, std::array<SquirrelValue, 1>{fixture.Vm.RootTable()}));
	fixture.Vm.ResetExecution();
	fixture.Vm.DispatchBorrowedReceiver(fixture.Vm.BorrowObjectIdentity(instance.Value()), [&](const auto&) {
		RequireFailure([&]() {
			fixture.Close->RequireCanClose();
		});
	});
	sgre::scene::ParallelTaskCallbacks callback;
	callback.Update = [&](auto) {
		RequireFailure([&]() {
			fixture.Close->Close();
		});
	};
	const auto task = fixture.Hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Append(std::move(callback));
	fixture.Hooks.Update(sgre::scene::LooperHookPhase::PostLoop);
	auto observer = fixture.Hooks.Group(sgre::scene::LooperHookPhase::PostLoop).Extract(task);
	observer.Release();
	const auto child = fixture.Tree.CreateFolder();
	fixture.Tree.VisitChildren({}, [&](auto) {
		RequireFailure([&]() {
			fixture.Close->Close();
		});
	});
	fixture.Tree.Release(child);
	bool foreign_rejected = false;
	std::thread foreign([&]() {
		try {
			fixture.Close->Close();
		} catch (const std::runtime_error&) {
			foreign_rejected = true;
		}
	});
	foreign.join();
	Require(foreign_rejected && fixture.Events.empty() && fixture.Close->Phase() == PreparedDriverClosePhase::Threads && fixture.Controller.Parallel().Count() == 7 && !fixture.Vm.IsClosed(), "active/borrowed/foreign owner admission must make no sibling or scene mutation");
	fixture.Close->Close();
}

static void TestNotificationFailurePreservesLaterOwners(const std::filesystem::path& directory) {
	PreparedCloseFixture fixture(directory);
	const auto listener = fixture.Vm.Retain(fixture.Listener());
	const auto instance = fixture.Vm.Retain(fixture.Threads.Spawn(fixture.DormantFunction()));
	const auto handle = fixture.Threads.ThreadHandle(instance.Value());
	bool fail = true;
	fixture.Objects.Subscribe(instance.Value(), listener.Value(), [&](const auto&) {
		if (fail) {
			throw std::runtime_error("notification sentinel");
		}

		return false;
	});
	RequireFailure([&]() {
		fixture.Close->Close();
	});
	Require(fixture.Close->Phase() == PreparedDriverClosePhase::Threads && fixture.Scheduler.Info(handle).Status == ScriptThreadStatus::LoadingFunction && !fixture.Vm.IsClosed() && fixture.Logs.IsInitialized() && !fixture.Manager.IsClosed() && !fixture.Meter.IsClosed() && fixture.Controller.Parallel().Count() == 7, "failed real notification must preserve later owner roots and reset only its operation scope");
	fail = false;
	fixture.Close->Close();
}

static void TestSupportedFailureCompletionKeepsFirstError(const std::filesystem::path& directory) {
	PreparedCloseFixture fixture(directory);
	fixture.MonitorVmClose(true);
	fixture.MonitorActualSceneAndSerial(true);
	fixture.FailFont = true;
	bool first_error = false;
	try {
		fixture.Close->Close();
	} catch (const std::runtime_error& error) {
		first_error = std::string_view(error.what()).find("VM release sentinel") != std::string_view::npos;
	}
	Require(first_error && fixture.Close->Phase() == PreparedDriverClosePhase::Closed && fixture.Events == std::vector<std::string>{"vm", "priority", "font", "font", "serial"}, "completed actual VM/font/Serial failures must retire all admitted structural owners and retain first error");
	Require(fixture.Manager.IsClosed() && fixture.Meter.IsClosed() && fixture.Hooks.CanClose() && fixture.Background.CanClose() && fixture.Controller.Serial().Count() == 0, "failure-completing close must not leave any actual worker/font/callback root");
	fixture.Close->Close();
}

static void ExitAfterExpectedDestruction() {
	std::_Exit(0);
}

static void ExitAfterIsolatedDestruction() {
	std::_Exit(86);
}

static void DestroyUnclosedPreparedOwner(const std::filesystem::path& directory, bool verify) {
	PreparedCloseFixture fixture(directory);
	Require(fixture.Close->Phase() == PreparedDriverClosePhase::Threads && fixture.Controller.Parallel().Count() == 7 && !fixture.Vm.IsClosed(), "intended destructor fixture must retain actual prepared scene and VM before hook is armed");
	std::set_terminate(verify ? ExitAfterExpectedDestruction : ExitAfterIsolatedDestruction);
	fixture.Close.reset();
}

int main(int argc, char** argv) {
	try {
		Require(argc >= 2 && argc <= 3, "usage: prepared_driver_close_tests <fixture-directory> [--destroy-unclosed|--verify-destroy-unclosed]");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		if (argc == 3) {
			const std::string_view mode(argv[2]);
			Require(mode == "--destroy-unclosed" || mode == "--verify-destroy-unclosed", "unknown prepared close verification mode");
			DestroyUnclosedPreparedOwner(directory, mode == "--verify-destroy-unclosed");

			return 1;
		}

		TestPendingBorrowBeforeSiblingMutation(directory);
		TestActualLoadedFileAndRetirementOrder(directory);
		TestLateOwnerRecheckAndResume(directory);
		TestActiveVmSceneMeterAndForeignGuards(directory);
		TestNotificationFailurePreservesLaterOwners(directory);
		TestSupportedFailureCompletionKeepsFirstError(directory);
		std::cout << "six prepared Driver close groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
