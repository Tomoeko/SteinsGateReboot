#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::script::BytecodeProgram ThrowInteger(std::int32_t number) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.Parameters.push_back(std::string("this"));
	program.MainFunction.StackSize = 2;
	program.MainFunction.SourceName = std::string("independent-error-fixture");
	program.MainFunction.Name = std::string("thrower");
	sgre::script::SquirrelInstruction load;
	load.Opcode = 2;
	load.Argument0 = 1;
	load.Argument1 = number;
	program.MainFunction.Instructions.push_back(load);
	sgre::script::SquirrelInstruction raise;
	raise.Opcode = 58;
	raise.Argument0 = 1;
	program.MainFunction.Instructions.push_back(raise);

	return program;
}

static void TestContextErrorsPersistIndependently() {
	SquirrelVm vm;
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	Require(std::holds_alternative<std::monostate>(vm.LastError()), "new VM last error must start null");
	try {
		static_cast<void>(vm.Invoke(vm.CreateClosure(ThrowInteger(41)), receiver));
	} catch (const sgre::script::SquirrelVmException& error) {
		Require(std::get<std::int32_t>(error.ThrownValue()) == 41, "script exception identity changed");
	}

	Require(std::get<std::int32_t>(vm.LastError()) == 41, "failed script call must retain its last error");
	const auto child = vm.CreateContext();
	Require(std::holds_alternative<std::monostate>(vm.LastErrorInContext(child)), "new child must not inherit its parent's last error");
	try {
		static_cast<void>(vm.InvokeInContext(child, vm.CreateClosure(ThrowInteger(73)), receiver));
	} catch (const sgre::script::SquirrelVmException&) {
	}

	const auto success = vm.CreateNativeClosure("successful_after_failure", [](SquirrelVm&, std::span<const SquirrelValue>) {
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{9};

		return result;
	});
	Require(std::get<std::int32_t>(vm.Invoke(success, receiver).Value) == 9 && std::get<std::int32_t>(vm.LastError()) == 41 && std::get<std::int32_t>(vm.LastErrorInContext(child)) == 73, "successful calls must preserve independent prior error values");
	vm.ReleaseContext(child);
}

static void TestNativeTypedFailureRetainsOwnedError() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto child = vm.CreateContext();
	const auto native = vm.CreateNativeClosure("typed_native_error", [](SquirrelVm&, std::span<const SquirrelValue> arguments) -> sgre::script::SquirrelNativeResult {
		throw sgre::script::SquirrelVmException(arguments[1], "independent typed native error");
	});
	const auto callback_root = vm.Retain(native);
	SquirrelValue cycle = vm.CreateArray();
	vm.AppendArray(cycle, cycle);
	const auto weak = vm.Retain(vm.CreateWeakReference(cycle));
	{
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), cycle};
		try {
			static_cast<void>(vm.InvokeInContext(child, native, arguments, 1000, false));
		} catch (const sgre::script::SquirrelVmException& error) {
			Require(error.ThrownValue() == cycle, "native call failure must retain the raw error object");
		}
	}

	cycle = {};
	Require(vm.CollectGarbage() == 0 && vm.ResolveWeakReference(weak.Value()) == vm.LastErrorInContext(child), "child last-error ownership must be a real collector root");
	vm.ReleaseContext(child);
	Require(vm.CollectGarbage() == 1 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak.Value())), "releasing the child must release its last-error root and permit the remaining cycle to finalize");
}

static void TestNativeFailureStatusAndSafetyErrors() {
	SquirrelVm vm;
	const auto native = vm.CreateNativeClosure("plain_native_error", [](SquirrelVm&, std::span<const SquirrelValue>) -> sgre::script::SquirrelNativeResult {
		throw std::runtime_error("independent native call failure");
	});
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	bool script_status = false;
	try {
		static_cast<void>(vm.Invoke(native, receiver));
	} catch (const sgre::script::SquirrelVmException& error) {
		script_status = StringText(error.ThrownValue()) == "independent native call failure";
	}

	Require(script_status && StringText(vm.LastError()) == "independent native call failure", "native API call failures must expose Squirrel status and last-error value");
	bool safety_status = false;
	try {
		static_cast<void>(vm.Invoke(vm.CreateClosure(ThrowInteger(1)), receiver, 0));
	} catch (const sgre::script::SquirrelVmException&) {
		throw std::runtime_error("instruction budget safety failure became an ignored script status");
	} catch (const std::runtime_error& error) {
		safety_status = std::string(error.what()) == "Squirrel instruction budget exceeded";
	}

	Require(safety_status, "authored execution-budget failures must remain explicit host diagnostics");
	const auto nested = vm.CreateNativeClosure("nested_safety_failure", [](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		static_cast<void>(current.InvokeInContext(current.CurrentContext(), current.CreateClosure(ThrowInteger(1)), arguments, 0));

		return sgre::script::SquirrelNativeResult{};
	});
	safety_status = false;
	try {
		static_cast<void>(vm.Invoke(nested, receiver));
	} catch (const sgre::script::SquirrelVmException&) {
		throw std::runtime_error("nested instruction budget safety failure became a native script status");
	} catch (const std::runtime_error& error) {
		safety_status = std::string(error.what()) == "Squirrel instruction budget exceeded";
	}

	Require(safety_status, "nested host safety failures must remain explicit through native call boundaries");
}

