#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first, std::int32_t second = 0, std::uint8_t third = 0) {
	SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = first;
	instruction.Argument1 = second;
	instruction.Argument2 = third;

	return instruction;
}

static BytecodeProgram Program(std::string name, std::vector<SquirrelInstruction> instructions) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-nested-get-fixture");
	program.MainFunction.Name = std::move(name);
	program.MainFunction.Parameters = {std::string("this"), std::string("target")};
	program.MainFunction.StackSize = 4;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static void SetDelegate(SquirrelVm& vm, const SquirrelValue& table, const SquirrelValue& delegate) {
	auto program = Program("set-delegate", {Instruction(53, 3, 1, 2), Instruction(19, 255)});
	program.MainFunction.Parameters.push_back(std::string("parent"));
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), table, delegate};
	static_cast<void>(vm.InvokeWithoutResultInContext(vm.RootContext(), vm.CreateClosure(std::move(program)), arguments));
}

static SquirrelValue Read(SquirrelVm& vm, const SquirrelValue& receiver, bool default_key = false, bool host = false) {
	if (host) {
		return vm.ReadSlot(receiver, default_key ? SquirrelValue(std::string("weakref")) : SquirrelValue(std::int32_t{8}));
	}

	auto program = Program("read-main", {Instruction(9, 3, 0, 1), Instruction(19, 1, 3)});
	program.MainFunction.Literals.push_back(default_key ? sgre::script::SquirrelLiteral(std::string("weakref")) : sgre::script::SquirrelLiteral(std::int32_t{8}));
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), receiver};
	const auto result = vm.Invoke(vm.CreateClosure(std::move(program)), arguments);
	Require(result.State == sgre::script::SquirrelRunState::Completed && !vm.IsSuspended(), "nested generic raw suspension must not mark the calling script suspended");

	return result.Value;
}

static SquirrelNativeResult Status(SquirrelVm& vm, std::int32_t status, std::int32_t value) {
	if (status > 0 || status == -666) {
		vm.PushNativeValue(value);
	} else if (status < 0) {
		vm.SetLastError(std::int32_t{-41});
	}

	return SquirrelNativeResult::StackStatus(status);
}

