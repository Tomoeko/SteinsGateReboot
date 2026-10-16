#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using sgre::script::SquirrelObjectHandle;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestImmediateExpirationAndReuse() {
	sgre::script::SquirrelVmLimits limits;
	limits.MaxObjects = 4;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue array = vm.CreateArray();
	const auto first_index = std::get<SquirrelObjectHandle>(array).Index;
	const auto first_generation = std::get<SquirrelObjectHandle>(array).Generation;
	const auto weak = vm.CreateWeakReference(array);
	Require(vm.ResolveWeakReference(weak) == array, "new weak wrapper lost target");
	array = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "last strong release must expire weak target immediately");
	for (std::size_t iteration = 0; iteration < 1000; ++iteration) {
		const auto replacement = vm.CreateArray();
		Require(std::get<SquirrelObjectHandle>(replacement).Index == first_index && std::get<SquirrelObjectHandle>(replacement).Generation != first_generation, "freed arena index must be reused with a new generation");
	}
}

static void TestWeakWrapperCacheLifetime() {
	sgre::script::SquirrelVmLimits limits;
	limits.MaxObjects = 4;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto array = vm.CreateArray();
	SquirrelValue weak = vm.CreateWeakReference(array);
	const auto first_generation = std::get<SquirrelObjectHandle>(weak).Generation;
	weak = {};
	const auto new_weak = vm.CreateWeakReference(array);
	Require(std::get<SquirrelObjectHandle>(new_weak).Generation != first_generation && vm.ResolveWeakReference(new_weak) == array, "weak wrapper destruction must unlink the target's cached wrapper");
}

static void TestDeepReleaseIsBounded() {
	sgre::script::SquirrelVmLimits limits;
	limits.MaxObjects = 10004;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue tree = vm.CreateArray();
	const auto weak = vm.CreateWeakReference(tree);
	for (std::size_t depth = 0; depth < 10000; ++depth) {
		const std::array<SquirrelValue, 1> child{tree};
		tree = vm.CreateArray(child);
	}

	tree = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "iterative release must expire the deepest array");
	for (std::size_t iteration = 0; iteration < 10000; ++iteration) {
		static_cast<void>(vm.CreateArray());
	}
}

static void TestInstanceReleaseAndReplacement() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("member"), std::int32_t{42});
	SquirrelValue instance = vm.CreateInstance(class_value);
	const auto weak = vm.CreateWeakReference(instance);
	std::size_t old_calls = 0;
	std::size_t calls = 0;
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm&, const SquirrelValue&) {
		++old_calls;
	});
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue& receiver) {
		Require(current.ResolveWeakReference(weak) == receiver, "native release hook must observe its live weak self");
		Require(current.IsInstanceOf(receiver, class_value) && std::get<std::int32_t>(current.GetSlot(receiver, std::string("member"))) == 42, "release hook must precede class and field drops");
		++calls;
	});
	instance = {};
	Require(old_calls == 0 && calls == 1, "release hook registration must replace the single native callback");
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "released instance must clear weak target after its hook");
}

static void TestInstanceResurrection() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("member"), std::int32_t{71});
	SquirrelValue resurrected;
	SquirrelValue instance = vm.CreateInstance(class_value);
	const auto weak = vm.CreateWeakReference(instance);
	std::size_t calls = 0;
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm&, const SquirrelValue& receiver) {
		resurrected = receiver;
		++calls;
	});
	instance = {};
	Require(calls == 1 && vm.ResolveWeakReference(weak) == resurrected, "an explicitly retained instance must survive the native release-hook guard");
	Require(std::get<std::int32_t>(vm.GetSlot(resurrected, std::string("member"))) == 71, "reference release resurrection must preserve class and fields");
	resurrected = {};
	Require(calls == 1 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "resurrected instance's overwritten hook must not run again");
}

static void TestReleaseHookExceptions() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	SquirrelValue instance = vm.CreateInstance(class_value);
	const auto weak = vm.CreateWeakReference(instance);
	vm.SetInstanceReleaseHook(instance, [](SquirrelVm&, const SquirrelValue&) {
		throw std::runtime_error("host release failure");
	});
	instance = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "release callback exception must not escape a value destructor or strand storage");
	bool caught = false;
	try {
		vm.RethrowReleaseFailure();
	} catch (const std::runtime_error& error) {
		caught = std::string(error.what()) == "host release failure";
	}

	Require(caught, "explicit release diagnostics must retain callback exceptions");
	vm.RethrowReleaseFailure();
}

