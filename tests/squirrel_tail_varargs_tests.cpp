#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first, std::int32_t second = 0, std::uint8_t third = 0, std::uint8_t fourth = 0) {
	SquirrelInstruction result;
	result.Opcode = opcode;
	result.Argument0 = first;
	result.Argument1 = second;
	result.Argument2 = third;
	result.Argument3 = fourth;

	return result;
}

static sgre::script::SquirrelFunction Function(std::string name, std::uint32_t registers) {
	sgre::script::SquirrelFunction function;
	function.SourceName = std::string("independent-tail-variadic-fixture");
	function.Name = std::move(name);
	function.Parameters = {std::string("this")};
	function.StackSize = registers;

	return function;
}

static void TestTailRetirement(bool forwarded) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto root = vm.RootTable();
	const auto type = vm.CreateClass();
	vm.NewSlot(root, std::string("TailOwnerType"), type);
	std::vector<std::int32_t> releases;
	std::size_t observations = 0;
	vm.NewSlot(root, std::string("make_owner"), vm.CreateNativeClosure("make_actual_owner", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2, "owner factory requires its real receiver and ordinal");
		const auto ordinal = std::get<std::int32_t>(arguments[1]);
		const auto instance = current.CreateInstance(type);
		current.SetInstanceReleaseHook(instance, [&, ordinal](SquirrelVm&, const SquirrelValue&) {
			releases.push_back(ordinal);
		});

		return sgre::script::SquirrelNativeResult::ReturnValue(instance);
	}));
	vm.NewSlot(root, std::string("inspect_tail"), vm.CreateNativeClosure("inspect_actual_tail", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && std::get<std::int32_t>(arguments[1]) == (forwarded ? 2 : 0), "callee must own only its genuine newly bound variable arguments");
		Require(releases == (forwarded ? std::vector<std::int32_t>{1} : std::vector<std::int32_t>{1, 0}), "old variadic owners must retire in native reverse order before callee execution; forwarded owners remain live");
		static_cast<void>(current.CollectGarbage());
		++observations;

		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	}));

	auto leaf = Function("tail_leaf", 7);
	leaf.Literals = {std::string("inspect_tail")};
	leaf.HasVariableParameters = forwarded;
	if (!forwarded) {
		leaf.Parameters.push_back(std::string("value"));
	}
	leaf.Instructions = {Instruction(28, 2), Instruction(8, 3, 0, 0, 4), Instruction(10, 5, 2), Instruction(6, 6, 3, 4, 2)};
	if (forwarded) {
		leaf.Instructions.push_back(Instruction(2, 1, 0));
		leaf.Instructions.push_back(Instruction(29, 2, 1));
		leaf.Instructions.push_back(Instruction(19, 1, 2));
	} else {
		leaf.Instructions.push_back(Instruction(19, 1, 1));
	}

	auto worker = Function("variadic_worker", 8);
	worker.HasVariableParameters = true;
	worker.Functions.push_back(std::move(leaf));
	worker.Instructions = {Instruction(48, 1), Instruction(10, 2, 0)};
	if (forwarded) {
		worker.Instructions.push_back(Instruction(2, 4, 0));
		worker.Instructions.push_back(Instruction(29, 3, 4));
		worker.Instructions.push_back(Instruction(2, 4, 77));
	} else {
		worker.Instructions.push_back(Instruction(2, 3, 777));
	}
	worker.Instructions.push_back(Instruction(5, 5, 1, 2, forwarded ? 3 : 2));
	worker.Instructions.push_back(Instruction(19, 1, 5));

	sgre::script::BytecodeProgram program;
	program.MainFunction = Function("outer", 8);
	program.MainFunction.Literals = {std::string("make_owner")};
	program.MainFunction.Functions.push_back(std::move(worker));
	program.MainFunction.Instructions = {Instruction(48, 1), Instruction(8, 2, 0, 0, 3), Instruction(2, 4, 0), Instruction(6, 7, 2, 3, 2), Instruction(8, 2, 0, 0, 3), Instruction(2, 4, 1), Instruction(6, 6, 2, 3, 2), Instruction(10, 2, 0), Instruction(10, 3, 7), Instruction(10, 4, 6), Instruction(20, 6, 2), Instruction(6, 7, 1, 2, 3), Instruction(19, 1, 7)};
	const std::array<SquirrelValue, 1> arguments{root};
	const auto paused = vm.Invoke(vm.CreateClosure(std::move(program)), arguments);
	Require(paused.State == sgre::script::SquirrelRunState::Suspended, "the actual leaf callback must suspend at the serialized observation boundary");
	const auto snapshot = vm.ObserveContext(paused.Context, 8, 1);
	Require(snapshot.FrameCount == 2 && snapshot.Frames.size() == 2 && snapshot.Frames[0].Function == "outer" && snapshot.Frames[1].Function == "tail_leaf", "tail replacement must reuse the worker frame and preserve its real caller through suspension");
	const auto completed = vm.ResumeInContext(paused.Context, {});
	Require(completed.State == sgre::script::SquirrelRunState::Completed, "resuming the real tail leaf must complete its original caller");
	auto result = completed.Value;
	Require(observations == 1, "the real tail callee must execute exactly once");
	if (forwarded) {
		Require(vm.Type(result) == sgre::script::SquirrelValueType::Instance && releases == std::vector<std::int32_t>{1}, "forwarded variable argument must remain the actual owned return object");
	} else {
		Require(std::get<std::int32_t>(result) == 777 && releases == std::vector<std::int32_t>{1, 0}, "tail callee must return directly into the original caller destination");
	}
	result = {};
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestTailRetirement(false);
		TestTailRetirement(true);
		std::cout << "Actual variadic tail reuse, reverse release, forwarded ownership and collection PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
