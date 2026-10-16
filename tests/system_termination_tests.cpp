#include "sgre/script/system_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected process termination boundary rejection");
}

static void InvokeTerminate(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& system) {
	const auto method = vm.GetSlot(system, std::string("terminate"));
	const std::array<sgre::script::SquirrelValue, 2> arguments{system, std::int32_t{97}};
	static_cast<void>(vm.Invoke(method, arguments));
}

static void TestConfigurationAndReturnedService() {
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	RequireError([&] {
		InvokeTerminate(vm, system.SystemClass());
	},
	    "no platform process termination service");
	RequireError([&] {
		system.ConfigureProcessTermination({});
	},
	    "both actual services");
	bool live = true;
	bool fail_preflight = true;
	std::size_t checks = 0;
	std::size_t calls = 0;
	sgre::script::ScriptProcessTerminationServices services;
	services.RequireLive = [&] {
		++checks;
		if (!live || fail_preflight) {
			throw std::runtime_error("actual termination owner is unavailable");
		}
	};
	services.TerminateProcess = [&] {
		++calls;
	};
	RequireError([&] {
		system.ConfigureProcessTermination(services);
	},
	    "actual termination owner");
	Require(checks == 1 && calls == 0, "failed preflight must not run or publish the termination service");
	fail_preflight = false;
	system.ConfigureProcessTermination(services);
	Require(checks == 2 && calls == 0, "successful configuration must preflight without terminating");
	RequireError([&] {
		system.ConfigureProcessTermination(services);
	},
	    "one live owner");
	Require(checks == 2, "duplicate configuration must fail before invoking a new owner's preflight");
	live = false;
	RequireError([&] {
		InvokeTerminate(vm, system.SystemClass());
	},
	    "actual termination owner");
	Require(checks == 3 && calls == 0, "retired containing owner must fail before termination");
	live = true;
	for (std::size_t index = 0; index < 2; ++index) {
		RequireError([&] {
			InvokeTerminate(vm, system.SystemClass());
		},
		    "service returned from its nonreturning exit(0) boundary");
	}
	Require(checks == 5 && calls == 2, "returned service must report failure and restore reentry guard for the next genuine attempt");
	vm.Close();
	std::cout << "unconfigured/incomplete/failed/duplicate/stale/returned process boundaries PASS\n";
}

static void TestReentryAndOwnerThread() {
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	std::size_t reentry_rejections = 0;
	std::size_t terminating = 0;
	sgre::script::ScriptProcessTerminationServices services;
	services.RequireLive = [&] {
		RequireError([&] {
			system.ConfigureProcessTermination(services);
		},
		    "cannot reenter");
		++reentry_rejections;
	};
	services.TerminateProcess = [&] {
		++terminating;
		RequireError([&] {
			InvokeTerminate(vm, system.SystemClass());
		},
		    "cannot reenter");
		++reentry_rejections;
	};
	std::exception_ptr worker_failure;
	std::thread worker([&] {
		try {
			RequireError([&] {
				system.ConfigureProcessTermination(services);
			},
			    "live serialized binding owner");
		} catch (...) {
			worker_failure = std::current_exception();
		}
	});
	worker.join();
	if (worker_failure) {
		std::rethrow_exception(worker_failure);
	}
	Require(reentry_rejections == 0 && terminating == 0, "foreign thread must not touch preflight or termination callbacks");
	system.ConfigureProcessTermination(services);
	Require(reentry_rejections == 1 && terminating == 0, "configuration preflight recursion must reject before service publication");
	RequireError([&] {
		InvokeTerminate(vm, system.SystemClass());
	},
	    "service returned");
	Require(reentry_rejections == 3 && terminating == 1, "recursive invocation and configuration must not reenter the actual process callback");
	vm.Close();
	std::cout << "actual owner-thread and recursive preflight/invocation guards PASS\n";
}

static void TestBorrowedOwnerAndBindingRetirement() {
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	auto bindings = std::make_unique<sgre::script::ScriptSystemBindings>(vm, clock);
	const auto system = bindings->SystemClass();
	auto owner = std::make_shared<int>(7);
	const auto weak = std::weak_ptr<int>(owner);
	std::size_t calls = 0;
	sgre::script::ScriptProcessTerminationServices services;
	services.RequireLive = [weak] {
		const auto actual = weak.lock();
		Require(actual && *actual == 7, "borrowed actual termination owner expired");
	};
	services.TerminateProcess = [&] {
		++calls;
	};
	bindings->ConfigureProcessTermination(std::move(services));
	owner.reset();
	Require(weak.expired(), "termination configuration must not secretly retain its borrowed containing owner");
	RequireError([&] {
		InvokeTerminate(vm, system);
	},
	    "borrowed actual termination owner expired");
	Require(calls == 0, "expired source owner must prevent the termination callback");
	bindings.reset();
	RequireError([&] {
		InvokeTerminate(vm, system);
	},
	    "native System bindings have expired");
	Require(calls == 0, "retained script method must not resurrect a retired binding or touch stale services");
	vm.Close();
	std::cout << "borrowed process owner and retained-method binding retirement PASS\n";
}

static void TestPreflightRetiringVm() {
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	std::size_t calls = 0;
	sgre::script::ScriptProcessTerminationServices services;
	services.RequireLive = [&] {
		vm.Close();
	};
	services.TerminateProcess = [&] {
		++calls;
	};
	RequireError([&] {
		system.ConfigureProcessTermination(std::move(services));
	},
	    "preflight retired its actual VM");
	Require(vm.IsClosed() && calls == 0, "preflight VM retirement must reject before publishing or invoking the process callback");
	std::cout << "preflight VM retirement preserves borrowed owner safety PASS\n";
}

struct ExitStackSentinel {
	~ExitStackSentinel() {
		static_cast<void>(std::fputs("stack-destructor-sentinel\n", stdout));
	}
};

static void RunConfiguredExitChild() {
	Require(std::setvbuf(stdout, nullptr, _IOFBF, 4096) == 0, "owned child could not establish a buffered stdio witness");
	ExitStackSentinel sentinel;
	sgre::script::SquirrelVm vm;
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	sgre::script::ScriptProcessTerminationServices services;
	const auto owner_thread = std::this_thread::get_id();
	services.RequireLive = [&vm, owner_thread] {
		Require(std::this_thread::get_id() == owner_thread && !vm.IsClosed(), "owned exit child lost its actual serialized VM owner");
	};
	services.TerminateProcess = [] {
		std::exit(0);
	};
	system.ConfigureProcessTermination(std::move(services));
	Require(std::fputs("configured-exit-before\n", stdout) >= 0, "owned child stdio write failed");
	InvokeTerminate(vm, system.SystemClass());
	static_cast<void>(std::fputs("post-terminate-sentinel\n", stdout));
	throw std::runtime_error("configured exit(0) returned into its script caller");
}

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--exit-child") {
			RunConfiguredExitChild();

			return 91;
		}
		Require(argc == 1, "usage: system_termination_tests [--exit-child]");
		TestConfigurationAndReturnedService();
		TestReentryAndOwnerThread();
		TestBorrowedOwnerAndBindingRetirement();
		TestPreflightRetiringVm();

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
