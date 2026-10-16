#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

using sgre::script::SquirrelObjectIdentity;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected scoped receiver failure: " + std::string(message));
}

static sgre::script::SquirrelVmLimits ReclaimingLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static bool Expired(SquirrelVm& vm, const SquirrelValue& weak) {
	return std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak));
}

static void TestNoStrongReferenceAndSafeExpiration() {
	std::size_t releases = 0;
	SquirrelVm vm(ReclaimingLimits());
	const auto class_value = vm.CreateClass();
	SquirrelValue target = vm.CreateInstance(class_value);
	const auto identity = vm.BorrowObjectIdentity(target);
	const auto weak = vm.CreateWeakReference(target);
	const auto output = vm.CreateTable();
	vm.SetInstanceReleaseHook(target, [&](auto&, const auto&) {
		++releases;
	});
	RequireError([&] {
		vm.DispatchBorrowedReceiver(identity, [&](const SquirrelValue& receiver) {
			Require(vm.Type(receiver) == sgre::script::SquirrelValueType::Instance && vm.BorrowObjectIdentity(receiver) == identity && vm.RetainedReferenceCount(receiver) == 0, "borrow must lend the exact live instance without an external root");
			target = {};
			Require(releases == 1 && Expired(vm, weak), "borrowed callback scope must not delay the actual last-reference release");
			RequireError([&] {
				static_cast<void>(vm.Type(receiver));
			},
			    "does not belong");
			RequireError([&] {
				vm.NewSlot(output, std::string("retired"), receiver);
			},
			    "does not belong");
		});
	},
	    "retired during dispatch");
	Require(!vm.HasSlot(output, std::string("retired")), "a retired borrow must not leak into VM storage");
	vm.RethrowReleaseFailure();
	const auto replacement = vm.CreateInstance(class_value);
	vm.DispatchBorrowedReceiver(vm.BorrowObjectIdentity(replacement), [&](const auto& receiver) {
		Require(vm.Type(receiver) == sgre::script::SquirrelValueType::Instance, "expiration must restore the dispatch guard for a new owner");
	});
}

static void TestNormalCopiedValuesOwn() {
	SquirrelVm vm(ReclaimingLimits());
	const auto class_value = vm.CreateClass();
	SquirrelValue target = vm.CreateInstance(class_value);
	const auto identity = vm.BorrowObjectIdentity(target);
	const auto weak = vm.CreateWeakReference(target);
	SquirrelValue copied;
	SquirrelValue moved_from_const;
	vm.DispatchBorrowedReceiver(identity, [&](const SquirrelValue& receiver) {
		copied = receiver;
		moved_from_const = std::move(receiver);
		target = {};
		Require(!Expired(vm, weak), "ordinary copies and moves from a const borrow must acquire owned references");
	});
	copied = {};
	Require(!Expired(vm, weak), "the independent owned copy must survive scope completion and earlier copy release");
	moved_from_const = {};
	Require(Expired(vm, weak), "normal escaped copies must retire their owner after the actual last copy drops");
	vm.RethrowReleaseFailure();
}

static void TestVmStorageAndHostRootOwn() {
	SquirrelVm vm(ReclaimingLimits());
	const auto class_value = vm.CreateClass();
	SquirrelValue target = vm.CreateInstance(class_value);
	const auto identity = vm.BorrowObjectIdentity(target);
	const auto weak = vm.CreateWeakReference(target);
	const auto table = vm.CreateTable();
	const auto array = vm.CreateArray();
	sgre::script::SquirrelHostRoot root;
	vm.DispatchBorrowedReceiver(identity, [&](const SquirrelValue& receiver) {
		vm.NewSlot(table, std::string("receiver"), receiver);
		vm.AppendArray(array, receiver);
		root = vm.Retain(receiver);
		Require(vm.RetainedReferenceCount(receiver) == 1, "an explicit host root remains an actual separate relation");
		target = {};
	});
	vm.SetSlot(table, std::string("receiver"), {});
	vm.ClearArray(array);
	Require(!Expired(vm, weak), "explicit host ownership must survive all normal stored-reference retirement");
	root = {};
	Require(Expired(vm, weak), "stored and retained copies must leave no non-owning receiver value behind");
	vm.RethrowReleaseFailure();
}

static void TestIdentityStorageRetirementAndReuse() {
	SquirrelVm vm(ReclaimingLimits());
	const auto class_value = vm.CreateClass();
	SquirrelValue target = vm.CreateInstance(class_value);
	auto identity_record = std::make_unique<SquirrelObjectIdentity>(vm.BorrowObjectIdentity(target));
	vm.DispatchBorrowedReceiver(*identity_record, [&](const auto& receiver) {
		identity_record.reset();
		Require(vm.Type(receiver) == sgre::script::SquirrelValueType::Instance, "retiring external identity storage must not invalidate a still-owned receiver");
	});
	const auto index = vm.BorrowObjectIdentity(target).Index;
	identity_record = std::make_unique<SquirrelObjectIdentity>(vm.BorrowObjectIdentity(target));
	SquirrelValue replacement;
	RequireError([&] {
		vm.DispatchBorrowedReceiver(*identity_record, [&](const auto&) {
			identity_record.reset();
			target = {};
			replacement = vm.CreateInstance(class_value);
			Require(vm.BorrowObjectIdentity(replacement).Index == index, "fixture must reuse the retiring owner's exact arena slot during callback");
		});
	},
	    "retired during dispatch");
	vm.DispatchBorrowedReceiver(vm.BorrowObjectIdentity(replacement), [&](const auto& receiver) {
		Require(vm.Type(receiver) == sgre::script::SquirrelValueType::Instance, "old receiver expiration must not invalidate its live slot replacement");
	});
	vm.RethrowReleaseFailure();
}

