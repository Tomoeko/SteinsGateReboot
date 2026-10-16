#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first, std::int32_t second = 0, std::uint8_t third = 0, std::uint8_t fourth = 0) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = first;
	instruction.Argument1 = second;
	instruction.Argument2 = third;
	instruction.Argument3 = fourth;

	return instruction;
}

static sgre::script::BytecodeProgram Program(std::vector<sgre::script::SquirrelInstruction> instructions) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-native-capture-fixture");
	program.MainFunction.Name = std::string("main");
	program.MainFunction.Parameters = {std::string("this")};
	program.MainFunction.StackSize = 8;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static void TestCaptureOrder(std::size_t capture_count, bool script_call) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	std::vector<SquirrelValue> pushed;
	for (std::size_t index = 0; index < capture_count; ++index) {
		if (index % 2 == 0) {
			pushed.push_back(vm.CreateString("independent-native-capture-" + std::to_string(index)));
		} else {
			pushed.push_back(static_cast<std::int32_t>(100 + index));
		}
	}

	std::size_t callbacks = 0;
	sgre::script::SquirrelNativeSignature signature;
	signature.ParameterCheck = 3;
	signature.TypeCheck = {2, 2, 2};
	const auto closure = vm.CreateNativeClosureWithCaptures("capture_order", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		++callbacks;
		Require(arguments.size() == 3 + capture_count && active.NativeStackSize() == arguments.size(), "native callbacks must observe original users followed by actual captures");
		for (std::size_t index = 0; index < 3; ++index) {
			Require(std::get<std::int32_t>(arguments[index]) == static_cast<std::int32_t>((index + 1) * 10), "capture append must preserve all original user argument cells");
		}

		for (std::size_t index = 0; index < capture_count; ++index) {
			const auto& expected = pushed[capture_count - index - 1];
			Require(arguments[3 + index] == expected && active.NativeStackValue(static_cast<std::int32_t>(4 + index)) == expected, "native stored capture order must reverse construction stack push order");
		}

		return SquirrelNativeResult::ReturnValue(std::int32_t{99});
	},
	    pushed, signature);
	if (script_call) {
		vm.NewSlot(vm.RootTable(), std::string("capture_order"), closure);
		auto program = Program({Instruction(8, 1, 0, 0, 2), Instruction(2, 2, 10), Instruction(2, 3, 20), Instruction(2, 4, 30), Instruction(6, 5, 1, 2, 3), Instruction(19, 1, 5)});
		program.MainFunction.Literals = {std::string("capture_order")};
		const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
		Require(std::get<std::int32_t>(vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value) == 99, "bytecode call must consume its explicit native return after capture append");
	} else {
		const std::array<SquirrelValue, 3> arguments{std::int32_t{10}, std::int32_t{20}, std::int32_t{30}};
		Require(std::get<std::int32_t>(vm.Invoke(closure, arguments).Value) == 99, "generic native call must return its explicit value after capture append");
	}

	Require(callbacks == 1, "each independent capture order path must invoke its callback once");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestUserSignatureIsIndependentOfCaptures() {
	SquirrelVm vm;
	std::size_t callbacks = 0;
	const std::array<SquirrelValue, 3> captures{std::int32_t{11}, std::int32_t{22}, std::int32_t{33}};
	sgre::script::SquirrelNativeSignature signature;
	signature.ParameterCheck = 2;
	signature.TypeCheck = {32, 2};
	const auto closure = vm.CreateNativeClosureWithCaptures("signature", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		++callbacks;
		Require(arguments.size() == 5 && active.NativeStackSize() == 5, "exact parameter two must accept two users plus three captures");

		return SquirrelNativeResult::NoValues();
	},
	    captures, signature);
	const std::array<SquirrelValue, 1> short_arguments{vm.RootTable()};
	bool rejected = false;
	try {
		static_cast<void>(vm.Invoke(closure, short_arguments));
	} catch (const sgre::script::SquirrelVmException& error) {
		rejected = std::string(error.what()).find("wrong number of parameters") != std::string::npos;
	}

	Require(rejected && callbacks == 0, "captured values must not satisfy missing original user parameters");
	const std::array<SquirrelValue, 2> wrong_type{vm.RootTable(), true};
	rejected = false;
	try {
		static_cast<void>(vm.Invoke(closure, wrong_type));
	} catch (const sgre::script::SquirrelVmException& error) {
		rejected = std::string(error.what()).find("type mismatch") != std::string::npos;
	}

	Require(rejected && callbacks == 0, "native type masks must still select original user positions");
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), std::int32_t{9}};
	static_cast<void>(vm.Invoke(closure, arguments));
	Require(callbacks == 1, "failed signatures must leave the closure and context reusable");
	vm.Close();
	vm.RethrowReleaseFailure();
}

