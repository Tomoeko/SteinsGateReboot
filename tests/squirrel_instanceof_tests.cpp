#include "sgre/script/squirrel_vm.hpp"

#include "fixtures/squirrel_instanceof_native.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t output, std::int32_t argument1, std::uint8_t argument2 = 0) {
	sgre::script::SquirrelInstruction result;
	result.Opcode = opcode;
	result.Argument0 = output;
	result.Argument1 = argument1;
	result.Argument2 = argument2;

	return result;
}

static sgre::script::BytecodeProgram ComparisonProgram(std::uint8_t destination) {
	sgre::script::BytecodeProgram program;
	auto& function = program.MainFunction;
	function.Name = std::string("original_instanceof_domain");
	function.SourceName = std::string("independent-instanceof-fixture.nut");
	function.Parameters = {std::string("this"), std::string("right"), std::string("left")};
	function.StackSize = 4;
	function.Instructions = {Instruction(42, destination, 1, 2), Instruction(19, 1, destination)};

	return program;
}

static void TestOriginalOperandAndAliasDomain() {
	for (const auto& original : kNativeInstanceOfCases) {
		sgre::script::SquirrelVm vm;
		const auto base = vm.CreateClass();
		const auto derived = vm.CreateClass(base);
		const auto unrelated = vm.CreateClass();
		const auto instance = vm.CreateInstance(derived);
		const auto base_instance = vm.CreateInstance(base);
		auto value_program = ComparisonProgram(3);
		value_program.MainFunction.Instructions = {Instruction(19, 255, 0)};
		const std::array<sgre::script::SquirrelValue, 10> values{{std::monostate{}, std::int32_t{37}, 1.5f, true, std::string("optional renderer"), vm.CreateTable(), vm.CreateArray(), vm.CreateClosure(std::move(value_program)), base, instance}};
		sgre::script::SquirrelValue right;
		sgre::script::SquirrelValue left;
		if (original.Case < 300) {
			right = values[original.Case / 30];
			left = values[original.Case / 3 % 10];
		} else {
			const std::array<sgre::script::SquirrelValue, 3> classes{{base, derived, unrelated}};
			const auto variant = (original.Case - 300) / 3;
			right = classes[static_cast<std::size_t>(variant) % classes.size()];
			left = variant < 3 ? instance : base_instance;
		}
		const auto actual_host = vm.IsInstanceOf(left, right);
		const auto closure = vm.CreateClosure(ComparisonProgram(original.Destination));
		const std::array<sgre::script::SquirrelValue, 3> arguments{{vm.RootTable(), right, left}};
		const auto actual_opcode = vm.Invoke(closure, arguments);
		const auto* result = std::get_if<bool>(&actual_opcode.Value);
		if (actual_host != original.Expected || actual_opcode.State != sgre::script::SquirrelRunState::Completed || result == nullptr || *result != original.Expected) {
			throw std::runtime_error("instanceof differs from original opcode/ancestry/result assignment at case " + std::to_string(original.Case));
		}
	}
}

int main() {
	try {
		TestOriginalOperandAndAliasDomain();
		std::cout << "original instanceof operand/ancestry/alias cases passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
