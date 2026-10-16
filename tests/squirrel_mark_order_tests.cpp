#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestMarkedGraphControlsLaterFinalizerOrder(bool shared_descendant) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	const auto class_root = vm.Retain(class_value);
	std::array<SquirrelValue, 4> arrays;
	for (auto& array : arrays) {
		array = vm.CreateArray();
		vm.AppendArray(array, array);
	}

	vm.AppendArray(arrays[0], arrays[1]);
	vm.AppendArray(arrays[0], arrays[2]);
	vm.AppendArray(arrays[1], arrays[3]);
	if (shared_descendant) {
		vm.AppendArray(arrays[2], arrays[3]);
	}

	std::vector<std::int32_t> callbacks;
	for (std::size_t index = 0; index < arrays.size(); ++index) {
		const auto instance = vm.CreateInstance(class_value);
		vm.SetInstanceReleaseHook(instance, [&, index](SquirrelVm&, const SquirrelValue&) {
			callbacks.push_back(static_cast<std::int32_t>(index));
		});
		vm.AppendArray(arrays[index], instance);
	}

	const auto outer_weak = vm.CreateWeakReference(arrays[0]);
	vm.NewSlot(vm.RootTable(), std::string("graph"), arrays[0]);
	arrays = {};
	Require(vm.CollectGarbage() == 0 && callbacks.empty(), "the first collection must mark the reachable graph without invoking finalizers");
	vm.SetSlot(vm.RootTable(), std::string("graph"), {});
	Require(vm.CollectGarbage() == 4, "native postorder marking must preserve four explicit array iterations after unrooting");
	vm.RethrowReleaseFailure();
	Require(callbacks == std::vector<std::int32_t>{0, 2, 1, 3}, "second collection must follow the native restored chain A,C,B,D and cascade each owned callback");
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(outer_weak)), "the unrooted graph must retire all native weak targets");
}

static void TestRepeatedReachabilityDoesNotReorderSharedChildren() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	const auto class_root = vm.Retain(class_value);
	SquirrelValue first = vm.CreateArray();
	SquirrelValue second = vm.CreateArray();
	SquirrelValue leaf = vm.CreateArray();
	vm.AppendArray(first, first);
	vm.AppendArray(first, leaf);
	vm.AppendArray(second, second);
	vm.AppendArray(second, leaf);
	vm.AppendArray(leaf, leaf);
	std::vector<std::int32_t> callbacks;
	for (const auto& pair : std::array<std::pair<SquirrelValue, std::int32_t>, 3>{{{first, 0}, {second, 1}, {leaf, 2}}}) {
		const auto owner = vm.CreateInstance(class_value);
		vm.SetInstanceReleaseHook(owner, [&, index = pair.second](SquirrelVm&, const SquirrelValue&) {
			callbacks.push_back(index);
		});
		vm.AppendArray(pair.first, owner);
	}

	std::array<SquirrelValue, 2> roots{first, second};
	SquirrelValue root = vm.CreateArray(roots);
	vm.NewSlot(vm.RootTable(), std::string("roots"), root);
	roots = {};
	root = {};
	first = {};
	second = {};
	leaf = {};
	Require(vm.CollectGarbage() == 0, "all shared children must remain reachable during the first collection");
	vm.SetSlot(vm.RootTable(), std::string("roots"), {});
	Require(vm.CollectGarbage() == 3, "the acyclic root holder must release before the three native cyclic array iterations");
	vm.RethrowReleaseFailure();
	Require(callbacks == std::vector<std::int32_t>{1, 0, 2}, "an already visited shared descendant must retain its first traversal placement");
}

static void TestDeepMarkAndReleaseRemainIterative() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxObjects = 10010;
	SquirrelVm vm(limits);
	SquirrelValue chain = vm.CreateArray();
	vm.AppendArray(chain, chain);
	const auto leaf_weak = vm.CreateWeakReference(chain);
	for (std::size_t depth = 0; depth < 10000; ++depth) {
		const std::array<SquirrelValue, 1> child{chain};
		chain = vm.CreateArray(child);
	}

	vm.NewSlot(vm.RootTable(), std::string("deep"), chain);
	chain = {};
	Require(vm.CollectGarbage() == 0, "a ten-thousand-level reachable graph must mark without native C++ stack recursion");
	vm.SetSlot(vm.RootTable(), std::string("deep"), {});
	Require(vm.CollectGarbage() == 1 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(leaf_weak)), "deep release must leave exactly the unrooted leaf self cycle for explicit finalization");
}

int main() {
	try {
		TestMarkedGraphControlsLaterFinalizerOrder(false);
		TestMarkedGraphControlsLaterFinalizerOrder(true);
		TestRepeatedReachabilityDoesNotReorderSharedChildren();
		TestDeepMarkAndReleaseRemainIterative();
		std::cout << "Squirrel iterative native mark-order checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
