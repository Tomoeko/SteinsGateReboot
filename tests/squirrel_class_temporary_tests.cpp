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

static BytecodeProgram Program(std::vector<SquirrelInstruction> instructions, bool target_argument = false) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-Class-temporary-fixture");
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

static void DirectNativeCall(SquirrelVm& vm) {
	const auto callback = vm.CreateNativeClosure("independent-Class-facade-output-clear", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::NoValues();
	});
	static_cast<void>(vm.Invoke(callback, {}));
}

static void ReturnNullFromScript(SquirrelVm& vm) {
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	static_cast<void>(vm.Invoke(vm.CreateClosure(Program({Instruction(19, 255)})), arguments));
}

enum class ConstructorKind {
	Absent,
	Null,
	NativeZero,
	NativeValue,
	ScriptNull,
	ScriptValue,
	ScriptFailure
};

static void TestClassTemporary(ConstructorKind kind, bool bytecode_call) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue prior_weak;
	{
		const auto prior = vm.CreateString("independent-prior-Class-call-temporary");
		prior_weak = vm.CreateWeakReference(prior);
		auto seed = Program({Instruction(1, 1, 0), Instruction(19, 1, 1)});
		seed.MainFunction.Literals.push_back(std::string("independent-prior-Class-call-temporary"));
		const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
		static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(seed)), arguments));
	}

	SquirrelValue class_weak;
	SquirrelValue constructor_weak;
	SquirrelValue result_weak;
	bool constructor_called = false;
	{
		const auto class_value = vm.CreateClass();
		class_weak = vm.CreateWeakReference(class_value);
		if (kind == ConstructorKind::Null) {
			vm.NewSlot(class_value, std::string("constructor"), SquirrelValue{});
		} else if (kind != ConstructorKind::Absent) {
			SquirrelValue constructor;
			if (kind == ConstructorKind::NativeZero || kind == ConstructorKind::NativeValue) {
				constructor = vm.CreateNativeClosure("independent-Class-native-constructor", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
					Require(arguments.size() == 1 && current.Type(arguments[0]) == SquirrelValueType::Instance, "the actual native Class constructor must receive its created instance");
					constructor_called = true;
					if (kind == ConstructorKind::NativeZero) {
						return SquirrelNativeResult::NoValues();
					}

					const auto result = current.CreateString("independent-native-constructor-output");
					result_weak = current.CreateWeakReference(result);

					return SquirrelNativeResult::ReturnValue(result);
				});
			} else {
				auto program = kind == ConstructorKind::ScriptFailure ? Program({Instruction(2, 1, 91), Instruction(58, 1)}) : Program({Instruction(19, 255)});
				if (kind == ConstructorKind::ScriptValue) {
					program = Program({Instruction(1, 1, 0), Instruction(19, 1, 1)});
					program.MainFunction.Literals.push_back(std::string("independent-script-constructor-output"));
				}

				constructor = vm.CreateClosure(std::move(program));
				if (kind == ConstructorKind::ScriptValue) {
					const auto result = vm.CreateString("independent-script-constructor-output");
					result_weak = vm.CreateWeakReference(result);
				}
			}

			constructor_weak = vm.CreateWeakReference(constructor);
			vm.NewSlot(class_value, std::string("constructor"), constructor);
		}

		bool failed = false;
		try {
			if (bytecode_call) {
				auto parent = Program({Instruction(10, 2, 0), Instruction(6, 3, 1, 2, 1), Instruction(2, 3, 91), Instruction(58, 3)}, true);
				const std::array<SquirrelValue, 2> arguments{vm.RootTable(), class_value};
				static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(parent)), arguments));
			} else {
				const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
				const auto result = vm.Invoke(class_value, arguments);
				Require(vm.IsInstanceOf(result.Value, class_value), "successful generic Class calls must return their created instance separately from ignored constructor output");
			}
		} catch (const sgre::script::SquirrelVmException& error) {
			failed = std::get<std::int32_t>(error.ThrownValue()) == 91;
		}

		Require(failed == (bytecode_call || kind == ConstructorKind::ScriptFailure), "Class constructor/caller typed failure status must preserve the original native boundary");
		if (kind == ConstructorKind::NativeZero || kind == ConstructorKind::NativeValue) {
			Require(constructor_called, "the Class method lookup must actually invoke the registered native constructor");
		}
	}

	DirectNativeCall(vm);
	Require(!Alive(vm, class_weak), "Class temporary lookup must not keep the original Class alive after instance/output/caller retirement");
	const bool script_kind = kind == ConstructorKind::ScriptNull || kind == ConstructorKind::ScriptValue || kind == ConstructorKind::ScriptFailure;
	Require(Alive(vm, prior_weak) == (!bytecode_call && !script_kind), "absent/native generic construction must preserve the prior temporary, while bytecode and generic script construction replace it");
	if (kind != ConstructorKind::Absent && kind != ConstructorKind::Null) {
		Require(Alive(vm, constructor_weak) == (bytecode_call || kind == ConstructorKind::ScriptFailure), "bytecode Class lookup must retain its constructor, while generic script execution retains its return value on success");
	}

	if (kind == ConstructorKind::ScriptValue) {
		Require(Alive(vm, result_weak), "bytecode constructor ownership or generic returned temporary must retain the original script constructor literal");
	}

	ReturnNullFromScript(vm);
	Require(!Alive(vm, prior_weak), "actual script temporary replacement must retire any preserved prior Class-call temporary");
	if (kind != ConstructorKind::Absent && kind != ConstructorKind::Null) {
		Require(!Alive(vm, constructor_weak), "actual temporary replacement must retire an otherwise unretained Class constructor");
	}

	if (kind == ConstructorKind::ScriptValue) {
		Require(!Alive(vm, result_weak), "retiring the Class constructor or returned temporary must retire its remaining script literal");
	}

	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const bool bytecode_call : std::array<bool, 2>{false, true}) {
			for (const auto kind : std::array<ConstructorKind, 7>{ConstructorKind::Absent, ConstructorKind::Null, ConstructorKind::NativeZero, ConstructorKind::NativeValue, ConstructorKind::ScriptNull, ConstructorKind::ScriptValue, ConstructorKind::ScriptFailure}) {
				TestClassTemporary(kind, bytecode_call);
			}
		}

		std::cout << "Squirrel Class temporary checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
