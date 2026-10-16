#include "sgre/script/squirrel_vm.hpp"

#include "sgre/script/object_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestExplicitCloseOrderAndExpiration() {
	std::vector<std::int32_t> calls;
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("self"), {});
	const auto class_root = vm.Retain(class_value);
	SquirrelValue root_owned = vm.CreateInstance(class_value);
	SquirrelValue cyclic = vm.CreateInstance(class_value);
	const auto root_identity = vm.BorrowObjectIdentity(root_owned);
	const auto cycle_identity = vm.BorrowObjectIdentity(cyclic);
	vm.SetSlot(cyclic, std::string("self"), cyclic);
	vm.SetInstanceReleaseHook(root_owned, [&](SquirrelVm& current, const SquirrelValue& receiver) {
		Require(std::holds_alternative<std::monostate>(current.RootTable()), "root VM clears its root cell before releasing the table's children");
		Require(current.IsInstanceOf(receiver, class_value), "root-owned instance hook must precede class release");
		calls.push_back(1);
	});
	vm.SetInstanceReleaseHook(cyclic, [&](SquirrelVm& current, const SquirrelValue& receiver) {
		Require(std::holds_alternative<std::monostate>(current.RootTable()), "remaining collectable finalization must follow root VM finalization");
		Require(current.IsInstanceOf(receiver, class_value) && current.GetSlot(receiver, std::string("self")) == receiver, "remaining instance hook must precede class and field clearing");
		calls.push_back(2);
	});
	vm.NewSlot(vm.RootTable(), std::string("root_owned"), root_owned);
	root_owned = {};
	cyclic = {};
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(vm.IsClosed() && calls == std::vector<std::int32_t>{1, 2}, "close must finalize root-owned objects before remaining cycles");
	Require(!vm.ResolveObjectIdentity(root_identity) && !vm.ResolveObjectIdentity(cycle_identity), "close must expire borrowed identities even when scoped roots remain outside the VM");
	bool root_expired = false;
	try {
		static_cast<void>(class_root.Value());
	} catch (const std::runtime_error&) {
		root_expired = true;
	}

	Require(root_expired, "close must expire external scoped-root access");
	bool closed_rejected = false;
	try {
		static_cast<void>(vm.CreateArray());
	} catch (const std::runtime_error&) {
		closed_rejected = true;
	}

	Require(closed_rejected, "closed VM must reject new object allocation");
	vm.Close();
	Require(calls.size() == 2, "close must be idempotent");
}

static void TestDestructorUsesLiveVmInterface() {
	std::size_t calls = 0;
	{
		SquirrelVm vm;
		const auto class_value = vm.CreateClass();
		vm.NewSlot(class_value, std::string("self"), {});
		SquirrelValue instance = vm.CreateInstance(class_value);
		vm.SetSlot(instance, std::string("self"), instance);
		vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue& receiver) {
			Require(current.Type(receiver) == sgre::script::SquirrelValueType::Instance, "destructor close callback must retain a valid VM implementation until it returns");
			++calls;
		});
		instance = {};
	}

	Require(calls == 1, "VM destructor must perform remaining instance finalization");
}

static void TestCloseCapturesCallbackFailure() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	SquirrelValue instance = vm.CreateInstance(class_value);
	vm.SetInstanceReleaseHook(instance, [](SquirrelVm&, const SquirrelValue&) {
		throw std::runtime_error("close callback failure");
	});
	vm.Close();
	bool caught = false;
	try {
		vm.RethrowReleaseFailure();
	} catch (const std::runtime_error& error) {
		caught = std::string(error.what()) == "close callback failure";
	}

	Require(caught && vm.IsClosed(), "close must complete safely and retain explicit callback diagnostics");
}

static void TestContextResultDropsBeforeRemainingObjects() {
	std::vector<std::int32_t> calls;
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	SquirrelValue result_instance = vm.CreateInstance(class_value);
	const auto result_identity = vm.BorrowObjectIdentity(result_instance);
	vm.SetInstanceReleaseHook(result_instance, [&](SquirrelVm&, const SquirrelValue&) {
		calls.push_back(1);
	});
	const auto echo = vm.CreateNativeClosure("close_result_fixture", [](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		sgre::script::SquirrelNativeResult result;
		result.Value = arguments[1];

		return result;
	});
	{
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), result_instance};
		static_cast<void>(vm.Invoke(echo, arguments));
	}

	result_instance = {};
	Require(vm.ResolveObjectIdentity(result_identity).has_value(), "completed context must still own its result before close");
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(calls == std::vector<std::int32_t>{1} && !vm.ResolveObjectIdentity(result_identity), "close must release owned context results and their native instance hook");
}

