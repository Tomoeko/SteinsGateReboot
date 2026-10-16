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

static void TestTableReleasePhases(bool collection, bool with_delegate) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::vector<std::int32_t> events;
	SquirrelValue table_weak;
	SquirrelValue delegate_weak;
	SquirrelValue key_weak;
	SquirrelValue value_weak;
	SquirrelVm vm(limits);
	const auto owner_class = vm.CreateClass();
	const auto class_root = vm.Retain(owner_class);
	SquirrelValue value = vm.CreateInstance(owner_class);
	SquirrelValue delegate_value = with_delegate ? vm.CreateInstance(owner_class) : SquirrelValue{};
	SquirrelValue delegate = with_delegate ? vm.CreateTable() : SquirrelValue{};
	// Allocate the containing table last so collection enters its native finalizer before its children.
	SquirrelValue table = vm.CreateTable();
	table_weak = vm.CreateWeakReference(table);
	delegate_weak = with_delegate ? vm.CreateWeakReference(delegate) : SquirrelValue{};
	SquirrelValue key = vm.CreateString("independent-table-key-release");
	key_weak = vm.CreateWeakReference(key);
	value_weak = vm.CreateWeakReference(value);
	vm.SetInstanceReleaseHook(value, [&](SquirrelVm& active, const SquirrelValue&) {
		Require(active.Type(active.ResolveWeakReference(key_weak)) == SquirrelValueType::Null, "table key's canonical weak target must expire before its owned value release callback");
		Require(active.Type(active.ResolveWeakReference(value_weak)) == SquirrelValueType::Instance, "owned value identity must remain alive throughout its actual native release callback");
		Require(active.Type(active.ResolveWeakReference(table_weak)) == SquirrelValueType::Table, "containing table weak identity must survive its child release phases");
		if (with_delegate) {
			const bool delegate_alive = active.Type(active.ResolveWeakReference(delegate_weak)) == SquirrelValueType::Table;
			Require(delegate_alive == collection, "ordinary table destruction must retire delegate first, while collection must retire it after node values");
		}

		events.push_back(1);
	});
	if (with_delegate) {
		vm.SetInstanceReleaseHook(delegate_value, [&](SquirrelVm& active, const SquirrelValue&) {
			const bool key_alive = active.Type(active.ResolveWeakReference(key_weak)) == SquirrelValueType::String;
			Require(key_alive != collection, "delegate child cleanup must observe intact keys before ordinary node destruction and expired keys after finalization");
			Require(active.Type(active.ResolveWeakReference(table_weak)) == SquirrelValueType::Table, "table must retain its identity throughout delegate destruction");
			events.push_back(0);
		});
		vm.NewSlot(delegate, std::string("child"), delegate_value);
		AttachDelegate(vm, table, delegate);
	}

	vm.NewSlot(table, key, value);
	key = {};
	value = {};
	delegate_value = {};
	delegate = {};
	if (collection) {
		Require(vm.CollectGarbage() == 1, "containing table finalization must account for its owned descendants through one explicit iteration");
		Require(vm.Type(vm.ResolveWeakReference(table_weak)) == SquirrelValueType::Table, "an unrooted but externally strong table must survive collection finalization");
	}

	table = {};
	vm.RethrowReleaseFailure();
	const auto expected = with_delegate ? collection ? std::vector<std::int32_t>{1, 0} : std::vector<std::int32_t>{0, 1} : std::vector<std::int32_t>{1};
	Require(events == expected, "table release callbacks must follow the recovered node/delegate phase distinction without duplicate finalization");
	Require(vm.Type(vm.ResolveWeakReference(table_weak)) == SquirrelValueType::Null && vm.Type(vm.ResolveWeakReference(key_weak)) == SquirrelValueType::Null && vm.Type(vm.ResolveWeakReference(value_weak)) == SquirrelValueType::Null, "completed table deletion must expire every owned weak target");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const auto collection : {false, true}) {
			TestTableReleasePhases(collection, false);
			TestTableReleasePhases(collection, true);
		}

		std::cout << "four independent table lifetime phase fixtures pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
