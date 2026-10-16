#include "sgre/script/object_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using sgre::script::ScriptObjectBindings;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected failure: " + std::string(expected));
}

static SquirrelValue InvokeMethod(SquirrelVm& vm, const SquirrelValue& instance, const std::string& name, std::span<const SquirrelValue> parameters = {}) {
	std::vector<SquirrelValue> arguments{instance};
	arguments.insert(arguments.end(), parameters.begin(), parameters.end());

	return vm.Invoke(vm.GetSlot(instance, name), arguments).Value;
}

static void TestNamedBaseConstructorReceiver() {
	SquirrelVm vm;
	ScriptObjectBindings objects(vm);
	const auto derived = vm.CreateClass(objects.ObjectClass());
	std::size_t global_calls = 0;
	vm.RegisterNative("fixture_log", [&](SquirrelVm& current, const auto arguments) {
		Require(arguments.size() == 1 && current.IsInstanceOf(arguments[0], derived), "implicit global call must retain the actual derived Object receiver");
		++global_calls;

		return SquirrelNativeResult{};
	});
	sgre::script::BytecodeProgram program;
	program.MainFunction.Parameters = {std::string("this")};
	program.MainFunction.StackSize = 4;
	program.MainFunction.Literals = {std::string("Object"), std::string("constructor"), std::string("fixture_log")};
	const auto append = [&](std::uint8_t opcode, std::uint8_t argument0, std::int32_t argument1, std::uint8_t argument2, std::uint8_t argument3) {
		sgre::script::SquirrelInstruction instruction;
		instruction.Opcode = opcode;
		instruction.Argument0 = argument0;
		instruction.Argument1 = argument1;
		instruction.Argument2 = argument2;
		instruction.Argument3 = argument3;
		program.MainFunction.Instructions.push_back(instruction);
	};
	append(21, 1, 0, 0, 0);
	append(9, 1, 0, 1, 0);
	append(8, 1, 1, 1, 2);
	append(6, 1, 1, 2, 1);
	append(8, 1, 2, 0, 2);
	append(6, 1, 1, 2, 1);
	append(19, 255, 0, 0, 0);
	vm.NewSlot(derived, std::string("constructor"), vm.CreateClosure(std::move(program)));
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	const auto instance = vm.Invoke(derived, receiver).Value;
	Require(objects.HasInstance(instance) && vm.IsInstanceOf(instance, derived), "named Object.constructor call must initialize the actual derived instance");
	Require(global_calls == 1, "failed native Object getter must permit the current constructor receiver's root-table fallback");
}

static void TestRegisteredWeakReferenceMember() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptObjectBindings objects(vm);
	const auto derived = vm.CreateClass(objects.ObjectClass());
	SquirrelValue owner = vm.CreateInstance(derived);
	const auto picture = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(picture);
	objects.AttachInstance(owner, picture);
	Require(vm.HasSlot(owner, std::string("weakref")), "Sqrat weakref must be a copied ordinary member before Object's delegate getter runs");
	const auto method = vm.ReadSlot(owner, std::string("weakref"));
	Require(!vm.HasBoundEnvironment(method), "ordinary weakref must not bind to the delegated picture instance");

	sgre::script::BytecodeProgram program;
	program.MainFunction.Parameters = {std::string("this"), std::string("owner")};
	program.MainFunction.StackSize = 4;
	program.MainFunction.Literals = {std::string("weakref")};
	sgre::script::SquirrelInstruction prepare;
	prepare.Opcode = 8;
	prepare.Argument0 = 2;
	prepare.Argument2 = 1;
	prepare.Argument3 = 3;
	program.MainFunction.Instructions.push_back(prepare);
	sgre::script::SquirrelInstruction call;
	call.Opcode = 6;
	call.Argument0 = 2;
	call.Argument1 = 2;
	call.Argument2 = 3;
	call.Argument3 = 1;
	program.MainFunction.Instructions.push_back(call);
	sgre::script::SquirrelInstruction result;
	result.Opcode = 19;
	result.Argument0 = 1;
	result.Argument1 = 2;
	program.MainFunction.Instructions.push_back(result);
	const auto closure = vm.CreateClosure(std::move(program));
	SquirrelValue weak;
	{
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), owner};
		weak = vm.Invoke(closure, arguments).Value;
	}
	Require(vm.ResolveWeakReference(weak) == owner, "owner.weakref() must preserve the actual receiver rather than its delegated BasePicture");
	const std::array<SquirrelValue, 1> extra{picture};
	const auto extra_weak = InvokeMethod(vm, owner, "weakref", extra);
	Require(vm.ResolveWeakReference(extra_weak) == picture, "native Sqrat weakref reads its last physical argument, including extra arguments");
	const std::array<SquirrelValue, 1> scalar{std::int32_t{47}};
	Require(InvokeMethod(vm, owner, "weakref", scalar) == scalar[0], "native weakref must return a non-reference-counted final scalar unchanged");
	owner = {};
	Require(vm.Type(vm.ResolveWeakReference(weak)) == sgre::script::SquirrelValueType::Null, "registered weakref must not retain its original instance through either native or script frames");
}