static void TestChildCleanupCanReleaseItsContextDuringClose() {
	std::size_t calls = 0;
	SquirrelVm vm;
	const auto child = vm.CreateContext();
	const auto class_value = vm.CreateClass();
	SquirrelValue instance = vm.CreateInstance(class_value);
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue&) {
		current.ReleaseContext(child);
		++calls;
	});
	const auto echo = vm.CreateNativeClosure("child_close_result", [](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		sgre::script::SquirrelNativeResult result;
		result.Value = arguments[1];

		return result;
	});
	{
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), instance};
		static_cast<void>(vm.InvokeInContext(child, echo, arguments));
	}

	instance = {};
	const auto root = vm.RootContext();
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(calls == 1 && vm.IsClosed(), "child context cleanup must safely release the same context being cleared under its close guard");
	for (const auto handle : {child, root}) {
		bool rejected = false;
		try {
			static_cast<void>(vm.ContextState(handle));
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected, "all context handles must be invalid after close cleanup completes");
	}
}

static void TestMigratedNativeObjectClose() {
	std::vector<std::int32_t> calls;
	SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	const auto derived = vm.CreateClass(objects.ObjectClass());
	const auto destructor = vm.CreateNativeClosure("close_native_object_destructor", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 1 && objects.HasInstance(arguments[0]), "script destructor must observe the native Object base before native owner release");
		Require(std::holds_alternative<std::monostate>(current.RootTable()), "native Object destructor must follow root-cell clearing");
		calls.push_back(1);

		return sgre::script::SquirrelNativeResult{};
	});
	vm.NewSlot(derived, std::string("destructor"), destructor);
	SquirrelValue instance = vm.CreateInstance(derived);
	objects.AttachInstance(instance);
	const auto identity = vm.BorrowObjectIdentity(instance);
	const auto listener = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(listener);
	objects.Subscribe(instance, listener, [&](const SquirrelValue& receiver) {
		Require(objects.HasInstance(receiver), "completion notification must precede native Object record removal");
		calls.push_back(3);

		return false;
	});
	objects.ConfigureNativeRelease(instance, [&](const SquirrelValue& receiver) {
		calls.push_back(2);
		objects.ReleaseInstance(receiver);
		Require(!objects.HasInstance(receiver), "native owner cleanup must retire its Object base record during the release hook");
	});
	vm.NewSlot(vm.RootTable(), std::string("native_object"), instance);
	instance = {};
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(calls == std::vector<std::int32_t>{1, 2, 3} && !vm.ResolveObjectIdentity(identity), "migrated Object close must preserve script destructor, native cleanup, completion and identity-expiration order");
}

static void TestEarlyCloseAllowsNativeHostRoots() {
	sgre::script::SquirrelVmLimits limits;
	limits.MaxObjects = 96;
	SquirrelVm vm(limits);
	std::vector<sgre::script::SquirrelHostRoot> roots;
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("member"), std::int32_t{71});
	SquirrelValue instance = vm.CreateInstance(class_value);
	const auto identity = vm.BorrowObjectIdentity(instance);
	std::size_t calls = 0;
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue& receiver) {
		Require(std::holds_alternative<std::monostate>(current.RootTable()), "native shutdown AddRef must remain valid after root clearing");
		roots.push_back(current.Retain(receiver));
		roots.push_back(current.Retain(receiver));
		Require(current.RetainedReferenceCount(receiver) == 2, "close callback duplicate AddRefs must retain one shared value with two explicit counts");
		for (std::int32_t index = 0; index < 20; ++index) {
			const auto array = current.CreateArray();
			current.AppendArray(array, index);
			roots.push_back(current.Retain(array));
		}

		Require(current.IsInstanceOf(receiver, class_value) && std::get<std::int32_t>(current.GetSlot(receiver, std::string("member"))) == 71, "reference-release resurrection must preserve class and fields during the callback");
		++calls;
	});
	vm.NewSlot(vm.RootTable(), std::string("native_root_creation"), instance);
	instance = {};
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(calls == 1 && roots.size() == 22 && !vm.ResolveObjectIdentity(identity), "shutdown must retire all roots created by earlier release callbacks before expiring identities");
	for (const auto& root : roots) {
		bool expired = false;
		try {
			static_cast<void>(root.Value());
		} catch (const std::runtime_error&) {
			expired = true;
		}

		Require(expired, "callback-created scoped roots must not outlive a closed VM registry");
	}

	roots.clear();
}

static void TestCloseRestrictionsRemainExplicit() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	SquirrelValue instance = vm.CreateInstance(class_value);
	bool close_rejected = false;
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue&) {
		try {
			current.Close();
		} catch (const std::runtime_error& error) {
			close_rejected = std::string(error.what()) == "Squirrel cannot close during execution, release or collection";
		}
	});
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(close_rejected, "unsupported reentrant close must fail explicitly");
	bool retain_rejected = false;
	try {
		static_cast<void>(vm.Retain(std::int32_t{0}));
	} catch (const std::runtime_error& error) {
		retain_rejected = std::string(error.what()) == "Squirrel cannot retain host roots after shutdown reference retirement begins";
	}

	Require(retain_rejected, "new roots after reference-registry retirement must fail explicitly");
}

