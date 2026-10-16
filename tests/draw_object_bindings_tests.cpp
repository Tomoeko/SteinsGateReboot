#include "sgre/script/draw_object_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected native DrawObject failure: " + std::string(message));
}

static sgre::script::SquirrelVmLimits ReclaimingLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static void TestRealNativeOwnerAndWeakBorrow() {
	sgre::script::SquirrelVm vm(ReclaimingLimits());
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptDrawObjectBindings draws(vm, objects);
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(draws.DrawObjectClass(), std::array<sgre::script::SquirrelValue, 1>{vm.RootTable()}));
	},
	    "can't create instance");
	const auto subclass = vm.CreateClass(draws.DrawObjectClass());
	auto target = vm.CreateInstance(subclass);
	std::size_t calls = 0;
	std::size_t releases = 0;
	sgre::script::ScriptNativeDrawObjectCallbacks callbacks;
	callbacks.Draw = [&](std::uint32_t argument, std::uint32_t opacity) {
		Require(argument == 0x87654321u && opacity == 0xfedcba98u && vm.RetainedReferenceCount(target) == 0, "concrete native owner must receive full DWORD arguments without a script receiver host root");
		++calls;
	};
	callbacks.Release = [&] {
		++releases;
	};
	draws.AttachNativeDrawOwner(target, std::move(callbacks));
	const auto borrowed = draws.BorrowNativeDrawOwner(target);
	Require(borrowed && borrowed->IsLive(), "native target lookup must borrow its actual attached callback owner");
	borrowed->Draw(0x87654321u, 0xfedcba98u);
	const auto weak = vm.CreateWeakReference(target);
	target = {};
	Require(calls == 1 && releases == 1 && !borrowed->IsLive() && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "native borrow must not keep its script target or native callback owner alive");
	RequireFailure([&] {
		borrowed->Draw(0, 0);
	},
	    "expired after");
}