static void TestPropertiesAndDelegates() {
	SquirrelVm vm;
	ScriptObjectBindings objects(vm);
	const auto derived = vm.CreateClass(objects.ObjectClass());
	std::int32_t stored = 23;
	vm.NewSlot(derived, std::string("getValue"), vm.CreateNativeClosure("getValue", [&](SquirrelVm& current, const auto arguments) {
		Require(arguments.size() == 1 && current.IsInstanceOf(arguments[0], derived), "getter receiver changed");
		SquirrelNativeResult result;
		result.Value = stored;

		return result;
	}));
	vm.NewSlot(derived, std::string("setValue"), vm.CreateNativeClosure("setValue", [&](SquirrelVm&, const auto arguments) {
		Require(arguments.size() == 2, "setter parameter convention changed");
		stored = std::get<std::int32_t>(arguments[1]);

		return SquirrelNativeResult{};
	}));
	const auto delegate = vm.CreateTable();
	vm.NewSlot(delegate, std::string("fallback"), std::int32_t{51});
	const std::array<SquirrelValue, 2> constructor_arguments{vm.RootTable(), delegate};
	const auto instance = vm.Invoke(derived, constructor_arguments).Value;
	Require(objects.HasInstance(instance) && objects.Delegate(instance) == delegate, "inherited Object constructor did not retain base state");
	Require(std::get<std::int32_t>(vm.ReadSlot(instance, std::string("value"))) == 23, "Object getter name capitalization or nested return failed");
	vm.AssignSlot(instance, std::string("value"), std::int32_t{39});
	Require(stored == 39, "Object setter invocation did not reach receiver/value callback");
	Require(std::get<std::int32_t>(vm.ReadSlot(instance, std::string("fallback"))) == 51, "Object delegate lookup failed");
	vm.AssignSlot(instance, std::string("fallback"), std::int32_t{67});
	Require(std::get<std::int32_t>(vm.GetSlot(delegate, std::string("fallback"))) == 67, "Object delegate assignment failed");
	const std::array<SquirrelValue, 1> property{std::string("value")};
	Require(std::get<bool>(InvokeMethod(vm, instance, "hasSetProp", property)), "Object hasSetProp did not check setter method");
	const std::array<SquirrelValue, 1> missing{std::string("missing")};
	RequireFailure([&] {
		static_cast<void>(InvokeMethod(vm, instance, "hasSetProp", missing));
	},
	    "index does not exist");
	const std::array<SquirrelValue, 1> empty_property{std::string("")};
	Require(!vm.HasSlot(instance, std::string("_get")), "native Object meta getter must remain separate from its ordinary get alias");
	RequireFailure([&] {
		static_cast<void>(InvokeMethod(vm, instance, "get", empty_property));
	},
	    "nonempty string");
	RequireFailure([&] {
		static_cast<void>(vm.ReadSlot(instance, std::string("")));
	},
	    "the index doesn't exist");
	RequireFailure([&] {
		objects.AttachInstance(instance);
	},
	    "already initialized");
}

static void TestDelegateClosureBinding() {
	SquirrelVm vm;
	ScriptObjectBindings objects(vm);
	const auto delegate_class = vm.CreateClass();
	vm.NewSlot(delegate_class, std::string("answer"), std::int32_t{72});
	vm.NewSlot(delegate_class, std::string("method"), vm.CreateNativeClosure("delegate.method", [](SquirrelVm& current, const auto arguments) {
		SquirrelNativeResult result;
		result.Value = current.GetSlot(arguments[0], std::string("answer"));

		return result;
	}));
	const auto delegate = vm.CreateInstance(delegate_class);
	const auto object = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(object, delegate);
	const auto callable = vm.ReadSlot(object, std::string("method"));
	Require(vm.HasBoundEnvironment(callable), "unbound delegate-instance method was not rebound");
	const std::array<SquirrelValue, 1> receiver{object};
	Require(std::get<std::int32_t>(vm.Invoke(callable, receiver).Value) == 72, "bound native closure did not replace its call receiver");
	const auto table = vm.CreateTable();
	vm.NewSlot(table, std::string("method"), vm.GetSlot(delegate, std::string("method")));
	objects.SetDelegate(object, table);
	Require(!vm.HasBoundEnvironment(vm.ReadSlot(object, std::string("method"))), "table delegate method must retain its unbound receiver behavior");
}

