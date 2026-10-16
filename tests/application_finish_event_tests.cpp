#include "application_finish_event_fixture.hpp"

#include "sgre/core/frame_clock.hpp"
#include "sgre/script/system_bindings.hpp"

#include <iostream>
#include <memory>
#include <span>

static void TestEventCallbackContextAndLifetime() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptApplicationFinishBindings finish(vm);
	sgre::core::ApplicationEventControl events;
	sgre::core::ApplicationEventCallbacks receiver(events, FinishOperations(events, finish));
	static_cast<void>(events.ExchangeCallback(sgre::core::ApplicationEventSlot::Close, &receiver));
	const auto root = vm.RootTable();
	const auto child = vm.CreateContext();
	const auto child_root = vm.CreateTable();
	vm.SetRootTableInContext(child, child_root);
	std::size_t callbacks = 0;
	auto marker = std::make_shared<int>(17);
	const auto weak = std::weak_ptr<int>(marker);
	auto callback = vm.CreateNativeClosure("actual finish decision", [&, retained = marker](sgre::script::SquirrelVm& current, std::span<const sgre::script::SquirrelValue> arguments) {
		RequireFinish(current.CurrentContext() == current.RootContext() && arguments.size() == 2 && arguments[0] == root && *retained == 17 && !weak.expired(), "close must invoke the retained callback in the genuine bootstrap context");
		RequireFinish(!receiver.CanClose() && !events.CanClose(), "close callback must retain actual receiver and event owners until return");
		++callbacks;

		return sgre::script::SquirrelNativeResult::ReturnValue(std::get<std::int32_t>(arguments[1]) == 2);
	});
	RegisterFinish(vm, callback);
	callback = {};
	marker.reset();
	RequireFinish(!weak.expired(), "registered finish must retain its closure capture after all ordinary copies retire");
	const auto request = vm.CreateNativeClosure("child close request", [&](auto&, auto) {
		events.RequestClose(1);
		events.Flush();
		RequireFinish(!events.State().CloseAccepted && !events.State().Quit && callbacks == 1, "actual finish veto must leave the event loop open");

		return sgre::script::SquirrelNativeResult::NoValues();
	});
	const std::array<sgre::script::SquirrelValue, 1> arguments{child_root};
	static_cast<void>(vm.InvokeInContext(child, request, arguments));
	RequireFinish(vm.CurrentContext() == vm.RootContext() && vm.ContextState(child) == sgre::script::SquirrelContextState::Idle, "nested finish evaluation must restore the invoking context");
	RequireFinishError([&] {
		static_cast<void>(finish.EvaluateCloseRequest(2, 0));
	},
	    "positive instruction budget");
	RequireFinish(callbacks == 1 && !events.State().CloseAccepted, "authored admission failure must not invoke or approve the finish callback");
	events.RequestClose(2);
	events.Flush();
	RequireFinish(events.State().CloseAccepted && !events.State().Quit && callbacks == 2, "callback approval must remain a close decision, not bypass the containing quit phase");
	RegisterFinish(vm, {});
	static_cast<void>(vm.CollectGarbage());
	RequireFinish(weak.expired(), "replacing finish must retire its previous capture without an extra event-owner pin");
	receiver.Close();
	vm.ReleaseContext(child);
	events.Close();
	vm.Close();
	std::cout << "actual events, bootstrap callback, veto/approve and receiver lifetime PASS\n";
}