static void TestArrayReleaseOrder() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	SquirrelValue left = vm.CreateInstance(class_value);
	SquirrelValue right = vm.CreateInstance(class_value);
	const auto left_weak = vm.CreateWeakReference(left);
	const auto right_weak = vm.CreateWeakReference(right);
	std::vector<std::int32_t> calls;
	vm.SetInstanceReleaseHook(left, [&](SquirrelVm& current, const SquirrelValue&) {
		Require(!std::holds_alternative<std::monostate>(current.ResolveWeakReference(right_weak)), "first child release hook must precede the second element drop");
		calls.push_back(1);
	});
	vm.SetInstanceReleaseHook(right, [&](SquirrelVm& current, const SquirrelValue&) {
		Require(std::holds_alternative<std::monostate>(current.ResolveWeakReference(left_weak)), "second child release hook must follow the first child's complete release");
		calls.push_back(2);
	});
	SquirrelValue parent;
	{
		const std::array<SquirrelValue, 2> children{left, right};
		parent = vm.CreateArray(children);
	}

	left = {};
	right = {};
	parent = {};
	vm.RethrowReleaseFailure();
	Require(calls == std::vector<std::int32_t>{1, 2}, "iterative array finalization must retain native ascending child release order");
}

static void TestReplacementIsVisibleBeforeRelease() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	SquirrelValue left = vm.CreateInstance(class_value);
	const auto right = vm.CreateInstance(class_value);
	const std::array<SquirrelValue, 1> null_element{};
	const auto array = vm.CreateArray(null_element);
	vm.SetSlot(array, std::int32_t{0}, left);
	std::size_t calls = 0;
	vm.SetInstanceReleaseHook(left, [&](SquirrelVm& current, const SquirrelValue&) {
		Require(current.GetSlot(array, std::int32_t{0}) == right, "replacement must be installed before the old value's release hook");
		++calls;
	});
	left = {};
	vm.SetSlot(array, std::int32_t{0}, right);
	vm.RethrowReleaseFailure();
	Require(calls == 1, "array value replacement must release its previous strong reference");
}

static void TestNativeCollectionCorpus() {
	for (const bool pair : {false, true}) {
		for (const bool rooted : {false, true}) {
			sgre::script::SquirrelVmLimits limits;
			limits.ReclaimObjects = true;
			SquirrelVm vm(limits);
			const std::array<SquirrelValue, 1> null_element{};
			SquirrelValue left = vm.CreateArray(null_element);
			SquirrelValue right = pair ? vm.CreateArray(null_element) : left;
			vm.SetSlot(left, std::int32_t{0}, right);
			if (pair) {
				vm.SetSlot(right, std::int32_t{0}, left);
			}

			const auto weak = vm.CreateWeakReference(left);
			sgre::script::SquirrelHostRoot root;
			if (rooted) {
				root = vm.Retain(left);
			}

			left = {};
			right = {};
			const auto first = vm.CollectGarbage();
			Require(first == (rooted ? 0u : 1u), "self and pair collection counts must match the exact private native VM corpus");
			Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)) != rooted, "external roots must protect the cycle while surviving weak wrappers expire without roots");
			root = {};
			const auto second = vm.CollectGarbage();
			Require(second == (rooted ? 1u : 0u) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "root release must make the cycle collectable with one explicit finalize iteration");
		}
	}

	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue array = vm.CreateArray();
	const auto weak = vm.CreateWeakReference(array);
	array = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)) && vm.CollectGarbage() == 0, "immediate reference release must precede collection and return zero native finalize iterations");
}

static void TestRootGraphCollection() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const std::array<SquirrelValue, 1> null_element{};
	SquirrelValue value = vm.CreateArray(null_element);
	vm.SetSlot(value, std::int32_t{0}, value);
	const auto weak = vm.CreateWeakReference(value);
	vm.NewSlot(vm.ConstantTable(), std::string("cycle"), value);
	value = {};
	Require(vm.CollectGarbage() == 0 && !std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "shared constant table members must be native mark roots");
	vm.SetSlot(vm.ConstantTable(), std::string("cycle"), {});
	Require(vm.CollectGarbage() == 1 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "released shared table edge must stop protecting the cycle");
	const auto class_value = vm.CreateClass();
	auto class_root = vm.Retain(class_value);
	vm.NewSlot(class_value, std::string("self"), {});
	SquirrelValue instance = vm.CreateInstance(class_value);
	vm.SetSlot(instance, std::string("self"), instance);
	const auto instance_weak = vm.CreateWeakReference(instance);
	std::size_t calls = 0;
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue& receiver) {
		Require(current.IsInstanceOf(receiver, class_value) && current.GetSlot(receiver, std::string("self")) == receiver, "cycle finalizer hook must precede native class and field clearing");
		++calls;
	});
	instance = {};
	Require(vm.CollectGarbage() == 1 && calls == 1 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(instance_weak)), "instance self cycle must finalize its owner once and expire after field clearing");
	vm.RethrowReleaseFailure();
}

