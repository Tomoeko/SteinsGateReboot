#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using sgre::script::SquirrelHostRoot;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestReleasedNativeRootNodesAreReusedInReleaseOrder() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::vector<std::int32_t> callbacks;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	vm.NewSlot(vm.RootTable(), std::string("callback_class"), class_value);
	std::array<SquirrelValue, 6> arrays;
	std::array<SquirrelHostRoot, 6> roots;
	for (std::size_t index = 0; index < arrays.size(); ++index) {
		arrays[index] = vm.CreateArray();
		vm.AppendArray(arrays[index], arrays[index]);
		const auto instance = vm.CreateInstance(class_value);
		vm.SetInstanceReleaseHook(instance, [&, index](SquirrelVm&, const SquirrelValue&) {
			callbacks.push_back(static_cast<std::int32_t>(index));
		});
		vm.AppendArray(arrays[index], instance);
	}

	for (std::size_t index = 0; index < 4; ++index) {
		roots[index] = vm.Retain(arrays[index]);
	}
	const auto duplicate = vm.Retain(arrays[0]);
	Require(vm.RetainedReferenceCount(arrays[0]) == 2, "duplicate AddRef must increment the existing physical node count");
	roots[3] = {};
	roots[1] = {};
	roots[4] = vm.Retain(arrays[4]);
	roots[5] = vm.Retain(arrays[5]);
	arrays = {};
	const auto first_count = vm.CollectGarbage();
	Require(first_count == 4 && callbacks.size() == 2, "the first collection must finalize the two unmarked arrays and their preceding native instances");
	vm.RethrowReleaseFailure();
	callbacks.clear();
	for (auto& root : roots) {
		root = {};
	}

	// Keep the duplicate's node alive while proving every other physical slot's restored traversal order.
	Require(vm.CollectGarbage() == 3, "an independently retained duplicate must keep its original cycle alive");
	vm.RethrowReleaseFailure();
	Require(callbacks == std::vector<std::int32_t>{5, 2, 4}, "released native root nodes must be reused from the LIFO free chain before ascending collector traversal");
	Require(vm.RetainedReferenceCount(duplicate.Value()) == 1, "duplicate token must remain attached to the same native root node after node reuse");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestRootGrowthPreservesPhysicalNodeOrderAndTokens() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::vector<std::int32_t> callbacks;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	vm.NewSlot(vm.RootTable(), std::string("callback_class"), class_value);
	std::array<SquirrelValue, 35> arrays;
	std::array<SquirrelHostRoot, 35> roots;
	for (std::size_t index = 0; index < arrays.size(); ++index) {
		arrays[index] = vm.CreateArray();
		vm.AppendArray(arrays[index], arrays[index]);
		const auto instance = vm.CreateInstance(class_value);
		vm.SetInstanceReleaseHook(instance, [&, index](SquirrelVm&, const SquirrelValue&) {
			callbacks.push_back(static_cast<std::int32_t>(index));
		});
		vm.AppendArray(arrays[index], instance);
		if (index < 32) {
			roots[index] = vm.Retain(arrays[index]);
		}
	}

	std::vector<std::int32_t> physical_order;
	for (std::int32_t index = 0; index < 32; ++index) {
		physical_order.push_back(index);
	}

	for (const auto index : {30, 2, 17}) {
		roots[static_cast<std::size_t>(index)] = {};
	}

	for (std::size_t index = 32; index < arrays.size(); ++index) {
		roots[index] = vm.Retain(arrays[index]);
	}

	physical_order[17] = 32;
	physical_order[2] = 33;
	physical_order[30] = 34;
	for (std::size_t index = 0; index < arrays.size(); ++index) {
		if (index != 30 && index != 2 && index != 17) {
			Require(roots[index].Value() == arrays[index] && vm.RetainedReferenceCount(arrays[index]) == 1, "root tokens must retain their original identities through native capacity growth and freed-node reuse");
		}
	}

	arrays = {};
	Require(vm.CollectGarbage() == 6 && callbacks.size() == 3, "unmarked old owners must release before the expanded native root graph is restored");
	vm.RethrowReleaseFailure();
	callbacks.clear();
	for (auto& root : roots) {
		root = {};
	}

	Require(vm.CollectGarbage() == 32, "all thirty-two restored array cycles must require their native direct finalize iteration");
	vm.RethrowReleaseFailure();
	Require(callbacks == std::vector<std::int32_t>(physical_order.rbegin(), physical_order.rend()), "doubling the full native node storage must preserve ascending physical entries before postorder restoration");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestRetainInsideLastReleaseCanReuseTheFreedNode() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelHostRoot replacement;
	std::size_t calls = 0;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	SquirrelValue instance = vm.CreateInstance(class_value);
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue& receiver) {
		replacement = current.Retain(receiver);
		++calls;
	});
	auto original = vm.Retain(instance);
	const auto weak = vm.CreateWeakReference(instance);
	instance = {};
	original = {};
	Require(calls == 1 && vm.RetainedReferenceCount(replacement.Value()) == 1 && vm.ResolveWeakReference(weak) == replacement.Value(), "last native Release must publish its freed slot before the retained owner's callback runs");
	replacement = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "replacement root release must retire the resurrected native owner once");
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestReleasedNativeRootNodesAreReusedInReleaseOrder();
		TestRootGrowthPreservesPhysicalNodeOrderAndTokens();
		TestRetainInsideLastReleaseCanReuseTheFreedNode();
		std::cout << "Squirrel native external-root order checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
