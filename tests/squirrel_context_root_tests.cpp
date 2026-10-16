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

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t argument1 = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = destination;
	instruction.Argument1 = argument1;
	instruction.Argument2 = argument2;
	instruction.Argument3 = argument3;

	return instruction;
}

static sgre::script::BytecodeProgram Program(std::vector<sgre::script::SquirrelInstruction> instructions) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.Parameters.push_back(std::string("this"));
	program.MainFunction.StackSize = 5;
	program.MainFunction.SourceName = std::string("independent-root-fixture");
	program.MainFunction.Name = std::string("root_fixture");
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static void SetDelegate(SquirrelVm& vm, const SquirrelValue& object, const SquirrelValue& delegate) {
	auto program = Program({Instruction(53, 2, 0, 1), Instruction(19, 1, 2)});
	program.MainFunction.Parameters.push_back(std::string("delegate"));
	const auto closure = vm.CreateClosure(std::move(program));
	const std::array<SquirrelValue, 2> arguments{object, delegate};
	static_cast<void>(vm.InvokeInContext(vm.CurrentContext(), closure, arguments));
}

static void TestIndependentRootSnapshots() {
	SquirrelVm vm;
	const auto original = vm.RootTable();
	const auto constants = vm.ConstantTable();
	const auto child = vm.CreateContext();
	const auto replacement = vm.CreateTable();
	vm.SetRootTable(replacement);
	Require(vm.RootTable() == replacement && vm.RootTableInContext(child) == original, "a child must own a creation-time root copy independent of later parent replacement");
	vm.NewSlot(original, std::string("shared_value"), std::int32_t{19});
	Require(std::get<std::int32_t>(vm.GetSlot(vm.RootTableInContext(child), std::string("shared_value"))) == 19 && vm.ConstantTable() == constants, "copied root identities must observe table mutations while constants remain separate shared storage");

	sgre::script::SquirrelContextHandle grandchild;
	sgre::script::SquirrelContextHandle explicit_base_child;
	const auto create_child = vm.CreateNativeClosure("create_root_snapshot", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		Require(current.RootTable() == original && current.CurrentContext() == child, "native calls must observe the executing context's root");
		grandchild = current.CreateContext();
		explicit_base_child = current.CreateContextFrom(current.RootContext());

		return sgre::script::SquirrelNativeResult{};
	});
	const std::array<SquirrelValue, 1> receiver{vm.CreateTable()};
	static_cast<void>(vm.InvokeInContext(child, create_child, receiver));
	Require(vm.RootTableInContext(explicit_base_child) == replacement, "an explicit creator must select its own root independently of the current child");
	Require(vm.RootTableInContext(grandchild) == original && vm.RootTable() == replacement, "context creation inside a child must copy the child root and restore the caller context");
	vm.SetRootTableInContext(grandchild, {});
	Require(std::holds_alternative<std::monostate>(vm.RootTableInContext(grandchild)) && vm.RootTableInContext(child) == original, "null root replacement must affect only the target context");
	vm.ReleaseContext(grandchild);
	vm.ReleaseContext(explicit_base_child);
	vm.ReleaseContext(child);
}

static void TestRootBaseCallbacksAndLoadOpcode() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto original = vm.RootTable();
	const auto get_root = vm.GetSlot(original, std::string("getroottable"));
	const auto set_root = vm.GetSlot(original, std::string("setroottable"));
	const auto load_root = vm.CreateClosure(Program({Instruction(21, 1), Instruction(19, 1, 1)}));
	const auto child = vm.CreateContext();
	const auto replacement = vm.CreateTable();
	const std::array<SquirrelValue, 2> arguments{original, replacement};
	Require(std::holds_alternative<std::monostate>(vm.InvokeInContext(child, set_root, arguments).Value), "native base root setter must return its cleared argument cell as null");
	const std::array<SquirrelValue, 1> receiver{original};
	Require(vm.InvokeInContext(child, get_root, receiver).Value == replacement && vm.InvokeInContext(child, load_root, receiver).Value == replacement && vm.Invoke(load_root, receiver).Value == original, "root getter and opcode must use execution root independently of receiver and closure creation");
	const std::array<SquirrelValue, 2> null_arguments{original, SquirrelValue{}};
	Require(std::holds_alternative<std::monostate>(vm.InvokeInContext(child, set_root, null_arguments).Value) && std::holds_alternative<std::monostate>(vm.InvokeInContext(child, load_root, receiver).Value), "null root must remain a valid root-load result");
	vm.ReleaseContext(child);
}

