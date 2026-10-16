#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelContextHandle;
using sgre::script::SquirrelContextState;
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

static SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t argument1 = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
	SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = destination;
	instruction.Argument1 = argument1;
	instruction.Argument2 = argument2;
	instruction.Argument3 = argument3;

	return instruction;
}

static BytecodeProgram Program(std::vector<SquirrelInstruction> instructions) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-read-context-fixture");
	program.MainFunction.Name = std::string("read_context_fixture");
	program.MainFunction.Parameters = {std::string("this"), std::string("key")};
	program.MainFunction.StackSize = 4;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static SquirrelValue Target(SquirrelVm& vm, const SquirrelValue& method) {
	const std::array<std::uint8_t, 1> payload{37};
	const auto target = vm.CreateUserData(payload);
	const auto delegate = vm.CreateTable();
	vm.NewSlot(delegate, std::string("_get"), method);
	vm.SetUserDataDelegate(target, delegate);

	return target;
}

static void TestSelectedGetterRestoresCaller(bool root_caller) {
	SquirrelVm vm;
	const auto child = vm.CreateContext();
	const auto caller = root_caller ? vm.RootContext() : child;
	const auto selected = root_caller ? child : vm.RootContext();
	const auto selected_root = vm.CreateTable();
	vm.SetRootTableInContext(selected, selected_root);
	vm.SetLastErrorInContext(caller, std::int32_t{91});
	vm.SetLastErrorInContext(selected, std::int32_t{23});
	std::size_t getters = 0;
	const auto method = vm.CreateNativeClosure("selected-context-getter", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(active.CurrentContext() == selected && active.RootTable() == selected_root && active.ContextState(selected) == SquirrelContextState::Running, "selected getter must execute in the requested context and root");
		Require(arguments.size() == 2 && active.NativeStackSize() == 2 && std::get<std::int32_t>(arguments[1]) == 8, "selected native getter must have only its actual receiver/key argument frame");
		Require(active.ContextState(caller) == SquirrelContextState::Running && std::get<std::int32_t>(active.LastErrorInContext(caller)) == 91, "switching the getter must preserve the live caller and its error owner");
		active.SetLastError(std::int32_t{73});
		++getters;

		return SquirrelNativeResult::ReturnValue(std::int32_t{42});
	});
	const auto target = Target(vm, method);
	const auto invoke = vm.CreateNativeClosure("selected-context-caller", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		const auto caller_root = active.RootTable();
		Require(active.CurrentContext() == caller && arguments.size() == 2 && active.NativeStackSize() == 2, "caller must begin with its own native frame");
		const auto output = active.ReadSlotInContext(selected, target, std::int32_t{8});
		Require(std::get<std::int32_t>(output) == 42 && active.CurrentContext() == caller && active.RootTable() == caller_root && active.NativeStackSize() == 2, "selected getter must restore the caller's root and allocated frame before returning");
		Require(active.ContextState(selected) == SquirrelContextState::Idle && std::get<std::int32_t>(active.LastError()) == 91 && std::get<std::int32_t>(active.LastErrorInContext(selected)) == 73, "selected error changes must remain separate after getter cleanup");

		return SquirrelNativeResult::ReturnValue(output);
	});
	const std::array<SquirrelValue, 2> arguments{vm.RootTableInContext(caller), std::int32_t{9}};
	const auto result = vm.InvokeInContext(caller, invoke, arguments);
	Require(getters == 1 && std::get<std::int32_t>(result.Value) == 42 && vm.CurrentContext() == vm.RootContext(), "outer invocation must complete once and restore its host context");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestFailureTargetsSelectedError() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto child = vm.CreateContext();
	std::size_t handler_calls = 0;
	const auto handler = vm.CreateNativeClosure("unused-selected-error-handler", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		++handler_calls;

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 2> setup{vm.RootTable(), handler};
	static_cast<void>(vm.InvokeWithoutResultInContext(vm.RootContext(), vm.GetSlot(vm.RootTable(), std::string("seterrorhandler")), setup));
	vm.SetLastErrorInContext(child, std::int32_t{91});
	const auto method = vm.CreateClosure(Program({Instruction(2, 2, 77), Instruction(58, 2)}));
	const auto target = Target(vm, method);
	const auto invoke = vm.CreateNativeClosure("selected-get-failure-caller", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		bool failed = false;
		try {
			static_cast<void>(active.ReadSlotInContext(active.RootContext(), target, std::int32_t{8}));
		} catch (const sgre::script::SquirrelVmException& error) {
			failed = std::string(error.what()) == "the index doesn't exist";
		}

		Require(failed && active.CurrentContext() == child && std::get<std::int32_t>(active.LastError()) == 91, "failed selected general Get must restore caller and preserve its unrelated error");
		Require(sgre::script::StringText(active.LastErrorInContext(active.RootContext())) == "the index doesn't exist" && handler_calls == 0 && active.ContextState(active.RootContext()) == SquirrelContextState::Idle, "general Get must replace the selected lookup error without an invented error-handler call");

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTableInContext(child)};
	static_cast<void>(vm.InvokeInContext(child, invoke, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestSharedInstructionBudget() {
	SquirrelVm vm;
	const auto child = vm.CreateContext();
	vm.SetLastError(std::int32_t{23});
	vm.SetLastErrorInContext(child, std::int32_t{91});
	const auto target = Target(vm, vm.CreateClosure(Program({Instruction(24, 0, -1)})));
	const auto invoke = vm.CreateNativeClosure("shared-read-budget-caller", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		bool exhausted = false;
		try {
			static_cast<void>(active.ReadSlotInContext(active.RootContext(), target, std::int32_t{8}, 1));
		} catch (const std::runtime_error& error) {
			exhausted = std::string(error.what()).find("instruction budget exceeded") != std::string::npos;
		}

		Require(exhausted && active.CurrentContext() == child && active.NativeStackSize() == 1 && active.ContextState(active.RootContext()) == SquirrelContextState::Idle, "bounded selected script getter must restore both physical frames and caller after failure");
		Require(std::get<std::int32_t>(active.LastError()) == 91 && std::get<std::int32_t>(active.LastErrorInContext(active.RootContext())) == 23, "authored instruction safeguard must not replace either native error cell");

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTableInContext(child)};
	const auto result = vm.InvokeInContext(child, invoke, arguments, 4);
	Require(result.Instructions == 4, "nested selected Get must consume the caller's remaining budget instead of resetting it to its optional argument");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestForeignAndRetiredContexts() {
	SquirrelVm vm;
	SquirrelVm other;
	const auto child = vm.CreateContext();
	const auto retired = vm.CreateContext();
	vm.ReleaseContext(retired);
	const auto table = vm.CreateTable();
	vm.NewSlot(table, std::int32_t{8}, std::int32_t{42});
	vm.SetLastErrorInContext(child, std::int32_t{91});
	const auto invoke = vm.CreateNativeClosure("invalid-read-context-caller", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		for (const auto context : {other.RootContext(), retired}) {
			bool rejected = false;
			try {
				static_cast<void>(active.ReadSlotInContext(context, table, std::int32_t{8}));
			} catch (const std::runtime_error&) {
				rejected = true;
			}

			Require(rejected && active.CurrentContext() == child && std::get<std::int32_t>(active.LastError()) == 91 && active.NativeStackSize() == 1, "invalid target contexts must fail validation before changing the caller's identity or error owner");
		}

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTableInContext(child)};
	static_cast<void>(vm.InvokeInContext(child, invoke, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestReturnedOwnerRetiresAfterContextRestore() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto child = vm.CreateContext();
	std::size_t releases = 0;
	SquirrelValue output_weak;
	const auto method = vm.CreateNativeClosure("selected-return-owner", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		Require(active.CurrentContext() == active.RootContext(), "output must originate in selected root getter");
		const std::array<std::uint8_t, 1> bytes{37};
		const auto output = active.CreateUserData(bytes);
		output_weak = active.CreateWeakReference(output);
		active.SetUserDataReleaseHook(output, [&](SquirrelVm& current, const SquirrelValue&, std::span<const std::uint8_t> payload) {
			Require(current.CurrentContext() == child && current.NativeStackSize() == 1 && payload.size() == 1 && payload[0] == 37, "returned value retirement must occur in restored caller after selected native frame has ended");
			current.SetLastError(std::int32_t{97});
			++releases;
		});

		return SquirrelNativeResult::ReturnValue(output);
	});
	const auto target = Target(vm, method);
	const auto invoke = vm.CreateNativeClosure("selected-return-owner-caller", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		{
			const auto output = active.ReadSlotInContext(active.RootContext(), target, std::int32_t{8});
			Require(active.Type(output) == SquirrelValueType::UserData && releases == 0, "general getter output must survive through its actual returned owner");
		}

		active.RethrowReleaseFailure();
		Require(releases == 1 && active.Type(active.ResolveWeakReference(output_weak)) == SquirrelValueType::Null && std::get<std::int32_t>(active.LastError()) == 97, "no facade result or selected temporary may retain a native getter's returned payload");

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTableInContext(child)};
	static_cast<void>(vm.InvokeInContext(child, invoke, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCurrentReadUsesActiveContext() {
	SquirrelVm vm;
	const auto child = vm.CreateContext();
	const auto target = Target(vm, vm.CreateNativeClosure("current-read-getter", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		Require(active.CurrentContext() == child && active.NativeStackSize() == 2, "ordinary ReadSlot must keep the executing context as its default");
		active.SetLastError(std::int32_t{73});

		return SquirrelNativeResult::ReturnValue(std::int32_t{42});
	}));
	const auto invoke = vm.CreateNativeClosure("current-read-caller", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		Require(std::get<std::int32_t>(active.ReadSlot(target, std::int32_t{8})) == 42 && active.CurrentContext() == child && active.NativeStackSize() == 1, "same-context nested Get must restore the existing native caller frame");

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTableInContext(child)};
	static_cast<void>(vm.InvokeInContext(child, invoke, arguments));
	Require(std::get<std::int32_t>(vm.LastErrorInContext(child)) == 73 && vm.Type(vm.LastError()) == SquirrelValueType::Null, "ordinary ReadSlot must target only active context diagnostics");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestDirectMembersAndNoRootRetry() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto child = vm.CreateContext();
	vm.SetLastErrorInContext(child, std::int32_t{91});
	const auto table = vm.CreateTable();
	const auto class_value = vm.CreateClass();
	vm.NewSlot(table, std::int32_t{8}, std::int32_t{42});
	vm.NewSlot(class_value, std::int32_t{8}, std::int32_t{73});
	const auto instance = vm.CreateInstance(class_value);
	vm.NewSlot(vm.RootTable(), std::string("root-only-key"), std::int32_t{89});
	const auto invoke = vm.CreateNativeClosure("selected-direct-lookup-caller", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		Require(std::get<std::int32_t>(active.ReadSlotInContext(active.RootContext(), table, std::int32_t{8})) == 42 && std::get<std::int32_t>(active.ReadSlotInContext(active.RootContext(), instance, std::int32_t{8})) == 73, "selected general Get must preserve direct Table and copied Instance fields");
		for (const auto& target : {SquirrelValue(table), SquirrelValue(class_value)}) {
			bool failed = false;
			try {
				static_cast<void>(active.ReadSlotInContext(active.RootContext(), target, target == table ? std::string("root-only-key") : std::string("weakref")));
			} catch (const sgre::script::SquirrelVmException& error) {
				failed = std::string(error.what()) == "the index doesn't exist";
			}

			Require(failed && active.CurrentContext() == child && std::get<std::int32_t>(active.LastError()) == 91 && sgre::script::StringText(active.LastErrorInContext(active.RootContext())) == "the index doesn't exist", "selected host Get must exclude implicit root retry and Class default delegate while preserving caller error");
		}

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTableInContext(child)};
	static_cast<void>(vm.InvokeInContext(child, invoke, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestSelectedErrorReleaseContext() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto child = vm.CreateContext();
	vm.SetLastErrorInContext(child, std::int32_t{91});
	std::size_t releases = 0;
	const std::array<std::uint8_t, 1> payload{37};
	SquirrelValue old_error = vm.CreateUserData(payload);
	vm.SetUserDataReleaseHook(old_error, [&](SquirrelVm& active, const SquirrelValue&, std::span<const std::uint8_t>) {
		Require(active.CurrentContext() == active.RootContext() && sgre::script::StringText(active.LastError()) == "the index doesn't exist" && active.ContextState(child) == SquirrelContextState::Running, "failed general Get must assign selected error before releasing old error in its actual selected VM scope");
		active.SetLastError(std::int32_t{97});
		++releases;
	});
	vm.SetLastError(old_error);
	old_error = {};
	const auto table = vm.CreateTable();
	const auto invoke = vm.CreateNativeClosure("selected-error-release-caller", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		bool failed = false;
		try {
			static_cast<void>(active.ReadSlotInContext(active.RootContext(), table, std::int32_t{8}));
		} catch (const sgre::script::SquirrelVmException& error) {
			failed = std::string(error.what()) == "the index doesn't exist";
		}

		active.RethrowReleaseFailure();
		Require(failed && releases == 1 && active.CurrentContext() == child && std::get<std::int32_t>(active.LastError()) == 91 && std::get<std::int32_t>(active.LastErrorInContext(active.RootContext())) == 97, "context restoration must preserve reentrant selected error replacement and caller ownership");

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTableInContext(child)};
	static_cast<void>(vm.InvokeInContext(child, invoke, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestSuspendedTargetPreservesContinuation() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto child = vm.CreateContext();
	auto program = Program({Instruction(10, 3, 0), Instruction(6, 2, 1, 3, 1), Instruction(19, 1, 2)});
	const auto suspend = vm.GetSlot(vm.RootTable(), std::string("suspend"));
	const std::array<SquirrelValue, 2> arguments{vm.RootTableInContext(child), suspend};
	const auto result = vm.InvokeInContext(child, vm.CreateClosure(std::move(program)), arguments);
	Require(result.State == sgre::script::SquirrelRunState::Suspended && vm.ContextState(child) == SquirrelContextState::Suspended, "fixture must suspend a genuine child script before the attempted getter");
	const auto target = Target(vm, vm.CreateNativeClosure("unused-suspended-getter", [](SquirrelVm&, std::span<const SquirrelValue>) {
		throw std::runtime_error("suspended getter must not run");

		return SquirrelNativeResult::NoValues();
	}));
	bool rejected = false;
	try {
		static_cast<void>(vm.ReadSlotInContext(child, target, std::int32_t{8}));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && vm.CurrentContext() == vm.RootContext() && vm.ContextState(child) == SquirrelContextState::Suspended, "unsupported getter execution in suspended target must preserve its saved action and caller context");
	const auto resumed = vm.ResumeInContext(child, std::int32_t{53});
	Require(resumed.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(resumed.Value) == 53, "getter rejection must not consume the existing genuine suspension continuation");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestSelectedGetterRestoresCaller(false);
		TestSelectedGetterRestoresCaller(true);
		TestFailureTargetsSelectedError();
		TestSharedInstructionBudget();
		TestForeignAndRetiredContexts();
		TestReturnedOwnerRetiresAfterContextRestore();
		TestCurrentReadUsesActiveContext();
		TestSuspendedTargetPreservesContinuation();
		TestDirectMembersAndNoRootRetry();
		TestSelectedErrorReleaseContext();
		std::cout << "ten independent context-aware general Get fixture groups pass\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
