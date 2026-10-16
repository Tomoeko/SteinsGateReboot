#include "sgre/script/squirrel_vm.hpp"

#include "sgre/script/layer_bindings.hpp"

#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

	throw std::runtime_error("expected nested receiver failure");
}

static sgre::script::SquirrelVmLimits Limits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static bool Expired(SquirrelVm& vm, const SquirrelValue& weak) {
	return std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak));
}

static void TestDistinctReceiversAndCycleRejection() {
	SquirrelVm vm(Limits());
	const auto class_value = vm.CreateClass();
	const auto parent = vm.CreateInstance(class_value);
	const auto child = vm.CreateInstance(class_value);
	const auto parent_identity = vm.BorrowObjectIdentity(parent);
	const auto child_identity = vm.BorrowObjectIdentity(child);
	std::vector<int> calls;
	vm.DispatchBorrowedReceiver(parent_identity, [&](const auto& parent_receiver) {
		calls.push_back(1);
		vm.DispatchBorrowedReceiver(child_identity, [&](const auto& child_receiver) {
			calls.push_back(2);
			Require(vm.BorrowObjectIdentity(parent_receiver) == parent_identity && vm.BorrowObjectIdentity(child_receiver) == child_identity, "distinct native owner scopes must preserve both exact borrowed identities");
			RequireError([&] {
				vm.DispatchBorrowedReceiver(parent_identity, [&](const auto&) {
					calls.push_back(3);
				});
			},
			    "same receiver");
			RequireError([&] {
				vm.Close();
			},
			    "borrowed receiver dispatch");
		});
		calls.push_back(4);
	});
	Require(calls == std::vector<int>{1, 2, 4}, "same-receiver recursion must reject without disturbing distinct LIFO traversal");
	vm.Close();
}