static void TestCloseClearsLastErrorBeforeHandlerOwner() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto class_value = vm.CreateClass();
	const auto class_root = vm.Retain(class_value);
	SquirrelValue first = vm.CreateInstance(class_value);
	SquirrelValue error_owner = vm.CreateInstance(class_value);
	SquirrelValue handler_owner = vm.CreateInstance(class_value);
	std::vector<std::int32_t> calls;
	vm.SetInstanceReleaseHook(first, [&](SquirrelVm&, const SquirrelValue&) {
		calls.push_back(1);
	});
	vm.SetInstanceReleaseHook(error_owner, [&](SquirrelVm& current, const SquirrelValue&) {
		Require(std::holds_alternative<std::monostate>(current.LastError()), "last-error cell must become null before old-error release callbacks");
		calls.push_back(2);
	});
	vm.SetInstanceReleaseHook(handler_owner, [&](SquirrelVm&, const SquirrelValue&) {
		calls.push_back(3);
	});
	vm.NewSlot(vm.RootTable(), std::string("root_owner"), first);
	first = {};
	auto capture = std::make_shared<sgre::script::SquirrelHostRoot>(vm.Retain(handler_owner));
	SquirrelValue handler = vm.CreateNativeClosure("owned_error_handler", [capture](SquirrelVm&, std::span<const SquirrelValue>) {
		return sgre::script::SquirrelNativeResult{};
	});
	{
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), handler};
		static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("seterrorhandler")), arguments));
	}

	capture.reset();
	handler = {};
	handler_owner = {};
	const auto native = vm.CreateNativeClosure("last_error_close_fixture", [](SquirrelVm&, std::span<const SquirrelValue> arguments) -> sgre::script::SquirrelNativeResult {
		throw sgre::script::SquirrelVmException(arguments[1], "owned last error");
	});
	{
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), error_owner};
		try {
			static_cast<void>(vm.InvokeInContext(vm.RootContext(), native, arguments, 1000, false));
		} catch (const sgre::script::SquirrelVmException&) {
		}
	}

	error_owner = {};
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(calls == std::vector<std::int32_t>{1, 2, 3} && std::holds_alternative<std::monostate>(vm.LastError()), "root, last-error and error-handler owner cleanup must retain native Close order");
}

static void TestUnhandledErrorRestoresHandlerSnapshot() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	std::size_t notifications = 0;
	const auto handler = vm.CreateNativeClosure("failing_error_handler", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) -> sgre::script::SquirrelNativeResult {
		Require(arguments.size() == 2 && std::get<std::int32_t>(arguments[1]) == 29, "error handler must receive the original error snapshot");
		++notifications;
		throw std::runtime_error("independent handler failure");
	});
	const std::array<SquirrelValue, 2> install{vm.RootTable(), handler};
	static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("seterrorhandler")), install));
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	try {
		static_cast<void>(vm.Invoke(vm.CreateClosure(ThrowInteger(29)), receiver));
	} catch (const sgre::script::SquirrelVmException& error) {
		Require(std::get<std::int32_t>(error.ThrownValue()) == 29, "failed error handler must not replace the unhandled thrown value");
	}

	Require(notifications == 1 && std::get<std::int32_t>(vm.LastError()) == 29, "unhandled exit must restore the original last-error snapshot after handler failure");
	const auto array = vm.CreateArray();
	try {
		static_cast<void>(vm.ReadSlot(array, std::int32_t{8}));
	} catch (const sgre::script::SquirrelVmException&) {
	}

	Require(StringText(vm.LastError()) == "the index doesn't exist", "native general-get failure must replace the prior context error with its fixed diagnostic");
}

int main() {
	try {
		TestUnhandledErrorRestoresHandlerSnapshot();
		TestContextErrorsPersistIndependently();
		TestNativeTypedFailureRetainsOwnedError();
		TestNativeFailureStatusAndSafetyErrors();
		TestCloseClearsLastErrorBeforeHandlerOwner();
		std::cout << "Squirrel error-state checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