static void TestInvocationOutputOwnsNormally() {
	SquirrelVm vm(ReclaimingLimits());
	const auto class_value = vm.CreateClass();
	SquirrelValue target = vm.CreateInstance(class_value);
	const auto weak = vm.CreateWeakReference(target);
	const auto identity = vm.BorrowObjectIdentity(target);
	const auto echo = vm.CreateNativeClosure("scoped_receiver.echo", [](auto&, const auto arguments) {
		Require(arguments.size() == 1, "ordinary invocation must receive the real copied receiver");

		return sgre::script::SquirrelNativeResult::ReturnValue(arguments[0]);
	});
	SquirrelValue output;
	vm.DispatchBorrowedReceiver(identity, [&](const SquirrelValue& receiver) {
		const std::array<SquirrelValue, 1> arguments{receiver};
		output = vm.Invoke(echo, arguments).Value;
		vm.ResetExecution();
		target = {};
	});
	Require(!Expired(vm, weak) && vm.Type(output) == sgre::script::SquirrelValueType::Instance, "native invocation output must own a copied receiver outside the borrow");
	output = {};
	Require(Expired(vm, weak), "normal output retirement must expire the instance after invocation storage is reset");
	vm.RethrowReleaseFailure();
}

static void TestIdentityValidationAndSlotReuse() {
	SquirrelVm vm(ReclaimingLimits());
	const auto class_value = vm.CreateClass();
	SquirrelValue target = vm.CreateInstance(class_value);
	const auto identity = vm.BorrowObjectIdentity(target);
	std::size_t operations = 0;
	const auto operation = [&](const auto&) {
		++operations;
	};
	RequireError([&] {
		vm.DispatchBorrowedReceiver({}, operation);
	},
	    "does not belong");
	RequireError([&] {
		vm.DispatchBorrowedReceiver(identity, {});
	},
	    "concrete operation");
	SquirrelVm foreign;
	const auto foreign_identity = foreign.BorrowObjectIdentity(foreign.CreateInstance(foreign.CreateClass()));
	RequireError([&] {
		vm.DispatchBorrowedReceiver(foreign_identity, operation);
	},
	    "does not belong");
	const auto table = vm.CreateTable();
	RequireError([&] {
		vm.DispatchBorrowedReceiver(vm.BorrowObjectIdentity(table), operation);
	},
	    "instance identity");
	auto invalid_generation = identity;
	++invalid_generation.Generation;
	RequireError([&] {
		vm.DispatchBorrowedReceiver(invalid_generation, operation);
	},
	    "has retired");
	target = {};
	const auto replacement = vm.CreateInstance(class_value);
	const auto replacement_identity = vm.BorrowObjectIdentity(replacement);
	Require(replacement_identity.Index == identity.Index && replacement_identity.Generation != identity.Generation, "fixture must reuse a retired object slot with another generation");
	RequireError([&] {
		vm.DispatchBorrowedReceiver(identity, operation);
	},
	    "has retired");
	auto forged = identity;
	forged.Index = replacement_identity.Index;
	forged.Generation = replacement_identity.Generation;
	forged.Owner = replacement_identity.Owner;
	RequireError([&] {
		vm.DispatchBorrowedReceiver(forged, operation);
	},
	    "has retired");
	SquirrelObjectIdentity destroyed_owner;
	{
		SquirrelVm temporary;
		destroyed_owner = temporary.BorrowObjectIdentity(temporary.CreateInstance(temporary.CreateClass()));
	}
	RequireError([&] {
		vm.DispatchBorrowedReceiver(destroyed_owner, operation);
	},
	    "does not belong");
	Require(operations == 0, "invalid identity or operation must reject before any receiver callback");
	vm.Close();
	RequireError([&] {
		vm.DispatchBorrowedReceiver(replacement_identity, operation);
	},
	    "live VM");
}