static void TestChildReinstallsRecoveredGlobals() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto original = vm.RootTable();
	const auto original_getter = vm.GetSlot(original, std::string("getroottable"));
	const auto string_length = vm.GetSlot(std::string("value"), std::string("len"));
	vm.SetSlot(original, std::string("getroottable"), std::int32_t{77});
	const auto child = vm.CreateContext();
	const auto renewed_getter = vm.GetSlot(original, std::string("getroottable"));
	Require(vm.Type(renewed_getter) == sgre::script::SquirrelValueType::NativeClosure && renewed_getter != original_getter && vm.GetSlot(std::string("value"), std::string("len")) == string_length, "child creation must reinstall fresh recovered globals in its copied root while retaining shared type delegates");

	const auto replacement = vm.CreateTable();
	vm.NewSlot(replacement, std::string("custom_marker"), std::int32_t{51});
	vm.SetRootTable(replacement);
	const auto second_child = vm.CreateContext();
	const std::array<SquirrelValue, 1> receiver{original};
	Require(vm.RootTableInContext(second_child) == replacement && vm.InvokeInContext(second_child, vm.GetSlot(replacement, std::string("getroottable")), receiver).Value == replacement && std::get<std::int32_t>(vm.GetSlot(replacement, std::string("custom_marker"))) == 51 && std::get<std::int32_t>(vm.GetSlot(replacement, std::string("_intsize_"))) == 4, "child registration must target the creator's replacement table and preserve its unrelated slots");
	vm.SetRootTable({});
	const auto null_child = vm.CreateContext();
	Require(std::holds_alternative<std::monostate>(vm.RootTableInContext(null_child)) && std::holds_alternative<std::monostate>(vm.LastErrorInContext(null_child)), "native base registration against a null root must leave it null without a failed newslot status");
	vm.ReleaseContext(null_child);
	vm.ReleaseContext(second_child);
	vm.ReleaseContext(child);
}

static void TestRegistrationSnapshotAndTemporaryStackRoot() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto root_snapshot = vm.CreateTable();
	const auto replacement = vm.CreateTable();
	const auto environment = vm.CreateTable();
	const auto snapshot_weak = vm.Retain(vm.CreateWeakReference(root_snapshot));
	const auto delegate = vm.CreateTable();
	std::size_t callbacks = 0;
	const auto method = vm.CreateNativeClosure("registration_root_mutation", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 3 && arguments[0] == environment && StringText(arguments[1]) == "seterrorhandler", "registration must follow native order and honor the metamethod's bound environment");
		current.SetRootTableInContext(current.RootContext(), replacement);
		current.SetRootTable(replacement);
		SetDelegate(current, root_snapshot, {});
		static_cast<void>(current.CollectGarbage());
		Require(current.ResolveWeakReference(snapshot_weak.Value()) == root_snapshot, "the registrar's actual pushed root snapshot must remain a collector root when boundenv replaces the metamethod receiver");
		++callbacks;

		return sgre::script::SquirrelNativeResult{};
	});
	vm.NewSlot(delegate, std::string("_newslot"), vm.BindEnvironment(method, environment));
	SetDelegate(vm, root_snapshot, delegate);
	vm.SetRootTable(root_snapshot);
	const auto child = vm.CreateContext();
	Require(callbacks == 1 && vm.RootTable() == replacement && vm.RootTableInContext(child) == replacement && !vm.HasSlot(replacement, std::string("getroottable")) && vm.HasSlot(root_snapshot, std::string("getroottable")) && std::get<std::int32_t>(vm.GetSlot(root_snapshot, std::string("_intsize_"))) == 4, "root replacement during registration must not redirect its remaining slots and removal of the delegate must resume native raw insertion");
	vm.ReleaseContext(child);
}