static void TestScriptNativeBaseOrderAndFailureCleanup() {
	sgre::script::SquirrelVm vm(ReclaimingLimits());
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptDrawObjectBindings draws(vm, objects, 1);
	std::vector<std::uint32_t> events;
	const auto subclass = vm.CreateClass(draws.DrawObjectClass());
	vm.NewSlot(subclass, std::string("destructor"), vm.CreateNativeClosure("draw subclass destructor", [&](auto&, auto) {
		events.push_back(1);

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	auto target = vm.CreateInstance(subclass);
	sgre::script::ScriptNativeDrawObjectCallbacks callbacks;
	callbacks.Draw = [](auto, auto) {
	};
	callbacks.Release = [&] {
		events.push_back(2);
	};
	draws.AttachNativeDrawOwner(target, std::move(callbacks));
	const auto listener = vm.Invoke(objects.ObjectClass(), std::array<sgre::script::SquirrelValue, 1>{vm.RootTable()}).Value;
	objects.Subscribe(target, listener, [&](const auto&) {
		events.push_back(3);

		return true;
	});
	const auto identity = vm.BorrowObjectIdentity(target);
	target = {};
	vm.RethrowReleaseFailure();
	Require(events == std::vector<std::uint32_t>{1, 2, 3} && !vm.ResolveObjectIdentity(identity), "singular native bridge must deliver script destructor before native callback release before Object completion");

	const auto plain_subclass = vm.CreateClass(draws.DrawObjectClass());
	const auto replacement = vm.CreateInstance(plain_subclass);
	callbacks.Draw = [](auto, auto) {
	};
	callbacks.Release = [] {
		throw std::runtime_error("concrete native owner cleanup failure");
	};
	draws.AttachNativeDrawOwner(replacement, std::move(callbacks));
	const auto borrowed = draws.BorrowNativeDrawOwner(replacement);
	RequireFailure([&] {
		draws.ReleaseInstance(replacement);
	},
	    "cleanup failure");
	Require(!borrowed->IsLive() && !objects.HasInstance(replacement) && !draws.BorrowNativeDrawOwner(replacement), "native cleanup diagnostic must still erase owner and base records");
	const auto third = vm.CreateInstance(plain_subclass);
	callbacks = {};
	callbacks.Draw = [](auto, auto) {
	};
	draws.AttachNativeDrawOwner(third, std::move(callbacks));
	Require(draws.BorrowNativeDrawOwner(third)->IsLive(), "failed cleanup must release bounded native owner capacity");
	draws.ReleaseInstance(third);
}

static void TestActiveDrawPreflightAndScopedFailures() {
	sgre::script::SquirrelVm vm(ReclaimingLimits());
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptDrawObjectBindings draws(vm, objects);
	const auto target = vm.CreateInstance(vm.CreateClass(draws.DrawObjectClass()));
	std::size_t released = 0;
	sgre::script::ScriptNativeDrawObjectCallbacks callbacks;
	callbacks.Draw = [&](auto, auto) {
		RequireFailure([&] {
			draws.ReleaseInstance(target);
		},
		    "active draw");
		throw std::runtime_error("native draw operation failed");
	};
	callbacks.Release = [&] {
		++released;
	};
	draws.AttachNativeDrawOwner(target, std::move(callbacks));
	const auto borrowed = draws.BorrowNativeDrawOwner(target);
	RequireFailure([&] {
		borrowed->Draw(1, 2);
	},
	    "draw operation failed");
	Require(borrowed->IsLive() && released == 0 && objects.HasInstance(target), "active-draw preflight must leave actual owner and base state intact");
	draws.ReleaseInstance(target);
	Require(released == 1 && !borrowed->IsLive(), "failed draw must leave no active callback flag that blocks later native retirement");
}

static void TestTemporaryTargetDropsBeforeNativeDispatch() {
	sgre::script::SquirrelVm vm(ReclaimingLimits());
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptDrawObjectBindings draws(vm, objects);
	auto target = vm.CreateInstance(vm.CreateClass(draws.DrawObjectClass()));
	std::size_t releases = 0;
	sgre::script::ScriptNativeDrawObjectCallbacks callbacks;
	callbacks.Draw = [](auto, auto) {
		throw std::runtime_error("expired native payload must never run");
	};
	callbacks.Release = [&] {
		++releases;
	};
	draws.AttachNativeDrawOwner(target, std::move(callbacks));
	const auto weak = vm.CreateWeakReference(target);
	std::optional<sgre::script::ScriptNativeDrawObjectBorrow> native;
	{
		const auto temporary_target = vm.ResolveWeakReference(weak);
		native = draws.BorrowNativeDrawOwner(temporary_target);
		target = {};
		Require(native->IsLive() && releases == 0, "actual resolved script temporary must own the last strong target before its pop");
	}

	Require(releases == 1 && !native->IsLive(), "popping the last actual strong target must retire native owner before virtual dispatch");
	RequireFailure([&] {
		native->Draw(1, 2);
	},
	    "expired after");
}

static void TestCloseWithRetiredClassAncestry() {
	sgre::script::SquirrelVm vm(ReclaimingLimits());
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptDrawObjectBindings draws(vm, objects);
	const auto subclass = vm.CreateClass(draws.DrawObjectClass());
	const auto target = vm.CreateInstance(subclass);
	std::size_t releases = 0;
	sgre::script::ScriptNativeDrawObjectCallbacks callbacks;
	callbacks.Draw = [](auto, auto) {
	};
	callbacks.Release = [&] {
		++releases;
	};
	draws.AttachNativeDrawOwner(target, std::move(callbacks));
	const auto borrowed = draws.BorrowNativeDrawOwner(target);
	vm.NewSlot(vm.RootTable(), std::string("concrete_owner"), target);
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(releases == 1 && !borrowed->IsLive(), "native owner cleanup must use retained identity after Class ancestry finalization");
}

int main() {
	try {
		TestRealNativeOwnerAndWeakBorrow();
		TestScriptNativeBaseOrderAndFailureCleanup();
		TestActiveDrawPreflightAndScopedFailures();
		TestTemporaryTargetDropsBeforeNativeDispatch();
		TestCloseWithRetiredClassAncestry();
		std::cout << "Genuine DrawObject native owner fixtures passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