static void TestCompletionObservers() {
	SquirrelVm vm;
	ScriptObjectBindings objects(vm);
	const auto target = vm.CreateInstance(objects.ObjectClass());
	const auto first = vm.CreateInstance(objects.ObjectClass());
	const auto second = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(target);
	objects.AttachInstance(first);
	objects.AttachInstance(second);
	std::vector<std::int32_t> events;
	objects.Subscribe(target, first, [&](const SquirrelValue& object) {
		Require(object == target, "Object completion identity changed");
		events.push_back(1);

		return false;
	});
	objects.Subscribe(target, second, [&](const SquirrelValue&) {
		events.push_back(2);

		return true;
	});
	objects.Subscribe(target, first, [&](const SquirrelValue&) {
		events.push_back(3);

		return true;
	});
	objects.Notify(target, false);
	Require(events == std::vector<std::int32_t>({1, 2}), "notify must stop after first listener consumes completion");
	events.clear();
	objects.Notify(target, true);
	Require(events == std::vector<std::int32_t>({1, 3}), "notifyAll must preserve remaining subscription order");
	events.clear();
	objects.Notify(target, true);
	Require(events.empty(), "notifyAll did not clear subscriptions");
	objects.Subscribe(target, first, [&](const SquirrelValue&) {
		events.push_back(4);

		return true;
	});
	objects.Subscribe(target, first, [&](const SquirrelValue&) {
		events.push_back(5);

		return true;
	});
	objects.Unsubscribe(target, first);
	objects.Notify(target);
	Require(events.empty(), "Unsubscribe must remove duplicate subscriptions for a listener");
	objects.ReleaseInstance(target);
	Require(!objects.HasInstance(target), "native Object release retained its base record");
}

static void TestBridgeOwnership() {
	SquirrelVm vm;
	SquirrelValue instance;
	SquirrelValue method;
	{
		ScriptObjectBindings objects(vm);
		instance = vm.CreateInstance(objects.ObjectClass());
		objects.AttachInstance(instance);
		const std::uint8_t derived_tag = 0;
		sgre::script::SquirrelInstanceHostIdentity identity;
		identity.TypeTag = &derived_tag;
		identity.Value = 44;
		vm.SetInstanceHostIdentity(instance, identity);
		Require(vm.InstanceHostIdentity(instance, &derived_tag) == identity && objects.HasInstance(instance), "Object registry overwrote derived native identity");
		method = vm.GetSlot(instance, std::string("getDelegate"));
	}

	const std::array<SquirrelValue, 1> receiver{instance};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(method, receiver));
	},
	    "bindings have expired");
}

static void TestNumericNotificationMutation() {
	for (const bool consumed : {false, true}) {
		SquirrelVm vm;
		ScriptObjectBindings objects(vm);
		const auto target = vm.CreateInstance(objects.ObjectClass());
		const auto first = vm.CreateInstance(objects.ObjectClass());
		const auto second = vm.CreateInstance(objects.ObjectClass());
		const auto third = vm.CreateInstance(objects.ObjectClass());
		objects.AttachInstance(target);
		std::vector<std::int32_t> events;
		objects.Subscribe(target, first, [&](const SquirrelValue&) {
			events.push_back(1);
			objects.Unsubscribe(target, first);

			return consumed;
		});
		objects.Subscribe(target, second, [&](const SquirrelValue&) {
			events.push_back(2);

			return false;
		});
		objects.Subscribe(target, third, [&](const SquirrelValue&) {
			events.push_back(3);

			return false;
		});
		objects.Notify(target, false);
		Require(events == (consumed ? std::vector<std::int32_t>{1} : std::vector<std::int32_t>{1, 3}), "notification must follow current numeric indices after listener removal");
		events.clear();
		objects.Notify(target, true);
		Require(events == (consumed ? std::vector<std::int32_t>{3} : std::vector<std::int32_t>{2, 3}), "consumed completion must remove the current numeric index rather than its original listener");
	}

	SquirrelVm vm;
	ScriptObjectBindings objects(vm);
	const auto target = vm.CreateInstance(objects.ObjectClass());
	const auto listener = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(target);
	std::size_t notifications = 0;
	objects.Subscribe(target, listener, [&](const SquirrelValue&) {
		++notifications;
		RequireFailure([&] {
			objects.ReleaseInstance(target);
		},
		    "release during completion notification");

		return false;
	});
	objects.Notify(target);
	Require(notifications == 1 && objects.HasInstance(target), "rejected release reentry must preserve the active native notification record");
}