static void TestRegistrationNewSlotSuccessAndFailure() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto intercepted = vm.CreateTable();
	const auto delegate = vm.CreateTable();
	std::size_t callbacks = 0;
	const auto method = vm.CreateNativeClosure("registration_intercept", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 3 && arguments[0] == intercepted, "registration newslot receiver changed");
		++callbacks;

		return sgre::script::SquirrelNativeResult{};
	});
	vm.NewSlot(delegate, std::string("_newslot"), method);
	SetDelegate(vm, intercepted, delegate);
	vm.SetRootTable(intercepted);
	const auto child = vm.CreateContext();
	Require(callbacks == 19 && !vm.FindRawSlot(intercepted, std::string("getroottable")) && !vm.FindRawSlot(intercepted, std::string("_version_")) && !vm.FindRawSlot(intercepted, std::string("split")), "a successful newslot metamethod must consume only recovered base and version registrations while its delegate remains installed");
	vm.ReleaseContext(child);

	vm.SetSlot(delegate, std::string("_newslot"), vm.CreateNativeClosure("registration_failed_intercept", [&](SquirrelVm&, std::span<const SquirrelValue>) -> sgre::script::SquirrelNativeResult {
		++callbacks;
		throw sgre::script::SquirrelVmException(std::int32_t{73}, "independent registration hook failure");
	}));
	const auto second_child = vm.CreateContext();
	Require(callbacks == 38 && vm.HasSlot(intercepted, std::string("getroottable")) && std::get<std::int32_t>(vm.GetSlot(intercepted, std::string("_intsize_"))) == 4 && std::get<std::int32_t>(vm.LastErrorInContext(second_child)) == 73 && !vm.FindRawSlot(intercepted, std::string("split")), "failed table newslot hooks must retain their last error and fall back to raw base/version insertion without string-library reinstallation");
	vm.ReleaseContext(second_child);
}

static void TestInitialStringLibraryAndChildBasePhases() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto initial_root = vm.RootTable();
	const auto split = vm.GetSlot(initial_root, std::string("split"));
	const auto override_split = vm.CreateNativeClosure("overrideSplitMarker", [](SquirrelVm&, const auto) {
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{71});
	});
	vm.SetSlot(initial_root, std::string("split"), override_split);
	const auto inherited = vm.CreateContext();
	Require(vm.RootTableInContext(inherited) == initial_root && vm.GetSlot(initial_root, std::string("split")) == override_split, "child base setup must preserve an actual root's overridden string-library slot identity");
	const std::array<SquirrelValue, 1> receiver{initial_root};
	Require(std::get<std::int32_t>(vm.InvokeInContext(inherited, override_split, receiver).Value) == 71, "child initialization replaced the original overridden global callable");
	vm.ReleaseContext(inherited);

	const auto replacement = vm.CreateTable();
	vm.SetRootTable(replacement);
	const auto child = vm.CreateContext();
	Require(vm.RootTableInContext(child) == replacement && vm.HasSlot(replacement, std::string("getroottable")) && !vm.FindRawSlot(replacement, std::string("split")), "native child setup must register base globals on a replacement root without reinstalling startup string globals");
	const std::array<SquirrelValue, 3> arguments{replacement, std::string("a,b"), std::string(",")};
	const auto result = vm.InvokeInContext(child, split, arguments).Value;
	Require(vm.Length(result) == 2 && StringText(vm.GetSlot(result, std::int32_t{0})) == "a" && StringText(vm.GetSlot(result, std::int32_t{1})) == "b" && !vm.FindRawSlot(replacement, std::string("split")), "an existing string-library closure must execute in a child without publishing itself into that child's replacement root");
	vm.ReleaseContext(child);
}

