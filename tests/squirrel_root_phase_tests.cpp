#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void StoreLastError(SquirrelVm& vm, const SquirrelValue& value) {
	const auto raise = vm.CreateNativeClosure("independent-typed-error", [](SquirrelVm&, std::span<const SquirrelValue> arguments) -> sgre::script::SquirrelNativeResult {
		throw sgre::script::SquirrelVmException(arguments[1], "independent typed error");
	});
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), value};
	try {
		static_cast<void>(vm.InvokeInContext(vm.CurrentContext(), raise, arguments, 1000, false));
	} catch (const sgre::script::SquirrelVmException&) {
	}

	Require(vm.LastError() == value, "typed native failure must install the exact last-error value before root traversal");
}

static void TestGlobalRootVmPrecedesCallerAndSharedConstantRoots() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::vector<std::int32_t> callbacks;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	vm.NewSlot(vm.RootTable(), std::string("callback_class"), class_value);
	const auto child = vm.CreateContextFrom(vm.RootContext());
	SquirrelValue child_root = vm.CreateTable();
	vm.SetRootTableInContext(child, child_root);
	std::array<SquirrelValue, 5> arrays;
	for (std::size_t index = 0; index < arrays.size(); ++index) {
		arrays[index] = vm.CreateArray();
		vm.AppendArray(arrays[index], arrays[index]);
		const auto instance = vm.CreateInstance(class_value);
		vm.SetInstanceReleaseHook(instance, [&, index](SquirrelVm&, const SquirrelValue&) {
			callbacks.push_back(static_cast<std::int32_t>(index));
		});
		vm.AppendArray(arrays[index], instance);
	}

	vm.NewSlot(vm.RootTable(), std::string("root_cycle"), arrays[0]);
	vm.NewSlot(child_root, std::string("child_cycle"), arrays[1]);
	StoreLastError(vm, arrays[2]);
	auto external_root = vm.Retain(arrays[3]);
	vm.NewSlot(vm.ConstantTable(), std::string("constant_cycle"), arrays[4]);
	SquirrelValue collector = vm.CreateNativeClosure("independent-child-collector", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		Require(current.CurrentContext() == child, "collector fixture must execute from the child context");
		sgre::script::SquirrelNativeResult result;
		result.Value = static_cast<std::int32_t>(current.CollectGarbage());

		return result;
	});
	arrays = {};
	{
		const std::array<SquirrelValue, 1> arguments{child_root};
		Require(std::get<std::int32_t>(vm.InvokeInContext(child, collector, arguments).Value) == 0 && callbacks.empty(), "global VM, shared and explicit child roots must preserve every reachable cycle");
	}

	collector = {};
	external_root = {};
	vm.SetSlot(vm.ConstantTable(), std::string("constant_cycle"), {});
	vm.SetSlot(vm.RootTable(), std::string("root_cycle"), {});
	vm.SetSlot(child_root, std::string("child_cycle"), {});
	vm.ReleaseContext(child);
	child_root = {};
	StoreLastError(vm, {});
	Require(vm.CollectGarbage() == 5, "the five restored cycles must each require one native direct finalize iteration");
	vm.RethrowReleaseFailure();
	Require(callbacks == std::vector<std::int32_t>{1, 4, 3, 0, 2}, "root VM last-error/root phases must precede RefTable/constants even when collection was requested by a child");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first, std::int32_t second) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = first;
	instruction.Argument1 = second;

	return instruction;
}

static SquirrelValue CapturingHandler(SquirrelVm& vm, const SquirrelValue& value) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-handler-root-fixture");
	program.MainFunction.Parameters = {std::string("this"), std::string("captured")};
	program.MainFunction.StackSize = 3;
	program.MainFunction.Instructions = {Instruction(48, 2, 0), Instruction(19, 1, 2)};
	sgre::script::SquirrelFunction child;
	child.SourceName = std::string("independent-handler-root-fixture");
	child.Name = std::string("error_handler");
	child.Parameters = {std::string("this"), std::string("error")};
	child.StackSize = 2;
	child.Instructions = {Instruction(19, 1, 1)};
	sgre::script::SquirrelOuterBinding capture;
	capture.Type = 0;
	capture.Source = std::int32_t{1};
	capture.Name = std::string("captured");
	child.OuterBindings.push_back(capture);
	program.MainFunction.Functions.push_back(child);
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), value};

	return vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
}

static void SetHandler(SquirrelVm& vm, const SquirrelValue& value) {
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), value};
	static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("seterrorhandler")), arguments));
}

static void TestErrorHandlerCapturePrecedesRootAndConstantValues() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::vector<std::int32_t> callbacks;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto class_value = vm.CreateClass();
	vm.NewSlot(vm.RootTable(), std::string("callback_class"), class_value);
	std::array<SquirrelValue, 4> arrays;
	for (std::size_t index = 0; index < arrays.size(); ++index) {
		arrays[index] = vm.CreateArray();
		vm.AppendArray(arrays[index], arrays[index]);
		const auto instance = vm.CreateInstance(class_value);
		vm.SetInstanceReleaseHook(instance, [&, index](SquirrelVm&, const SquirrelValue&) {
			callbacks.push_back(static_cast<std::int32_t>(index));
		});
		vm.AppendArray(arrays[index], instance);
	}

	StoreLastError(vm, arrays[0]);
	SetHandler(vm, CapturingHandler(vm, arrays[1]));
	vm.NewSlot(vm.RootTable(), std::string("root_cycle"), arrays[2]);
	vm.NewSlot(vm.ConstantTable(), std::string("constant_cycle"), arrays[3]);
	arrays = {};
	Require(vm.CollectGarbage() == 0 && callbacks.empty(), "the real error-handler closure must mark its captured array before the root and shared constants");
	SetHandler(vm, {});
	StoreLastError(vm, {});
	sgre::script::BytecodeProgram clear_temporary;
	clear_temporary.MainFunction.Parameters = {std::string("this")};
	clear_temporary.MainFunction.StackSize = 1;
	clear_temporary.MainFunction.Instructions = {Instruction(19, 255, 0)};
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(clear_temporary)), arguments));
	vm.SetSlot(vm.RootTable(), std::string("root_cycle"), {});
	vm.SetSlot(vm.ConstantTable(), std::string("constant_cycle"), {});
	Require(vm.CollectGarbage() == 4, "clearing error state and table edges must expose all four native array cycles");
	vm.RethrowReleaseFailure();
	Require(callbacks == std::vector<std::int32_t>{3, 2, 1, 0}, "last error then handler then root must retain the native reverse postorder callback sequence");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestGlobalRootVmPrecedesCallerAndSharedConstantRoots();
		TestErrorHandlerCapturePrecedesRootAndConstantValues();
		std::cout << "Squirrel native root-phase checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