static void TestActualDistinctChildManagers() {
	sgre::scene::LayerTree tree(8);
	SquirrelVm vm(Limits());
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	const auto parent = vm.Invoke(layers.LayerFolderClass(), receiver).Value;
	vm.ResetExecution();
	const std::array<SquirrelValue, 2> child_arguments{vm.RootTable(), parent};
	const auto child = vm.Invoke(layers.LayerFolderClass(), child_arguments).Value;
	vm.ResetExecution();
	const auto parent_handle = layers.Handle(parent);
	const auto child_handle = layers.Handle(child);
	const auto parent_identity = vm.BorrowObjectIdentity(parent);
	const auto child_identity = vm.BorrowObjectIdentity(child);
	std::vector<sgre::scene::LayerHandle> calls;
	tree.VisitChildren({}, [&](const auto outer) {
		Require(outer == parent_handle, "fixture root manager must contain the actual folder owner");
		vm.DispatchBorrowedReceiver(parent_identity, [&](const auto& parent_receiver) {
			Require(layers.Handle(parent_receiver) == outer, "borrowed dispatch must resolve actual existing Layer binding state without another registry");
			calls.push_back(outer);
			tree.VisitChildren(parent_handle, [&](const auto inner) {
				Require(inner == child_handle, "fixture child manager must contain the actual existing child folder");
				vm.DispatchBorrowedReceiver(child_identity, [&](const auto& child_receiver) {
					Require(layers.Handle(child_receiver) == inner && layers.Handle(parent_receiver) == outer, "distinct native child managers must nest borrowed receiver scopes while parent state remains valid");
					calls.push_back(inner);
				});
			});
		});
	});
	Require(calls == std::vector<sgre::scene::LayerHandle>{parent_handle, child_handle}, "actual owner list and VM receiver scope nesting must share registration order");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNestedLastReferenceAndFailureRestoration() {
	std::size_t releases = 0;
	SquirrelVm vm(Limits());
	const auto class_value = vm.CreateClass();
	const auto parent = vm.CreateInstance(class_value);
	SquirrelValue child = vm.CreateInstance(class_value);
	const auto child_identity = vm.BorrowObjectIdentity(child);
	const auto weak = vm.CreateWeakReference(child);
	vm.SetInstanceReleaseHook(child, [&](auto&, const auto&) {
		++releases;
	});
	vm.DispatchBorrowedReceiver(vm.BorrowObjectIdentity(parent), [&](const auto& parent_receiver) {
		RequireError([&] {
			vm.DispatchBorrowedReceiver(child_identity, [&](const auto&) {
				child = {};
				Require(releases == 1 && Expired(vm, weak), "nested dispatch must not add a strong receiver root");
			});
		},
		    "retired during dispatch");
		Require(vm.Type(parent_receiver) == sgre::script::SquirrelValueType::Instance, "child expiry must restore the still-live parent scope");
		const auto replacement = vm.CreateInstance(class_value);
		RequireError([&] {
			vm.DispatchBorrowedReceiver(vm.BorrowObjectIdentity(replacement), [](const auto&) {
				throw std::runtime_error("nested callback sentinel");
			});
		},
		    "nested callback sentinel");
		vm.DispatchBorrowedReceiver(vm.BorrowObjectIdentity(replacement), [&](const auto&) {
			++releases;
		});
	});
	Require(releases == 2, "nested exception must restore the child depth before a subsequent distinct receiver");
	vm.RethrowReleaseFailure();
}

static void TestBoundedDepth() {
	SquirrelVm vm(Limits());
	const auto class_value = vm.CreateClass();
	std::vector<SquirrelValue> receivers;
	std::vector<SquirrelObjectIdentity> identities;
	for (std::size_t index = 0; index != 65; ++index) {
		receivers.push_back(vm.CreateInstance(class_value));
		identities.push_back(vm.BorrowObjectIdentity(receivers.back()));
	}
	std::size_t entries = 0;
	std::function<void(std::size_t)> descend;
	descend = [&](std::size_t index) {
		vm.DispatchBorrowedReceiver(identities[index], [&](const auto&) {
			++entries;
			if (index + 1 == identities.size()) {
				throw std::runtime_error("depth guard failed before sixty-fifth receiver");
			}
			if (index == 63) {
				RequireError([&] {
					descend(index + 1);
				},
				    "depth exceeds 64");
			} else {
				descend(index + 1);
			}
		});
	};
	descend(0);
	Require(entries == 64, "bounded distinct receiver scopes must reject before the extra callback");
	vm.DispatchBorrowedReceiver(identities.back(), [&](const auto&) {
		++entries;
	});
	Require(entries == 65, "depth failure must unwind every scope and leave ordinary dispatch available");
}

static void TestNestedBorrowDoesNotMarkCycles() {
	SquirrelVm vm(Limits());
	const auto class_value = vm.CreateClass();
	const auto class_root = vm.Retain(class_value);
	vm.NewSlot(class_value, std::string("self"), {});
	SquirrelValue parent = vm.CreateInstance(class_value);
	SquirrelValue child = vm.CreateInstance(class_value);
	vm.SetSlot(parent, std::string("self"), parent);
	vm.SetSlot(child, std::string("self"), child);
	const auto parent_identity = vm.BorrowObjectIdentity(parent);
	const auto child_identity = vm.BorrowObjectIdentity(child);
	const auto parent_weak = vm.CreateWeakReference(parent);
	const auto child_weak = vm.CreateWeakReference(child);
	parent = {};
	child = {};
	RequireError([&] {
		vm.DispatchBorrowedReceiver(parent_identity, [&](const auto& parent_receiver) {
			RequireError([&] {
				vm.DispatchBorrowedReceiver(child_identity, [&](const auto&) {
					Require(vm.CollectGarbage() == 2 && Expired(vm, parent_weak) && Expired(vm, child_weak), "nested borrowed metadata must not mark either real unrooted instance cycle");
				});
			},
			    "retired during dispatch");
			RequireError([&] {
				static_cast<void>(vm.Type(parent_receiver));
			},
			    "does not belong");
		});
	},
	    "retired during dispatch");
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestDistinctReceiversAndCycleRejection();
		TestActualDistinctChildManagers();
		TestNestedLastReferenceAndFailureRestoration();
		TestBoundedDepth();
		TestNestedBorrowDoesNotMarkCycles();
		std::cout << "5 bounded nested receiver groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
