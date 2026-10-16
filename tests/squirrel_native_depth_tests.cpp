#include "sgre/script/squirrel_vm.hpp"

#include "sgre/script/object_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::script::BytecodeProgram ThrowString(std::string text, std::string name, bool constructor = false) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.Parameters.push_back(std::string("this"));
	if (constructor) {
		program.MainFunction.Parameters.push_back(std::string("message"));
	}
	program.MainFunction.StackSize = 3;
	program.MainFunction.Name = name;
	program.MainFunction.SourceName = std::string("independent-native-depth-fixture");
	program.MainFunction.Literals.push_back(std::move(text));
	sgre::script::SquirrelInstruction load;
	load.Opcode = 1;
	load.Argument0 = 2;
	program.MainFunction.Instructions.push_back(load);
	sgre::script::SquirrelInstruction raise;
	raise.Opcode = 58;
	raise.Argument0 = 2;
	program.MainFunction.Instructions.push_back(raise);

	return program;
}

static SquirrelValue InstallFailingExceptionClass(SquirrelVm& vm, std::size_t& constructors) {
	vm.InstallRecoveredBaseLibrary();
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("constructor"), vm.CreateClosure(ThrowString("independent constructor failure", "failing_exception_constructor", true)));
	vm.SetTraceCallback([&constructors](const sgre::script::SquirrelTraceEvent& event) {
		if (event.Function == "failing_exception_constructor" && event.Instruction == 0) {
			++constructors;
		}
	});
	const std::array<SquirrelValue, 2> install{vm.RootTable(), class_value};
	static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("setexceptionclass")), install));

	return class_value;
}

static void TestFailingExceptionConstructorStatus() {
	SquirrelVm vm;
	std::size_t constructors = 0;
	const auto exception_class = InstallFailingExceptionClass(vm, constructors);
	const auto thrower = vm.CreateClosure(ThrowString("independent outer error", "outer_thrower"));
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	bool failed = false;
	try {
		static_cast<void>(vm.Invoke(thrower, receiver));
	} catch (const sgre::script::SquirrelVmException& error) {
		Require(vm.IsInstanceOf(error.ThrownValue(), exception_class) && vm.LastError() == error.ThrownValue(), "failed constructor must retain the outer exception instance and its last-error ownership");
		failed = true;
	}

	Require(failed && constructors == 99, "the native depth limit must terminate recursive string constructor failures without executing a 101st native level");
	const auto success = vm.CreateNativeClosure("success_after_native_stack_error", [](SquirrelVm&, std::span<const SquirrelValue>) {
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{42};

		return result;
	});
	Require(std::get<std::int32_t>(vm.Invoke(success, receiver).Value) == 42, "native depth must unwind completely after constructor status failure");
}

static void TestNativeCallbackDepthStatus() {
	SquirrelVm vm;
	std::size_t callbacks = 0;
	SquirrelValue recursive;
	recursive = vm.CreateNativeClosure("recursive_native_callback", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) -> sgre::script::SquirrelNativeResult {
		++callbacks;
		const auto nested = current.InvokeInContext(current.CurrentContext(), recursive, arguments);
		sgre::script::SquirrelNativeResult result;
		result.Value = nested.Value;

		return result;
	});
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	bool failed = false;
	try {
		static_cast<void>(vm.Invoke(recursive, receiver));
	} catch (const sgre::script::SquirrelVmException& error) {
		Require(StringText(error.ThrownValue()) == "Native stack overflow" && vm.LastError() == error.ThrownValue(), "native callback overflow must retain exact failed script-call status diagnostic");
		failed = true;
	}

	Require(failed && callbacks == 100, "native callbacks must execute at most 100 active native levels");
}

static void TestAuthoredDepthSafetyRemainsSeparate() {
	sgre::script::SquirrelVmLimits limits;
	limits.MaxFrames = 3;
	SquirrelVm vm(limits);
	SquirrelValue recursive;
	recursive = vm.CreateNativeClosure("limited_native_callback", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) -> sgre::script::SquirrelNativeResult {
		static_cast<void>(current.InvokeInContext(current.CurrentContext(), recursive, arguments));

		return {};
	});
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	bool failed = false;
	try {
		static_cast<void>(vm.Invoke(recursive, receiver));
	} catch (const sgre::script::SquirrelVmException&) {
		throw std::runtime_error("authored native invocation budget became ignored script status");
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()) == "Squirrel native invocation depth limit exceeded", "authored native invocation guard diagnostic changed");
		failed = true;
	}

	Require(failed, "authored native invocation safety budget must remain effective below the native threshold");
}

static void TestObjectCloseIgnoresOnlyNativeConstructorStatus() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	std::size_t constructors = 0;
	const auto exception_class = InstallFailingExceptionClass(vm, constructors);
	const auto exception_root = vm.Retain(exception_class);
	const auto derived = vm.CreateClass(objects.ObjectClass());
	vm.NewSlot(derived, std::string("destructor"), vm.CreateClosure(ThrowString("independent script destructor failure", "script_destructor")));
	const auto class_root = vm.Retain(derived);
	SquirrelValue instance = vm.CreateInstance(derived);
	objects.AttachInstance(instance);
	std::size_t cleanups = 0;
	objects.ConfigureNativeRelease(instance, [&](const SquirrelValue&) {
		++cleanups;
	});
	vm.NewSlot(vm.RootTable(), std::string("owner"), instance);
	instance = {};
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(cleanups == 1 && constructors == 99 && vm.IsClosed(), "Object Close must retain failed script-call diagnostics while completing native cleanup at the native counter limit");
}

int main() {
	try {
		TestFailingExceptionConstructorStatus();
		TestNativeCallbackDepthStatus();
		TestAuthoredDepthSafetyRemainsSeparate();
		TestObjectCloseIgnoresOnlyNativeConstructorStatus();
		std::cout << "Squirrel native-depth checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