static void TestWeakNativeOwnership() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptObjectBindings objects(vm);
	SquirrelValue instance = vm.CreateInstance(objects.ObjectClass());
	SquirrelValue delegate = vm.CreateTable();
	objects.AttachInstance(instance, delegate);
	Require(objects.Delegate(instance) == delegate, "native weak delegate lost its live target");
	delegate = {};
	Require(std::holds_alternative<std::monostate>(objects.Delegate(instance)), "Object delegate must not retain its target");

	sgre::script::SquirrelObjectIdentity weak_identity;
	{
		const auto weak = vm.CreateWeakReference(instance);
		weak_identity = vm.BorrowObjectIdentity(weak);
	}

	const auto weak = vm.CreateWeakReference(instance);
	Require(vm.BorrowObjectIdentity(weak) == weak_identity, "native Object must externally retain its cached weak-self wrapper");
	instance = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "native Object registry must not retain its script instance");
	vm.RethrowReleaseFailure();
}

static void TestNativeSubscriberRoots() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptObjectBindings objects(vm);
	SquirrelValue target = vm.CreateInstance(objects.ObjectClass());
	SquirrelValue listener = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(target);
	objects.AttachInstance(listener);
	auto target_root = vm.Retain(target);
	const auto listener_weak = vm.CreateWeakReference(listener);
	std::size_t notifications = 0;
	objects.Subscribe(target, listener, [&](const SquirrelValue& receiver) {
		Require(receiver == target_root.Value(), "completion callback receiver must remain the live native target");
		++notifications;

		return true;
	});
	listener = {};
	static_cast<void>(vm.CollectGarbage());
	Require(!std::holds_alternative<std::monostate>(vm.ResolveWeakReference(listener_weak)), "native subscriber array must mark its listener during collection");
	objects.Notify(target, false);
	vm.RethrowReleaseFailure();
	Require(notifications == 1 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(listener_weak)), "consumed subscriber must release after callback and native bookkeeping");
	target_root = {};
	target = {};
	vm.RethrowReleaseFailure();
}

static void TestNativeReleaseOrdering() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptObjectBindings objects(vm);
	const auto derived = vm.CreateClass(objects.ObjectClass());
	std::vector<std::int32_t> events;
	vm.NewSlot(derived, std::string("destructor"), vm.CreateNativeClosure("fixture.destructor", [&](SquirrelVm&, const auto arguments) {
		Require(arguments.size() == 1 && objects.HasInstance(arguments[0]), "script destructor must observe native Object state before cleanup");
		events.push_back(1);

		return SquirrelNativeResult{};
	}));
	SquirrelValue target = vm.CreateInstance(derived);
	const auto listener = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(target);
	objects.AttachInstance(listener);
	objects.Subscribe(target, listener, [&](const SquirrelValue&) {
		events.push_back(3);

		return false;
	});
	objects.ConfigureNativeRelease(target, [&](const SquirrelValue& receiver) {
		events.push_back(2);
		objects.ReleaseInstance(receiver);
		events.push_back(4);
	});
	const auto weak = vm.CreateWeakReference(target);
	target = {};
	vm.RethrowReleaseFailure();
	Require(events == std::vector<std::int32_t>{1, 2, 3, 4} && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "derived release must order script destructor, native cleanup and base notification");
}