static void TestBorrowedIdentitiesAndHookReentry() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue value = vm.CreateArray();
	const auto identity = vm.BorrowObjectIdentity(value);
	Require(vm.ResolveObjectIdentity(identity) == value && vm.BorrowObjectIdentity(value) == identity, "borrowed identity must resolve the same live allocation");
	value = {};
	Require(!vm.ResolveObjectIdentity(identity), "borrowed identities must not retain object storage");
	const auto reused = vm.CreateArray();
	Require(std::get<SquirrelObjectHandle>(reused).Index == identity.Index && !vm.ResolveObjectIdentity(identity) && vm.BorrowObjectIdentity(reused) != identity, "stale identity must not resolve a recycled slot");
	Require(!vm.ResolveObjectIdentity({}), "empty identity must resolve empty");
	SquirrelVm foreign;
	bool foreign_failed = false;
	try {
		static_cast<void>(foreign.ResolveObjectIdentity(vm.BorrowObjectIdentity(reused)));
	} catch (const std::runtime_error&) {
		foreign_failed = true;
	}

	Require(foreign_failed, "foreign identities must reject their owner");
	auto forged = identity;
	const auto live_identity = vm.BorrowObjectIdentity(reused);
	forged.Index = live_identity.Index;
	forged.Generation = live_identity.Generation;
	forged.Owner = live_identity.Owner;
	Require(!vm.ResolveObjectIdentity(forged), "private weak identity token must reject retargeting stale public identity fields");
	sgre::script::SquirrelObjectIdentity detached;
	{
		SquirrelVm owner(limits);
		detached = owner.BorrowObjectIdentity(owner.CreateArray());
	}

	detached.Index = live_identity.Index;
	detached.Generation = live_identity.Generation;
	detached.Owner = live_identity.Owner;
	Require(!vm.ResolveObjectIdentity(detached), "destroyed-owner identity must not retarget a later VM even when owner address fields coincide");
	const auto class_value = vm.CreateClass();
	SquirrelValue instance = vm.CreateInstance(class_value);
	SquirrelValue resurrected;
	std::size_t replacements = 0;
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue& receiver) {
		resurrected = receiver;
		current.SetInstanceReleaseHook(receiver, [&](SquirrelVm&, const SquirrelValue&) {
			++replacements;
		});
	});
	instance = {};
	resurrected = {};
	Require(replacements == 0, "native release clears its hook after callback execution, including a reentrant replacement");
	vm.RethrowReleaseFailure();
}

static void TestSavedGeneratorHasNoOwningSelfCycle() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::BytecodeProgram program;
	program.MainFunction.Parameters.push_back(std::string("this"));
	program.MainFunction.StackSize = 2;
	program.MainFunction.IsGenerator = true;
	sgre::script::SquirrelInstruction exit;
	exit.Opcode = 19;
	exit.Argument0 = 255;
	program.MainFunction.Instructions.push_back(exit);
	const auto closure = vm.CreateClosure(std::move(program));
	const auto closure_root = vm.Retain(closure);
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	SquirrelValue generator = vm.Invoke(closure, receiver).Value;
	const auto weak = vm.CreateWeakReference(generator);
	generator = {};
	vm.ResetExecution();
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)) && vm.CollectGarbage() == 0, "unused saved generator must not retain itself and require cycle collection");
}

static void TestNativeArgumentsAreMarkRoots() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto collector = vm.CreateNativeClosure("collect_inside_native_callback", [](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(current.CollectGarbage() == 0 && current.Length(arguments[1]) == 1, "active native callback arguments must remain traced like the native VM stack");
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{23};

		return result;
	});
	const std::array<SquirrelValue, 1> null_element{};
	SquirrelValue cycle = vm.CreateArray(null_element);
	vm.SetSlot(cycle, std::int32_t{0}, cycle);
	const auto weak = vm.CreateWeakReference(cycle);
	{
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), cycle};
		Require(std::get<std::int32_t>(vm.Invoke(collector, arguments).Value) == 23, "native callback result changed during collection");
	}

	const auto collector_root = vm.Retain(collector);
	cycle = {};
	Require(vm.CollectGarbage() == 1 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "returned native frame arguments must stop protecting their cycle");
}

