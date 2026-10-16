#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first, std::int32_t second = 0, std::uint8_t third = 0, std::uint8_t fourth = 0) {
	SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = first;
	instruction.Argument1 = second;
	instruction.Argument2 = third;
	instruction.Argument3 = fourth;

	return instruction;
}

static BytecodeProgram Program(std::vector<SquirrelInstruction> instructions) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-native-temporary-fixture");
	program.MainFunction.Name = std::string("main");
	program.MainFunction.Parameters = {std::string("this")};
	program.MainFunction.StackSize = 4;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static bool Alive(SquirrelVm& vm, const SquirrelValue& weak) {
	return vm.Type(vm.ResolveWeakReference(weak)) != sgre::script::SquirrelValueType::Null;
}

static void CallDirectNativeWithoutValues(SquirrelVm& vm) {
	const auto callback = vm.CreateNativeClosure("independent-direct-no-values", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::NoValues();
	});
	static_cast<void>(vm.Invoke(callback, {}));
}

static void ReturnNullFromScript(SquirrelVm& vm) {
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	static_cast<void>(vm.Invoke(vm.CreateClosure(Program({Instruction(19, 255)})), arguments));
}

static void TestFailedCallRetainsTargetAcrossDirectNativeCall(bool script_target, bool target_fails = true) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue weak;
	{
		SquirrelValue target;
		if (script_target) {
			target = vm.CreateClosure(target_fails ? Program({Instruction(2, 1, 91), Instruction(58, 1)}) : Program({Instruction(19, 255)}));
		} else {
			target = vm.CreateNativeClosure("independent-failure-then-throw", [](SquirrelVm&, std::span<const SquirrelValue>) {
				return SquirrelNativeResult::NoValues();
			});
		}

		weak = vm.CreateWeakReference(target);
		auto parent = Program({Instruction(10, 2, 0), Instruction(6, 3, 1, 2, 1), Instruction(2, 2, 91), Instruction(58, 2)});
		parent.MainFunction.Parameters.push_back(std::string("target"));
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), target};
		bool failed = false;
		try {
			static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(parent)), arguments));
		} catch (const sgre::script::SquirrelVmException& error) {
			failed = std::get<std::int32_t>(error.ThrownValue()) == 91;
		}

		Require(failed, "the independent target/parent failure must reach the actual original typed unwind boundary");
	}

	Require(Alive(vm, weak), "failed Execute must keep its last called target in the VM temporary after caller/argument retirement");
	Require(vm.CollectGarbage() == 0 && Alive(vm, weak), "the native temporary must be an actual collector root");
	CallDirectNativeWithoutValues(vm);
	Require(Alive(vm, weak), "direct GenericCall of a native closure must not replace the prior VM temporary");
	ReturnNullFromScript(vm);
	Require(!Alive(vm, weak), "a new script Execute and null return must retire the previous temporary target");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestSuccessfulReturnRetainsReturnedTargetAcrossDirectNativeCall() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue weak;
	{
		const auto target = vm.CreateNativeClosure("independent-returned-target", [](SquirrelVm&, std::span<const SquirrelValue>) {
			return SquirrelNativeResult::NoValues();
		});
		weak = vm.CreateWeakReference(target);
		auto parent = Program({Instruction(10, 2, 0), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 1)});
		parent.MainFunction.Parameters.push_back(std::string("target"));
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), target};
		Require(vm.Invoke(vm.CreateClosure(std::move(parent)), arguments).Value == target, "top-level LeaveFrame must return the same target identity");
	}

	CallDirectNativeWithoutValues(vm);
	Require(Alive(vm, weak), "retiring the facade result must not retire the separate native returned-value temporary");
	Require(vm.CollectGarbage() == 0 && Alive(vm, weak), "returned temporary identity must stay marked after facade output retirement");
	ReturnNullFromScript(vm);
	Require(!Alive(vm, weak), "later script temporary replacement must expire an otherwise unretained returned target");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestSuccessfulNullReturnRetiresCallTarget() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue weak;
	{
		const auto target = vm.CreateNativeClosure("independent-null-return-target", [](SquirrelVm&, std::span<const SquirrelValue>) {
			return SquirrelNativeResult::NoValues();
		});
		weak = vm.CreateWeakReference(target);
		auto parent = Program({Instruction(10, 2, 0), Instruction(6, 3, 1, 2, 1), Instruction(19, 255)});
		parent.MainFunction.Parameters.push_back(std::string("target"));
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), target};
		static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(parent)), arguments));
	}

	Require(!Alive(vm, weak), "successful null top-level return must replace its prior callable temporary rather than retain the last target");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestResetExecutionClearsOwnedTemporary() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue weak;
	{
		const auto target = vm.CreateNativeClosure("independent-reset-temporary-target", [](SquirrelVm&, std::span<const SquirrelValue>) {
			return SquirrelNativeResult::NoValues();
		});
		weak = vm.CreateWeakReference(target);
		auto program = Program({Instruction(19, 1, 1)});
		program.MainFunction.Parameters.push_back(std::string("target"));
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), target};
		static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(program)), arguments));
	}

	CallDirectNativeWithoutValues(vm);
	Require(Alive(vm, weak), "a direct native call must leave the separately owned returned temporary alive");
	vm.ResetExecution();
	Require(!Alive(vm, weak), "the authored ResetExecution control must retire execution-owned temporary and output state");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCloseDropsRootBeforeTemporary() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	std::vector<std::int32_t> released;
	SquirrelValue root_weak;
	SquirrelValue temporary_weak;
	{
		const auto class_value = vm.CreateClass();
		vm.NewSlot(vm.RootTable(), std::string("fixture-class"), class_value);
		const auto root_instance = vm.CreateInstance(class_value);
		root_weak = vm.CreateWeakReference(root_instance);
		vm.SetInstanceReleaseHook(root_instance, [&](SquirrelVm& current, const SquirrelValue&) {
			Require(Alive(current, temporary_weak), "root release must precede native temporary release during Close");
			released.push_back(0);
		});
		vm.NewSlot(vm.RootTable(), std::string("fixture-root-owner"), root_instance);
		const auto temporary_instance = vm.CreateInstance(class_value);
		temporary_weak = vm.CreateWeakReference(temporary_instance);
		vm.SetInstanceReleaseHook(temporary_instance, [&](SquirrelVm& current, const SquirrelValue&) {
			Require(!Alive(current, root_weak), "temporary release must observe the retired root owner during Close");
			released.push_back(1);
		});
		auto program = Program({Instruction(19, 1, 1)});
		program.MainFunction.Parameters.push_back(std::string("target"));
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), temporary_instance};
		static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(program)), arguments));
	}

	CallDirectNativeWithoutValues(vm);
	Require(Alive(vm, root_weak) && Alive(vm, temporary_weak), "root and VM temporary must independently retain their native owner before Close");
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(released == std::vector<std::int32_t>{0, 1}, "native context Close must release root ownership before its separate VM temporary");
}

