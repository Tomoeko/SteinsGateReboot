#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first, std::int32_t second = 0, std::uint8_t third = 0, std::uint8_t fourth = 0) {
	SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = first;
	instruction.Argument1 = second;
	instruction.Argument2 = third;
	instruction.Argument3 = fourth;

	return instruction;
}

static BytecodeProgram Program(std::vector<SquirrelInstruction> instructions) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-native-stack-fixture");
	program.MainFunction.Name = std::string("main");
	program.MainFunction.Parameters = {std::string("this")};
	program.MainFunction.StackSize = 10;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

enum class ReturnCase {
	NoValues,
	ExistingTop,
	OneNull,
	PushedStringWithoutValue,
	OneString,
	TwoValues,
	Compatibility
};

static void TestPhysicalNativeReturn(ReturnCase return_case) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue previous_weak;
	SquirrelValue pushed_weak;
	vm.RegisterNative("provide", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		const auto value = active.CreateString("independent-previous-native-stack-cell");
		previous_weak = active.CreateWeakReference(value);

		return SquirrelNativeResult{value, false};
	});
	vm.RegisterNative("target", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 3 && active.NativeStackSize() == 3, "the active physical native frame must expose exactly its three original arguments");
		Require(std::get<std::int32_t>(active.NativeStackValue(1)) == 10 && std::get<std::int32_t>(active.NativeStackValue(2)) == 20 && std::get<std::int32_t>(active.NativeStackValue(-1)) == 30, "positive and negative native indices must select original raw argument cells");
		if (return_case == ReturnCase::NoValues) {
			return SquirrelNativeResult::NoValues();
		}

		if (return_case == ReturnCase::ExistingTop) {
			return SquirrelNativeResult::StackStatus(1);
		}

		if (return_case == ReturnCase::OneNull) {
			return SquirrelNativeResult::ReturnValue({});
		}

		if (return_case == ReturnCase::Compatibility) {
			return SquirrelNativeResult{};
		}

		const auto value = active.CreateString("independent-pushed-native-stack-cell");
		pushed_weak = active.CreateWeakReference(value);
		if (return_case == ReturnCase::OneString) {
			return SquirrelNativeResult::ReturnValue(value);
		}

		active.PushNativeValue(value);
		if (return_case == ReturnCase::PushedStringWithoutValue) {
			return SquirrelNativeResult::NoValues();
		}

		active.PushNativeValue(std::int32_t{42});

		return SquirrelNativeResult::StackStatus(2);
	});
	vm.RegisterNative("verify", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 4, "verification must receive result and affected original caller cells");
		const bool previous_alive = active.Type(active.ResolveWeakReference(previous_weak)) == sgre::script::SquirrelValueType::String;
		Require(previous_alive == (return_case == ReturnCase::NoValues || return_case == ReturnCase::ExistingTop || return_case == ReturnCase::Compatibility), "only an actual push must overwrite and expire the higher original caller cell");
		if (return_case == ReturnCase::ExistingTop) {
			Require(std::get<std::int32_t>(arguments[1]) == 30 && active.Type(arguments[2]) == sgre::script::SquirrelValueType::Null, "a positive native return without pushing must return and clear the existing last argument");
		} else {
			Require(std::get<std::int32_t>(arguments[2]) == 30, "native return must not clear a different original argument cell");
		}

		if (return_case == ReturnCase::OneString) {
			Require(sgre::script::StringText(arguments[1]) == "independent-pushed-native-stack-cell" && active.Type(arguments[3]) == sgre::script::SquirrelValueType::Null, "one explicit value must be returned while its original physical result cell is cleared");
		} else if (return_case == ReturnCase::TwoValues) {
			Require(std::get<std::int32_t>(arguments[1]) == 42 && sgre::script::StringText(arguments[3]) == "independent-pushed-native-stack-cell", "any positive native count must return only the last value while the earlier pushed cell remains owned");
		} else if (return_case == ReturnCase::PushedStringWithoutValue) {
			Require(active.Type(arguments[1]) == sgre::script::SquirrelValueType::Null && sgre::script::StringText(arguments[3]) == "independent-pushed-native-stack-cell", "status zero must keep the pushed physical value and expose null as its call result");
		} else if (return_case != ReturnCase::ExistingTop) {
			Require(active.Type(arguments[1]) == sgre::script::SquirrelValueType::Null, "zero/explicit-null/compatibility cases must produce their declared null call value");
		}

		Require(active.CollectGarbage() == 0, "live physical native/script operands must remain collector roots");

		return SquirrelNativeResult::NoValues();
	});
	auto program = Program({Instruction(8, 1, 0, 0, 2), Instruction(6, 5, 1, 2, 1), Instruction(8, 1, 1, 0, 2), Instruction(2, 2, 10), Instruction(2, 3, 20), Instruction(2, 4, 30), Instruction(6, 7, 1, 2, 3), Instruction(8, 1, 2, 0, 2), Instruction(10, 3, 7), Instruction(10, 4, 4), Instruction(10, 5, 5), Instruction(6, 8, 1, 2, 4), Instruction(19, 255)});
	program.MainFunction.Literals = {std::string("provide"), std::string("target"), std::string("verify")};
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(program)), arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestStackAccessBoundsAndFailure() {
	SquirrelVm vm;
	bool outside_rejected = false;
	try {
		static_cast<void>(vm.NativeStackSize());
	} catch (const std::runtime_error& error) {
		outside_rejected = std::string(error.what()).find("active native frame") != std::string::npos;
	}

	Require(outside_rejected, "native physical access outside a real native frame must reject");
	vm.RegisterNative("bounds", [](SquirrelVm& active, std::span<const SquirrelValue>) {
		for (const auto index : {std::int32_t{0}, std::int32_t{-2}, std::int32_t{2}}) {
			bool rejected = false;
			try {
				static_cast<void>(active.NativeStackValue(index));
			} catch (const std::runtime_error& error) {
				rejected = std::string(error.what()).find("logical extent") != std::string::npos;
			}

			Require(rejected, "invalid native stack indices must reject without altering active operands");
		}

		active.PushNativeValue(std::int32_t{71});
		active.SetNativeStackSize(1);
		Require(active.NativeStackSize() == 1, "native logical shrinking must restore the declared user extent");
		active.SetNativeStackSize(2);
		Require(active.Type(active.NativeStackValue(-1)) == sgre::script::SquirrelValueType::Null, "native growth must push null rather than expose an old physical value");

		return SquirrelNativeResult::ReturnValue(std::int32_t{8});
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	Require(std::get<std::int32_t>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("bounds")), arguments).Value) == 8, "checked native stack operations must preserve successful execution");
	const auto failure = vm.CreateNativeClosure("failure-status", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::StackStatus(-1);
	});
	bool failed = false;
	try {
		static_cast<void>(vm.Invoke(failure, arguments));
	} catch (const sgre::script::SquirrelVmException& error) {
		failed = vm.Type(error.ThrownValue()) == sgre::script::SquirrelValueType::Null;
	}

	Require(failed, "negative native status must fail using the existing null last error rather than inventing an error value");
	Require(std::get<std::int32_t>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("bounds")), arguments).Value) == 8, "failed native status must retire its exact frame and leave the context reusable");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNegativeStatusPreservesTypedLastErrorAndTrap() {
	SquirrelVm vm;
	vm.RegisterNative("fail", [](SquirrelVm& active, std::span<const SquirrelValue>) {
		auto failing = Program({Instruction(2, 1, 91), Instruction(58, 1)});
		const std::array<SquirrelValue, 1> arguments{active.RootTable()};
		try {
			static_cast<void>(active.InvokeInContext(active.CurrentContext(), active.CreateClosure(std::move(failing)), arguments));
		} catch (const sgre::script::SquirrelVmException& error) {
			Require(std::get<std::int32_t>(error.ThrownValue()) == 91 && std::get<std::int32_t>(active.LastError()) == 91, "the existing typed failure must survive the nested original invocation boundary");
		}

		return SquirrelNativeResult::StackStatus(-1);
	});
	auto program = Program({Instruction(56, 8, 5), Instruction(8, 1, 0, 0, 2), Instruction(6, 7, 1, 2, 1), Instruction(57, 1), Instruction(2, 8, -1), Instruction(19, 1, 8), Instruction(19, 1, 8)});
	program.MainFunction.Literals = {std::string("fail")};
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	Require(std::get<std::int32_t>(vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value) == 91, "negative native status must propagate the existing typed last error into the script trap");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestEmptyPositiveReturnRejectsBeforeReadingOutsideFrame() {
	SquirrelVm vm;
	const auto callback = vm.CreateNativeClosure("empty-positive", [](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments.empty() && active.NativeStackSize() == 0, "the empty native callback must expose an empty logical frame");

		return SquirrelNativeResult::StackStatus(1);
	});
	bool rejected = false;
	try {
		static_cast<void>(vm.Invoke(callback, {}));
	} catch (const std::runtime_error& error) {
		rejected = std::string(error.what()).find("requires a current stack value") != std::string::npos;
	}

	Require(rejected, "a positive status on an empty frame must reject before dereferencing a preceding physical cell");
	const auto valid = vm.CreateNativeClosure("empty-zero", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::NoValues();
	});
	Require(vm.Type(vm.Invoke(valid, {}).Value) == sgre::script::SquirrelValueType::Null, "empty-frame status zero must remain valid after a rejected return");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestExplicitNativeSuspensionUsesExistingTop() {
	SquirrelVm vm;
	vm.RegisterNative("pause", [](SquirrelVm& active, std::span<const SquirrelValue>) {
		Require(std::get<std::int32_t>(active.NativeStackValue(-1)) == 23, "the suspend callback must observe its actual current last argument");

		return SquirrelNativeResult::StackStatus(-666);
	});
	auto program = Program({Instruction(8, 1, 0, 0, 2), Instruction(2, 3, 23), Instruction(6, 4, 1, 2, 2), Instruction(19, 1, 4)});
	program.MainFunction.Literals = {std::string("pause")};
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	const auto suspended = vm.Invoke(vm.CreateClosure(std::move(program)), arguments);
	Require(suspended.State == sgre::script::SquirrelRunState::Suspended && std::get<std::int32_t>(suspended.Value) == 23, "native -666 must copy its actual last stack value and suspend the original script frame");
	Require(std::get<std::int32_t>(vm.Resume(std::int32_t{51}).Value) == 51, "resume must deliver the resumed value to the original native call destination");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestDirectNativeSuspendRequestIsNotScriptSuspension() {
	SquirrelVm vm;
	const auto callback = vm.CreateNativeClosure("direct-pause-request", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::StackStatus(-666);
	});
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), std::int32_t{23}};
	const auto result = vm.Invoke(callback, arguments);
	Require(result.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(result.Value) == 23 && !vm.IsSuspended(), "GenericCall must consume the native suspension-request value without suspending a script dispatcher");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNativeGetterSuspendRequestReturnsItsValue() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("_get"), vm.CreateNativeClosure("native-get-request", [](SquirrelVm& active, std::span<const SquirrelValue>) {
		active.PushNativeValue(std::int32_t{41});

		return SquirrelNativeResult::StackStatus(-666);
	}));
	const auto instance = vm.CreateInstance(class_value);
	Require(std::get<std::int32_t>(vm.ReadSlot(instance, std::string("independent-property"))) == 41 && !vm.IsSuspended(), "a native generic getter call must return the requested value while discarding its local suspend flag");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNativeConstructorSuspendRequestHasNoDispatcherDestination(bool call_from_script) {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("constructor"), vm.CreateNativeClosure("native-constructor-request", [](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 1 && active.Type(arguments[0]) == sgre::script::SquirrelValueType::Instance, "the native constructor must receive its actual allocated instance");
		active.PushNativeValue(std::int32_t{43});

		return SquirrelNativeResult::StackStatus(-666);
	}));
	SquirrelValue instance;
	if (call_from_script) {
		auto program = Program({Instruction(10, 2, 0), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)});
		program.MainFunction.Parameters.push_back(std::string("class"));
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), class_value};
		instance = vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
	} else {
		const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
		instance = vm.Invoke(class_value, arguments).Value;
	}

	Require(vm.Type(instance) == sgre::script::SquirrelValueType::Instance && vm.IsInstanceOf(instance, class_value) && !vm.IsSuspended(), "constructor GenericCall must discard a native local suspension request and return the allocated instance");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const auto return_case : {ReturnCase::NoValues, ReturnCase::ExistingTop, ReturnCase::OneNull, ReturnCase::PushedStringWithoutValue, ReturnCase::OneString, ReturnCase::TwoValues, ReturnCase::Compatibility}) {
			TestPhysicalNativeReturn(return_case);
		}

		TestStackAccessBoundsAndFailure();
		TestNegativeStatusPreservesTypedLastErrorAndTrap();
		TestEmptyPositiveReturnRejectsBeforeReadingOutsideFrame();
		TestExplicitNativeSuspensionUsesExistingTop();
		TestDirectNativeSuspendRequestIsNotScriptSuspension();
		TestNativeGetterSuspendRequestReturnsItsValue();
		TestNativeConstructorSuspendRequestHasNoDispatcherDestination(false);
		TestNativeConstructorSuspendRequestHasNoDispatcherDestination(true);
		std::cout << "Squirrel explicit native stack checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