static void TestDirectArrayInterop() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto array = vm.CreateArray();
	vm.AppendArray(array, std::int32_t{4});
	vm.AppendArray(array, std::int32_t{8});
	vm.AppendArray(array, std::int32_t{12});
	Require(std::get<std::int32_t>(vm.RemoveArray(array, 1)) == 8 && vm.Length(array) == 2 && std::get<std::int32_t>(vm.GetSlot(array, std::int32_t{1})) == 12, "direct array removal must return the old value and shift remaining values");
	for (const auto invalid_index : {std::int32_t{-1}, std::int32_t{2}}) {
		bool failed = false;
		try {
			static_cast<void>(vm.RemoveArray(array, invalid_index));
		} catch (const std::runtime_error&) {
			failed = true;
		}

		Require(failed && vm.Length(array) == 2, "direct remove must reject out-of-range indices without mutation");
	}

	vm.ClearArray(array);
	Require(vm.Length(array) == 0 && !vm.HasSlot(vm.RootTable(), std::string("array")), "direct array operations must work without installing base delegates");
	const auto class_value = vm.CreateClass();
	SquirrelValue left = vm.CreateInstance(class_value);
	SquirrelValue right = vm.CreateInstance(class_value);
	const auto left_weak = vm.CreateWeakReference(left);
	const auto right_weak = vm.CreateWeakReference(right);
	vm.AppendArray(array, left);
	vm.AppendArray(array, right);
	std::vector<std::int32_t> calls;
	vm.SetInstanceReleaseHook(left, [&](SquirrelVm& current, const SquirrelValue& receiver) {
		Require(current.Length(array) == 2 && current.GetSlot(array, std::int32_t{0}) == receiver, "explicit clear must leave a checked raw current-slot identity visible during its native release guard");
		Require(!std::holds_alternative<std::monostate>(current.ResolveWeakReference(right_weak)), "explicit clear must not release a later child early");
		calls.push_back(1);
	});
	vm.SetInstanceReleaseHook(right, [&](SquirrelVm& current, const SquirrelValue& receiver) {
		Require(current.Length(array) == 2 && current.GetSlot(array, std::int32_t{1}) == receiver && std::holds_alternative<std::monostate>(current.ResolveWeakReference(left_weak)), "explicit clear must release ascending children before changing logical size");
		calls.push_back(2);
	});
	left = {};
	right = {};
	vm.ClearArray(array);
	vm.RethrowReleaseFailure();
	Require(calls == std::vector<std::int32_t>{1, 2} && vm.Length(array) == 0 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(right_weak)), "direct clear must finish child release and clear logical size");
	const auto invalid_array = vm.CreateTable();
	bool clear_failed = false;
	try {
		vm.ClearArray(invalid_array);
	} catch (const std::runtime_error&) {
		clear_failed = true;
	}

	Require(clear_failed, "direct array clear must reject other container types");
}

static void TestNestedReleaseCallbackOrder() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	const auto owner = vm.CreateTable();
	SquirrelValue first = vm.CreateInstance(class_value);
	SquirrelValue second = vm.CreateInstance(class_value);
	std::vector<std::int32_t> calls;
	vm.SetInstanceReleaseHook(second, [&](SquirrelVm& current, const SquirrelValue&) {
		Require(std::holds_alternative<std::monostate>(current.GetSlot(owner, std::string("second"))), "nested native release must observe incoming null before previous-value destruction");
		calls.push_back(2);
	});
	vm.SetInstanceReleaseHook(first, [&](SquirrelVm& current, const SquirrelValue&) {
		calls.push_back(1);
		current.SetSlot(owner, std::string("second"), {});
		calls.push_back(3);
	});
	vm.NewSlot(owner, std::string("second"), second);
	second = {};
	first = {};
	vm.RethrowReleaseFailure();
	Require(calls == std::vector<std::int32_t>{1, 2, 3}, "nested release callbacks must complete synchronously before their releasing callback resumes");
}