static void TestMissingConfirmationAndFailures() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptApplicationFinishBindings finish(vm);
	sgre::core::ApplicationEventControl events;
	sgre::core::ApplicationEventCallbacks receiver(events, FinishOperations(events, finish));
	static_cast<void>(events.ExchangeCallback(sgre::core::ApplicationEventSlot::Close, &receiver));
	events.RequestClose(1);
	RequireFinishError([&] {
		events.Flush();
	},
	    "actual platform confirmation");
	RequireFinish(!events.State().CloseAccepted && !events.State().ClosePending && !events.State().Quit, "missing platform confirmation must veto before reporting its precise boundary");
	RegisterFinish(vm, vm.CreateNativeClosure("typed script callback failure", [](auto&, auto) -> sgre::script::SquirrelNativeResult {
		throw std::runtime_error("authored callback error");
	}));
	events.RequestClose(1);
	events.Flush();
	RequireFinish(events.State().CloseAccepted, "native typed callback failure retains its recovered default approval");
	events.ClearClose();
	events.RequestClose(0);
	RequireFinishError([&] {
		events.Flush();
	},
	    "actual Sound shutdown");
	RequireFinish(!events.State().CloseAccepted && !events.State().Quit, "unperformed reason0 Sound shutdown must not become approval");
	receiver.Close();
	events.Close();
	vm.Close();
	std::cout << "missing confirmation, native callback error and Sound boundary PASS\n";
}

static void TestSystemExitRequest() {
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	sgre::core::ApplicationEventControl events;
	const auto exit = vm.GetSlot(system.SystemClass(), std::string("exit"));
	const std::array<sgre::script::SquirrelValue, 2> arguments{system.SystemClass(), std::string("unused physical argument")};
	RequireFinishError([&] {
		static_cast<void>(vm.Invoke(exit, arguments));
	},
	    "actual containing application close owner");
	RequireFinish(!events.State().ClosePending, "unconfigured System.exit must not mutate another owner");
	system.ConfigureApplicationClose(events);
	RequireFinishError([&] {
		system.ConfigureApplicationClose(events);
	},
	    "one live containing event owner");
	const auto result = vm.Invoke(exit, arguments);
	RequireFinish(result.State == sgre::script::SquirrelRunState::Completed && std::holds_alternative<std::monostate>(result.Value) && events.State().ClosePending && events.State().CloseCode == 1 && !events.State().Quit, "System.exit must publish actual WM_CLOSE reason1 and no result, not terminate");
	const auto terminate = vm.GetSlot(system.SystemClass(), std::string("terminate"));
	RequireFinishError([&] {
		static_cast<void>(vm.Invoke(terminate, arguments));
	},
	    "original CRT process-exit boundary");
	RequireFinish(!events.State().Quit, "unsupported native CRT termination must not fabricate engine quit or cleanup");
	vm.Close();
	events.Close();
	std::cout << "System.exit actual close request and precise CRT termination boundary PASS\n";
}

static void TestOriginalFinish(const std::filesystem::path& filename) {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptApplicationFinishBindings finish(vm);
	sgre::core::ApplicationEventControl events;
	const auto previous = InstallOriginalFinish(vm, filename);
	sgre::core::ApplicationEventCallbacks receiver(events, FinishOperations(events, finish));
	static_cast<void>(events.ExchangeCallback(sgre::core::ApplicationEventSlot::Close, &receiver));
	for (const auto reason : {1u, 2u}) {
		events.RequestClose(reason);
		events.Flush();
		const auto continuation = vm.GetSlot(vm.RootTable(), std::string("sync"));
		RequireFinish(!events.State().CloseAccepted && !events.State().Quit && continuation != previous && vm.Type(continuation) == sgre::script::SquirrelValueType::Closure, "unchanged original finish callback must install its real confirmation continuation and veto direct close");
	}
	receiver.Close();
	events.Close();
	vm.Close();
	std::cout << "unchanged original finish callback reason1/2 continuation publication PASS; prepared null app, confirmation body unexecuted\n";
}

int main(int argc, char** argv) {
	try {
		RequireFinish(argc == 1 || argc == 2, "usage: application_finish_event_tests [original_override.sqbc]");
		TestEventCallbackContextAndLifetime();
		TestMissingConfirmationAndFailures();
		TestSystemExitRequest();
		if (argc == 2) {
			TestOriginalFinish(argv[1]);
		}

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