static void TestReleaseFailureCleanup() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptObjectBindings objects(vm);
	const auto derived = vm.CreateClass(objects.ObjectClass());
	vm.NewSlot(derived, std::string("destructor"), vm.CreateNativeClosure("fixture.destructor", [](SquirrelVm&, const auto) -> SquirrelNativeResult {
		throw std::runtime_error("script release failure");
	}));
	SquirrelValue target = vm.CreateInstance(derived);
	objects.AttachInstance(target);
	std::size_t native_calls = 0;
	objects.ConfigureNativeRelease(target, [&](const SquirrelValue&) {
		++native_calls;
		throw std::runtime_error("native release failure");
	});
	const auto weak = vm.CreateWeakReference(target);
	const auto identity = vm.BorrowObjectIdentity(target);
	target = {};
	Require(native_calls == 1 && !vm.ResolveObjectIdentity(identity) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "release failure must not strand the script instance or native roots");
	Require(sgre::script::StringText(vm.LastError()) == "script release failure", "ignored script destructor status must preserve its native VM last-error diagnostic");
	RequireFailure([&] {
		vm.RethrowReleaseFailure();
	},
	    "native release failure");
	vm.RethrowReleaseFailure();
}

static void TestWeakDestructorMethod() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptObjectBindings objects(vm);
	std::size_t calls = 0;
	const auto method = vm.CreateNativeClosure("weak_destructor", [&](SquirrelVm&, const auto arguments) {
		Require(arguments.size() == 1 && objects.HasInstance(arguments[0]), "resolved weak destructor must receive its live native base owner");
		++calls;

		return SquirrelNativeResult{};
	});
	const auto derived = vm.CreateClass(objects.ObjectClass());
	vm.NewSlot(derived, std::string("destructor"), vm.CreateWeakReference(method));
	SquirrelValue instance = vm.CreateInstance(derived);
	objects.AttachInstance(instance);
	const auto identity = vm.BorrowObjectIdentity(instance);
	instance = {};
	vm.RethrowReleaseFailure();
	Require(calls == 1 && !vm.ResolveObjectIdentity(identity), "native destructor lookup must resolve a weak callable once before invoking it");
}

static void TestDestructorDiscardsNativeOutput(std::int32_t status) {
	std::vector<std::int32_t> events;
	SquirrelValue output_weak;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptObjectBindings objects(vm);
	const auto derived = vm.CreateClass(objects.ObjectClass());
	vm.NewSlot(derived, std::string("destructor"), vm.CreateNativeClosure("fixture.discarded_destructor_output", [&](SquirrelVm& active, const auto arguments) {
		Require(arguments.size() == 1 && objects.HasInstance(arguments[0]), "discarded destructor result must still execute on its live native Object receiver");
		const std::array<std::uint8_t, 1> bytes{37};
		const auto output = active.CreateUserData(bytes);
		active.SetUserDataReleaseHook(output, [&](SquirrelVm&, const SquirrelValue&, const auto payload) {
			Require(payload.size() == 1 && payload[0] == 37, "discarded native destructor output must release its actual initialized payload");
			events.push_back(1);
		});
		output_weak = active.CreateWeakReference(output);
		if (status == -666) {
			active.PushNativeValue(output);

			return SquirrelNativeResult::StackStatus(status);
		}

		return SquirrelNativeResult::ReturnValue(output);
	}));
	SquirrelValue target = vm.CreateInstance(derived);
	objects.AttachInstance(target);
	objects.ConfigureNativeRelease(target, [&](const SquirrelValue& receiver) {
		Require(events == std::vector<std::int32_t>{1} && vm.Type(vm.ResolveWeakReference(output_weak)) == sgre::script::SquirrelValueType::Null, "native Object destructor retval0 must retire positive/raw-666 output before native owner cleanup");
		events.push_back(2);
		objects.ReleaseInstance(receiver);
	});
	const auto target_weak = vm.CreateWeakReference(target);
	target = {};
	vm.RethrowReleaseFailure();
	Require(events == std::vector<std::int32_t>{1, 2} && vm.Type(vm.ResolveWeakReference(target_weak)) == sgre::script::SquirrelValueType::Null && !vm.IsSuspended(), "discarded native destructor result must preserve ordinary release and direct native-call nonsuspension");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestNamedBaseConstructorReceiver();
		TestRegisteredWeakReferenceMember();
		TestPropertiesAndDelegates();
		TestDelegateClosureBinding();
		TestCompletionObservers();
		TestBridgeOwnership();
		TestNumericNotificationMutation();
		TestWeakNativeOwnership();
		TestNativeSubscriberRoots();
		TestNativeReleaseOrdering();
		TestReleaseFailureCleanup();
		TestWeakDestructorMethod();
		TestDestructorDiscardsNativeOutput(1);
		TestDestructorDiscardsNativeOutput(-666);
		std::cout << "Native Object binding checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