static void TestInitialGeneratorOutputIsSeparateFromClosureTemporary() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue closure_weak;
	SquirrelValue generator_weak;
	{
		auto program = Program({Instruction(19, 255)});
		program.MainFunction.IsGenerator = true;
		const auto closure = vm.CreateClosure(std::move(program));
		closure_weak = vm.CreateWeakReference(closure);
		const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
		const auto generator = vm.Invoke(closure, arguments).Value;
		Require(vm.Type(generator) == sgre::script::SquirrelValueType::Generator, "initial generator invocation must return an actual saved Generator");
		generator_weak = vm.CreateWeakReference(generator);
	}

	CallDirectNativeWithoutValues(vm);
	Require(!Alive(vm, generator_weak), "retiring the facade output must retire an otherwise unretained initial Generator");
	Require(Alive(vm, closure_weak), "initial generator entry must leave its original script closure in the VM temporary after generator deletion");
	Require(vm.CollectGarbage() == 0 && Alive(vm, closure_weak), "the original initial-generator closure must remain marked independently of its returned Generator");
	ReturnNullFromScript(vm);
	Require(!Alive(vm, closure_weak), "later script temporary replacement must retire the original generator closure");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestFailedCallRetainsTargetAcrossDirectNativeCall(false);
		TestFailedCallRetainsTargetAcrossDirectNativeCall(true);
		TestFailedCallRetainsTargetAcrossDirectNativeCall(true, false);
		TestSuccessfulReturnRetainsReturnedTargetAcrossDirectNativeCall();
		TestSuccessfulNullReturnRetiresCallTarget();
		TestInitialGeneratorOutputIsSeparateFromClosureTemporary();
		TestResetExecutionClearsOwnedTemporary();
		TestCloseDropsRootBeforeTemporary();
		std::cout << "Squirrel native temporary ownership checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
