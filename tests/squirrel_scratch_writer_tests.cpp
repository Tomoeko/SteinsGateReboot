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
using sgre::script::SquirrelValueType;
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

static BytecodeProgram Program(std::vector<SquirrelInstruction> instructions, bool target_argument = true) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-primary-scratch-writer-fixture");
	program.MainFunction.Name = std::string("main");
	program.MainFunction.Parameters = {std::string("this")};
	if (target_argument) {
		program.MainFunction.Parameters.push_back(std::string("target"));
	}

	program.MainFunction.StackSize = 4;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static bool Alive(SquirrelVm& vm, const SquirrelValue& weak) {
	return vm.Type(vm.ResolveWeakReference(weak)) != SquirrelValueType::Null;
}

static void InvokeWithTypedFailure(SquirrelVm& vm, const SquirrelValue& closure, std::span<const SquirrelValue> arguments) {
	bool failed = false;
	try {
		static_cast<void>(vm.Invoke(closure, arguments));
	} catch (const sgre::script::SquirrelVmException& error) {
		failed = std::get<std::int32_t>(error.ThrownValue()) == 91;
	}

	Require(failed, "the independent primary operation must reach its following typed failure");
}

static void RetireTemporaryByScript(SquirrelVm& vm) {
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	static_cast<void>(vm.Invoke(vm.CreateClosure(Program({Instruction(19, 255)}, false)), arguments));
}

static void DirectNativeCall(SquirrelVm& vm) {
	const auto direct = vm.CreateNativeClosure("independent-direct-output-clear", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::NoValues();
	});
	static_cast<void>(vm.Invoke(direct, {}));
}

static SquirrelInstruction ReadInstruction(std::uint8_t opcode) {
	if (opcode == 7) {
		return Instruction(7, 3, 2, 1, 2);
	}

	if (opcode == 8) {
		return Instruction(8, 3, 0, 1, 2);
	}

	if (opcode == 9) {
		return Instruction(9, 3, 0, 1);
	}

	return Instruction(14, 3, 1, 2);
}