static void TestNativeStatus(std::optional<std::int32_t> inner_status, std::int32_t outer_status, bool direct = false, bool default_key = false, bool host = false) {
	std::vector<std::int32_t> calls;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto first = vm.CreateTable();
	const auto second = vm.CreateTable();
	SetDelegate(vm, first, second);
	const std::array<std::uint8_t, 1> bytes{37};
	const auto receiver = vm.CreateUserData(bytes);
	vm.SetUserDataDelegate(receiver, first);
	if (inner_status) {
		vm.NewSlot(second, std::string("_get"), vm.CreateNativeClosure("inner-status", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
			Require(arguments.size() == 2 && arguments[0] == first && std::get<std::int32_t>(arguments[1]) == 8, "inner getter must receive the actual delegated table and key");
			calls.push_back(1);

			return Status(active, *inner_status, 77);
		}));
	}

	vm.NewSlot(first, std::string("_get"), vm.CreateNativeClosure("outer-status", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && arguments[0] == receiver && std::get<std::int32_t>(arguments[1]) == 8, "outer getter must receive the original userdata and key");
		calls.push_back(2);

		return Status(active, outer_status, 89);
	}));
	if (direct) {
		vm.NewSlot(first, std::int32_t{8}, std::int32_t{73});
	}

	const bool expected_failure = !direct && !default_key && inner_status == -1 && outer_status == -1;
	SquirrelValue value;
	bool failed = false;
	try {
		value = Read(vm, receiver, default_key, host);
	} catch (const sgre::script::SquirrelVmException& error) {
		Require(expected_failure && std::string(error.what()).find(host ? "the index doesn't exist" : "index '8'") != std::string::npos, "general bytecode failure must report the actual missing key");
		failed = true;
	}

	Require(failed == expected_failure, "general bytecode getter failed at the wrong boundary");
	if (default_key) {
		Require(value == vm.GetSlot(vm.CreateTable(), std::string("weakref")) && calls.empty(), "the real Table default must skip every nested getter");
	} else if (direct) {
		Require(std::get<std::int32_t>(value) == 73 && calls.empty(), "direct delegated slot must skip every nested getter");
	} else {
		const std::vector<std::int32_t> expected_calls = inner_status ? (inner_status == -1 ? std::vector<std::int32_t>{1, 2} : std::vector<std::int32_t>{1}) : std::vector<std::int32_t>{2};
		Require(calls == expected_calls, "native nested and outer getters ran out of order");
		if (!failed) {
			const auto chosen_status = inner_status && inner_status != -1 ? *inner_status : outer_status;
			if (chosen_status == 0) {
				Require(vm.Type(value) == SquirrelValueType::Null, "successful zero native result must stop with null");
			} else {
				Require(std::get<std::int32_t>(value) == (inner_status && inner_status != -1 ? 77 : 89), "native nested getter chose the wrong output");
			}
		}
	}

	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestScriptGetter(bool throws, bool host = false) {
	std::int32_t outer_calls = 0;
	std::int32_t error_calls = 0;
	std::int32_t script_steps = 0;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto handler = vm.CreateNativeClosure("get-error-handler", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		++error_calls;

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 2> handler_arguments{vm.RootTable(), handler};
	static_cast<void>(vm.InvokeWithoutResultInContext(vm.RootContext(), vm.GetSlot(vm.RootTable(), std::string("seterrorhandler")), handler_arguments));
	const auto first = vm.CreateTable();
	const auto second = vm.CreateTable();
	SetDelegate(vm, first, second);
	const std::array<std::uint8_t, 1> bytes{37};
	const auto receiver = vm.CreateUserData(bytes);
	vm.SetUserDataDelegate(receiver, first);
	auto script = Program("script-inner", {Instruction(2, 1, throws ? -41 : 77), throws ? Instruction(58, 1) : Instruction(19, 1, 1)});
	script.MainFunction.StackSize = 2;
	vm.NewSlot(second, std::string("_get"), vm.CreateClosure(std::move(script)));
	vm.NewSlot(first, std::string("_get"), vm.CreateNativeClosure("after-script", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments[0] == receiver && std::get<std::int32_t>(arguments[1]) == 8, "failed real script getter must resume the original userdata read");
		++outer_calls;

		return SquirrelNativeResult::ReturnValue(std::int32_t{89});
	}));
	vm.SetTraceCallback([&](const sgre::script::SquirrelTraceEvent& event) {
		if (event.Function == "script-inner") {
			++script_steps;
		}
	});
	const auto value = Read(vm, receiver, false, host);
	Require(std::get<std::int32_t>(value) == (throws ? 89 : 77) && outer_calls == (throws ? 1 : 0), "genuine script return or throw must control the outer getter continuation");
	Require(script_steps == 2 && error_calls == 0, "real getter bytecode must execute while recoverable inner throw suppresses the outer handler");
	if (throws) {
		Require(std::get<std::int32_t>(vm.LastError()) == -41, "successful outer getter must preserve the original inner thrown value");
	}

	vm.SetTraceCallback({});
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestDelegateRetirement(bool late_replace, bool host = false) {
	std::vector<std::int32_t> phases;
	SquirrelValue weak_first;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	SquirrelValue first = vm.CreateTable();
	const auto second = vm.CreateTable();
	const auto replacement = vm.CreateTable();
	SetDelegate(vm, first, second);
	weak_first = vm.CreateWeakReference(first);
	const std::array<std::uint8_t, 1> bytes{37};
	const auto receiver = vm.CreateUserData(bytes);
	vm.SetUserDataDelegate(receiver, first);
	vm.NewSlot(second, std::string("_get"), vm.CreateNativeClosure("mutating-inner", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(active.Type(arguments[0]) == SquirrelValueType::Table && arguments[0] == active.ResolveWeakReference(weak_first), "mutation must start inside the live delegated table getter");
		phases.push_back(1);
		active.SetUserDataDelegate(receiver, late_replace ? SquirrelValue{} : replacement);
		active.SetLastError(std::int32_t{-41});

		return SquirrelNativeResult::StackStatus(-1);
	}));
	vm.NewSlot(first, std::string("_get"), vm.CreateNativeClosure("obsolete-outer", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		throw std::runtime_error("obsolete outer getter must not be selected");

		return SquirrelNativeResult::NoValues();
	}));
	vm.NewSlot(replacement, std::string("_get"), vm.CreateNativeClosure("replacement-outer", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments[0] == receiver && active.Type(active.ResolveWeakReference(weak_first)) == SquirrelValueType::Null, "outer lookup must happen after the old delegated table expires");
		phases.push_back(3);

		return SquirrelNativeResult::ReturnValue(std::int32_t{97});
	}));
	if (late_replace) {
		const auto owned_value = vm.CreateUserData(bytes);
		vm.SetUserDataReleaseHook(owned_value, [&](SquirrelVm& active, const SquirrelValue&, std::span<std::uint8_t> payload) {
			Require(payload[0] == 37 && active.Type(active.ResolveWeakReference(weak_first)) == SquirrelValueType::Table, "old Table must remain weakly live while its owned value releases");
			bool active_frame = true;
			try {
				static_cast<void>(active.NativeStackSize());
			} catch (const std::runtime_error& error) {
				Require(std::string(error.what()).find("active native frame") != std::string::npos, "native frame observation failed for another reason");
				active_frame = false;
			}

			Require(!active_frame, "failed getter argument frame must retire before delegate-owned userdata releases");
			phases.push_back(2);
			active.SetUserDataDelegate(receiver, replacement);
		});
		vm.NewSlot(first, std::int32_t{7}, owned_value);
	}

	first = {};
	const auto value = Read(vm, receiver, false, host);
	vm.RethrowReleaseFailure();
	Require(std::get<std::int32_t>(value) == 97 && phases == (late_replace ? std::vector<std::int32_t>{1, 2, 3} : std::vector<std::int32_t>{1, 3}), "current delegate must be reread after original delegated-local retirement and callbacks");
	Require(std::get<std::int32_t>(vm.LastError()) == -41, "delegate mutation must not erase the inner failure value");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNativeHostOutputHasNoFacadeRoot() {
	std::int32_t releases = 0;
	SquirrelValue weak;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto methods = vm.CreateTable();
	const std::array<std::uint8_t, 1> bytes{37};
	const auto receiver = vm.CreateUserData(bytes);
	vm.SetUserDataDelegate(receiver, methods);
	vm.NewSlot(methods, std::string("_get"), vm.CreateNativeClosure("owned-host-output", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		const auto output = active.CreateUserData(bytes);
		weak = active.CreateWeakReference(output);
		active.SetUserDataReleaseHook(output, [&](SquirrelVm& releasing, const SquirrelValue&, std::span<std::uint8_t> payload) {
			Require(payload[0] == 37 && releasing.Type(releasing.ResolveWeakReference(weak)) == SquirrelValueType::UserData, "host output must release its initialized payload before weak expiry");
			++releases;
		});

		return SquirrelNativeResult::ReturnValue(output);
	}));
	SquirrelValue output = vm.ReadSlot(receiver, std::int32_t{8});
	Require(vm.Type(output) == SquirrelValueType::UserData && releases == 0, "host read must return the actual owned userdata");
	output = {};
	vm.RethrowReleaseFailure();
	Require(releases == 1 && vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null, "direct native getter output must not acquire an invented facade result root or native temporary");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestScriptHostTemporaryOwnership(bool throws) {
	SquirrelValue weak;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto first = vm.CreateTable();
	const auto second = vm.CreateTable();
	SetDelegate(vm, first, second);
	const std::array<std::uint8_t, 1> bytes{37};
	const auto receiver = vm.CreateUserData(bytes);
	vm.SetUserDataDelegate(receiver, first);
	auto program = Program("temporary-script-getter", {Instruction(2, 1, -41), Instruction(58, 1)});
	SquirrelValue output_owner;
	if (!throws) {
		output_owner = vm.CreateUserData(bytes);
		vm.NewSlot(first, std::int32_t{7}, output_owner);
		program.MainFunction.Literals.push_back(std::int32_t{7});
		program.MainFunction.Instructions = {Instruction(9, 1, 0, 0), Instruction(19, 1, 1)};
	}

	SquirrelValue script = vm.CreateClosure(std::move(program));
	weak = vm.CreateWeakReference(throws ? script : output_owner);
	vm.NewSlot(second, std::string("_get"), script);
	vm.NewSlot(first, std::string("_get"), vm.CreateNativeClosure("outer-temp-control", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::ReturnValue(std::int32_t{89});
	}));
	SquirrelValue output = vm.ReadSlot(receiver, std::int32_t{8});
	Require(throws ? std::get<std::int32_t>(output) == 89 : output == output_owner, "direct host script getter produced the wrong output");
	vm.SetSlot(second, std::string("_get"), {});
	if (!throws) {
		vm.SetSlot(first, std::int32_t{7}, {});
	}

	script = {};
	output_owner = {};
	output = {};
	Require(vm.Type(vm.ResolveWeakReference(weak)) == (throws ? SquirrelValueType::Closure : SquirrelValueType::UserData), "real script execution must retain its failed target or successful return in the native temporary independently of host output");
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	auto clear = Program("clear-native-temp", {Instruction(19, 255)});
	clear.MainFunction.Parameters.resize(1);
	static_cast<void>(vm.InvokeWithoutResultInContext(vm.RootContext(), vm.CreateClosure(std::move(clear)), arguments));
	Require(vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null, "subsequent real script return must retire the actual native temporary owner");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestHostGetterBudgetCleanup() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto first = vm.CreateTable();
	const auto second = vm.CreateTable();
	SetDelegate(vm, first, second);
	const std::array<std::uint8_t, 1> bytes{37};
	const auto receiver = vm.CreateUserData(bytes);
	vm.SetUserDataDelegate(receiver, first);
	auto loop = Program("bounded-script-getter", {Instruction(24, 0, -1)});
	vm.NewSlot(second, std::string("_get"), vm.CreateClosure(std::move(loop)));
	bool exhausted = false;
	try {
		static_cast<void>(vm.ReadSlot(receiver, std::int32_t{8}, 3));
	} catch (const std::runtime_error& error) {
		exhausted = std::string(error.what()).find("instruction budget exceeded") != std::string::npos;
	}

	Require(exhausted && vm.ContextState(vm.RootContext()) == sgre::script::SquirrelContextState::Idle && !vm.IsSuspended(), "bounded getter failure must restore host context, physical script frame and suspension state");
	vm.SetSlot(second, std::string("_get"), vm.CreateNativeClosure("after-budget", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::ReturnValue(std::int32_t{42});
	}));
	Require(std::get<std::int32_t>(vm.ReadSlot(receiver, std::int32_t{8})) == 42, "host getter safety failure must leave the original VM usable");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestDelegatedLocalsAreNotCollectorRoots() {
	std::vector<std::int32_t> phases;
	SquirrelValue weak_first;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	SquirrelValue first = vm.CreateTable();
	const auto second = vm.CreateTable();
	const auto third = vm.CreateTable();
	const auto replacement = vm.CreateTable();
	const auto replacement_root = vm.Retain(replacement);
	SetDelegate(vm, first, second);
	SetDelegate(vm, second, third);
	weak_first = vm.CreateWeakReference(first);
	const std::array<std::uint8_t, 1> bytes{37};
	const auto receiver = vm.CreateUserData(bytes);
	vm.SetUserDataDelegate(receiver, first);
	vm.NewSlot(first, std::string("_get"), vm.CreateNativeClosure("unrooted-old-outer", [](SquirrelVm&, std::span<const SquirrelValue>) {
		throw std::runtime_error("finalized delegated table must not keep its obsolete getter");

		return SquirrelNativeResult::NoValues();
	}));
	vm.NewSlot(replacement, std::string("_get"), vm.CreateNativeClosure("after-local-collection", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments[0] == receiver && active.Type(active.ResolveWeakReference(weak_first)) == SquirrelValueType::Null, "collected Get local must retire weak identity before outer lookup");
		phases.push_back(4);

		return SquirrelNativeResult::ReturnValue(std::int32_t{97});
	}));
	{
		const auto owned_value = vm.CreateUserData(bytes);
		vm.SetUserDataReleaseHook(owned_value, [&](SquirrelVm& active, const SquirrelValue&, std::span<std::uint8_t> payload) {
			Require(payload[0] == 37 && active.Type(active.ResolveWeakReference(weak_first)) == SquirrelValueType::Table && active.NativeStackSize() == 2, "collector must finalize the original strong Get local without making it an external root");
			phases.push_back(2);
			active.SetUserDataDelegate(receiver, replacement);
		});
		vm.NewSlot(first, std::int32_t{7}, owned_value);
	}

	vm.NewSlot(third, std::string("_get"), vm.CreateNativeClosure("collector-inner", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments[0] == second, "deep getter must receive the current delegated table instead of the saved parent local");
		phases.push_back(1);
		active.SetUserDataDelegate(receiver, {});
		Require(active.CollectGarbage() > 0, "detached strong C++ Get locals must remain eligible for genuine collection");
		active.RethrowReleaseFailure();
		Require(active.Type(active.ResolveWeakReference(weak_first)) == SquirrelValueType::Table, "collector finalization must leave the original Get local storage alive until its ordinary retirement");
		phases.push_back(3);
		active.SetLastError(std::int32_t{-41});

		return SquirrelNativeResult::StackStatus(-1);
	}));
	first = {};
	Require(std::get<std::int32_t>(Read(vm, receiver)) == 97 && phases == std::vector<std::int32_t>{1, 2, 3, 4}, "strong Get locals must not become collector roots or hide their native finalization order");
	vm.RethrowReleaseFailure();
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNonCallableInnerPreservesLastError(std::int32_t mode) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto first = vm.CreateTable();
	const auto second = vm.CreateTable();
	SetDelegate(vm, first, second);
	const std::array<std::uint8_t, 1> bytes{37};
	const auto receiver = vm.CreateUserData(bytes);
	vm.SetUserDataDelegate(receiver, first);
	SquirrelValue inner = std::int32_t{42};
	SquirrelValue first_weak;
	if (mode != 0) {
		SquirrelValue target = vm.CreateNativeClosure("noncalled-inner", [](SquirrelVm&, std::span<const SquirrelValue>) {
			throw std::runtime_error("noncallable resolved inner target must not execute");

			return SquirrelNativeResult::NoValues();
		});
		first_weak = vm.CreateWeakReference(target);
		inner = mode == 1 ? first_weak : vm.CreateWeakReference(first_weak);
	}

	vm.NewSlot(second, std::string("_get"), inner);
	vm.NewSlot(first, std::string("_get"), vm.CreateNativeClosure("preserved-error-outer", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		Require(std::holds_alternative<std::int32_t>(active.LastError()) && std::get<std::int32_t>(active.LastError()) == 23, "GenericCall must reject noncallable inner metadata without overwriting the previous error");

		return SquirrelNativeResult::ReturnValue(std::int32_t{89});
	}));
	vm.SetLastError(std::int32_t{23});
	Require(std::get<std::int32_t>(vm.ReadSlot(receiver, std::int32_t{8})) == 89, "noncallable inner metadata must continue to the current outer getter");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestGetterArgumentsHaveNoCallablePrefix() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto first = vm.CreateTable();
	const auto second = vm.CreateTable();
	SetDelegate(vm, first, second);
	const std::array<std::uint8_t, 1> bytes{37};
	const auto receiver = vm.CreateUserData(bytes);
	vm.SetUserDataDelegate(receiver, first);
	vm.NewSlot(second, std::string("_get"), vm.CreateNativeClosure("physical-meta-arguments", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments[0] == first && active.NativeAllocatedStackValue(-3) == receiver, "MetaCall must use its pushed receiver/key extent without an invented callable stack cell");

		return SquirrelNativeResult::ReturnValue(std::int32_t{77});
	}));
	const auto parent = vm.CreateNativeClosure("native-parent-read", [](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		const auto count = active.NativeStackSize();
		const auto output = active.ReadSlot(arguments[1], std::int32_t{8});
		Require(active.NativeStackSize() == count && active.NativeStackValue(2) == arguments[1], "native getter must restore the actual running parent argument frame");

		return SquirrelNativeResult::ReturnValue(output);
	});
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), receiver};
	Require(std::get<std::int32_t>(vm.Invoke(parent, arguments).Value) == 77, "parent native read must receive the original nested getter result");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestClassGetter(bool host, std::optional<std::int32_t> status) {
	std::int32_t constructor_calls = 0;
	std::int32_t releases = 0;
	SquirrelValue weak_output;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto first = vm.CreateTable();
	const auto second = vm.CreateTable();
	SetDelegate(vm, first, second);
	const std::array<std::uint8_t, 1> bytes{37};
	const auto receiver = vm.CreateUserData(bytes);
	vm.SetUserDataDelegate(receiver, first);
	const auto class_value = vm.CreateClass();
	if (status) {
		vm.NewSlot(class_value, std::string("constructor"), vm.CreateNativeClosure("get-class-constructor", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
			Require(active.IsInstanceOf(arguments[0], class_value) && std::get<std::int32_t>(arguments[1]) == 8, "Class getter must replace its physical receiver with the created instance before constructor invocation");
			++constructor_calls;
			if (*status > 0 || *status == -666) {
				const auto output = active.CreateUserData(bytes);
				weak_output = active.CreateWeakReference(output);
				active.SetUserDataReleaseHook(output, [&](SquirrelVm& releasing, const SquirrelValue&, std::span<std::uint8_t> payload) {
					Require(payload[0] == 37 && releasing.Type(releasing.ResolveWeakReference(weak_output)) == SquirrelValueType::UserData, "Class getter unused result must retire its initialized owned payload");
					++releases;
				});
				active.PushNativeValue(output);
			} else if (*status == -1) {
				active.SetLastError(std::int32_t{-41});
			}

			return SquirrelNativeResult::StackStatus(*status);
		}));
	}

	vm.NewSlot(second, std::string("_get"), class_value);
	bool failed = false;
	SquirrelValue output;
	try {
		output = Read(vm, receiver, false, host);
	} catch (const sgre::script::SquirrelVmException& error) {
		Require(status == -1 && std::string(error.what()).find(host ? "the index doesn't exist" : "index '8'") != std::string::npos, "failed Class constructor must reach the actual final lookup error boundary");
		failed = true;
	}

	Require(failed == (status == -1) && constructor_calls == (status ? 1 : 0), "Class getter must use the original constructor status");
	if (!failed) {
		Require(vm.IsInstanceOf(output, class_value) && !vm.IsSuspended(), "Class getter must preserve the real created instance while discarding raw native constructor output and suspension request");
	}

	const bool owns_unused_output = status && (*status > 0 || *status == -666);
	Require(releases == (owns_unused_output ? 1 : 0), "unused native constructor output must retire before Class getter returns");
	if (owns_unused_output) {
		Require(vm.Type(vm.ResolveWeakReference(weak_output)) == SquirrelValueType::Null, "unused Class getter constructor output must have no invented result or temporary root");
	}

	vm.RethrowReleaseFailure();
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestClassConstructorOutputRetiresWithMetaArguments(std::int32_t status) {
	std::vector<std::int32_t> phases;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto methods = vm.CreateTable();
	const std::array<std::uint8_t, 1> bytes{37};
	const auto receiver = vm.CreateUserData(bytes);
	vm.SetUserDataDelegate(receiver, methods);
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("constructor"), vm.CreateNativeClosure("class-output-phase", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		phases.push_back(1);
		const auto output = active.CreateUserData(bytes);
		active.SetUserDataReleaseHook(output, [&](SquirrelVm& releasing, const SquirrelValue&, std::span<std::uint8_t> payload) {
			Require(payload[0] == 37 && releasing.NativeStackSize() == 4 && std::get<std::int32_t>(releasing.NativeStackValue(-1)) == 8 && releasing.IsInstanceOf(releasing.NativeStackValue(-2), class_value), "unused constructor result must release after its native frame retires and before MetaCall pops its original argument extent");
			phases.push_back(2);
		});
		active.PushNativeValue(output);

		return SquirrelNativeResult::StackStatus(status);
	}));
	vm.NewSlot(methods, std::string("_get"), class_value);
	const auto parent = vm.CreateNativeClosure("constructor-phase-parent", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		const auto output = active.ReadSlot(arguments[1], std::int32_t{8});
		active.RethrowReleaseFailure();
		Require(active.NativeStackSize() == 2 && active.IsInstanceOf(output, class_value), "Class getter must restore its actual parent extent after unused-output release");
		phases.push_back(3);

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), receiver};
	static_cast<void>(vm.InvokeWithoutResultInContext(vm.RootContext(), parent, arguments));
	Require(phases == std::vector<std::int32_t>{1, 2, 3}, "Class getter constructor result must retire at the original GenericClass ownership boundary");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestScriptClassConstructorOutputAlias(bool host, bool throws) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto methods = vm.CreateTable();
	const std::array<std::uint8_t, 1> bytes{37};
	const auto receiver = vm.CreateUserData(bytes);
	vm.SetUserDataDelegate(receiver, methods);
	const auto class_value = vm.CreateClass();
	auto constructor = Program("script-class-constructor", {Instruction(2, 1, throws ? -41 : 77), throws ? Instruction(58, 1) : Instruction(19, 1, 1)});
	constructor.MainFunction.StackSize = 2;
	vm.NewSlot(class_value, std::string("constructor"), vm.CreateClosure(std::move(constructor)));
	vm.NewSlot(methods, std::string("_get"), class_value);
	bool failed = false;
	SquirrelValue output;
	try {
		output = Read(vm, receiver, false, host);
	} catch (const sgre::script::SquirrelVmException& error) {
		Require(throws && std::string(error.what()).find(host ? "the index doesn't exist" : "index '8'") != std::string::npos, "failed script Class constructor must preserve the native final lookup error boundary");
		failed = true;
	}

	Require(failed == throws, "Class getter must use the real script constructor return or throw status");
	if (!throws) {
		Require(host ? vm.IsInstanceOf(output, class_value) : std::holds_alternative<std::int32_t>(output) && std::get<std::int32_t>(output) == 77, "original script constructor temporary overwrites only an aliased bytecode Get output, while separate host output retains its created instance");
	}

	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const auto host : {false, true}) {
			for (const auto inner : {0, 1, 2, -1, -666}) {
				TestNativeStatus(inner, 1, false, false, host);
			}

			TestNativeStatus(-1, 0, false, false, host);
			TestNativeStatus(-1, -1, false, false, host);
			TestNativeStatus({}, 1, false, false, host);
			TestNativeStatus(1, 1, true, false, host);
			TestNativeStatus(1, 1, false, true, host);
			TestScriptGetter(false, host);
			TestScriptGetter(true, host);
			TestDelegateRetirement(false, host);
			TestDelegateRetirement(true, host);
		}

		TestNativeHostOutputHasNoFacadeRoot();
		TestScriptHostTemporaryOwnership(false);
		TestScriptHostTemporaryOwnership(true);
		TestHostGetterBudgetCleanup();
		TestDelegatedLocalsAreNotCollectorRoots();
		for (const auto mode : {0, 1, 2}) {
			TestNonCallableInnerPreservesLastError(mode);
		}

		TestGetterArgumentsHaveNoCallablePrefix();
		for (const auto host : {false, true}) {
			TestClassGetter(host, {});
			for (const auto status : {0, 1, -1, -666}) {
				TestClassGetter(host, status);
			}
		}

		TestClassConstructorOutputRetiresWithMetaArguments(1);
		TestClassConstructorOutputRetiresWithMetaArguments(-666);
		for (const auto host : {false, true}) {
			for (const auto throws : {false, true}) {
				TestScriptClassConstructorOutputAlias(host, throws);
			}
		}
		std::cout << "fifty-three private nested getter fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