static void TestShutdownAllocationWorkLimit() {
	sgre::script::SquirrelVmLimits limits;
	limits.MaxObjects = 16;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	SquirrelValue instance = vm.CreateInstance(class_value);
	std::size_t allocations = 0;
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue&) {
		for (std::size_t index = 0; index < limits.MaxObjects * 2; ++index) {
			const auto array = current.CreateArray();
			++allocations;
		}
	});
	vm.NewSlot(vm.RootTable(), std::string("allocating_finalizer"), instance);
	instance = {};
	vm.Close();
	bool caught = false;
	try {
		vm.RethrowReleaseFailure();
	} catch (const std::runtime_error& error) {
		caught = std::string(error.what()) == "Squirrel shutdown allocation work exceeds limit";
	}

	Require(caught && allocations == limits.MaxObjects && vm.IsClosed(), "shutdown must bound cumulative callback-created allocations even when each allocation immediately frees its arena slot");
}

static void TestSiblingReleaseDuringObjectCompletion() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	const auto owner_class = vm.CreateClass(objects.ObjectClass());
	const auto owner_slots = vm.CreateTable();
	SquirrelValue first = vm.CreateInstance(owner_class);
	SquirrelValue second = vm.CreateInstance(owner_class);
	SquirrelValue listener = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(first);
	objects.AttachInstance(second);
	objects.AttachInstance(listener);
	const auto first_identity = vm.BorrowObjectIdentity(first);
	const auto second_identity = vm.BorrowObjectIdentity(second);
	const auto listener_identity = vm.BorrowObjectIdentity(listener);
	std::vector<std::int32_t> calls;
	objects.ConfigureNativeRelease(second, [&](const SquirrelValue& receiver) {
		const auto outer = vm.ResolveObjectIdentity(first_identity);
		Require(outer && objects.HasInstance(*outer), "sibling cleanup must observe the releasing Object while its completion callback is active");
		Require(std::holds_alternative<std::monostate>(vm.GetSlot(owner_slots, std::string("second"))), "sibling native cleanup must observe replacement before old owner release");
		calls.push_back(3);
		objects.ReleaseInstance(receiver);
	});
	objects.Subscribe(first, listener, [&](const SquirrelValue& receiver) {
		Require(objects.HasInstance(receiver), "completion callback lost its source Object base state");
		calls.push_back(2);
		vm.SetSlot(owner_slots, std::string("second"), {});
		Require(!vm.ResolveObjectIdentity(second_identity), "sibling native owner and Object base must finish release before completion resumes");
		calls.push_back(4);

		return false;
	});
	objects.ConfigureNativeRelease(first, [&](const SquirrelValue& receiver) {
		calls.push_back(1);
		objects.ReleaseInstance(receiver);
		Require(!objects.HasInstance(receiver), "derived cleanup must finish its base Object notification and remove the base record");
		calls.push_back(5);
	});
	vm.NewSlot(owner_slots, std::string("second"), second);
	second = {};
	listener = {};
	first = {};
	vm.RethrowReleaseFailure();
	Require(calls == std::vector<std::int32_t>{1, 2, 3, 4, 5}, "derived owner, completion and synchronous sibling release order changed");
	Require(!vm.ResolveObjectIdentity(first_identity) && !vm.ResolveObjectIdentity(listener_identity), "base notification must release its subscriber array listener and both borrowed native identities");
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(calls.size() == 5, "already completed sibling native owners must not release twice during VM close");
}

static void TestNativeOwnerSurvivesEarlierClassFinalization() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	const auto attributes = vm.CreateTable();
	const auto derived = vm.CreateClass(objects.ObjectClass(), attributes);
	const auto instance = vm.CreateInstance(derived);
	objects.AttachInstance(instance);
	vm.NewSlot(attributes, std::string("native_owner"), instance);
	vm.NewSlot(vm.RootTable(), std::string("derived_native_class"), derived);
	bool retired_ancestry = false;
	std::size_t releases = 0;
	objects.ConfigureNativeRelease(instance, [&](const SquirrelValue& receiver) {
		retired_ancestry = !vm.IsInstanceOf(receiver, objects.ObjectClass());
		Require(objects.HasInstance(receiver), "registered native base ownership must survive earlier script Class.ParentClass finalization");
		objects.ReleaseInstance(receiver);
		++releases;
	});
	static_cast<void>(vm.CollectGarbage());
	Require(vm.IsInstanceOf(instance, objects.ObjectClass()) && objects.HasInstance(instance), "live derived native owner must remain valid after root marking");
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(retired_ancestry && releases == 1, "class-before-instance close fixture must exercise actual retired ancestry and retained native base cleanup");
}

int main() {
	try {
		TestNativeOwnerSurvivesEarlierClassFinalization();
		TestSiblingReleaseDuringObjectCompletion();
		TestShutdownAllocationWorkLimit();
		TestExplicitCloseOrderAndExpiration();
		TestDestructorUsesLiveVmInterface();
		TestCloseCapturesCallbackFailure();
		TestContextResultDropsBeforeRemainingObjects();
		TestChildCleanupCanReleaseItsContextDuringClose();
		TestMigratedNativeObjectClose();
		TestEarlyCloseAllowsNativeHostRoots();
		TestCloseRestrictionsRemainExplicit();
		std::cout << "Squirrel close checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
