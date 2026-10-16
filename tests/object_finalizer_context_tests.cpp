#include "sgre/script/object_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t target, std::int32_t first = 0, std::uint8_t second = 0, std::uint8_t third = 0) {
	SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = target;
	instruction.Argument1 = first;
	instruction.Argument2 = second;
	instruction.Argument3 = third;

	return instruction;
}

static SquirrelValue ScriptDestructor(SquirrelVm& vm) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-Object-finalizer-base-VM");
	program.MainFunction.Parameters = {std::string("this")};
	program.MainFunction.StackSize = 4;
	program.MainFunction.Literals = {std::string("observeFinalizer")};
	program.MainFunction.Instructions = {
	    Instruction(21, 1),
	    Instruction(9, 2, 0, 1),
	    Instruction(10, 3, 0),
	    Instruction(6, 255, 2, 3, 1),
	    Instruction(19, 255)};

	return vm.CreateClosure(std::move(program));
}

static SquirrelValue LookupAfterLastTemporaryRelease(SquirrelVm& vm) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-real-Object-index-release");
	program.MainFunction.Parameters = {std::string("this"), std::string("owned-array"), std::string("lookup-receiver")};
	program.MainFunction.StackSize = 6;
	program.MainFunction.Literals = {std::string("field")};
	program.MainFunction.Instructions = {
	    Instruction(2, 3, 0),
	    Instruction(14, 4, 1, 3),
	    Instruction(20, 4, 1),
	    Instruction(13, 5, 1, 3, 4),
	    Instruction(9, 4, 0, 2),
	    Instruction(19, 1, 4)};

	return vm.CreateClosure(std::move(program));
}

static void TestActualObjectReleaseOnBaseVm(bool instance_receiver, bool script_destructor, bool release_from_child) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	const auto base = vm.RootContext();
	const auto child = vm.CreateContext();
	const auto child_root = vm.CreateTable();
	vm.SetRootTableInContext(child, child_root);
	const auto context = release_from_child ? child : base;
	std::vector<std::int32_t> order;
	sgre::script::SquirrelObjectIdentity expected_receiver;
	std::size_t observations = 0;
	const auto observe = vm.CreateNativeClosure("independent-observe-actual-Object-finalizer", [&](SquirrelVm& active, const auto arguments) {
		Require(active.CurrentContext() == base && active.RootTable() == vm.RootTableInContext(base), "native225ff0/228e80 must dispatch on the fixed bootstrap VM even when a child released the last owner");
		Require(arguments.size() == 1 && active.BorrowObjectIdentity(arguments[0]) == expected_receiver && objects.HasInstance(arguments[0]), "actual destructor receives its still-live native Object before owner cleanup");
		order.push_back(1);
		++observations;

		return SquirrelNativeResult::NoValues();
	});
	vm.NewSlot(vm.RootTableInContext(base), std::string("observeFinalizer"), observe);
	vm.NewSlot(child_root, std::string("observeFinalizer"), vm.CreateNativeClosure("wrong-child-finalizer-root", [](auto&, auto) -> SquirrelNativeResult {
		throw std::runtime_error("Object finalizer incorrectly selected the releasing child root");
	}));
	const auto derived = vm.CreateClass(objects.ObjectClass());
	vm.NewSlot(derived, std::string("destructor"), script_destructor ? ScriptDestructor(vm) : observe);
	const auto target_class = vm.CreateClass();
	vm.NewSlot(target_class, std::string("field"), std::int32_t{41});
	const auto receiver = instance_receiver ? vm.CreateInstance(target_class) : target_class;
	SquirrelValue previous_weak;
	SquirrelValue owned_array;
	{
		const auto previous = vm.CreateInstance(derived);
		objects.AttachInstance(previous);
		const auto identity = vm.BorrowObjectIdentity(previous);
		previous_weak = vm.CreateWeakReference(previous);
		// This identity token cannot defer the genuine last-release callback.
		expected_receiver = identity;
		objects.ConfigureNativeRelease(previous, [&, identity](const auto& actual_receiver) {
			Require(vm.CurrentContext() == context && vm.BorrowObjectIdentity(actual_receiver) == identity && observations == 1, "script finalizer returns to the actual releasing context before native cleanup");
			order.push_back(2);
			objects.ReleaseInstance(actual_receiver);
		});
		owned_array = vm.CreateArray(std::array<SquirrelValue, 1>{previous});
	}
	const auto bytecode = LookupAfterLastTemporaryRelease(vm);
	const std::array<SquirrelValue, 3> arguments{vm.RootTableInContext(context), owned_array, receiver};
	const bool same_vm_unsafe_output = script_destructor && !release_from_child;
	try {
		const auto result = vm.InvokeInContext(context, bytecode, arguments);
		Require(!same_vm_unsafe_output && result.Value == SquirrelValue{std::int32_t{41}}, "a child encoded-index temporary must survive the actual base-VM destructor without restoration or invented return values");
	} catch (const std::runtime_error& error) {
		Require(same_vm_unsafe_output && std::string_view(error.what()) == "Squirrel class lookup temporary is not a bounded encoded integer", error.what());
	}
	vm.RethrowReleaseFailure();
	Require(observations == 1 && order == std::vector<std::int32_t>{1, 2} && vm.Type(vm.ResolveWeakReference(previous_weak)) == SquirrelValueType::Null, "real Object script/native retirement runs once before weak expiration");
	Require(vm.CurrentContext() == base && vm.RootTableInContext(child) == child_root, "nested finalizer restores caller context and preserves child root identity");
	vm.ReleaseContext(child);
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const bool instance : {false, true}) {
			TestActualObjectReleaseOnBaseVm(instance, false, true);
			TestActualObjectReleaseOnBaseVm(instance, true, true);
			TestActualObjectReleaseOnBaseVm(instance, true, false);
		}
		std::cout << "PASS real Object fixed-base-VM destructor, child Class/Instance lookup temporary preservation, native cleanup/weak lifetime and retained same-VM safety guard\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
