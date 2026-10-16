#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void AttachDelegate(SquirrelVm& vm, const SquirrelValue& table, const SquirrelValue& delegate) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-table-delegate-fixture");
	program.MainFunction.Parameters = {std::string("this"), std::string("delegate")};
	program.MainFunction.StackSize = 2;
	sgre::script::SquirrelInstruction set_delegate;
	set_delegate.Opcode = 53;
	set_delegate.Argument0 = 0;
	set_delegate.Argument1 = 0;
	set_delegate.Argument2 = 1;
	sgre::script::SquirrelInstruction return_table;
	return_table.Opcode = 19;
	return_table.Argument0 = 1;
	return_table.Argument1 = 0;
	program.MainFunction.Instructions = {set_delegate, return_table};
	const std::array<SquirrelValue, 2> arguments{table, delegate};
	static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(program)), arguments));
	vm.ResetContext(vm.RootContext());
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first = 0, std::int32_t second = 0, std::uint8_t third = 0) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = first;
	instruction.Argument1 = second;
	instruction.Argument2 = third;

	return instruction;
}

static SquirrelValue PhysicalKeys(SquirrelVm& vm, const SquirrelValue& table) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-table-clear-order-fixture");
	program.MainFunction.Parameters = {std::string("this"), std::string("table")};
	program.MainFunction.StackSize = 7;
	program.MainFunction.Instructions = {Instruction(31, 6), Instruction(51, 1, 3, 2), Instruction(52, 1, 3, 2), Instruction(32, 6, 2), Instruction(24, 0, -4), Instruction(19, 1, 6)};
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), table};

	return vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
}

static void TestTableClear(std::int32_t filler_count) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelValue key_weak;
	SquirrelValue value_weak;
	SquirrelValue table_weak;
	SquirrelValue delegate_weak;
	std::int32_t callbacks = 0;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto owner_class = vm.CreateClass();
	const auto class_root = vm.Retain(owner_class);
	SquirrelValue value = vm.CreateInstance(owner_class);
	SquirrelValue delegate = vm.CreateTable();
	const auto table = vm.CreateTable();
	const auto table_root = vm.Retain(table);
	table_weak = vm.CreateWeakReference(table);
	delegate_weak = vm.CreateWeakReference(delegate);
	SquirrelValue key = vm.CreateString("independent-clear-key-release");
	key_weak = vm.CreateWeakReference(key);
	value_weak = vm.CreateWeakReference(value);
	vm.SetInstanceReleaseHook(value, [&](SquirrelVm& active, const SquirrelValue&) {
		Require(active.Type(active.ResolveWeakReference(key_weak)) == SquirrelValueType::Null, "table clear must expire its canonical key before the owned value callback");
		Require(active.Type(active.ResolveWeakReference(value_weak)) == SquirrelValueType::Instance, "clear must retain the native value weak identity during cleanup");
		Require(active.Type(active.ResolveWeakReference(delegate_weak)) == SquirrelValueType::Table, "table clear must preserve its existing delegate through node cleanup");
		Require(active.Length(active.ResolveWeakReference(table_weak)) == static_cast<std::size_t>(filler_count + 1), "native table clear resets used count only after owned node callbacks finish");
		++callbacks;
	});
	vm.NewSlot(delegate, std::string("still-delegated"), std::int32_t{73});
	AttachDelegate(vm, table, delegate);
	for (std::int32_t index = 0; index < filler_count; ++index) {
		vm.NewSlot(table, index, index);
	}

	vm.NewSlot(table, key, value);
	key = {};
	value = {};
	delegate = {};
	const auto clear = vm.ReadSlot(table, std::string("clear"));
	const std::array<SquirrelValue, 1> arguments{table};
	Require(vm.Type(vm.Invoke(clear, arguments).Value) == SquirrelValueType::Null, "native table clear returns zero values");
	vm.RethrowReleaseFailure();
	Require(callbacks == 1 && vm.Length(table) == 0, "table clear must release every owned node once and finish with an empty own table");
	Require(std::get<std::int32_t>(vm.ReadSlot(table, std::string("still-delegated"))) == 73, "cleared table must keep its genuine delegate lookup");
	vm.NewSlot(table, std::int32_t{8}, std::int32_t{80});
	vm.NewSlot(table, std::int32_t{0}, std::int32_t{0});
	const auto keys = PhysicalKeys(vm, table);
	const std::array<std::int32_t, 2> expected = filler_count == 29 ? std::array<std::int32_t, 2>{0, 8} : std::array<std::int32_t, 2>{8, 0};
	Require(vm.Length(keys) == expected.size(), "post-clear native bucket traversal must contain the two new keys");
	for (std::size_t index = 0; index < expected.size(); ++index) {
		Require(std::get<std::int32_t>(vm.GetSlot(keys, static_cast<std::int32_t>(index))) == expected[index], "table clear must force a single native capacity-halving step instead of collapsing all large tables to four buckets");
	}

	vm.Close();
	vm.RethrowReleaseFailure();
	Require(callbacks == 1, "explicit VM close must not repeat a previously cleared value callback");
}

int main() {
	try {
		for (const auto count : {1, 5, 13, 29}) {
			TestTableClear(count);
		}

		std::cout << "four independent table clear phase and capacity fixtures pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