static void TestRootLookupAndAssignment() {
	SquirrelVm vm;
	const auto root_a = vm.RootTable();
	vm.NewSlot(root_a, std::string("counter"), std::int32_t{10});
	const auto child = vm.CreateContext();
	const auto root_b = vm.CreateTable();
	vm.NewSlot(root_b, std::string("counter"), std::int32_t{20});
	vm.SetRootTable(root_b);
	const std::array<SquirrelValue, 1> receiver{vm.CreateTable()};
	auto read_program = Program({Instruction(9, 1, 0, 0), Instruction(19, 1, 1)});
	read_program.MainFunction.Literals.push_back(std::string("counter"));
	const auto read = vm.CreateClosure(std::move(read_program));
	Require(std::get<std::int32_t>(vm.Invoke(read, receiver).Value) == 20 && std::get<std::int32_t>(vm.InvokeInContext(child, read, receiver).Value) == 10, "implicit this fallback must use each executing context's root");

	auto assign_program = Program({Instruction(1, 1, 0), Instruction(2, 2, 31), Instruction(13, 3, 0, 1, 2), Instruction(19, 1, 3)});
	assign_program.MainFunction.Literals.push_back(std::string("counter"));
	const auto assign = vm.CreateClosure(std::move(assign_program));
	Require(std::get<std::int32_t>(vm.InvokeInContext(child, assign, receiver).Value) == 31 && std::get<std::int32_t>(vm.GetSlot(root_a, std::string("counter"))) == 31 && std::get<std::int32_t>(vm.GetSlot(root_b, std::string("counter"))) == 20, "implicit this assignment must mutate only the current root's slot");
	vm.ReleaseContext(child);
}

static void TestRootErrorHandlerAndReset() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto original = vm.RootTable();
	SquirrelValue observed_receiver;
	std::size_t notifications = 0;
	const auto handler = vm.CreateNativeClosure("observe_child_root", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && arguments[0] == current.RootTable() && std::get<std::int32_t>(arguments[1]) == 37, "error handler receiver must be the failing context's current root");
		observed_receiver = arguments[0];
		++notifications;

		return sgre::script::SquirrelNativeResult{};
	});
	const std::array<SquirrelValue, 2> install{original, handler};
	static_cast<void>(vm.Invoke(vm.GetSlot(original, std::string("seterrorhandler")), install));
	const auto child = vm.CreateContext();
	const auto replacement = vm.CreateTable();
	vm.SetRootTableInContext(child, replacement);
	const auto thrower = vm.CreateClosure(Program({Instruction(2, 1, 37), Instruction(58, 1)}));
	const std::array<SquirrelValue, 1> receiver{original};
	try {
		static_cast<void>(vm.InvokeInContext(child, thrower, receiver));
	} catch (const sgre::script::SquirrelVmException&) {
	}

	vm.ResetContext(child);
	Require(notifications == 1 && observed_receiver == replacement && vm.RootTableInContext(child) == replacement && std::get<std::int32_t>(vm.LastErrorInContext(child)) == 37, "execution reset must preserve the context root, handler and last-error state");
	for (const SquirrelValue& invalid : {SquirrelValue{std::int32_t{4}}, SquirrelValue{true}, SquirrelValue{1.0F}, vm.CreateArray()}) {
		try {
			vm.SetRootTableInContext(child, invalid);
			throw std::runtime_error("invalid root unexpectedly accepted");
		} catch (const sgre::script::SquirrelVmException& error) {
			Require(std::string(error.what()) == "ivalid type" && vm.RootTableInContext(child) == replacement && StringText(vm.LastErrorInContext(child)) == "ivalid type", "invalid root type must preserve the old root and retain native typed diagnostic");
		}
	}

	SquirrelVm foreign_vm;
	try {
		vm.SetRootTableInContext(child, foreign_vm.RootTable());
		throw std::runtime_error("foreign root unexpectedly accepted");
	} catch (const sgre::script::SquirrelVmException&) {
		throw;
	} catch (const std::runtime_error&) {
	}

	Require(vm.RootTableInContext(child) == replacement, "foreign-owner rejection must not replace the context root");
	vm.ReleaseContext(child);
}