static void TestReentryCloseAndExceptionGuards() {
	SquirrelVm vm(ReclaimingLimits());
	const auto class_value = vm.CreateClass();
	const auto target = vm.CreateInstance(class_value);
	const auto identity = vm.BorrowObjectIdentity(target);
	std::size_t calls = 0;
	vm.DispatchBorrowedReceiver(identity, [&](const auto& receiver) {
		RequireError([&] {
			vm.DispatchBorrowedReceiver(identity, [&](const auto&) {
				++calls;
			});
		},
		    "cannot reenter");
		RequireError([&] {
			vm.Close();
		},
		    "borrowed receiver dispatch");
		Require(!vm.IsClosed() && vm.Type(receiver) == sgre::script::SquirrelValueType::Instance, "shutdown rejection must leave VM and receiver unchanged");
	});
	RequireError([&] {
		vm.DispatchBorrowedReceiver(identity, [](const auto&) {
			throw std::runtime_error("receiver callback sentinel");
		});
	},
	    "callback sentinel");
	vm.DispatchBorrowedReceiver(identity, [&](const auto&) {
		++calls;
	});
	Require(calls == 1, "callback and nested-operation failures must restore the scope guard");
	vm.Close();
	Require(vm.IsClosed(), "scope completion must allow ordinary VM Close");
}

static void TestOriginalFailureAndRetirementReentry() {
	std::size_t blocked = 0;
	SquirrelVm vm(ReclaimingLimits());
	const auto class_value = vm.CreateClass();
	SquirrelValue target = vm.CreateInstance(class_value);
	const auto identity = vm.BorrowObjectIdentity(target);
	vm.SetInstanceReleaseHook(target, [&](auto&, const auto&) {
		RequireError([&] {
			vm.DispatchBorrowedReceiver(identity, [](const auto&) {
				throw std::runtime_error("retiring receiver callback must not run");
			});
		},
		    "retirement");
		++blocked;
	});
	RequireError([&] {
		vm.DispatchBorrowedReceiver(identity, [&](const auto&) {
			target = {};
			throw std::runtime_error("first callback failure");
		});
	},
	    "first callback failure");
	Require(blocked == 1 && !vm.ResolveObjectIdentity(identity), "original callback failure must remain first while real native release still completes");
	vm.RethrowReleaseFailure();
	const auto replacement = vm.CreateInstance(class_value);
	vm.DispatchBorrowedReceiver(vm.BorrowObjectIdentity(replacement), [&](const auto&) {
		++blocked;
	});
	Require(blocked == 2, "failed expiration must leave dispatch available for other owners");
}

static void TestCollectionAndRealMarkRoots() {
	SquirrelVm vm(ReclaimingLimits());
	const auto class_value = vm.CreateClass();
	const auto class_root = vm.Retain(class_value);
	vm.NewSlot(class_value, std::string("self"), {});
	SquirrelValue target = vm.CreateInstance(class_value);
	vm.SetSlot(target, std::string("self"), target);
	const auto identity = vm.BorrowObjectIdentity(target);
	const auto weak = vm.CreateWeakReference(target);
	target = {};
	RequireError([&] {
		vm.DispatchBorrowedReceiver(identity, [&](const auto& receiver) {
			Require(vm.CollectGarbage() == 1 && Expired(vm, weak), "borrowed receiver dispatch must not introduce a collector mark root for an actual cycle");
			RequireError([&] {
				static_cast<void>(vm.Type(receiver));
			},
			    "does not belong");
		});
	},
	    "retired during dispatch");
	vm.RethrowReleaseFailure();
	SquirrelValue stored = vm.CreateInstance(class_value);
	const auto stored_identity = vm.BorrowObjectIdentity(stored);
	const auto stored_weak = vm.CreateWeakReference(stored);
	vm.DispatchBorrowedReceiver(stored_identity, [&](const auto& receiver) {
		vm.NewSlot(vm.RootTable(), std::string("stored_receiver"), receiver);
		stored = {};
		Require(vm.CollectGarbage() == 0 && !Expired(vm, stored_weak), "an actual VM root-table store must acquire and mark a borrowed receiver normally");
	});
	vm.SetSlot(vm.RootTable(), std::string("stored_receiver"), {});
	Require(Expired(vm, stored_weak), "borrow dispatch must not retain an owner after its actual marked reference is removed");
	vm.RethrowReleaseFailure();
}

static void TestCollectorGateUnchanged() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	const auto target = vm.CreateInstance(class_value);
	vm.DispatchBorrowedReceiver(vm.BorrowObjectIdentity(target), [&](const auto& receiver) {
		RequireError([&] {
			static_cast<void>(vm.CollectGarbage());
		},
		    "disabled");
		Require(vm.Type(receiver) == sgre::script::SquirrelValueType::Instance, "disabled collection must preserve the currently lent receiver");
	});
	RequireError([&] {
		static_cast<void>(vm.CollectGarbage());
	},
	    "disabled");
}

int main() {
	try {
		TestNoStrongReferenceAndSafeExpiration();
		TestNormalCopiedValuesOwn();
		TestVmStorageAndHostRootOwn();
		TestIdentityStorageRetirementAndReuse();
		TestInvocationOutputOwnsNormally();
		TestIdentityValidationAndSlotReuse();
		TestReentryCloseAndExceptionGuards();
		TestOriginalFailureAndRetirementReentry();
		TestCollectionAndRealMarkRoots();
		TestCollectorGateUnchanged();
		std::cout << "10 scoped non-owning receiver groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
