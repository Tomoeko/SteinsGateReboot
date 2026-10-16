#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>

using sgre::script::SquirrelContextState;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestScalarReplacementDoesNotExecute() {
	std::size_t handler_calls = 0;
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto root = vm.RootTable();
	const auto handler = vm.CreateNativeClosure("unused_error_assignment_handler", [&](SquirrelVm&, const auto) {
		++handler_calls;

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 2> setup{root, handler};
	static_cast<void>(vm.InvokeWithoutResultInContext(vm.RootContext(), vm.GetSlot(root, std::string("seterrorhandler")), setup));
	vm.SetLastError(std::int32_t{42});
	Require(std::get<std::int32_t>(vm.LastError()) == 42 && handler_calls == 0 && vm.ContextState(vm.RootContext()) == SquirrelContextState::Idle && vm.RootTable() == root, "native error replacement must change only the error cell without dispatch, wrapping or execution");
	vm.SetLastError({});
	Require(vm.Type(vm.LastError()) == SquirrelValueType::Null, "a supplied null error must replace the previous scalar directly");
}

static void TestCanonicalStringOwnership() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue text = vm.CreateString("first-party-native-diagnostic");
	const auto weak = vm.CreateWeakReference(text);
	vm.SetLastError(std::string("first-party-native-diagnostic"));
	Require(std::get<sgre::script::SquirrelString>(vm.LastError()).SharesIdentity(std::get<sgre::script::SquirrelString>(text)), "native diagnostic strings must use the current VM's canonical immutable string identity");
	text = {};
	Require(vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::String, "the last-error cell must remain the actual owner of its canonical string");
	vm.SetLastError({});
	Require(vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null, "error replacement must release its former canonical string owner");
}

static void TestReplacementBeforeOldRelease(bool reentrant) {
	std::size_t release_calls = 0;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const std::array<std::uint8_t, 1> bytes{37};
	SquirrelValue old = vm.CreateUserData(bytes);
	vm.SetUserDataReleaseHook(old, [&](SquirrelVm& active, const SquirrelValue&, const auto payload) {
		++release_calls;
		Require(payload.size() == 1 && payload[0] == 37 && std::get<std::int32_t>(active.LastError()) == 42, "native error replacement must install the new value before releasing the old payload");
		if (reentrant) {
			active.SetLastError(std::int32_t{73});
		}
	});
	const auto weak = vm.CreateWeakReference(old);
	vm.SetLastError(old);
	old = {};
	vm.SetLastError(std::int32_t{42});
	vm.RethrowReleaseFailure();
	Require(release_calls == 1 && vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null && std::get<std::int32_t>(vm.LastError()) == (reentrant ? 73 : 42), "old error release must complete without overriding a callback's later replacement");
}

static void TestSelfAliasPreservesOwner() {
	std::size_t releases = 0;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const std::array<std::uint8_t, 1> bytes{9};
	SquirrelValue value = vm.CreateUserData(bytes);
	vm.SetUserDataReleaseHook(value, [&](SquirrelVm&, const SquirrelValue&, const auto) {
		++releases;
	});
	vm.SetLastError(value);
	value = {};
	SquirrelValue alias = vm.LastError();
	const auto references = vm.RetainedReferenceCount(alias);
	vm.SetLastError(alias);
	Require(vm.RetainedReferenceCount(alias) == references && releases == 0, "self-aliased error replacement must acquire the incoming owner before retiring the old one");
	alias = {};
	vm.SetLastError({});
	vm.RethrowReleaseFailure();
	Require(releases == 1, "clearing the final error owner must release its payload exactly once");
}

static void TestWeakErrorRemainsRaw() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue target = vm.CreateArray();
	const auto weak = vm.CreateWeakReference(target);
	const auto identity = vm.BorrowObjectIdentity(weak);
	vm.SetLastError(weak);
	target = {};
	Require(vm.Type(vm.LastError()) == SquirrelValueType::WeakReference && vm.BorrowObjectIdentity(vm.LastError()) == identity && vm.Type(vm.ResolveWeakReference(vm.LastError())) == SquirrelValueType::Null, "native error assignment must retain the actual weak wrapper without resolving or retaining its target");
}

static void TestIndependentContextAndCollectorOwnership() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto child = vm.CreateContext();
	vm.SetLastError(std::int32_t{91});
	SquirrelValue cycle = vm.CreateArray();
	vm.AppendArray(cycle, cycle);
	const auto weak = vm.Retain(vm.CreateWeakReference(cycle));
	vm.SetLastErrorInContext(child, cycle);
	cycle = {};
	Require(vm.CollectGarbage() == 0 && vm.Type(vm.ResolveWeakReference(weak.Value())) == SquirrelValueType::Array && std::get<std::int32_t>(vm.LastError()) == 91, "selected child error must be independently owned and marked without changing the global error");
	vm.ReleaseContext(child);
	Require(vm.CollectGarbage() == 1 && vm.Type(vm.ResolveWeakReference(weak.Value())) == SquirrelValueType::Null, "retiring the child must release its selected error and allow real cycle reclamation");
}