enum class CaptureReturn {
	NoValues,
	ExistingLast,
	Failure
};

static void TestInactivePhysicalCaptureLifetime(CaptureReturn return_case) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	std::array<SquirrelValue, 2> pushed{vm.CreateString("independent-native-first-pushed"), vm.CreateString("independent-native-second-pushed")};
	const std::array<SquirrelValue, 2> weak{vm.CreateWeakReference(pushed[0]), vm.CreateWeakReference(pushed[1])};
	SquirrelValue closure = vm.CreateNativeClosureWithCaptures("physical_captures", [return_case](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(active.NativeStackSize() == 3 && arguments.size() == 3, "physical native capture cells must belong to the active callback frame");
		Require(sgre::script::StringText(arguments[1]) == "independent-native-second-pushed" && sgre::script::StringText(active.NativeStackValue(-1)) == "independent-native-first-pushed", "the physical top must select the first pushed captured string");

		return return_case == CaptureReturn::NoValues ? SquirrelNativeResult::NoValues() : SquirrelNativeResult::StackStatus(return_case == CaptureReturn::Failure ? -1 : 1);
	},
	    pushed);
	pushed = {};
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	bool failed = false;
	try {
		const auto result = vm.Invoke(closure, arguments);
		if (return_case == CaptureReturn::ExistingLast) {
			Require(sgre::script::StringText(result.Value) == "independent-native-first-pushed", "positive native status must return the real last capture when no value was pushed");
		} else {
			Require(vm.Type(result.Value) == SquirrelValueType::Null, "zero native status must leave captured cells without creating a result");
		}
	} catch (const sgre::script::SquirrelVmException& error) {
		failed = vm.Type(error.ThrownValue()) == SquirrelValueType::Null;
	}

	Require(failed == (return_case == CaptureReturn::Failure), "negative native capture status must preserve its existing error value");
	const auto clear_result = vm.CreateNativeClosure("clear_result", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::NoValues();
	});
	const auto clear_result_root = vm.Retain(clear_result);
	static_cast<void>(vm.Invoke(clear_result, arguments));
	closure = {};
	Require((vm.Type(vm.ResolveWeakReference(weak[0])) == SquirrelValueType::String) == (return_case != CaptureReturn::ExistingLast), "only an actual positive return must clear the last physical capture cell before closure retirement");
	Require(vm.Type(vm.ResolveWeakReference(weak[1])) == SquirrelValueType::String, "restoring native logical top must preserve earlier inactive physical capture ownership");
	Require(vm.CollectGarbage() == 0, "allocated inactive string cells must remain reachable after their closure owner is deleted");
	const auto grow = vm.CreateNativeClosure("clear_hidden_captures", [](SquirrelVm& active, std::span<const SquirrelValue>) {
		active.SetNativeStackSize(3);
		Require(active.Type(active.NativeStackValue(2)) == SquirrelValueType::Null && active.Type(active.NativeStackValue(3)) == SquirrelValueType::Null, "logical growth must overwrite hidden captured values with actual null cells");

		return SquirrelNativeResult::NoValues();
	});
	static_cast<void>(vm.Invoke(grow, arguments));
	Require(vm.Type(vm.ResolveWeakReference(weak[0])) == SquirrelValueType::Null && vm.Type(vm.ResolveWeakReference(weak[1])) == SquirrelValueType::Null, "overwriting the last physical capture owners must expire their canonical weak targets");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCaptureOwnedReleaseOrder(bool bound) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto owner_class = vm.CreateClass();
	std::array<SquirrelValue, 3> owners{vm.CreateInstance(owner_class), vm.CreateInstance(owner_class), vm.CreateInstance(owner_class)};
	std::vector<std::size_t> releases;
	SquirrelValue closure_weak;
	for (std::size_t index = 0; index < owners.size(); ++index) {
		vm.SetInstanceReleaseHook(owners[index], [&, index](SquirrelVm& active, const SquirrelValue&) {
			Require(active.Type(active.ResolveWeakReference(closure_weak)) == SquirrelValueType::NativeClosure, "native closure identity must remain alive throughout owned capture retirement");
			releases.push_back(index);
		});
	}

	SquirrelValue closure = vm.CreateNativeClosureWithCaptures("release_order", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::NoValues();
	},
	    owners);
	const auto environment = vm.CreateTable();
	if (bound) {
		closure = vm.BindEnvironment(closure, environment);
	}

	closure_weak = vm.CreateWeakReference(closure);
	owners = {};
	closure = {};
	vm.RethrowReleaseFailure();
	Require(releases == std::vector<std::size_t>{2, 1, 0}, "native capture retirement must traverse stored order, the reverse of original stack pushes");
	Require(vm.Type(vm.ResolveWeakReference(closure_weak)) == SquirrelValueType::Null, "closure weak identity must expire after all owned capture callbacks");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestBoundCaptureRawValuesAndCollection() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue environment = vm.CreateTable();
	const auto environment_identity = vm.BorrowObjectIdentity(environment);
	SquirrelValue target = vm.CreateTable();
	const auto target_weak = vm.CreateWeakReference(target);
	const std::array<SquirrelValue, 4> pushed{std::monostate{}, true, std::bit_cast<float>(std::uint32_t{0x80000000}), target_weak};
	const auto closure = vm.CreateNativeClosureWithCaptures("raw_values", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 5 && active.NativeStackSize() == 5, "bound native closure must preserve all appended raw captured values");
		Require(active.Type(arguments[0]) == SquirrelValueType::Null && !active.ResolveObjectIdentity(environment_identity), "expired bound environment must replace only the original receiver with null");
		Require(arguments[1] == target_weak && active.Type(arguments[1]) == SquirrelValueType::WeakReference && active.Type(active.ResolveWeakReference(arguments[1])) == SquirrelValueType::Null, "captured weak wrappers must remain raw while their unowned target expires");
		Require(std::bit_cast<std::uint32_t>(std::get<float>(arguments[2])) == 0x80000000 && std::get<bool>(arguments[3]) && active.Type(arguments[4]) == SquirrelValueType::Null, "native capture transfer must preserve scalar types, signed zero and null payloads");
		Require(active.CollectGarbage() == 0, "active closure/capture ownership must remain traced during native collection");

		return SquirrelNativeResult::NoValues();
	},
	    pushed);
	const auto original_root = vm.Retain(closure);
	SquirrelValue bound = vm.BindEnvironment(closure, environment);
	auto bound_root = vm.Retain(bound);
	environment = {};
	target = {};
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	static_cast<void>(vm.Invoke(bound, arguments));
	bound_root = {};
	bound = {};
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCapturedContainerCycle() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue container = vm.CreateTable();
	const auto container_weak = vm.CreateWeakReference(container);
	std::array<SquirrelValue, 1> pushed{container};
	SquirrelValue closure = vm.CreateNativeClosureWithCaptures("cycle", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::NoValues();
	},
	    pushed);
	const auto closure_weak = vm.CreateWeakReference(closure);
	vm.NewSlot(container, std::string("closure"), closure);
	auto root = vm.Retain(closure);
	container = {};
	pushed = {};
	closure = {};
	Require(vm.CollectGarbage() == 0, "real native closure capture edges must retain their rooted cyclic container graph");
	root = {};
	Require(vm.CollectGarbage() == 1, "one unreachable captured-container cycle must be finalized through the native owned capture edge");
	Require(vm.Type(vm.ResolveWeakReference(container_weak)) == SquirrelValueType::Null && vm.Type(vm.ResolveWeakReference(closure_weak)) == SquirrelValueType::Null, "unreachable closure/container cycles must expire both borrowed weak identities");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCapturedNativeSuspendInChild() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const std::array<SquirrelValue, 2> captures{std::int32_t{23}, std::int32_t{9}};
	sgre::script::SquirrelNativeSignature signature;
	signature.ParameterCheck = 1;
	const auto closure = vm.CreateNativeClosureWithCaptures("captured_suspend", [](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 3 && active.NativeStackSize() == 3 && std::get<std::int32_t>(active.NativeStackValue(-1)) == 23, "raw native suspension must read the actual last captured value");

		return SquirrelNativeResult::StackStatus(-666);
	},
	    captures, signature);
	vm.NewSlot(vm.RootTable(), std::string("captured_suspend"), closure);
	auto program = Program({Instruction(8, 1, 0, 0, 2), Instruction(2, 2, 17), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)});
	program.MainFunction.Literals = {std::string("captured_suspend")};
	const auto child = vm.CreateContext();
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	const auto first = vm.InvokeInContext(child, vm.CreateClosure(std::move(program)), arguments);
	Require(first.State == sgre::script::SquirrelRunState::Suspended && std::get<std::int32_t>(first.Value) == 23, "script CALL must consume native capture-backed suspension independently of its original receiver");
	Require(vm.CurrentContext() == vm.RootContext(), "capture-backed child suspension must restore the host context");
	Require(vm.CollectGarbage() == 0, "suspended child call metadata must retain its real closure and captured values");
	const auto resumed = vm.ResumeInContext(child, std::int32_t{53});
	Require(resumed.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(resumed.Value) == 53, "child wakeup must replace only the suspended result without appending captures a second time");
	vm.ReleaseContext(child);
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCaptureBoundsAndForeignValues() {
	sgre::script::SquirrelVmLimits limits;
	limits.MaxRegisters = 4;
	SquirrelVm vm(limits);
	const std::array<SquirrelValue, 5> too_many{};
	bool rejected = false;
	try {
		static_cast<void>(vm.CreateNativeClosureWithCaptures("bounded", [](SquirrelVm&, std::span<const SquirrelValue>) {
			return SquirrelNativeResult::NoValues();
		},
		    too_many));
	} catch (const std::runtime_error& error) {
		rejected = std::string(error.what()).find("capture count exceeds") != std::string::npos;
	}

	Require(rejected, "capture construction must reject its explicit allocation bound before accepting ownership");
	SquirrelVm other;
	const std::array<SquirrelValue, 1> foreign{other.CreateTable()};
	rejected = false;
	try {
		static_cast<void>(vm.CreateNativeClosureWithCaptures("foreign", [](SquirrelVm&, std::span<const SquirrelValue>) {
			return SquirrelNativeResult::NoValues();
		},
		    foreign));
	} catch (const std::runtime_error& error) {
		rejected = std::string(error.what()).find("does not belong to this VM") != std::string::npos;
	}

	Require(rejected, "native capture construction must preserve the existing VM owner check");
	const std::array<SquirrelValue, 4> captures{};
	std::size_t callbacks = 0;
	const auto closure = vm.CreateNativeClosureWithCaptures("combined_bound", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		++callbacks;

		return SquirrelNativeResult::NoValues();
	},
	    captures);
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	rejected = false;
	try {
		static_cast<void>(vm.Invoke(closure, arguments));
	} catch (const std::runtime_error& error) {
		rejected = std::string(error.what()).find("captures and arguments exceed") != std::string::npos;
	}

	Require(rejected && callbacks == 0, "combined user/capture stack extent must reject safely before native callback execution");
	vm.Close();
	vm.RethrowReleaseFailure();
	other.Close();
	other.RethrowReleaseFailure();
}

int main() {
	try {
		for (const auto count : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{5}}) {
			TestCaptureOrder(count, false);
			TestCaptureOrder(count, true);
		}

		TestUserSignatureIsIndependentOfCaptures();
		for (const auto return_case : {CaptureReturn::NoValues, CaptureReturn::ExistingLast, CaptureReturn::Failure}) {
			TestInactivePhysicalCaptureLifetime(return_case);
		}

		TestCaptureOwnedReleaseOrder(false);
		TestCaptureOwnedReleaseOrder(true);
		TestBoundCaptureRawValuesAndCollection();
		TestCapturedContainerCycle();
		TestCapturedNativeSuspendInChild();
		TestCaptureBoundsAndForeignValues();
		std::cout << "eighteen independent native capture fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