static void TestSuspendedRootReplacement() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto original = vm.RootTable();
	auto program = Program({Instruction(6, 2, 1, 0, 1), Instruction(21, 2), Instruction(19, 1, 2)});
	program.MainFunction.Parameters.push_back(std::string("suspend_callback"));
	const auto callable = vm.CreateClosure(std::move(program));
	const auto child = vm.CreateContext();
	const std::array<SquirrelValue, 2> receiver{original, vm.GetSlot(original, std::string("suspend"))};
	Require(vm.InvokeInContext(child, callable, receiver).State == sgre::script::SquirrelRunState::Suspended, "independent root fixture must suspend before root load");
	const auto replacement = vm.CreateTable();
	vm.SetRootTableInContext(child, replacement);
	Require(vm.ResumeInContext(child).Value == replacement && vm.RootTable() == original, "resumed root load must see the child replacement and leave the base VM unchanged");
	vm.ReleaseContext(child);
}

static void TestChildRootOwnershipAndClose() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue original = vm.RootTable();
	vm.NewSlot(original, std::string("cycle"), original);
	const auto weak = vm.Retain(vm.CreateWeakReference(original));
	const auto child = vm.CreateContext();
	vm.SetRootTable(vm.CreateTable());
	original = {};
	Require(vm.CollectGarbage() == 0 && vm.ResolveWeakReference(weak.Value()) == vm.RootTableInContext(child), "child root storage must retain and mark its root graph");
	vm.ReleaseContext(child);
	Require(vm.CollectGarbage() == 1 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak.Value())), "retiring the child must permit its detached root cycle to finalize");

	const auto class_value = vm.CreateClass();
	const auto class_root = vm.Retain(class_value);
	const auto close_child = vm.CreateContext();
	const auto child_root = vm.CreateTable();
	vm.SetRootTableInContext(close_child, child_root);
	std::vector<std::int32_t> releases;
	for (std::int32_t index = 1; index <= 2; ++index) {
		SquirrelValue instance = vm.CreateInstance(class_value);
		vm.SetInstanceReleaseHook(instance, [&, index](SquirrelVm& current, const SquirrelValue&) {
			Require(std::holds_alternative<std::monostate>(current.RootTable()), "base root must be retired before either native owner cleanup");
			if (index == 2) {
				// Native shared-list finalization visits this newer instance before its older context.
				Require(current.RootTableInContext(close_child) == child_root, "the original mixed-list order must preserve this exact retained child root during its newer instance finalizer");
			}
			releases.push_back(index);
		});
		vm.NewSlot(index == 1 ? vm.RootTable() : child_root, std::string("owner"), instance);
	}

	vm.Close();
	vm.RethrowReleaseFailure();
	bool child_expired = false;
	try {
		static_cast<void>(vm.ContextState(close_child));
	} catch (const std::runtime_error&) {
		child_expired = true;
	}

	Require(releases == std::vector<std::int32_t>{1, 2} && std::holds_alternative<std::monostate>(vm.RootTable()) && child_expired, "native bootstrap-first and mixed-list finalization must preserve owner order and expire the actual child context at Close");
}

int main() {
	try {
		TestIndependentRootSnapshots();
		TestRootBaseCallbacksAndLoadOpcode();
		TestChildReinstallsRecoveredGlobals();
		TestRegistrationSnapshotAndTemporaryStackRoot();
		TestRegistrationNewSlotSuccessAndFailure();
		TestInitialStringLibraryAndChildBasePhases();
		TestRootLookupAndAssignment();
		TestRootErrorHandlerAndReset();
		TestSuspendedRootReplacement();
		TestChildRootOwnershipAndClose();
		std::cout << "Squirrel context-root checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