static void TestCurrentAndExplicitGlobalSelection() {
	SquirrelVm vm;
	const auto child = vm.CreateContext();
	const auto callback = vm.CreateNativeClosure("selected_native_error", [&](SquirrelVm& active, const auto) {
		const auto top = active.NativeStackSize();
		active.SetLastError(std::int32_t{41});
		active.SetLastErrorInContext(active.RootContext(), std::int32_t{73});
		Require(active.CurrentContext() == child && active.ContextState(child) == SquirrelContextState::Running && active.NativeStackSize() == top && std::get<std::int32_t>(active.LastError()) == 41 && std::get<std::int32_t>(active.LastErrorInContext(active.RootContext())) == 73, "explicit global diagnostic replacement must preserve a concurrently active child's frame, error and stack");

		return SquirrelNativeResult::StackStatus(-1);
	});
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	bool failed = false;
	try {
		static_cast<void>(vm.InvokeInContext(child, callback, receiver));
	} catch (const sgre::script::SquirrelVmException& error) {
		failed = std::get<std::int32_t>(error.ThrownValue()) == 41;
	}
	Require(failed && std::get<std::int32_t>(vm.LastError()) == 73 && std::get<std::int32_t>(vm.LastErrorInContext(child)) == 41, "negative native status must use the selected raw error while preserving the other context's diagnostic");
	vm.ReleaseContext(child);
}

static void TestSelectedErrorReleasePreservesCaller(bool child_caller) {
	std::size_t releases = 0;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto child = vm.CreateContext();
	const auto caller = child_caller ? child : vm.RootContext();
	const auto target = child_caller ? vm.RootContext() : child;
	vm.SetLastErrorInContext(caller, std::int32_t{17});
	const std::array<std::uint8_t, 1> bytes{9};
	SquirrelValue old = vm.CreateUserData(bytes);
	vm.SetUserDataReleaseHook(old, [&](SquirrelVm& active, const SquirrelValue&, const auto) {
		++releases;
		Require(active.CurrentContext() == caller && std::get<std::int32_t>(active.LastError()) == 17 && std::get<std::int32_t>(active.LastErrorInContext(target)) == 42, "selected error retirement must execute its release callback in the existing caller context rather than changing execution to the target");
		if (child_caller) {
			Require(active.ContextState(child) == SquirrelContextState::Running && active.NativeStackSize() == 1, "selected global error release must preserve the running child callback and physical stack");
		}

		active.SetLastError(std::int32_t{91});
	});
	vm.SetLastErrorInContext(target, old);
	old = {};
	if (child_caller) {
		const auto replace = vm.CreateNativeClosure("global_error_from_child", [&](SquirrelVm& active, const auto) {
			active.SetLastErrorInContext(target, std::int32_t{42});

			return SquirrelNativeResult::NoValues();
		});
		const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
		static_cast<void>(vm.InvokeWithoutResultInContext(child, replace, arguments));
	} else {
		vm.SetLastErrorInContext(target, std::int32_t{42});
	}

	vm.RethrowReleaseFailure();
	Require(releases == 1 && std::get<std::int32_t>(vm.LastErrorInContext(caller)) == 91 && std::get<std::int32_t>(vm.LastErrorInContext(target)) == 42 && vm.CurrentContext() == vm.RootContext(), "caller reentry must replace its own diagnostic while preserving the selected target's new error");
	vm.ReleaseContext(child);
}

static void TestInvalidOwnersAndRetiredContexts() {
	SquirrelVm vm;
	SquirrelVm foreign;
	vm.SetLastError(std::int32_t{17});
	const auto child = vm.CreateContext();
	vm.ReleaseContext(child);
	std::size_t failures = 0;
	for (const auto operation : {0, 1, 2}) {
		try {
			if (operation == 0) {
				vm.SetLastError(foreign.RootTable());
			} else if (operation == 1) {
				vm.SetLastError(foreign.CreateString("foreign-owned-error"));
			} else {
				vm.SetLastErrorInContext(child, std::int32_t{23});
			}
		} catch (const std::runtime_error&) {
			++failures;
		}
	}
	Require(failures == 3 && std::get<std::int32_t>(vm.LastError()) == 17, "invalid diagnostic owners and retired contexts must reject before replacing an existing error");
	vm.Close();
	bool closed_failure = false;
	try {
		vm.SetLastError(std::int32_t{29});
	} catch (const std::runtime_error&) {
		closed_failure = true;
	}
	Require(closed_failure && vm.Type(vm.LastError()) == SquirrelValueType::Null, "closed VM error replacement must reject without reviving context ownership");
}

int main() {
	try {
		TestScalarReplacementDoesNotExecute();
		TestCanonicalStringOwnership();
		TestReplacementBeforeOldRelease(false);
		TestReplacementBeforeOldRelease(true);
		TestSelfAliasPreservesOwner();
		TestWeakErrorRemainsRaw();
		TestIndependentContextAndCollectorOwnership();
		TestCurrentAndExplicitGlobalSelection();
		TestSelectedErrorReleasePreservesCaller(false);
		TestSelectedErrorReleasePreservesCaller(true);
		TestInvalidOwnersAndRetiredContexts();
		std::cout << "eleven independent native error assignment fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