static void TestReadTemporarySurvivesContainerRetirement(std::uint8_t opcode, bool numeric_overwrite) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue weak;
	{
		const auto value = vm.CreateString("independent-owned-primary-read-result");
		weak = vm.CreateWeakReference(value);
		const std::array<SquirrelValue, 1> elements{value};
		const auto array = vm.CreateArray(elements);
		std::vector<SquirrelInstruction> instructions{Instruction(2, 2, 0), ReadInstruction(opcode)};
		if (numeric_overwrite) {
			instructions.push_back(Instruction(2, 2, 7));
			instructions.push_back(Instruction(2, 3, 9));
			instructions.push_back(Instruction(17, 2, 3, 2, '+'));
		} else {
			instructions.push_back(Instruction(2, 3, 7));
			instructions.push_back(Instruction(45, 3, 3));
		}

		instructions.push_back(Instruction(2, 3, 91));
		instructions.push_back(Instruction(58, 3));
		auto program = Program(std::move(instructions));
		program.MainFunction.Literals.push_back(std::int32_t{0});
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), array};
		InvokeWithTypedFailure(vm, vm.CreateClosure(std::move(program)), arguments);
	}

	Require(Alive(vm, weak) != numeric_overwrite, "GET/PREPCALL must retain its fetched value, numeric arithmetic must replace it, and scalar negation must preserve it");
	DirectNativeCall(vm);
	Require(Alive(vm, weak) != numeric_overwrite, "direct native calls must preserve primary bytecode temporary ownership");
	RetireTemporaryByScript(vm);
	Require(!Alive(vm, weak), "later script null return must retire the separately retained primary read value");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNativeMetamethodTemporary(std::uint8_t opcode) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue weak;
	{
		const auto class_value = vm.CreateClass();
		const auto method = vm.CreateNativeClosure("independent-primary-meta-result", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
			const auto result = current.CreateString("independent-owned-native-metamethod-result");
			weak = current.CreateWeakReference(result);

			return SquirrelNativeResult::ReturnValue(result);
		});
		const auto name = opcode == 17 ? "_add" : (opcode == 45 ? "_unm" : "_get");
		vm.NewSlot(class_value, std::string(name), method);
		const auto instance = vm.CreateInstance(class_value);
		SquirrelInstruction operation;
		if (opcode == 17) {
			operation = Instruction(17, 3, 2, 1, '+');
		} else if (opcode == 45) {
			operation = Instruction(45, 3, 1);
		} else {
			operation = ReadInstruction(opcode);
		}

		auto program = Program({Instruction(2, 2, 0), operation, Instruction(2, 3, 91), Instruction(58, 3)});
		program.MainFunction.Literals.push_back(std::int32_t{0});
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), instance};
		InvokeWithTypedFailure(vm, vm.CreateClosure(std::move(program)), arguments);
	}

	Require(Alive(vm, weak), "a successful native GET/arithmetic/negation metamethod must write its actual returned value into the primary VM temporary");
	Require(vm.CollectGarbage() == 0 && Alive(vm, weak), "the primary metamethod temporary must survive its original instance/class owners and remain traced");
	DirectNativeCall(vm);
	Require(Alive(vm, weak), "direct native invocation must preserve a prior primary metamethod result");
	RetireTemporaryByScript(vm);
	Require(!Alive(vm, weak), "later script null return must retire the native metamethod temporary");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestScriptGetterTemporary(std::uint8_t opcode) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue weak;
	{
		const auto key = vm.CreateString("independent-owned-script-getter-key");
		weak = vm.CreateWeakReference(key);
		const auto class_value = vm.CreateClass();
		auto getter = Program({Instruction(19, 1, 1)});
		vm.NewSlot(class_value, std::string("_get"), vm.CreateClosure(std::move(getter)));
		const auto instance = vm.CreateInstance(class_value);
		auto program = Program({Instruction(1, 2, 0), ReadInstruction(opcode), Instruction(2, 3, 91), Instruction(58, 3)});
		program.MainFunction.Literals.push_back(std::string("independent-owned-script-getter-key"));
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), instance};
		InvokeWithTypedFailure(vm, vm.CreateClosure(std::move(program)), arguments);
	}

	Require(Alive(vm, weak), "script getter Execute-root return must preserve the exact output temporary after caller/prototype retirement");
	RetireTemporaryByScript(vm);
	Require(!Alive(vm, weak), "later script temporary replacement must retire the script getter's returned identity");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNewMemberTemporary(bool script_method) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue weak;
	{
		const auto class_value = vm.CreateClass();
		const auto key = vm.CreateString("independent-owned-newmember-key");
		if (script_method) {
			weak = vm.CreateWeakReference(key);
			auto method = Program({Instruction(19, 1, 1)});
			method.MainFunction.Parameters.push_back(std::string("value"));
			method.MainFunction.Parameters.push_back(std::string("attributes"));
			vm.NewSlot(class_value, std::string("_newmember"), vm.CreateClosure(std::move(method)));
		} else {
			const auto method = vm.CreateNativeClosure("independent-newmember-output", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
				Require(arguments.size() == 4, "Class newmember must pass receiver, key, value and attributes");
				const auto result = current.CreateString("independent-owned-newmember-output");
				weak = current.CreateWeakReference(result);

				return SquirrelNativeResult::ReturnValue(result);
			});
			vm.NewSlot(class_value, std::string("_newmember"), method);
		}

		auto program = Program({Instruction(1, 2, 0), Instruction(2, 3, 7), Instruction(60, 0, 1, 2, 3), Instruction(2, 3, 91), Instruction(58, 3)});
		program.MainFunction.Literals.push_back(std::string("independent-owned-newmember-key"));
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), class_value};
		InvokeWithTypedFailure(vm, vm.CreateClosure(std::move(program)), arguments);
		Require(!vm.HasSlot(class_value, key), "successful Class newmember must consume insertion while its return value stays in the native temporary");
	}

	Require(Alive(vm, weak), "Class newmember must retain its ignored native/script hook return in the VM temporary");
	RetireTemporaryByScript(vm);
	Require(!Alive(vm, weak), "actual temporary replacement must release the ignored newmember result");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const auto opcode : std::array<std::uint8_t, 4>{7, 8, 9, 14}) {
			TestReadTemporarySurvivesContainerRetirement(opcode, false);
			TestReadTemporarySurvivesContainerRetirement(opcode, true);
			TestNativeMetamethodTemporary(opcode);
			TestScriptGetterTemporary(opcode);
		}

		TestNativeMetamethodTemporary(17);
		TestNativeMetamethodTemporary(45);
		TestNewMemberTemporary(false);
		TestNewMemberTemporary(true);
		std::cout << "Squirrel primary temporary writer checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
