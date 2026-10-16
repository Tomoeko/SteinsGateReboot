#include "sgre/script/squirrel_vm.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

struct RootObservation {
	std::string Instance;
	bool BootstrapNil = false;
	bool FirstChildNil = false;
	bool SecondChildNil = false;
};

static void TestIndependentChildRootsFollowNativeCollectableOwnership(bool collect, bool reset) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto bootstrap = vm.RootContext();
	const auto actual_class = vm.CreateClass();
	sgre::script::SquirrelContextHandle first_child;
	sgre::script::SquirrelContextHandle second_child;
	std::vector<RootObservation> observations;
	const auto install = [&](const SquirrelValue& root, std::string name) {
		const auto instance = vm.CreateInstance(actual_class);
		vm.SetInstanceReleaseHook(instance, [&, name](SquirrelVm& actual, const SquirrelValue&) {
			Require(actual.CurrentContext() == bootstrap, "native Object finalization must not redirect execution to the retiring child");
			RootObservation observation;
			observation.Instance = name;
			observation.BootstrapNil = std::holds_alternative<std::monostate>(actual.RootTableInContext(bootstrap));
			observation.FirstChildNil = std::holds_alternative<std::monostate>(actual.RootTableInContext(first_child));
			observation.SecondChildNil = std::holds_alternative<std::monostate>(actual.RootTableInContext(second_child));
			observations.push_back(std::move(observation));
		});
		vm.NewSlot(root, std::string("owned_instance"), instance);
	};

	SquirrelValue first_root = vm.RootTable();
	install(first_root, "a");
	first_child = vm.CreateContext();
	SquirrelValue second_root = vm.CreateTable();
	vm.SetRootTable(second_root);
	first_root = {};
	install(second_root, "b");
	second_child = vm.CreateContext();
	second_root = {};
	Require(observations.empty(), "both actual child creation-time root owners must retain their distinct instances before Close");

	if (reset) {
		vm.ResetContext(first_child);
	}
	if (collect) {
		Require(vm.CollectGarbage() == 0, "actual context identities must mark their full owned roots without becoming collected objects");
	}

	vm.Close();
	vm.RethrowReleaseFailure();
	Require(vm.IsClosed() && observations.size() == 2, "real Close must complete both finalizers exactly once");
	// Expected native constructor/list order is checked independently by native_child_close.py.
	Require(observations[0].Instance == "b" && observations[0].BootstrapNil && !observations[0].FirstChildNil && observations[0].SecondChildNil, "native shared collectable traversal must retire newest child b while the older independent child root remains live");
	Require(observations[1].Instance == "a" && observations[1].BootstrapNil && observations[1].FirstChildNil && observations[1].SecondChildNil, "older child a must retire only after b through the same actual list order");
}

static void TestReusedContextKeepsItsNewCollectableIdentity() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto actual_class = vm.CreateClass();
	std::vector<std::string> observations;
	const auto install = [&](const SquirrelValue& root, std::string name) {
		const auto instance = vm.CreateInstance(actual_class);
		vm.SetInstanceReleaseHook(instance, [&, name](SquirrelVm& actual, const SquirrelValue&) {
			Require(std::holds_alternative<std::monostate>(actual.RootTable()), "bootstrap root must still clear before any mixed-list finalizer");
			observations.push_back(name);
		});
		vm.NewSlot(root, std::string("owned_instance"), instance);
	};

	SquirrelValue first_root = vm.RootTable();
	install(first_root, "a");
	const auto retired_child = vm.CreateContext();
	vm.ReleaseContext(retired_child);
	// Occupy the released index so the eventual a context is newer than b but has the lower reused index.
	const auto placeholder = vm.CreateContext();
	SquirrelValue second_root = vm.CreateTable();
	vm.SetRootTable(second_root);
	install(second_root, "b");
	const auto second_child = vm.CreateContext();
	vm.ReleaseContext(placeholder);
	const auto first_child = vm.CreateContext();
	vm.SetRootTableInContext(first_child, first_root);
	Require(first_child.Index < second_child.Index && first_child.Generation != retired_child.Generation, "fixture must reuse a genuine older context slot for the newer actual VM identity");
	bool stale_rejected = false;
	try {
		static_cast<void>(vm.RootTableInContext(retired_child));
	} catch (const std::runtime_error&) {
		stale_rejected = true;
	}

	Require(stale_rejected, "reused storage must never revive the retired context handle");
	first_root = {};
	second_root = {};
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(observations == std::vector<std::string>{"a", "b"}, "Close must follow actual collectable insertion identity instead of numeric context index or an age approximation");
}

static void TestFinalizersCanRetireCurrentAndNextContexts() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto actual_class = vm.CreateClass();
	const auto bootstrap = vm.RootContext();
	sgre::script::SquirrelContextHandle first_child;
	sgre::script::SquirrelContextHandle second_child;
	sgre::script::SquirrelContextHandle replacement;
	std::vector<std::string> observations;
	SquirrelValue first_root = vm.RootTable();
	{
		const auto instance = vm.CreateInstance(actual_class);
		vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& actual, const SquirrelValue&) {
			Require(actual.CurrentContext() == bootstrap && std::holds_alternative<std::monostate>(actual.RootTable()), "nested release must preserve bootstrap execution and its actual nil root");
			observations.push_back("a");
		});
		vm.NewSlot(first_root, std::string("owned_instance"), instance);
	}

	first_child = vm.CreateContext();
	SquirrelValue second_root = vm.CreateTable();
	vm.SetRootTable(second_root);
	first_root = {};
	{
		const auto instance = vm.CreateInstance(actual_class);
		vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& actual, const SquirrelValue&) {
			observations.push_back("b");
			Require(std::holds_alternative<std::monostate>(actual.RootTableInContext(second_child)), "current context must nil its root before its nested owner release");
			actual.ReleaseContext(first_child);
			replacement = actual.CreateContext();
			Require(replacement.Index == first_child.Index && replacement.Generation != first_child.Generation, "callback must reuse only the genuinely detached next context slot");
			actual.ReleaseContext(second_child);
		});
		vm.NewSlot(second_root, std::string("owned_instance"), instance);
	}

	second_child = vm.CreateContext();
	second_root = {};
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(vm.IsClosed() && observations == std::vector<std::string>{"b", "a"}, "post-finalizer cursor capture and protected current context must permit exact-once nested retirement without following a reused identity");
}

int main() {
	try {
		TestIndependentChildRootsFollowNativeCollectableOwnership(false, false);
		TestIndependentChildRootsFollowNativeCollectableOwnership(true, false);
		TestIndependentChildRootsFollowNativeCollectableOwnership(false, true);
		TestReusedContextKeepsItsNewCollectableIdentity();
		TestFinalizersCanRetireCurrentAndNextContexts();
		std::cout << "native child context collectable identity, marking and Close ownership matched\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