static void TestNestedReleaseCallbackDepthLimit() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxFrames = 2;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	const auto owner = vm.CreateTable();
	SquirrelValue first = vm.CreateInstance(class_value);
	SquirrelValue second = vm.CreateInstance(class_value);
	SquirrelValue third = vm.CreateInstance(class_value);
	const auto identity = vm.BorrowObjectIdentity(third);
	std::vector<std::int32_t> calls;
	vm.SetInstanceReleaseHook(first, [&](SquirrelVm& current, const SquirrelValue&) {
		calls.push_back(1);
		current.SetSlot(owner, std::int32_t{2}, {});
		calls.push_back(4);
	});
	vm.SetInstanceReleaseHook(second, [&](SquirrelVm& current, const SquirrelValue&) {
		calls.push_back(2);
		current.SetSlot(owner, std::int32_t{3}, {});
		calls.push_back(3);
	});
	vm.SetInstanceReleaseHook(third, [&](SquirrelVm&, const SquirrelValue&) {
		calls.push_back(5);
	});
	vm.NewSlot(owner, std::int32_t{2}, second);
	vm.NewSlot(owner, std::int32_t{3}, third);
	second = {};
	third = {};
	first = {};
	bool caught = false;
	try {
		vm.RethrowReleaseFailure();
	} catch (const std::runtime_error& error) {
		caught = std::string(error.what()) == "Squirrel native release callback depth exceeds limit";
	}

	Require(caught && calls == std::vector<std::int32_t>{1, 2, 3, 4} && !vm.ResolveObjectIdentity(identity), "depth-limited nested callbacks must retain a precise diagnostic and finish storage cleanup without further recursion");
}

static void TestArrayFinalizersPreserveCurrentRawIdentity() {
	for (const bool collection : {false, true}) {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;
		SquirrelVm vm(limits);
		const auto class_value = vm.CreateClass();
		const auto class_root = vm.Retain(class_value);
		SquirrelValue first = vm.CreateInstance(class_value);
		SquirrelValue second = vm.CreateInstance(class_value);
		SquirrelValue parent;
		{
			const std::array<SquirrelValue, 2> children{first, second};
			parent = vm.CreateArray(children);
		}

		const auto parent_identity = vm.BorrowObjectIdentity(parent);
		std::vector<std::int32_t> calls;
		vm.SetInstanceReleaseHook(first, [&](SquirrelVm& current, const SquirrelValue& receiver) {
			const auto array = current.ResolveObjectIdentity(parent_identity);
			Require(array && current.Length(*array) == 2 && current.GetSlot(*array, std::int32_t{0}) == receiver, "first child hook must observe its raw array identity and original logical size during array finalization");
			calls.push_back(1);
		});
		vm.SetInstanceReleaseHook(second, [&](SquirrelVm& current, const SquirrelValue& receiver) {
			const auto array = current.ResolveObjectIdentity(parent_identity);
			Require(array && current.Length(*array) == 2 && current.GetSlot(*array, std::int32_t{1}) == receiver, "second child hook must observe its own preserved raw slot and original array size");
			bool expired_prior_slot = false;
			try {
				static_cast<void>(current.GetSlot(*array, std::int32_t{0}));
			} catch (const std::runtime_error&) {
				expired_prior_slot = true;
			}

			Require(expired_prior_slot, "prior raw array identity must expire safely after its child completes release");
			calls.push_back(2);
		});
		first = {};
		second = {};
		if (collection) {
			static_cast<void>(vm.CollectGarbage());
			Require(vm.Length(parent) == 0, "collection finalization must set logical size to zero after ascending child release");
		}

		parent = {};
		vm.RethrowReleaseFailure();
		Require(calls == std::vector<std::int32_t>{1, 2} && !vm.ResolveObjectIdentity(parent_identity), "reference destruction and collection finalization must preserve child observation order and expire the array once");
	}
}

int main() {
	try {
		TestArrayFinalizersPreserveCurrentRawIdentity();
		TestNestedReleaseCallbackOrder();
		TestNestedReleaseCallbackDepthLimit();
		TestImmediateExpirationAndReuse();
		TestWeakWrapperCacheLifetime();
		TestDeepReleaseIsBounded();
		TestInstanceReleaseAndReplacement();
		TestInstanceResurrection();
		TestReleaseHookExceptions();
		TestArrayReleaseOrder();
		TestReplacementIsVisibleBeforeRelease();
		TestNativeCollectionCorpus();
		TestRootGraphCollection();
		TestBorrowedIdentitiesAndHookReentry();
		TestSavedGeneratorHasNoOwningSelfCycle();
		TestNativeArgumentsAreMarkRoots();
		TestDirectArrayInterop();
		std::cout << "Squirrel lifetime checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
