#include "sgre/script/squirrel_vm.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelFunction;
using sgre::script::SquirrelInstruction;
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

static SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t argument1 = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
	SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = destination;
	instruction.Argument1 = argument1;
	instruction.Argument2 = argument2;
	instruction.Argument3 = argument3;

	return instruction;
}

static BytecodeProgram Program(std::vector<SquirrelInstruction> instructions, std::uint32_t registers = 8) {
	BytecodeProgram program;
	program.MainFunction.Name = std::string("main");
	program.MainFunction.SourceName = std::string("independent-fixture.nut");
	program.MainFunction.Parameters.push_back(std::string("this"));
	program.MainFunction.StackSize = registers;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static SquirrelValue Run(SquirrelVm& vm, BytecodeProgram program) {
	const auto closure = vm.CreateClosure(std::move(program));
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	const auto result = vm.Invoke(closure, arguments);
	Require(result.State == sgre::script::SquirrelRunState::Completed, "fixture suspended unexpectedly");

	return result.Value;
}

static SquirrelValue BinaryOperation(std::uint8_t opcode, std::uint8_t operation, SquirrelValue left, SquirrelValue right) {
	SquirrelVm vm;
	auto program = Program({Instruction(opcode, 3, 2, 1, operation), Instruction(19, 1, 3)});
	program.MainFunction.Parameters.push_back(std::string("left"));
	program.MainFunction.Parameters.push_back(std::string("right"));
	const auto closure = vm.CreateClosure(std::move(program));
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), left, right};

	return vm.Invoke(closure, arguments).Value;
}

static void TestArithmetic() {
	Require(std::get<std::int32_t>(BinaryOperation(17, '+', std::numeric_limits<std::int32_t>::max(), std::int32_t{1})) == std::numeric_limits<std::int32_t>::min(), "integer addition did not wrap");
	Require(std::get<std::int32_t>(BinaryOperation(17, '*', std::int32_t{0x40000000}, std::int32_t{4})) == 0, "integer multiply did not wrap");
	Require(std::get<std::int32_t>(BinaryOperation(17, '/', std::int32_t{-9}, std::int32_t{4})) == -2, "integer division did not truncate toward zero");
	Require(std::get<std::int32_t>(BinaryOperation(17, '%', std::int32_t{-9}, std::int32_t{4})) == -1, "integer remainder sign changed");
	Require(std::get<float>(BinaryOperation(17, '+', std::int32_t{16777217}, 1.0f)) == 16777216.0f, "mixed arithmetic did not round to float32");
	Require(std::bit_cast<std::uint32_t>(std::get<float>(BinaryOperation(17, '*', 0.0f, std::numeric_limits<float>::infinity()))) == 0xffc00000, "invalid floating arithmetic must retain the native negative indefinite NaN");
	const auto left_nan = std::bit_cast<float>(std::uint32_t{0x7fc01234});
	const auto right_nan = std::bit_cast<float>(std::uint32_t{0xffc05678});
	Require(std::bit_cast<std::uint32_t>(std::get<float>(BinaryOperation(17, '+', left_nan, right_nan))) == 0xffc05678, "native add must prioritize the right NaN payload");
	Require(std::bit_cast<std::uint32_t>(std::get<float>(BinaryOperation(17, '-', left_nan, right_nan))) == 0x7fc01234, "native subtract must prioritize the left NaN payload");
	Require(std::isinf(std::get<float>(BinaryOperation(17, '/', 1.0f, 0.0f))), "float division by zero did not produce infinity");
	RequireFailure([] {
		static_cast<void>(BinaryOperation(17, '/', std::int32_t{1}, std::int32_t{0}));
	},
	    "division by zero");
	RequireFailure([] {
		static_cast<void>(BinaryOperation(17, '/', std::numeric_limits<std::int32_t>::min(), std::int32_t{-1}));
	},
	    "native integer division overflow");
}

static void TestComparison() {
	Require(!std::get<bool>(BinaryOperation(15, 0, 0.0f, -0.0f)), "same-type float equality must compare payload bits");
	const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc01234});
	Require(std::get<bool>(BinaryOperation(15, 0, nan, nan)), "equal NaN payloads must compare equal in this VM");
	Require(std::get<bool>(BinaryOperation(15, 0, std::int32_t{16777217}, 16777216.0f)), "mixed equality did not use float32 conversion");
	Require(std::get<bool>(BinaryOperation(40, 3, std::numeric_limits<std::int32_t>::max(), std::int32_t{-1})), "native integer ordering did not wrap subtraction");
	Require(std::get<bool>(BinaryOperation(40, 0, std::string("z"), std::string("a"))), "string ordering changed");
}

static void TestBitwise() {
	Require(std::get<std::int32_t>(BinaryOperation(18, 4, std::int32_t{1}, std::int32_t{33})) == 2, "shift count was not masked to five bits");
	Require(std::get<std::int32_t>(BinaryOperation(18, 5, std::int32_t{-4}, std::int32_t{1})) == -2, "arithmetic right shift changed");
	Require(std::get<std::int32_t>(BinaryOperation(18, 6, std::int32_t{-4}, std::int32_t{1})) == 2147483646, "logical right shift changed");
	RequireFailure([] {
		static_cast<void>(BinaryOperation(18, 0, 1.0f, std::int32_t{1}));
	},
	    "expected an integer");
}

static void TestBranchAndUnary() {
	SquirrelVm vm;
	const auto value = Run(vm, Program({Instruction(2, 1, 0), Instruction(26, 1, 1), Instruction(2, 2, 99), Instruction(2, 2, 12), Instruction(19, 1, 2)}));
	Require(std::get<std::int32_t>(value) == 12, "relative branch did not start at next instruction");
	const auto negative_zero = Run(vm, Program({Instruction(3, 1, 0), Instruction(45, 2, 1), Instruction(19, 1, 2)}));
	Require(std::bit_cast<std::uint32_t>(std::get<float>(negative_zero)) == 0x80000000, "float negation did not toggle the sign bit");
	const auto wrapped_negation = Run(vm, Program({Instruction(2, 1, std::numeric_limits<std::int32_t>::min()), Instruction(45, 2, 1), Instruction(19, 1, 2)}));
	Require(std::get<std::int32_t>(wrapped_negation) == std::numeric_limits<std::int32_t>::min(), "integer negation did not wrap");
}

static void TestContainersAndOwnership() {
	SquirrelVm vm;
	const auto table = vm.CreateTable();
	vm.NewSlot(table, std::string("self"), table);
	vm.NewSlot(table, std::int32_t{1}, std::string("first"));
	Require(!vm.HasSlot(table, 1.0f), "native table keys must retain numeric type identity");
	Require(StringText(vm.GetSlot(table, std::int32_t{1})) == "first", "integer table key changed");
	vm.SetSlot(table, std::string("self"), std::int32_t{7});
	Require(std::get<std::int32_t>(vm.GetSlot(table, std::string("self"))) == 7, "table assignment failed");
	const std::array<SquirrelValue, 2> elements{std::int32_t{5}, std::int32_t{8}};
	const auto array = vm.CreateArray(elements);
	Require(std::get<std::int32_t>(vm.GetSlot(array, 1.9f)) == 8, "array float index did not truncate");
	Require(std::get<std::int32_t>(vm.GetSlot(std::string("abc"), std::int32_t{-1})) == 'c', "string negative indexing changed");
	Require(std::get<std::int32_t>(vm.GetSlot(std::string(1, static_cast<char>(0xff)), std::int32_t{0})) == -1, "native signed character indexing changed");
	RequireFailure([&] {
		static_cast<void>(vm.GetSlot(std::string("abc"), std::int32_t{-3}));
	},
	    "string index exceeds");
	RequireFailure([&] {
		vm.NewSlot(table, SquirrelValue{}, std::int32_t{0});
	},
	    "null cannot");
	SquirrelVm other;
	RequireFailure([&] {
		static_cast<void>(other.GetSlot(table, std::string("self")));
	},
	    "does not belong");
}

static void TestClosureCaptureAndDefaults() {
	SquirrelVm vm;
	auto program = Program({Instruction(2, 1, 42), Instruction(48, 2, 0), Instruction(2, 1, 9), Instruction(10, 3, 0), Instruction(6, 4, 2, 3, 1), Instruction(19, 1, 4)});
	SquirrelFunction child;
	child.Name = std::string("capture");
	child.SourceName = std::string("independent-fixture.nut");
	child.Parameters.push_back(std::string("this"));
	child.Parameters.push_back(std::string("default"));
	child.StackSize = 4;
	child.DefaultParameters.push_back(1);
	sgre::script::SquirrelOuterBinding binding;
	binding.Type = 0;
	binding.Source = std::int32_t{1};
	binding.Name = std::string("captured");
	child.OuterBindings.push_back(binding);
	child.Instructions = {Instruction(27, 2, 0), Instruction(17, 3, 1, 2, '+'), Instruction(19, 1, 3)};
	program.MainFunction.Functions.push_back(std::move(child));
	Require(std::get<std::int32_t>(Run(vm, std::move(program))) == 84, "closure captures/defaults did not retain creation-time values");
}

static void TestNativeSuspension() {
	SquirrelVm vm;
	vm.RegisterNative("pause", [](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 1, "native receiver was not passed");
		sgre::script::SquirrelNativeResult result;
		result.Value = std::string("paused");
		result.Suspend = true;

		return result;
	});
	auto program = Program({Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)});
	program.MainFunction.Literals.push_back(std::string("pause"));
	const auto closure = vm.CreateClosure(std::move(program));
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	const auto paused = vm.Invoke(closure, arguments);
	Require(paused.State == sgre::script::SquirrelRunState::Suspended && vm.IsSuspended(), "native callback did not suspend the VM");
	Require(StringText(paused.Value) == "paused", "suspension payload lost");
	const auto resumed = vm.Resume(std::int32_t{73});
	Require(resumed.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(resumed.Value) == 73, "resumption did not assign native call destination");
}

static void TestTraps() {
	SquirrelVm vm;
	auto program = Program({Instruction(56, 3, 3), Instruction(2, 1, 91), Instruction(58, 1), Instruction(57, 1), Instruction(19, 1, 3)});
	Require(std::get<std::int32_t>(Run(vm, std::move(program))) == 91, "exception trap did not carry the thrown value");
	const auto error = Run(vm, Program({Instruction(56, 3, 4), Instruction(2, 1, 1), Instruction(2, 2, 0), Instruction(17, 4, 2, 1, '/'), Instruction(57, 1), Instruction(19, 1, 3)}));
	Require(StringText(error).find("division by zero") != std::string::npos, "runtime error did not reach the script trap");
}

static void TestGenerator() {
	SquirrelVm vm;
	auto program = Program({Instruction(48, 1, 0), Instruction(10, 2, 0), Instruction(6, 3, 1, 2, 1), Instruction(50, 4, 3), Instruction(50, 5, 3), Instruction(17, 6, 5, 4, '+'), Instruction(19, 1, 6)});
	SquirrelFunction child;
	child.Name = std::string("numbers");
	child.SourceName = std::string("independent-fixture.nut");
	child.Parameters.push_back(std::string("this"));
	child.StackSize = 2;
	child.IsGenerator = true;
	child.Instructions = {Instruction(2, 1, 5), Instruction(49, 1, 1), Instruction(2, 1, 7), Instruction(19, 1, 1)};
	program.MainFunction.Functions.push_back(std::move(child));
	Require(std::get<std::int32_t>(Run(vm, std::move(program))) == 12, "generator yield/resume/return ordering failed");
}

static void TestClassesAndConstructors() {
	SquirrelVm vm;
	auto program = Program({Instruction(59, 1, -1, 255), Instruction(1, 2, 0), Instruction(2, 3, 5), Instruction(60, 0, 1, 2, 3), Instruction(1, 2, 1), Instruction(48, 3, 0), Instruction(60, 0, 1, 2, 3), Instruction(10, 2, 0), Instruction(2, 3, 7), Instruction(6, 4, 1, 2, 2), Instruction(9, 5, 0, 4), Instruction(19, 1, 5)});
	program.MainFunction.Literals = {std::string("value"), std::string("constructor")};
	SquirrelFunction constructor;
	constructor.Name = std::string("constructor");
	constructor.SourceName = std::string("independent-fixture.nut");
	constructor.Parameters = {std::string("this"), std::string("initial")};
	constructor.StackSize = 4;
	constructor.Literals = {std::string("value")};
	constructor.Instructions = {Instruction(1, 2, 0), Instruction(13, 3, 0, 2, 1), Instruction(19, 255)};
	program.MainFunction.Functions.push_back(std::move(constructor));
	Require(std::get<std::int32_t>(Run(vm, std::move(program))) == 7, "constructor did not receive the created instance and initialize its field");

	auto inheritance = Program({Instruction(59, 1, -1, 255), Instruction(1, 2, 0), Instruction(2, 3, 11), Instruction(60, 0, 1, 2, 3), Instruction(59, 4, 1, 255), Instruction(10, 2, 0), Instruction(6, 5, 4, 2, 1), Instruction(42, 6, 1, 5), Instruction(26, 6, 2), Instruction(9, 7, 0, 5), Instruction(19, 1, 7), Instruction(2, 7, -1), Instruction(19, 1, 7)});
	inheritance.MainFunction.Literals = {std::string("inherited")};
	Require(std::get<std::int32_t>(Run(vm, std::move(inheritance))) == 11, "inherited fields or instanceof changed");

	RequireFailure([&] {
		static_cast<void>(Run(vm, Program({Instruction(59, 1, -1, 255), Instruction(10, 2, 0), Instruction(6, 3, 1, 2, 1), Instruction(2, 4, 1), Instruction(2, 5, 2), Instruction(60, 0, 1, 4, 5), Instruction(19, 255)})));
	},
	    "class is locked");
}

static SquirrelValue InheritThroughInstruction(SquirrelVm& vm, const SquirrelValue& base, const SquirrelValue& attributes) {
	auto program = Program({Instruction(59, 3, 1, 2), Instruction(19, 1, 3)});
	program.MainFunction.Parameters.push_back(std::string("base"));
	program.MainFunction.Parameters.push_back(std::string("attributes"));
	const auto closure = vm.CreateClosure(std::move(program));
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), base, attributes};

	return vm.Invoke(closure, arguments).Value;
}

static void TestClassHooksAndCachedMethods() {
	SquirrelVm vm;
	const auto base = vm.CreateClass();
	std::size_t inherited_calls = 0;
	vm.NewSlot(base, std::string("_inherited"), vm.CreateNativeClosure("inherit_hook", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && arguments[0] != base && current.Type(arguments[0]) == sgre::script::SquirrelValueType::Class, "_inherited must receive the new class and its attributes");
		++inherited_calls;
		current.NewSlot(arguments[0], std::string("inherited_attribute"), arguments[1]);

		return sgre::script::SquirrelNativeResult{};
	}));
	const auto derived = InheritThroughInstruction(vm, base, std::int32_t{73});
	Require(inherited_calls == 1 && std::get<std::int32_t>(vm.GetSlot(derived, std::string("inherited_attribute"))) == 73, "inherited hook did not retain its mutation");
	Require(!vm.HasSlot(base, std::string("inherited_attribute")), "inherited hook mutated its base receiver");

	const auto failed_base = vm.CreateClass();
	auto failed = Program({Instruction(2, 2, 91), Instruction(58, 2)}, 3);
	failed.MainFunction.Parameters.push_back(std::string("attributes"));
	vm.NewSlot(failed_base, std::string("_inherited"), vm.CreateClosure(std::move(failed)));
	Require(vm.Type(InheritThroughInstruction(vm, failed_base, {})) == sgre::script::SquirrelValueType::Class, "native inheritance creation ignores a failed _inherited call status");

	const auto member_class = vm.CreateClass();
	std::size_t member_calls = 0;
	vm.NewSlot(member_class, std::string("_newmember"), vm.CreateNativeClosure("member_hook", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 4 && arguments[0] == member_class && StringText(arguments[1]) == "created" && std::get<std::int32_t>(arguments[3]) == 41, "_newmember must receive class/key/value/attributes without an extra static parameter");
		++member_calls;
		current.NewSlot(arguments[0], arguments[1], std::get<std::int32_t>(arguments[2]) + 1);

		return sgre::script::SquirrelNativeResult{};
	}));
	auto member = Program({Instruction(2, 2, 41), Instruction(1, 3, 0), Instruction(2, 4, 7), Instruction(60, 3, 1, 3, 4), Instruction(9, 5, 0, 1), Instruction(19, 1, 5)});
	member.MainFunction.Parameters.push_back(std::string("class"));
	member.MainFunction.Literals = {std::string("created")};
	const auto member_closure = vm.CreateClosure(std::move(member));
	const std::array<SquirrelValue, 2> member_arguments{vm.RootTable(), member_class};
	Require(std::get<std::int32_t>(vm.Invoke(member_closure, member_arguments).Value) == 8 && member_calls == 1, "successful _newmember must consume normal insertion");

	const auto failed_member_class = vm.CreateClass();
	auto failed_member = Program({Instruction(2, 4, 19), Instruction(58, 4)}, 5);
	failed_member.MainFunction.Parameters = {std::string("this"), std::string("key"), std::string("value"), std::string("attributes")};
	vm.NewSlot(failed_member_class, std::string("_newmember"), vm.CreateClosure(std::move(failed_member)));
	const std::array<SquirrelValue, 2> failed_arguments{vm.RootTable(), failed_member_class};
	Require(std::get<std::int32_t>(vm.Invoke(member_closure, failed_arguments).Value) == 7, "failed _newmember must fall back to the encoded ordinary member insertion");

	const auto cached_base = vm.CreateClass();
	const auto getter = vm.CreateNativeClosure("cached_getter", [](SquirrelVm&, std::span<const SquirrelValue>) {
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{77};

		return result;
	});
	vm.NewSlot(cached_base, std::string("_get"), getter);
	const auto cached_derived = vm.CreateClass(cached_base);
	vm.NewSlot(cached_derived, std::string("_get"), std::int32_t{18});
	const auto cached_instance = vm.CreateInstance(cached_derived);
	Require(std::get<std::int32_t>(vm.GetSlot(cached_instance, std::string("_get"))) == 18, "method-to-field replacement did not create its ordinary field");
	Require(std::get<std::int32_t>(vm.ReadSlot(cached_instance, std::string("missing"))) == 77, "ordinary field replacement must not erase the inherited cached metamethod");
}

static void TestIteratorsAndDelegates() {
	SquirrelVm vm;
	const std::array<SquirrelValue, 3> numbers{std::int32_t{2}, std::int32_t{4}, std::int32_t{6}};
	const auto array = vm.CreateArray(numbers);
	auto program = Program({Instruction(2, 5, 0), Instruction(51, 1, 3, 2), Instruction(52, 1, 3, 2), Instruction(17, 5, 3, 5, '+'), Instruction(24, 0, -4), Instruction(19, 1, 5)});
	program.MainFunction.Parameters.push_back(std::string("numbers"));
	const auto callable = vm.CreateClosure(std::move(program));
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), array};
	Require(std::get<std::int32_t>(vm.Invoke(callable, arguments).Value) == 12, "array iterator did not preserve its cursor or jump over generator guard");

	const auto table = vm.CreateTable();
	vm.NewSlot(table, std::int32_t{2}, std::int32_t{20});
	vm.NewSlot(table, std::int32_t{0}, std::int32_t{0});
	vm.NewSlot(table, std::int32_t{1}, std::int32_t{10});
	auto table_program = Program({Instruction(31, 6, 3), Instruction(51, 1, 3, 2), Instruction(52, 1, 3, 2), Instruction(32, 6, 2), Instruction(24, 0, -4), Instruction(19, 1, 6)});
	table_program.MainFunction.Parameters.push_back(std::string("table"));
	const auto table_callable = vm.CreateClosure(std::move(table_program));
	const std::array<SquirrelValue, 2> table_arguments{vm.RootTable(), table};
	const auto keys = vm.Invoke(table_callable, table_arguments).Value;
	Require(vm.Length(keys) == 3, "table iterator count changed");
	for (std::int32_t index = 0; index < 3; ++index) {
		Require(std::get<std::int32_t>(vm.GetSlot(keys, index)) == index, "table iteration must follow physical bucket order instead of insertion order");
	}

	auto delegate = Program({Instruction(30, 1), Instruction(30, 2), Instruction(1, 3, 0), Instruction(2, 4, 81), Instruction(11, 4, 2, 3, 4), Instruction(53, 1, 1, 2), Instruction(9, 5, 0, 1), Instruction(19, 1, 5)});
	delegate.MainFunction.Literals = {std::string("from_delegate")};
	Require(std::get<std::int32_t>(Run(vm, std::move(delegate))) == 81, "table delegate lookup failed");
	RequireFailure([&] {
		static_cast<void>(Run(vm, Program({Instruction(30, 1), Instruction(53, 1, 1, 1), Instruction(19, 255)})));
	},
	    "delegate cycle detected");
}

static SquirrelValue IteratorPairs(SquirrelVm& vm, const SquirrelValue& iterable) {
	auto program = Program({Instruction(31, 6), Instruction(51, 1, 4, 2), Instruction(52, 1, 4, 2), Instruction(32, 6, 2), Instruction(32, 6, 3), Instruction(24, 0, -5), Instruction(19, 1, 6)});
	program.MainFunction.Parameters.push_back(std::string("iterable"));
	const auto closure = vm.CreateClosure(std::move(program));
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), iterable};

	return vm.Invoke(closure, arguments).Value;
}

static void TestGeneratorIteration() {
	SquirrelVm vm;
	auto numbers = Program({Instruction(2, 1, 3), Instruction(49, 1, 1), Instruction(2, 1, 5), Instruction(49, 1, 1), Instruction(2, 1, 99), Instruction(19, 1, 1)}, 2);
	numbers.MainFunction.IsGenerator = true;
	const auto closure = vm.CreateClosure(std::move(numbers));
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	const auto generator = vm.Invoke(closure, receiver).Value;
	const auto pairs = IteratorPairs(vm, generator);
	const std::array<std::int32_t, 4> expected{0, 3, 1, 5};
	Require(vm.Length(pairs) == expected.size(), "generator foreach included its final return value");
	for (std::size_t index = 0; index < expected.size(); ++index) {
		Require(std::get<std::int32_t>(vm.GetSlot(pairs, static_cast<std::int32_t>(index))) == expected[index], "generator foreach lost its zero-based key or yielded value");
	}

	Require(vm.Length(IteratorPairs(vm, generator)) == 0, "dead generator foreach must finish without resuming");
	auto resume = Program({Instruction(50, 2, 1), Instruction(19, 1, 2)});
	resume.MainFunction.Parameters.push_back(std::string("generator"));
	const auto resume_closure = vm.CreateClosure(std::move(resume));
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), generator};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(resume_closure, arguments));
	},
	    "generator cannot be resumed");
}

static void TestClassIteration() {
	SquirrelVm vm;
	const auto base = vm.CreateClass();
	vm.NewSlot(base, std::int32_t{2}, std::int32_t{20});
	vm.NewSlot(base, std::int32_t{0}, std::int32_t{0});
	vm.NewSlot(base, std::int32_t{1}, std::int32_t{10}, true);
	const auto derived = vm.CreateClass(base);
	vm.NewSlot(derived, std::int32_t{2}, std::int32_t{21});
	const auto pairs = IteratorPairs(vm, derived);
	Require(vm.Length(pairs) == 6, "class iteration omitted inherited members");
	for (std::int32_t index = 0; index < 3; ++index) {
		Require(std::get<std::int32_t>(vm.GetSlot(pairs, index * 2)) == index, "class iteration must follow its physical member-index buckets");
		Require(std::get<std::int32_t>(vm.GetSlot(pairs, index * 2 + 1)) == (index == 2 ? 21 : index * 10), "class iterator exposed encoded member indices instead of values");
	}

	Require(std::get<std::int32_t>(vm.GetSlot(base, std::int32_t{2})) == 20, "derived member replacement changed the base class");
}

static void TestCustomIteration() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("first"), std::int32_t{11});
	auto next = Program({Instruction(26, 1, 5), Instruction(15, 2, 0, 1, 1), Instruction(26, 2, 2), Instruction(1, 2, 1), Instruction(19, 1, 2), Instruction(19, 255), Instruction(1, 2, 0), Instruction(19, 1, 2)}, 3);
	next.MainFunction.Parameters.push_back(std::string("cursor"));
	next.MainFunction.Literals = {std::string("first"), std::string("second")};
	vm.NewSlot(class_value, std::string("_nexti"), vm.CreateClosure(std::move(next)));
	auto getter = Program({Instruction(2, 2, 37), Instruction(19, 1, 2)}, 3);
	getter.MainFunction.Parameters.push_back(std::string("key"));
	vm.NewSlot(class_value, std::string("_get"), vm.CreateClosure(std::move(getter)));
	const auto instance = vm.CreateInstance(class_value);
	const auto pairs = IteratorPairs(vm, instance);
	Require(vm.Length(pairs) == 4, "custom iterator did not stop when _nexti returned null");
	Require(StringText(vm.GetSlot(pairs, std::int32_t{0})) == "first" && std::get<std::int32_t>(vm.GetSlot(pairs, std::int32_t{1})) == 11, "custom iterator did not read its ordinary field");
	Require(StringText(vm.GetSlot(pairs, std::int32_t{2})) == "second" && std::get<std::int32_t>(vm.GetSlot(pairs, std::int32_t{3})) == 37, "custom iterator lost a string cursor or script _get continuation");

	const auto native_class = vm.CreateClass();
	std::size_t calls = 0;
	vm.NewSlot(native_class, std::string("_nexti"), vm.CreateNativeClosure("native_iterator", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2, "_nexti receiver/cursor argument count changed");
		++calls;
		sgre::script::SquirrelNativeResult result;
		if (std::holds_alternative<std::monostate>(arguments[1])) {
			result.Value = std::string("dynamic");
		} else {
			Require(StringText(arguments[1]) == "dynamic", "native iterator cursor was numerically coerced");
		}

		return result;
	}));
	vm.NewSlot(native_class, std::string("_get"), vm.CreateNativeClosure("native_iterator_value", [](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && StringText(arguments[1]) == "dynamic", "iterator _get did not receive its returned key");
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{81};

		return result;
	}));
	const auto native_pairs = IteratorPairs(vm, vm.CreateInstance(native_class));
	Require(calls == 2 && vm.Length(native_pairs) == 2 && std::get<std::int32_t>(vm.GetSlot(native_pairs, std::int32_t{1})) == 81, "nested native iterator continuations lost their value or end branch");

	const auto invalid_class = vm.CreateClass();
	auto invalid_next = Program({Instruction(1, 2, 0), Instruction(19, 1, 2)}, 3);
	invalid_next.MainFunction.Parameters.push_back(std::string("cursor"));
	invalid_next.MainFunction.Literals = {std::string("absent")};
	vm.NewSlot(invalid_class, std::string("_nexti"), vm.CreateClosure(std::move(invalid_next)));
	const auto invalid = vm.CreateInstance(invalid_class);
	RequireFailure([&] {
		static_cast<void>(IteratorPairs(vm, invalid));
	},
	    "_nexti returned an invalid idx");

	const auto failed_class = vm.CreateClass();
	auto failed_next = Program({Instruction(2, 2, 91), Instruction(58, 2)}, 3);
	failed_next.MainFunction.Parameters.push_back(std::string("cursor"));
	vm.NewSlot(failed_class, std::string("_nexti"), vm.CreateClosure(std::move(failed_next)));
	const auto failed = vm.CreateInstance(failed_class);
	RequireFailure([&] {
		static_cast<void>(IteratorPairs(vm, failed));
	},
	    "_nexti failed");
}

static void TestBaseLibrary() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	vm.InstallRecoveredBaseLibrary();
	const auto integer_conversion = vm.GetSlot(std::string("42tail"), std::string("tointeger"));
	const std::array<SquirrelValue, 1> receiver{std::string("42tail")};
	Require(std::get<std::int32_t>(vm.Invoke(integer_conversion, receiver).Value) == 42, "numeric delegate did not accept the native integer prefix");
	const std::array<SquirrelValue, 1> hexadecimal{std::string("0xFF")};
	Require(std::get<std::int32_t>(vm.Invoke(integer_conversion, hexadecimal).Value) == 255, "hexadecimal conversion changed");
	const std::array<SquirrelValue, 1> exponent{std::string("1e3")};
	Require(std::get<std::int32_t>(vm.Invoke(integer_conversion, exponent).Value) == 1, "native decimal-without-dot detection changed");
	const auto float_conversion = vm.GetSlot(std::string("3.5"), std::string("tofloat"));
	const std::array<SquirrelValue, 1> fractional{std::string("3.5tail")};
	Require(std::get<float>(vm.Invoke(float_conversion, fractional).Value) == 3.5f, "native floating prefix conversion changed");
	const auto root = vm.GetSlot(vm.RootTable(), std::string("getroottable"));
	const std::array<SquirrelValue, 1> root_arguments{vm.RootTable()};
	Require(vm.Invoke(root, root_arguments).Value == vm.RootTable(), "base root table callback changed");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.RootTable(), std::string("_intsize_"))) == 4, "base integer width must be 4");
	const auto array_constructor = vm.GetSlot(vm.RootTable(), std::string("array"));
	const std::array<SquirrelValue, 3> filled{vm.RootTable(), std::int32_t{3}, std::int32_t{19}};
	const auto array = vm.Invoke(array_constructor, filled).Value;
	Require(vm.Length(array) == 3 && std::get<std::int32_t>(vm.GetSlot(array, std::int32_t{2})) == 19, "base array fill changed");
}

static void TestSharedConstantTable() {
	SquirrelVm vm;
	const auto original = vm.ConstantTable();
	Require(vm.Type(original) == sgre::script::SquirrelValueType::Table && original != vm.RootTable() && vm.Length(original) == 0, "constant table must begin empty and separate from the root table");
	vm.NewSlot(original, std::string("KEY"), std::int32_t{19});
	Require(!vm.HasSlot(vm.RootTable(), std::string("KEY")), "constant registration must not create a root property");
	vm.InstallRecoveredBaseLibrary();
	const auto get_constants = vm.GetSlot(vm.RootTable(), std::string("getconsttable"));
	const auto set_constants = vm.GetSlot(vm.RootTable(), std::string("setconsttable"));
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	Require(vm.Invoke(get_constants, receiver).Value == original, "base getter must return the shared constant table identity");
	const auto child = vm.CreateContext();
	Require(vm.InvokeInContext(child, get_constants, receiver).Value == original, "execution contexts must share the constant table");
	const auto replacement = vm.CreateTable();
	vm.NewSlot(replacement, std::string("ControllerType"), std::int32_t{3});
	const std::array<SquirrelValue, 2> setter_arguments{vm.RootTable(), replacement};
	Require(std::holds_alternative<std::monostate>(vm.InvokeInContext(child, set_constants, setter_arguments).Value), "native setconsttable returns its cleared argument cell");
	Require(vm.ConstantTable() == replacement && vm.Invoke(get_constants, receiver).Value == replacement, "child replacement must update the shared table");
	Require(std::get<std::int32_t>(vm.GetSlot(original, std::string("KEY"))) == 19 && !vm.HasSlot(replacement, std::string("KEY")), "constant replacement must preserve the previous table without merging it");
	for (const SquirrelValue& invalid : {SquirrelValue{}, SquirrelValue{std::int32_t{1}}, SquirrelValue{false}, SquirrelValue{std::string("table")}, vm.CreateArray(), vm.CreateClass()}) {
		const std::array<SquirrelValue, 2> invalid_arguments{vm.RootTable(), invalid};
		try {
			static_cast<void>(vm.Invoke(set_constants, invalid_arguments));
			throw std::runtime_error("invalid constant table type was accepted");
		} catch (const sgre::script::SquirrelVmException& error) {
			Require(StringText(error.ThrownValue()) == "ivalid type, expected table", "constant setter must preserve the native typed error spelling");
		}

		Require(vm.ConstantTable() == replacement, "failed constant replacement must preserve the shared table");
	}

	SquirrelVm foreign;
	RequireFailure([&] {
		vm.SetConstantTable(foreign.ConstantTable());
	},
	    "does not belong");
	vm.SetConstantTable(original);
	Require(vm.ConstantTable() == original && vm.InvokeInContext(child, get_constants, receiver).Value == original, "host table replacement must be visible to every execution context");
	const auto metadata = vm.GetSlot(get_constants, std::string("getinfos"));
	const std::array<SquirrelValue, 1> metadata_arguments{get_constants};
	const auto information = vm.Invoke(metadata, metadata_arguments).Value;
	Require(std::get<std::int32_t>(vm.GetSlot(information, std::string("paramscheck"))) == 1, "constant getter must retain its native exact arity");
}

static SquirrelValue InvokeDelegate(SquirrelVm& vm, const SquirrelValue& receiver, std::string_view name, std::span<const SquirrelValue> parameters = {}) {
	std::vector<SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), parameters.begin(), parameters.end());

	return vm.Invoke(vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

static void TestAdditionalDelegates() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	Require(sgre::script::TryNativeInteger(true) == 1 && sgre::script::TryNativeInteger(-7.75f) == -7, "native integer coercion changed");
	Require(sgre::script::TryNativeInteger(std::numeric_limits<float>::quiet_NaN()) == std::numeric_limits<std::int32_t>::min(), "native float-to-integer indefinite result changed");
	Require(!sgre::script::TryNativeInteger(std::string("19")) && !sgre::script::TryNativeInteger({}), "native integer conversion must not parse strings or null");
	const std::array<SquirrelValue, 3> numbers{std::int32_t{2}, std::int32_t{4}, std::int32_t{6}};
	const auto array = vm.CreateArray(numbers);
	const std::array<SquirrelValue, 1> self{array};
	Require(std::holds_alternative<std::monostate>(InvokeDelegate(vm, array, "extend", self)) && vm.Length(array) == 6, "self extension must append the original range once");
	const std::array<SquirrelValue, 1> removed_index{std::int32_t{1}};
	Require(std::get<std::int32_t>(InvokeDelegate(vm, array, "remove", removed_index)) == 4 && vm.Length(array) == 5, "array removal did not return the removed value");
	const std::array<SquirrelValue, 2> slice_range{std::int32_t{-7}, std::int32_t{2}};
	const auto slice = InvokeDelegate(vm, array, "slice", slice_range);
	Require(vm.Length(slice) == 4 && std::holds_alternative<std::monostate>(vm.GetSlot(slice, std::int32_t{0})) && std::get<std::int32_t>(vm.GetSlot(slice, std::int32_t{2})) == 2, "native array slice retains a null prefix for a start below negative length");
	const auto table = vm.CreateTable();
	vm.NewSlot(table, std::int32_t{2}, std::int32_t{20});
	vm.NewSlot(table, std::int32_t{0}, std::int32_t{0});
	const auto keys = InvokeDelegate(vm, table, "keys");
	Require(vm.Length(keys) == 2 && std::get<std::int32_t>(vm.GetSlot(keys, std::int32_t{0})) == 0, "table keys must follow physical bucket order");
	const std::array<SquirrelValue, 1> needle{std::string("ana")};
	Require(std::get<std::int32_t>(InvokeDelegate(vm, std::string("banana"), "rfind", needle)) == 3, "rfind must include overlapping matches");
	const std::array<SquirrelValue, 2> suffix_search{std::string("x"), std::int32_t{3}};
	Require(std::get<std::int32_t>(InvokeDelegate(vm, std::string("a\0bx", 4), "find", suffix_search)) == 3, "native find applies C-string termination after its supplied start offset");
	const std::array<SquirrelValue, 2> substring_range{std::int32_t{-4}, std::int32_t{2}};
	Require(StringText(InvokeDelegate(vm, std::string("abcdef"), "substr", substring_range)) == "cd", "substr length must differ from slice end index");
	const std::array<SquirrelValue, 1> last_character{std::int32_t{-1}};
	Require(StringText(InvokeDelegate(vm, std::string("abc"), "charAt", last_character)) == "c", "charAt must return a one-byte string");
	Require(StringText(InvokeDelegate(vm, std::string("Ab\0Z", 4), "tolower")) == std::string("ab\0z", 4), "case conversion must retain the full byte length across null");
	const auto class_value = vm.CreateClass({}, std::int32_t{41});
	vm.NewSlot(class_value, std::string("field"), std::int32_t{7});
	const std::array<SquirrelValue, 1> whole_class{SquirrelValue{}};
	Require(std::get<std::int32_t>(InvokeDelegate(vm, class_value, "getattributes", whole_class)) == 41, "class attribute getter lost its own attributes");
	const std::array<SquirrelValue, 2> replacement_attributes{std::string("field"), std::int32_t{73}};
	Require(std::holds_alternative<std::monostate>(InvokeDelegate(vm, class_value, "setattributes", replacement_attributes)), "attribute setter must return its previous value");
	const std::array<SquirrelValue, 1> field_key{std::string("field")};
	Require(std::get<std::int32_t>(InvokeDelegate(vm, class_value, "getattributes", field_key)) == 73, "member attributes were not retained");
	const auto instance = InvokeDelegate(vm, class_value, "instance");
	Require(InvokeDelegate(vm, instance, "getclass") == class_value && std::get<bool>(InvokeDelegate(vm, instance, "rawin", field_key)), "native instance/class identity delegates changed");
	auto add = Program({Instruction(17, 3, 2, 1, '+'), Instruction(19, 1, 3)}, 4);
	add.MainFunction.Parameters = {std::string("this"), std::string("left"), std::string("right")};
	const auto closure = vm.CreateClosure(std::move(add));
	const std::array<SquirrelValue, 3> call_arguments{vm.RootTable(), std::int32_t{3}, std::int32_t{5}};
	Require(std::get<std::int32_t>(InvokeDelegate(vm, closure, "call", call_arguments)) == 8, "closure call did not pass its receiver and arguments");
	const std::array<SquirrelValue, 1> array_arguments{vm.CreateArray(call_arguments)};
	Require(std::get<std::int32_t>(InvokeDelegate(vm, closure, "acall", array_arguments)) == 8, "closure acall did not expand its owned argument array");
}

static void TestClassMemberCallReceivers() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto base = vm.CreateClass({}, std::int32_t{69});
	vm.NewSlot(base, std::string("identify"), vm.CreateNativeClosure("identify", [](SquirrelVm&, const auto arguments) {
		sgre::script::SquirrelNativeResult result;
		result.Value = arguments[0];

		return result;
	}));
	const auto instance = vm.CreateInstance(vm.CreateClass(base));
	auto literal_call = Program({Instruction(8, 2, 0, 1, 3), Instruction(6, 4, 2, 3, 1), Instruction(19, 1, 4)}, 5);
	literal_call.MainFunction.Parameters.push_back(std::string("base"));
	literal_call.MainFunction.Literals = {std::string("identify")};
	const std::array<SquirrelValue, 2> receiver{instance, base};
	Require(vm.Invoke(vm.CreateClosure(std::move(literal_call)), receiver).Value == instance, "ordinary class call preparation must retain the current frame's instance receiver");
	auto dynamic_call = Program({Instruction(1, 2, 0), Instruction(7, 3, 2, 1, 4), Instruction(6, 5, 3, 4, 1), Instruction(19, 1, 5)}, 6);
	dynamic_call.MainFunction.Parameters.push_back(std::string("base"));
	dynamic_call.MainFunction.Literals = {std::string("identify")};
	Require(vm.Invoke(vm.CreateClosure(std::move(dynamic_call)), receiver).Value == instance, "dynamic class call preparation must preserve the same receiver rule");
	auto default_call = Program({Instruction(8, 2, 0, 1, 3), Instruction(20, 4, 1), Instruction(6, 5, 2, 3, 2), Instruction(19, 1, 5)}, 6);
	default_call.MainFunction.Parameters.push_back(std::string("base"));
	default_call.MainFunction.Literals = {std::string("getattributes")};
	Require(std::get<std::int32_t>(vm.Invoke(vm.CreateClosure(std::move(default_call)), receiver).Value) == 69, "class-default delegate calls must retain the class receiver rather than the current instance");
	auto member_read = Program({Instruction(9, 2, 0, 1), Instruction(19, 1, 2)}, 3);
	member_read.MainFunction.Parameters.push_back(std::string("class"));
	member_read.MainFunction.Literals = {std::string("getattributes")};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(member_read)), receiver));
	},
	    "does not exist");
	vm.NewSlot(vm.RootTable(), std::string("getattributes"), vm.CreateNativeClosure("root_class_function", [](SquirrelVm&, const auto arguments) {
		sgre::script::SquirrelNativeResult result;
		result.Value = arguments[0];

		return result;
	}));
	const std::array<SquirrelValue, 1> class_receiver{base};
	auto root_call = Program({Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)}, 4);
	root_call.MainFunction.Literals = {std::string("getattributes")};
	Require(vm.Invoke(vm.CreateClosure(std::move(root_call)), class_receiver).Value == base, "current-this root lookup must precede the separate class-default PREPCALL fallback");
	auto root_read = Program({Instruction(9, 1, 0, 0), Instruction(19, 1, 1)}, 2);
	root_read.MainFunction.Literals = {std::string("getattributes")};
	Require(vm.Invoke(vm.CreateClosure(std::move(root_read)), class_receiver).Value == vm.GetSlot(vm.RootTable(), std::string("getattributes")), "class GET must permit current-this root lookup without exposing a default delegate");
	const auto inherited_base = vm.CreateClass();
	vm.NewSlot(inherited_base, std::string("identify"), vm.CreateNativeClosure("inherited_identify", [](SquirrelVm&, const auto arguments) {
		sgre::script::SquirrelNativeResult result;
		result.Value = arguments[0];

		return result;
	}));
	const auto inherited_class = vm.CreateClass(inherited_base);
	vm.NewSlot(inherited_base, std::string("late_field"), std::int32_t{41});
	vm.NewSlot(inherited_base, std::string("late_identify"), vm.GetSlot(inherited_base, std::string("identify")));
	vm.NewSlot(inherited_base, std::string("identify"), vm.CreateNativeClosure("changed_base_method", [](SquirrelVm&, const auto) {
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{73};

		return result;
	}));
	const auto inherited_instance = vm.CreateInstance(inherited_class);
	Require(vm.HasSlot(inherited_class, std::string("identify")) && vm.FindRawSlot(inherited_class, std::string("identify")), "ordinary inherited methods must be present in the copied member snapshot");
	Require(!vm.HasSlot(inherited_class, std::string("late_identify")) && !vm.HasSlot(inherited_instance, std::string("late_field")), "late base additions must remain absent from the derived class and instance snapshots");
	auto inherited_call = Program({Instruction(8, 2, 0, 1, 3), Instruction(6, 4, 2, 3, 1), Instruction(19, 1, 4)}, 5);
	inherited_call.MainFunction.Parameters.push_back(std::string("class"));
	inherited_call.MainFunction.Literals = {std::string("identify")};
	const std::array<SquirrelValue, 2> inherited_receiver{inherited_instance, inherited_class};
	Require(vm.Invoke(vm.CreateClosure(std::move(inherited_call)), inherited_receiver).Value == inherited_instance, "successful inherited class member lookup must retain the current instance receiver");
}

static void TestBuiltinSuspensionAndMetaErrorPolicy() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	auto suspend = Program({Instruction(8, 1, 0, 0, 2), Instruction(2, 3, 17), Instruction(2, 4, 29), Instruction(6, 5, 1, 2, 3), Instruction(19, 1, 5)}, 6);
	suspend.MainFunction.Literals = {std::string("suspend")};
	const std::array<SquirrelValue, 1> root{vm.RootTable()};
	const auto suspended = vm.Invoke(vm.CreateClosure(std::move(suspend)), root);
	Require(suspended.State == sgre::script::SquirrelRunState::Suspended && std::get<std::int32_t>(suspended.Value) == 29, "builtin suspension must expose the actual last supplied stack value");
	Require(std::get<std::int32_t>(vm.Resume(std::int32_t{41}).Value) == 41, "builtin suspension resume value did not reach its destination");
	auto no_arguments = Program({Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)}, 4);
	no_arguments.MainFunction.Literals = {std::string("suspend")};
	const auto no_value = vm.Invoke(vm.CreateClosure(std::move(no_arguments)), root);
	Require(no_value.State == sgre::script::SquirrelRunState::Suspended && no_value.Value == vm.RootTable(), "suspend without explicit values must expose its receiver stack slot");
	static_cast<void>(vm.Resume());
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("suspend")), root));
	},
	    "cannot suspend through native calls/metamethods");
	std::size_t notifications = 0;
	const auto handler = vm.CreateNativeClosure("error_observer", [&](SquirrelVm&, const auto) {
		++notifications;

		return sgre::script::SquirrelNativeResult{};
	});
	const std::array<SquirrelValue, 2> install{vm.RootTable(), handler};
	static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("seterrorhandler")), install));
	const auto class_value = vm.CreateClass();
	auto getter = Program({Instruction(2, 2, 83), Instruction(58, 2)}, 3);
	getter.MainFunction.Parameters.push_back(std::string("key"));
	vm.NewSlot(class_value, std::string("_get"), vm.CreateClosure(std::move(getter)));
	auto setter = Program({Instruction(2, 3, 95), Instruction(58, 3)}, 4);
	setter.MainFunction.Parameters = {std::string("this"), std::string("key"), std::string("value")};
	vm.NewSlot(class_value, std::string("_set"), vm.CreateClosure(std::move(setter)));
	vm.NewSlot(class_value, std::string("_tostring"), vm.CreateClosure(Program({Instruction(2, 1, 96), Instruction(58, 1)}, 2)));
	auto comparator = Program({Instruction(2, 2, 97), Instruction(58, 2)}, 3);
	comparator.MainFunction.Parameters.push_back(std::string("other"));
	vm.NewSlot(class_value, std::string("_cmp"), vm.CreateClosure(std::move(comparator)));
	const auto instance = vm.CreateInstance(class_value);
	try {
		static_cast<void>(vm.ReadSlot(instance, std::string("missing")));
	} catch (const sgre::script::SquirrelVmException&) {
	}

	try {
		vm.AssignSlot(instance, std::string("missing"), std::int32_t{1});
	} catch (const sgre::script::SquirrelVmException&) {
	}

	try {
		static_cast<void>(vm.CompareValues(instance, vm.CreateInstance(class_value)));
	} catch (const sgre::script::SquirrelVmException&) {
	}

	Require(vm.ToString(instance).starts_with("(instance : 0x"), "failed native _tostring must recover its original instance pointer diagnostic");
	Require(notifications == 0, "native nested metamethod calls must suppress direct error-handler notification");
	auto caller = Program({Instruction(9, 2, 0, 1), Instruction(19, 1, 2)}, 3);
	caller.MainFunction.Parameters.push_back(std::string("receiver"));
	caller.MainFunction.Literals = {std::string("missing")};
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), instance};
	try {
		static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(caller)), arguments));
	} catch (const sgre::script::SquirrelVmException&) {
	}

	Require(notifications == 1, "unhandled outer script errors must notify once after a failed metamethod");
}

static void TestFailedGetterFallbacks() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	vm.NewSlot(vm.RootTable(), std::string("counter"), std::int32_t{31});
	vm.NewSlot(vm.RootTable(), std::string("getclass"), std::int32_t{99});
	const auto class_value = vm.CreateClass();
	std::size_t getter_calls = 0;
	vm.NewSlot(class_value, std::string("_get"), vm.CreateNativeClosure("failed_native_getter", [&](SquirrelVm&, const auto) -> sgre::script::SquirrelNativeResult {
		++getter_calls;
		throw std::runtime_error("getter failed");
	}));
	std::int32_t assigned = 0;
	vm.NewSlot(class_value, std::string("_set"), vm.CreateNativeClosure("fallback_compound_setter", [&](SquirrelVm&, const auto arguments) {
		Require(StringText(arguments[1]) == "counter", "fallback compound continuation lost its key");
		assigned = std::get<std::int32_t>(arguments[2]);

		return sgre::script::SquirrelNativeResult{};
	}));
	vm.NewSlot(class_value, std::string("_nexti"), vm.CreateNativeClosure("fallback_iterator", [](SquirrelVm&, const auto arguments) {
		sgre::script::SquirrelNativeResult result;
		if (std::holds_alternative<std::monostate>(arguments[1])) {
			result.Value = std::string("counter");
		}

		return result;
	}));
	const auto instance = vm.CreateInstance(class_value);
	const std::array<SquirrelValue, 1> receiver{instance};
	auto read = Program({Instruction(9, 1, 0, 0), Instruction(19, 1, 1)}, 2);
	read.MainFunction.Literals = {std::string("counter")};
	const auto read_callable = vm.CreateClosure(std::move(read));
	Require(std::get<std::int32_t>(vm.Invoke(read_callable, receiver).Value) == 31 && getter_calls == 1, "failed native _get must retry the root for the current this receiver");
	auto defaults = Program({Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)}, 4);
	defaults.MainFunction.Literals = {std::string("getclass")};
	Require(vm.Invoke(vm.CreateClosure(std::move(defaults)), receiver).Value == class_value, "instance default delegate must precede root lookup after a failed getter");
	Require(vm.Type(vm.ReadSlot(instance, std::string("getclass"))) == sgre::script::SquirrelValueType::NativeClosure, "explicit native slot reading must recover its default delegate after getter failure");
	auto compound = Program({Instruction(1, 1, 0), Instruction(2, 2, 4), Instruction(34, 3, 2, 1, '+'), Instruction(19, 1, 3)}, 4);
	compound.MainFunction.Literals = {std::string("counter")};
	Require(std::get<std::int32_t>(vm.Invoke(vm.CreateClosure(std::move(compound)), receiver).Value) == 35 && assigned == 35, "failed-get root fallback must retain compound arithmetic and assignment continuations");
	auto iteration = Program({Instruction(51, 0, 3, 1), Instruction(52, 0, 1, 1), Instruction(19, 1, 2), Instruction(24, 0, -4), Instruction(19, 255)}, 4);
	Require(std::get<std::int32_t>(vm.Invoke(vm.CreateClosure(std::move(iteration)), receiver).Value) == 31, "failed-get root fallback must retain a custom iterator's value continuation");
	auto other_receiver = Program({Instruction(9, 2, 0, 1), Instruction(19, 1, 2)}, 3);
	other_receiver.MainFunction.Parameters.push_back(std::string("other"));
	other_receiver.MainFunction.Literals = {std::string("counter")};
	const std::array<SquirrelValue, 2> other_arguments{vm.RootTable(), instance};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(other_receiver)), other_arguments));
	},
	    "does not exist");
	std::size_t exception_constructions = 0;
	const auto exception_class = vm.CreateClass();
	vm.NewSlot(exception_class, std::string("constructor"), vm.CreateNativeClosure("count_exception_construction", [&](SquirrelVm&, const auto arguments) {
		Require(StringText(arguments[1]) == "fixture getter failed", "recovered lookup must not construct a replacement index exception");
		++exception_constructions;

		return sgre::script::SquirrelNativeResult{};
	}));
	const std::array<SquirrelValue, 2> install_exception{vm.RootTable(), exception_class};
	static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("setexceptionclass")), install_exception));
	const auto script_class = vm.CreateClass();
	auto failed = Program({Instruction(1, 2, 0), Instruction(58, 2)}, 3);
	failed.MainFunction.Parameters.push_back(std::string("key"));
	failed.MainFunction.Literals = {std::string("fixture getter failed")};
	vm.NewSlot(script_class, std::string("_get"), vm.CreateClosure(std::move(failed)));
	const std::array<SquirrelValue, 1> script_receiver{vm.CreateInstance(script_class)};
	Require(std::get<std::int32_t>(vm.Invoke(read_callable, script_receiver).Value) == 31, "failed script _get must unwind into the original root fallback continuation");
	Require(exception_constructions == 1, "successful root fallback must not construct an additional missing-index exception");
	const auto null_class = vm.CreateClass();
	vm.NewSlot(null_class, std::string("_get"), vm.CreateNativeClosure("successful_null_getter", [](SquirrelVm&, const auto) {
		return sgre::script::SquirrelNativeResult{};
	}));
	const std::array<SquirrelValue, 1> null_receiver{vm.CreateInstance(null_class)};
	Require(std::holds_alternative<std::monostate>(vm.Invoke(read_callable, null_receiver).Value), "successful null getter results must not trigger default or root fallback");
	const auto overriding_class = vm.CreateClass();
	vm.NewSlot(overriding_class, std::string("_get"), vm.CreateNativeClosure("successful_getter", [](SquirrelVm&, const auto) {
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{73};

		return result;
	}));
	Require(std::get<std::int32_t>(vm.ReadSlot(vm.CreateInstance(overriding_class), std::string("getclass"))) == 73, "successful getter must precede instance default delegate lookup");
}

static void RequirePointerDiagnostic(const SquirrelVm& vm, const SquirrelValue& value, const std::string& text) {
	const auto prefix = "(" + vm.TypeName(value) + " : 0x";
	Require(text.starts_with(prefix) && text.size() == prefix.size() + 17 && text.back() == ')', "native pointer diagnostic lost its type name, separators, or sixteen-digit extent");
	const auto digits = std::string_view(text).substr(prefix.size(), 16);
	Require(std::all_of(digits.begin(), digits.end(), [](char digit) {
		return (digit >= '0' && digit <= '9') || (digit >= 'A' && digit <= 'F');
	}),
	    "native pointer diagnostic must use uppercase hexadecimal digits");
	if (vm.Type(value) != sgre::script::SquirrelValueType::Null) {
		Require(digits != "0000000000000000", "real object diagnostics must expose their actual nonnull allocation identity");
	}
}

static void TestNativePointerStringConversion() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	Require(vm.ToString(SquirrelValue{}) == "(null : 0x0000000000000000)", "native null tostring uses the pointer diagnostic rather than an invented scalar spelling");
	const auto table = vm.CreateTable();
	const auto array = vm.CreateArray();
	const auto closure = vm.CreateClosure(Program({Instruction(19, 255)}, 1));
	const auto native = vm.CreateNativeClosure("pointer.fixture", [](SquirrelVm&, const auto) {
		return sgre::script::SquirrelNativeResult{};
	});
	const auto class_value = vm.CreateClass();
	const auto instance = vm.CreateInstance(class_value);
	const auto weak = vm.CreateWeakReference(instance);
	for (const auto& value : std::array<SquirrelValue, 7>{table, array, closure, native, class_value, instance, weak}) {
		RequirePointerDiagnostic(vm, value, vm.ToString(value));
	}

	const auto table_text = vm.ToString(table);
	Require(table_text != vm.ToString(vm.CreateTable()), "distinct owned objects must have distinct diagnostic addresses");
	for (std::size_t index = 0; index < 300; ++index) {
		static_cast<void>(vm.CreateArray());
	}

	Require(vm.ToString(table) == table_text, "arena vector growth must preserve each owned allocation's pointer identity");
	const auto failing_class = vm.CreateClass();
	std::size_t calls = 0;
	vm.NewSlot(failing_class, std::string("_tostring"), vm.CreateNativeClosure("pointer.failed_hook", [&](SquirrelVm&, const auto) -> sgre::script::SquirrelNativeResult {
		++calls;
		throw std::runtime_error("ignored tostring error");
	}));
	const auto failing = vm.CreateInstance(failing_class);
	RequirePointerDiagnostic(vm, failing, vm.ToString(failing));
	Require(calls == 1, "failed _tostring fallback must preserve the actual attempted hook call");
	const auto nonstring_class = vm.CreateClass();
	vm.NewSlot(nonstring_class, std::string("_tostring"), vm.CreateNativeClosure("pointer.nonstring_hook", [](SquirrelVm&, const auto) {
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{23};

		return result;
	}));
	const auto nonstring = vm.CreateInstance(nonstring_class);
	RequirePointerDiagnostic(vm, nonstring, vm.ToString(nonstring));
	Require(vm.Type(vm.GetSlot(array, std::string("tostring"))) == sgre::script::SquirrelValueType::NativeClosure, "pointer recovery must retain the actual array tostring delegate");
	Require(StringText(InvokeDelegate(vm, array, "tostring")) == vm.ToString(array), "default tostring delegates must expose the same recovered allocation identity");
	Require(vm.ToString(true) == "true" && vm.ToString(std::int32_t{-7}) == "-7" && vm.ToString(std::string("owned")) == "owned", "pointer fallback must preserve supported scalar/string conversions");
}

static void TestScopedHostRoots() {
	static_assert(!std::is_copy_constructible_v<sgre::script::SquirrelHostRoot>);
	static_assert(std::is_nothrow_move_constructible_v<sgre::script::SquirrelHostRoot>);
	SquirrelVm vm;
	const auto object = vm.CreateTable();
	Require(vm.RetainedReferenceCount(object) == 0, "ordinary VM handles must remain borrowed host values");
	auto first = vm.Retain(object);
	Require(first.Value() == object && vm.RetainedReferenceCount(object) == 1, "scoped host root did not retain its native object identity");
	{
		auto second = vm.Retain(object);
		auto moved = std::move(second);
		Require(moved.Value() == object && vm.RetainedReferenceCount(object) == 2, "moving a root must not duplicate or release its external reference");
		RequireFailure([&] {
			static_cast<void>(second.Value());
		},
		    "empty");
		moved = vm.Retain(std::int32_t{5});
		Require(vm.RetainedReferenceCount(object) == 1 && vm.RetainedReferenceCount(std::int32_t{5}) == 0, "move assignment must release its old root and leave scalar AddRef as a no-op");
	}

	first = {};
	Require(vm.RetainedReferenceCount(object) == 0, "final scoped release must remove the external root entry");
	const std::string text("a\0b", 3);
	auto text_root = vm.Retain(text);
	auto duplicate_text = vm.Retain(std::string(text));
	auto terminated_text = vm.Retain(std::string("a"));
	Require(vm.RetainedReferenceCount(text) == 2 && vm.RetainedReferenceCount(std::string("a")) == 1, "native interned string identity must retain its full byte extent and share equal contents");
	text_root = {};
	Require(duplicate_text.Value() == SquirrelValue(text) && vm.RetainedReferenceCount(text) == 1, "releasing one string root changed the duplicate's retained content");
	auto boolean_root = vm.Retain(true);
	Require(std::get<bool>(boolean_root.Value()) && vm.RetainedReferenceCount(true) == 0, "native boolean AddRef must not populate the reference table");
	SquirrelVm other;
	RequireFailure([&] {
		static_cast<void>(other.Retain(object));
	},
	    "does not belong");
	sgre::script::SquirrelHostRoot orphan;
	{
		auto owner = std::make_unique<SquirrelVm>();
		orphan = owner->Retain(owner->CreateArray());
		owner.reset();
		RequireFailure([&] {
			static_cast<void>(orphan.Value());
		},
		    "owner expired");
	}

	orphan = {};
	sgre::script::SquirrelVmLimits limits;
	limits.MaxObjects = 2;
	SquirrelVm bounded(limits);
	auto left = bounded.Retain(std::string("left"));
	auto right = bounded.Retain(std::string("right"));
	RequireFailure([&] {
		static_cast<void>(bounded.Retain(std::string("third")));
	},
	    "host root limit");
	Require(bounded.RetainedReferenceCount(std::string("left")) == 1 && bounded.RetainedReferenceCount(std::string("third")) == 0, "failed root allocation must leave existing references intact");
	left = {};
	auto reused = bounded.Retain(std::string("third"));
	Require(StringText(reused.Value()) == "third" && bounded.RetainedReferenceCount(std::string("right")) == 1, "released root slots must be safely reusable");
}

static void TestNativeGeneralSlotReads() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	vm.NewSlot(vm.RootTable(), std::string("x"), std::int32_t{19});
	const auto class_value = vm.CreateClass();
	const auto getter = vm.CreateNativeClosure("native_api_failed_getter", [](SquirrelVm&, const auto) -> sgre::script::SquirrelNativeResult {
		throw sgre::script::SquirrelVmException(std::int32_t{27}, "unrelated getter error");
	});
	vm.NewSlot(class_value, std::string("_get"), getter);
	vm.NewSlot(class_value, std::string("y"), std::int32_t{23});
	const auto instance = vm.CreateInstance(class_value);
	try {
		static_cast<void>(vm.ReadSlot(instance, std::string("x")));
		throw std::logic_error("native general get unexpectedly retried the root");
	} catch (const sgre::script::SquirrelVmException& error) {
		Require(StringText(error.ThrownValue()) == "the index doesn't exist", "native general get must replace the failed hook's typed error with its fixed missing-index string");
	}

	Require(vm.Type(vm.ReadSlot(instance, std::string("getclass"))) == sgre::script::SquirrelValueType::NativeClosure, "native general get must retry the instance default delegate after a failed hook");
	RequireFailure([&] {
		static_cast<void>(vm.ReadSlot(class_value, std::string("getattributes")));
	},
	    "the index doesn't exist");
	Require(vm.Type(vm.GetSlot(class_value, std::string("getattributes"))) == sgre::script::SquirrelValueType::NativeClosure, "nonexecuting reflection lookup must retain explicit class default metadata access");
	RequireFailure([&] {
		static_cast<void>(vm.ReadSlot(vm.CreateArray(), std::int32_t{0}));
	},
	    "the index doesn't exist");
	Require(std::get<std::int32_t>(vm.ReadSlot(std::string("point"), std::int32_t{-1})) == 't', "native general get lost signed string indexing");
	Require(vm.Type(vm.ReadSlot(std::int32_t{3}, std::string("tostring"))) == sgre::script::SquirrelValueType::NativeClosure, "native general get lost scalar default delegates");
	Require(std::get<std::int32_t>(vm.ReadSlot(instance, std::string("y"))) == 23, "ordinary instance field must precede its failing hook");
}

static void TestNativeArrayInsertionAndSort() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const std::array<SquirrelValue, 2> source{std::int32_t{3}, std::int32_t{1}};
	const auto array = vm.CreateArray(source);
	const std::array<SquirrelValue, 2> insertion{1.75f, std::int32_t{2}};
	Require(std::holds_alternative<std::monostate>(InvokeDelegate(vm, array, "insert", insertion)) && std::get<std::int32_t>(vm.GetSlot(array, std::int32_t{1})) == 2, "native insert must truncate float indices and return null");
	const auto weak_target = vm.CreateTable();
	const auto weak = vm.CreateWeakReference(weak_target);
	const std::array<SquirrelValue, 2> append{std::int32_t{3}, weak};
	static_cast<void>(InvokeDelegate(vm, array, "insert", append));
	Require(InvokeDelegate(vm, array, "top") == weak && vm.GetSlot(array, std::int32_t{3}) == weak_target, "native insert must retain the stored weak wrapper rather than flattening its target");
	static_cast<void>(InvokeDelegate(vm, array, "pop"));
	const std::array<SquirrelValue, 2> invalid{std::int32_t{-1}, std::int32_t{9}};
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, array, "insert", invalid));
	},
	    "index out of range");
	Require(std::holds_alternative<std::monostate>(InvokeDelegate(vm, array, "sort")), "native sort returns no value");
	for (std::int32_t index = 0; index < 3; ++index) {
		Require(std::get<std::int32_t>(vm.GetSlot(array, index)) == index + 1, "native default sort did not preserve ascending values");
	}

	const std::array<SquirrelValue, 3> unordered{std::int32_t{3}, std::int32_t{1}, std::int32_t{2}};
	const auto traced = vm.CreateArray(unordered);
	std::vector<std::array<std::int32_t, 2>> comparisons;
	const auto comparator = vm.CreateNativeClosure("traced_sort_comparator", [&](SquirrelVm& current, const auto arguments) {
		Require(arguments.size() == 3 && arguments[0] == current.RootTable(), "native sort comparator must receive root, value, and captured first pivot");
		const auto left = std::get<std::int32_t>(arguments[1]);
		const auto right = std::get<std::int32_t>(arguments[2]);
		comparisons.push_back({left, right});
		sgre::script::SquirrelNativeResult result;
		result.Value = static_cast<float>(left - right);

		return result;
	});
	const std::array<SquirrelValue, 1> compare_argument{comparator};
	static_cast<void>(InvokeDelegate(vm, traced, "sort", compare_argument));
	const std::vector<std::array<std::int32_t, 2>> expected{{1, 3}, {2, 3}, {2, 3}, {1, 2}, {1, 2}};
	Require(comparisons == expected, "native first-pivot scan and left-before-right comparison order changed");
	std::array<SquirrelValue, 4> identities;
	for (auto& value : identities) {
		value = vm.CreateTable();
	}

	const auto equal_values = vm.CreateArray(identities);
	const auto equal = vm.CreateNativeClosure("equal_sort_values", [](SquirrelVm&, const auto) {
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{0};

		return result;
	});
	const std::array<SquirrelValue, 1> equal_argument{equal};
	static_cast<void>(InvokeDelegate(vm, equal_values, "sort", equal_argument));
	for (std::int32_t index = 0; index < 4; ++index) {
		Require(vm.GetSlot(equal_values, index) == identities[static_cast<std::size_t>((index + 1) % 4)], "native equal-key quicksort permutation must retain its original unstable identity order");
	}

	auto script_comparison = Program({Instruction(17, 3, 2, 1, '-'), Instruction(19, 1, 3)}, 4);
	script_comparison.MainFunction.Parameters = {std::string("this"), std::string("left"), std::string("right")};
	const std::array<SquirrelValue, 1> script_argument{vm.CreateClosure(std::move(script_comparison))};
	const auto script_sorted = vm.CreateArray(unordered);
	static_cast<void>(InvokeDelegate(vm, script_sorted, "sort", script_argument));
	Require(std::get<std::int32_t>(vm.GetSlot(script_sorted, std::int32_t{0})) == 1 && std::get<std::int32_t>(vm.GetSlot(script_sorted, std::int32_t{2})) == 3, "sort did not support a real script comparator through its nested call boundary");
	const auto defective = vm.CreateNativeClosure("defective_sort_comparator", [](SquirrelVm&, const auto) {
		sgre::script::SquirrelNativeResult result;
		result.Value = true;

		return result;
	});
	const std::array<SquirrelValue, 1> defective_argument{defective};
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, vm.CreateArray(unordered), "sort", defective_argument));
	},
	    "Invalid qsort, probably compare function defect");
	const auto shortened = vm.CreateArray(unordered);
	const auto shrink = vm.CreateNativeClosure("shrinking_sort_comparator", [&](SquirrelVm& current, const auto) {
		static_cast<void>(InvokeDelegate(current, shortened, "clear"));
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{0};

		return result;
	});
	const std::array<SquirrelValue, 1> shrink_argument{shrink};
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, shortened, "sort", shrink_argument));
	},
	    "shortened its array");
	Require(vm.Length(shortened) == 0, "rejected unsafe sort must retain the comparator's actual mutation");
	std::vector<SquirrelValue> repeated(1500, std::int32_t{1});
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, vm.CreateArray(repeated), "sort"));
	},
	    "comparison-work limit exceeded");
}

static void TestNativeSortCallbackErrors() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const std::array<SquirrelValue, 2> unordered{std::int32_t{3}, std::int32_t{1}};
	auto throwing = Program({Instruction(2, 3, 73), Instruction(58, 3)}, 4);
	throwing.MainFunction.Parameters = {std::string("this"), std::string("left"), std::string("right")};
	const std::array<SquirrelValue, 1> throwing_argument{vm.CreateClosure(std::move(throwing))};
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, vm.CreateArray(unordered), "sort", throwing_argument));
	},
	    "compare func failed");
	auto null_throw = Program({Instruction(20, 1, 3), Instruction(58, 3)}, 4);
	null_throw.MainFunction.Parameters = {std::string("this"), std::string("left"), std::string("right")};
	const std::array<SquirrelValue, 1> null_argument{vm.CreateClosure(std::move(null_throw))};
	bool caught_null = false;
	try {
		static_cast<void>(InvokeDelegate(vm, vm.CreateArray(unordered), "sort", null_argument));
	} catch (const sgre::script::SquirrelVmException& error) {
		caught_null = std::holds_alternative<std::monostate>(error.ThrownValue());
	}

	Require(caught_null, "native sort preserves a failed comparator's null error instead of replacing it");
	const auto invalid = vm.CreateNativeClosure("invalid_comparison_result", [](SquirrelVm&, const auto) {
		sgre::script::SquirrelNativeResult result;
		result.Value = std::string("not numeric");

		return result;
	});
	const std::array<SquirrelValue, 1> invalid_argument{invalid};
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, vm.CreateArray(unordered), "sort", invalid_argument));
	},
	    "uninitialized integer comparison result");
	const std::array<SquirrelValue, 2> wrapped{std::numeric_limits<std::int32_t>::min(), std::int32_t{1}};
	const auto wrapped_array = vm.CreateArray(wrapped);
	static_cast<void>(InvokeDelegate(vm, wrapped_array, "sort"));
	Require(std::get<std::int32_t>(vm.GetSlot(wrapped_array, std::int32_t{0})) == 1, "native default sort must preserve wrapped integer subtraction ordering");
	const std::array<SquirrelValue, 2> weak_values{vm.CreateWeakReference(vm.CreateTable()), vm.CreateWeakReference(vm.CreateTable())};
	const auto weak_array = vm.CreateArray(weak_values);
	const auto weak_comparison = vm.CreateNativeClosure("raw_weak_sort_comparison", [](SquirrelVm& current, const auto arguments) {
		Require(current.Type(arguments[1]) == sgre::script::SquirrelValueType::WeakReference && current.Type(arguments[2]) == sgre::script::SquirrelValueType::WeakReference, "native sort comparator must receive raw stored weak wrappers");
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{0};

		return result;
	});
	const std::array<SquirrelValue, 1> weak_argument{weak_comparison};
	static_cast<void>(InvokeDelegate(vm, weak_array, "sort", weak_argument));
	Require(InvokeDelegate(vm, weak_array, "top") == weak_values[0], "native sort must preserve raw weak-wrapper identities during swaps");
}

static void TestMissingIndexDiagnostics() {
	SquirrelVm vm;
	auto read = Program({Instruction(14, 2, 0, 1), Instruction(19, 1, 2)}, 3);
	read.MainFunction.Parameters.push_back(std::string("key"));
	const auto closure = vm.CreateClosure(std::move(read));
	struct Vector {
		SquirrelValue Key;
		std::string Description;
	};
	const std::array<Vector, 5> vectors{
	    Vector{std::string(60, 'a'), std::string(50, 'a')},
	    Vector{std::string("cut\0ignored", 11), "cut"},
	    Vector{std::int32_t{-12}, "-12"},
	    Vector{0.1f, "0.10000000149012"},
	    Vector{true, "bool"}};
	for (const auto& vector : vectors) {
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), vector.Key};
		RequireFailure([&] {
			static_cast<void>(vm.Invoke(closure, arguments));
		},
		    "the index '" + vector.Description + "' does not exist");
	}

	const auto class_value = vm.CreateClass();
	std::size_t string_conversions = 0;
	vm.NewSlot(class_value, std::string("_tostring"), vm.CreateNativeClosure("diagnostic_key_string", [&](SquirrelVm&, const auto) {
		++string_conversions;
		sgre::script::SquirrelNativeResult result;
		result.Value = std::string("converted key");

		return result;
	}));
	const std::array<SquirrelValue, 2> instance_key{vm.RootTable(), vm.CreateInstance(class_value)};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(closure, instance_key));
	},
	    "the index 'instance' does not exist");
	Require(string_conversions == 0, "native missing-index diagnostics use the type name without invoking _tostring");
	vm.NewSlot(vm.RootTable(), std::int32_t{0}, std::int32_t{71});
	const std::array<SquirrelValue, 2> empty_array{vm.CreateArray(), std::int32_t{0}};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(closure, empty_array));
	},
	    "the index '0' does not exist");
}

static void TestNativeSignaturesAndReflection() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto root_lookup = vm.GetSlot(vm.RootTable(), std::string("getroottable"));
	const auto base_info = InvokeDelegate(vm, root_lookup, "getinfos");
	Require(std::get<bool>(vm.GetSlot(base_info, std::string("native"))) && StringText(vm.GetSlot(base_info, std::string("name"))) == "getroottable" && std::get<std::int32_t>(vm.GetSlot(base_info, std::string("paramscheck"))) == 1, "base callback reflection lost its registered native name or parameter count");
	Require(std::holds_alternative<std::monostate>(vm.GetSlot(base_info, std::string("typecheck"))), "absent native type checks must reflect null");
	const auto array = vm.CreateArray();
	const auto pop = vm.GetSlot(array, std::string("pop"));
	const auto pop_info = InvokeDelegate(vm, pop, "getinfos");
	const auto pop_types = vm.GetSlot(pop_info, std::string("typecheck"));
	Require(vm.Length(pop_types) == 1 && std::get<std::int32_t>(vm.GetSlot(pop_types, std::int32_t{0})) == 64, "array native reflection lost its actual type mask");
	const auto unnamed = vm.CreateNativeClosure("diagnostic label", [](SquirrelVm&, const auto) {
		return sgre::script::SquirrelNativeResult{};
	});
	Require(std::holds_alternative<std::monostate>(vm.GetSlot(InvokeDelegate(vm, unnamed, "getinfos"), std::string("name"))), "authored diagnostic labels must not invent native closure name metadata");
	sgre::script::SquirrelNativeSignature signature;
	signature.ParameterCheck = 2;
	signature.TypeCheck = {32, 2};
	signature.Name = "checked_callback";
	const auto environment = vm.CreateInstance(vm.CreateClass());
	const auto checked = vm.CreateNativeClosure("checked_callback", [environment](SquirrelVm&, const auto arguments) {
		Require(arguments.size() == 2 && arguments[0] == environment, "bound native receiver was not applied after signature validation");
		sgre::script::SquirrelNativeResult result;
		result.Value = arguments[1];

		return result;
	},
	    signature);
	const auto bound = vm.BindEnvironment(checked, environment);
	const std::array<SquirrelValue, 2> accepted{vm.RootTable(), std::int32_t{37}};
	Require(std::get<std::int32_t>(vm.Invoke(bound, accepted).Value) == 37, "signature validation must precede bound environment replacement");
	const std::array<SquirrelValue, 2> rejected{environment, std::int32_t{37}};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(bound, rejected));
	},
	    "type mismatch");
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(bound, std::span<const SquirrelValue>(accepted).first(1)));
	},
	    "wrong number of parameters");
	auto variadic = Program({Instruction(19, 255)}, 3);
	variadic.MainFunction.HasVariableParameters = true;
	variadic.MainFunction.Parameters.push_back(std::string("fixed"));
	const auto variadic_info = InvokeDelegate(vm, vm.CreateClosure(std::move(variadic)), "getinfos");
	const auto parameters = vm.GetSlot(variadic_info, std::string("parameters"));
	Require(!std::get<bool>(vm.GetSlot(variadic_info, std::string("native"))) && std::get<bool>(vm.GetSlot(variadic_info, std::string("varargs"))) && vm.Length(parameters) == 3 && StringText(vm.GetSlot(parameters, std::int32_t{2})) == "...", "script reflection must append the literal variadic parameter marker");
	Require(StringText(vm.GetSlot(variadic_info, std::string("src"))) == "independent-fixture.nut", "script reflection lost its source object");
	const std::array<SquirrelValue, 1> wrong_find_type{std::int32_t{7}};
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, std::string("text"), "find", wrong_find_type));
	},
	    "type mismatch");
}

static void TestSearchDelegates() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const std::array<SquirrelValue, 3> values{std::int32_t{2}, 2.0f, std::int32_t{3}};
	const auto array = vm.CreateArray(values);
	const std::array<SquirrelValue, 1> query{2.0f};
	Require(std::get<std::int32_t>(InvokeDelegate(vm, array, "find", query)) == 0, "native search must compare int/float values numerically");
	const auto all = InvokeDelegate(vm, array, "findall", query);
	Require(vm.Length(all) == 2 && std::get<std::int32_t>(vm.GetSlot(all, std::int32_t{1})) == 1, "native findall must retain matching keys in enumeration order");
	Require(std::get<bool>(InvokeDelegate(vm, array, "includes", query)) && std::get<std::int32_t>(InvokeDelegate(vm, array, "includeCount", query)) == 2, "native include count changed");
	const std::array<SquirrelValue, 1> absent{std::int32_t{8}};
	Require(std::holds_alternative<std::monostate>(InvokeDelegate(vm, array, "find", absent)), "native find must return null when no comparison matches");
	const auto table = vm.CreateTable();
	vm.NewSlot(table, std::int32_t{2}, 2.0f);
	vm.NewSlot(table, std::int32_t{0}, std::int32_t{2});
	vm.NewSlot(table, std::int32_t{1}, std::int32_t{3});
	const auto keys = InvokeDelegate(vm, table, "findall", query);
	Require(vm.Length(keys) == 2 && std::get<std::int32_t>(vm.GetSlot(keys, std::int32_t{0})) == 0 && std::get<std::int32_t>(vm.GetSlot(keys, std::int32_t{1})) == 2, "table search must follow physical buckets");
	Require(std::holds_alternative<std::monostate>(InvokeDelegate(vm, array, "removeValue", query)) && vm.Length(array) == 1 && std::get<std::int32_t>(vm.GetSlot(array, std::int32_t{0})) == 3, "removeValue must remove every native comparison match and return null");
	const std::array<SquirrelValue, 2> rejected_option{std::int32_t{3}, false};
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, array, "removeValue", rejected_option));
	},
	    "wrong number of parameters");
	const auto class_value = vm.CreateClass();
	SquirrelValue query_instance;
	vm.NewSlot(class_value, std::string("_cmp"), vm.CreateNativeClosure("asymmetric_cmp", [&](SquirrelVm&, const auto arguments) {
		sgre::script::SquirrelNativeResult result;
		result.Value = static_cast<std::int32_t>(arguments[0] == query_instance ? 0 : 1);

		return result;
	}));
	const auto element = vm.CreateInstance(class_value);
	query_instance = vm.CreateInstance(class_value);
	const std::array<SquirrelValue, 1> instances{element};
	const auto asymmetric = vm.CreateArray(instances);
	const std::array<SquirrelValue, 1> instance_query{query_instance};
	Require(!std::get<bool>(InvokeDelegate(vm, asymmetric, "includes", instance_query)), "find/include comparison must place the enumerated element on the left");
	static_cast<void>(InvokeDelegate(vm, asymmetric, "removeValue", instance_query));
	Require(vm.Length(asymmetric) == 0, "removeValue comparison must place its query on the left");
	const auto table_find = vm.GetSlot(table, std::string("find"));
	const std::array<SquirrelValue, 2> string_search{std::string("xyz"), std::int32_t{'y'}};
	Require(std::get<std::int32_t>(vm.Invoke(table_find, string_search).Value) == 1, "native wildcard search delegate must retain ordinary string iteration behavior");
}

static void TestStringReplacementAndMultibyteDelegates() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const std::array<SquirrelValue, 2> replacement{std::string("ana"), std::string("X")};
	Require(StringText(InvokeDelegate(vm, std::string("banana"), "replace", replacement)) == "bXna", "native replacement must use non-overlapping substring matches");
	const std::array<SquirrelValue, 2> empty_needle{std::string{}, std::string("replacement")};
	Require(StringText(InvokeDelegate(vm, std::string("a\0b", 3), "replace", empty_needle)) == std::string("a\0b", 3), "empty native replacement needle must return the original full string");
	const std::array<SquirrelValue, 2> binary_replacement{std::string("-"), std::string("N\0Z", 3)};
	Require(StringText(InvokeDelegate(vm, std::string("a-b"), "replace", binary_replacement)) == std::string("aN\0Zb", 5), "native replacement must copy the complete replacement byte length");
	const std::array<SquirrelValue, 1> delimiter{std::string("::")};
	const auto pieces = InvokeDelegate(vm, std::string("::a::::b::"), "split", delimiter);
	Require(vm.Length(pieces) == 5 && StringText(vm.GetSlot(pieces, std::int32_t{0})).empty() && StringText(vm.GetSlot(pieces, std::int32_t{2})).empty() && StringText(vm.GetSlot(pieces, std::int32_t{4})).empty(), "native split must use a substring delimiter and retain empty segments");
	const auto terminated = InvokeDelegate(vm, std::string("a\0tail", 6), "split", delimiter);
	Require(vm.Length(terminated) == 1 && StringText(vm.GetSlot(terminated, std::int32_t{0})) == "a", "native split must stop at C-string termination");
	const std::string multibyte = "A\xe3\x81\x82"
	                              "B\xf0\x9f\x98\x80";
	Require(std::get<std::int32_t>(InvokeDelegate(vm, multibyte, "mblen")) == 4, "native multibyte length changed");
	const std::array<SquirrelValue, 1> lead_position{std::int32_t{1}};
	const std::array<SquirrelValue, 1> continuation_position{std::int32_t{2}};
	Require(std::get<std::int32_t>(InvokeDelegate(vm, multibyte, "mbnext", lead_position)) == 3 && std::get<std::int32_t>(InvokeDelegate(vm, multibyte, "mbnext", continuation_position)) == 2, "mbnext returns encoded-byte advance, including a continuation run");
	const std::array<SquirrelValue, 2> middle{std::int32_t{1}, std::int32_t{2}};
	Require(StringText(InvokeDelegate(vm, multibyte, "mbsubstr", middle)) == std::string("\xe3\x81\x82"
	                                                                                     "B"),
	    "native multibyte substring arguments use character start and count");
	const std::array<SquirrelValue, 2> below_start{std::int32_t{-6}, std::int32_t{3}};
	Require(StringText(InvokeDelegate(vm, multibyte, "mbsubstr", below_start)) == "A", "native multibyte substring preserves its below-negative-length start behavior");
	Require(std::get<std::int32_t>(InvokeDelegate(vm, std::string("\xfe", 1), "mbnext")) == 6, "native multibyte helper retains its observed six-byte lead rule");
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, std::string("\xe3", 1), "mbsubstr"));
	},
	    "bounded string storage");
	const std::array<SquirrelValue, 1> empty_delimiter{std::string{}};
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, std::string("text"), "split", empty_delimiter));
	},
	    "empty");
	sgre::script::SquirrelVmLimits limits;
	limits.MaxStringBytes = 32;
	SquirrelVm bounded(limits);
	bounded.InstallRecoveredBaseLibrary();
	const std::array<SquirrelValue, 2> growth{std::string("a"), std::string("XXXX")};
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(bounded, std::string("aaaaaaaaa"), "replace", growth));
	},
	    "string limit");
}

static SquirrelValue InvokeGlobalSplit(SquirrelVm& vm, std::string source, std::string separators) {
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), std::move(source), std::move(separators)};
	const auto result = vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("split")), arguments);
	Require(result.State == sgre::script::SquirrelRunState::Completed, "global split unexpectedly suspended");

	return result.Value;
}

static void RequireSplitTokens(SquirrelVm& vm, const SquirrelValue& result, std::span<const std::string_view> expected) {
	Require(vm.Type(result) == sgre::script::SquirrelValueType::Array && vm.Length(result) == expected.size(), "global split token count changed");
	for (std::size_t index = 0; index < expected.size(); ++index) {
		Require(StringText(vm.GetSlot(result, static_cast<std::int32_t>(index))) == expected[index], "global split token bytes changed");
	}
}

static void TestGlobalSplitByteContractAndSignatures() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto split = vm.GetSlot(vm.RootTable(), std::string("split"));
	const auto info = InvokeDelegate(vm, split, "getinfos");
	const auto masks = vm.GetSlot(info, std::string("typecheck"));
	Require(std::get<std::int32_t>(vm.GetSlot(info, std::string("paramscheck"))) == 3 && vm.Length(masks) == 3 && StringText(vm.GetSlot(info, std::string("name"))) == "split", "global split lost its original name or exact receiver-inclusive arity");
	Require(std::get<std::int32_t>(vm.GetSlot(masks, std::int32_t{0})) == -1 && std::get<std::int32_t>(vm.GetSlot(masks, std::int32_t{1})) == 16 && std::get<std::int32_t>(vm.GetSlot(masks, std::int32_t{2})) == 16, "global split must retain its .ss signature");

	// Original global 0x140265a70/linked 0x14059b6d0 vectors; the delegate keeps different empty/subsequence rules.
	const std::array<std::string_view, 2> ab{"a", "b"};
	RequireSplitTokens(vm, InvokeGlobalSplit(vm, ",,a,,b,,", ","), ab);
	const std::array<std::string_view, 4> abcd{"a", "b", "c", "d"};
	RequireSplitTokens(vm, InvokeGlobalSplit(vm, "a,b;c d", ",; "), abcd);
	const std::array<std::string_view, 3> abc{"a", "b", "c"};
	RequireSplitTokens(vm, InvokeGlobalSplit(vm, "a::b:c", "::"), abc);
	RequireSplitTokens(vm, InvokeGlobalSplit(vm, "", ","), {});
	RequireSplitTokens(vm, InvokeGlobalSplit(vm, ",,,,", ","), {});
	RequireSplitTokens(vm, InvokeGlobalSplit(vm, std::string("\0tail", 5), ","), {});
	const std::array<std::string_view, 1> prefix{"abc"};
	RequireSplitTokens(vm, InvokeGlobalSplit(vm, std::string("abc\0def", 7), ","), prefix);
	RequireSplitTokens(vm, InvokeGlobalSplit(vm, "abc", std::string("\0x", 2)), prefix);
	const std::array<std::string_view, 2> nul_separator{"a;b", "c"};
	RequireSplitTokens(vm, InvokeGlobalSplit(vm, "a;b,c", std::string(",\0;", 3)), nul_separator);
	const std::array<std::string_view, 2> byte_tokens{std::string_view("\xc3", 1), std::string_view("X\xc3\xa0", 3)};
	RequireSplitTokens(vm, InvokeGlobalSplit(vm, std::string("\xc3\xa9X\xc3\xa0", 5), std::string("\xa9", 1)), byte_tokens);
	RequireFailure([&] {
		static_cast<void>(InvokeGlobalSplit(vm, "abc", ""));
	},
	    "empty separators string");

	const std::array<SquirrelValue, 4> complete{vm.RootTable(), std::string("a,b"), std::string(","), std::int32_t{7}};
	for (std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{4}}) {
		RequireFailure([&] {
			static_cast<void>(vm.Invoke(split, std::span<const SquirrelValue>(complete).first(count)));
		},
		    "wrong number of parameters");
	}

	std::size_t conversions = 0;
	const auto conversion_class = vm.CreateClass();
	vm.NewSlot(conversion_class, std::string("_tostring"), vm.CreateNativeClosure("forbidden_split_conversion", [&](SquirrelVm&, const auto) {
		++conversions;

		return sgre::script::SquirrelNativeResult::ReturnValue(std::string(","));
	}));
	const std::array<SquirrelValue, 6> rejected{SquirrelValue{}, std::int32_t{1}, 1.0f, true, vm.CreateTable(), vm.CreateInstance(conversion_class)};
	for (const auto& value : rejected) {
		for (const auto argument : {std::size_t{1}, std::size_t{2}}) {
			std::array<SquirrelValue, 3> arguments{std::int32_t{37}, std::string("a,b"), std::string(",")};
			arguments[argument] = value;
			RequireFailure([&] {
				static_cast<void>(vm.Invoke(split, arguments));
			},
			    "type mismatch");
		}
	}

	Require(conversions == 0, "global split signature checks must not invoke _tostring");
	const std::array<SquirrelValue, 3> scalar_receiver{std::int32_t{37}, std::string("a,b"), std::string(",")};
	RequireSplitTokens(vm, vm.Invoke(split, scalar_receiver).Value, ab);
	const std::array<SquirrelValue, 1> delegate_separator{std::string("::")};
	Require(vm.Length(InvokeDelegate(vm, std::string("::a::::b::"), "split", delegate_separator)) == 5, "global split must not replace the existing string delegate");
}

static void TestGlobalSplitScriptAndTokenLifetime() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	// The actual textFilterTips call prepares root/implicit-this split, then passes source and separators.
	auto program = Program({Instruction(8, 3, 0, 0, 4), Instruction(10, 5, 1), Instruction(10, 6, 2), Instruction(6, 7, 3, 4, 3), Instruction(19, 1, 7)}, 8);
	program.MainFunction.Parameters.push_back(std::string("source"));
	program.MainFunction.Parameters.push_back(std::string("separators"));
	program.MainFunction.Literals = {std::string("split")};
	SquirrelValue callable = vm.CreateClosure(std::move(program));
	std::array<SquirrelValue, 3> arguments{vm.RootTable(), vm.CreateString("retained-first,retained-second"), vm.CreateString(",")};
	SquirrelValue result = vm.Invoke(callable, arguments).Value;
	auto output = vm.Retain(result);
	const auto first_weak = vm.CreateWeakReference(vm.GetSlot(result, std::int32_t{0}));
	const auto weak_root = vm.Retain(first_weak);
	arguments = {};
	callable = {};
	result = {};
	vm.ResetExecution();
	vm.SetRootTable(vm.CreateTable());
	static_cast<void>(vm.CollectGarbage());
	const std::array<std::string_view, 2> expected{"retained-first", "retained-second"};
	RequireSplitTokens(vm, output.Value(), expected);
	Require(StringText(vm.ResolveWeakReference(weak_root.Value())) == expected[0], "global split array must own tokens after source, script and root retirement");
	vm.ClearArray(output.Value());
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_root.Value())), "clearing global split output must retire its last token ownership");
	output = {};
	vm.RethrowReleaseFailure();
}

static void TestGlobalSplitAllocationRollback() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxContainerSlots = 256;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	std::string too_many;
	for (std::size_t index = 0; index <= limits.MaxContainerSlots; ++index) {
		too_many += "token,";
	}

	RequireFailure([&] {
		static_cast<void>(InvokeGlobalSplit(vm, too_many, ","));
	},
	    "array slot limit exceeded");
	vm.ResetExecution();
	const std::array<std::string_view, 2> expected{"a", "b"};
	RequireSplitTokens(vm, InvokeGlobalSplit(vm, "a,b", ","), expected);

	limits.MaxInternedStrings = 512;
	SquirrelVm pooled(limits);
	pooled.InstallRecoveredBaseLibrary();
	const auto split = pooled.GetSlot(pooled.RootTable(), std::string("split"));
	const std::array<SquirrelValue, 3> arguments{pooled.RootTable(), pooled.CreateString("unique-partial-a,unique-partial-b"), pooled.CreateString(",")};
	std::vector<SquirrelValue> held;
	bool full = false;
	for (std::size_t index = 0; index <= limits.MaxInternedStrings; ++index) {
		try {
			held.push_back(pooled.CreateString("reserved-split-string-" + std::to_string(index)));
		} catch (const std::runtime_error& error) {
			Require(std::string_view(error.what()).find("aggregate interned string limit") != std::string_view::npos, error.what());
			full = true;
			break;
		}
	}

	Require(full && !held.empty(), "global split pool fixture did not reach its explicit capacity");
	held.pop_back();
	for (std::size_t attempt = 0; attempt < 3; ++attempt) {
		RequireFailure([&] {
			static_cast<void>(pooled.Invoke(split, arguments));
		},
		    "aggregate interned string limit");
		pooled.ResetExecution();
		const auto reclaimed_slot = pooled.CreateString("post-split-rollback");
		Require(StringText(reclaimed_slot) == "post-split-rollback", "failed split retained a partially appended token's pool budget");
	}

	held.clear();
	const std::array<std::string_view, 2> partial_expected{"unique-partial-a", "unique-partial-b"};
	RequireSplitTokens(pooled, pooled.Invoke(split, arguments).Value, partial_expected);
	pooled.RethrowReleaseFailure();
}

static constexpr std::array<const char*, 3> kGlobalTrimNames{"strip", "lstrip", "rstrip"};

static std::string InvokeGlobalTrim(SquirrelVm& vm, std::size_t kind, std::string source) {
	Require(kind < kGlobalTrimNames.size(), "global trim fixture kind is invalid");
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), std::move(source)};
	const auto result = vm.Invoke(vm.GetSlot(vm.RootTable(), std::string(kGlobalTrimNames[kind])), arguments);
	Require(result.State == sgre::script::SquirrelRunState::Completed, "global trim unexpectedly suspended");

	return StringText(result.Value);
}

static void TestGlobalTrimNativeByteRanges() {
	struct Case {
		std::string Source;
		std::array<std::string, 3> Expected;
	};
	// Native 2658c0/265980/2659e0 with original linked 588110 and its initial CRT table.
	const std::array cases{
	    Case{"", {"", "", ""}},
	    Case{" ", {"", "", " "}},
	    Case{"   ", {"", "", " "}},
	    Case{"\t\n\v\f\r ", {"", "", "\t"}},
	    Case{"  a  ", {"a", "a  ", "  a"}},
	    Case{std::string("\0tail", 5), {std::string("\0tail", 5), "", std::string("\0tail", 5)}},
	    Case{std::string("a\0tail", 6), {std::string("a\0tail", 6), "a", std::string("a\0tail", 6)}},
	    Case{std::string("  \0  ", 5), {std::string("\0", 1), "", std::string("  \0", 3)}},
	    Case{std::string(" \0", 2), {std::string("\0", 1), "", std::string(" \0", 2)}},
	    Case{std::string("a\0 \t", 4), {std::string("a\0", 2), "a", std::string("a\0", 2)}},
	    Case{std::string("\t a\0b \r", 7), {std::string("a\0b", 3), "a", std::string("\t a\0b", 5)}},
	    Case{std::string("\xff a \x80", 5), {std::string("\xff a \x80", 5), std::string("\xff a \x80", 5), std::string("\xff a \x80", 5)}},
	    Case{"\xc2\xa0"
	         "a"
	         "\xc2\xa0",
	        {"\xc2\xa0"
	         "a"
	         "\xc2\xa0",
	            "\xc2\xa0"
	            "a"
	            "\xc2\xa0",
	            "\xc2\xa0"
	            "a"
	            "\xc2\xa0"}},
	};
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	for (const auto& item : cases) {
		for (std::size_t kind = 0; kind < kGlobalTrimNames.size(); ++kind) {
			Require(InvokeGlobalTrim(vm, kind, item.Source) == item.Expected[kind], "global trim changed an original stored-range, first-byte or C-prefix result");
		}
	}
}

static void TestGlobalTrimSignaturesAndInitialPhase() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto initial_root = vm.RootTable();
	const auto marker = vm.CreateNativeClosure("trim_phase_override", [](SquirrelVm&, const auto) {
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{71});
	});
	std::size_t conversions = 0;
	const auto conversion_class = vm.CreateClass();
	vm.NewSlot(conversion_class, std::string("_tostring"), vm.CreateNativeClosure("forbidden_trim_conversion", [&](SquirrelVm&, const auto) {
		++conversions;

		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("text"));
	}));
	const std::array<SquirrelValue, 6> rejected{SquirrelValue{}, std::int32_t{1}, 1.0f, true, vm.CreateTable(), vm.CreateInstance(conversion_class)};
	std::array<SquirrelValue, 3> functions;
	for (std::size_t kind = 0; kind < kGlobalTrimNames.size(); ++kind) {
		const std::string name = kGlobalTrimNames[kind];
		const auto function = vm.GetSlot(initial_root, name);
		functions[kind] = function;
		const auto info = InvokeDelegate(vm, function, "getinfos");
		const auto masks = vm.GetSlot(info, std::string("typecheck"));
		Require(StringText(vm.GetSlot(info, std::string("name"))) == name && std::get<std::int32_t>(vm.GetSlot(info, std::string("paramscheck"))) == 2 && vm.Length(masks) == 2 && std::get<std::int32_t>(vm.GetSlot(masks, std::int32_t{0})) == -1 && std::get<std::int32_t>(vm.GetSlot(masks, std::int32_t{1})) == 16, "global trim lost its exact name/2/.s metadata");
		const std::array<SquirrelValue, 3> complete{initial_root, std::string("text"), std::int32_t{7}};
		for (std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{3}}) {
			RequireFailure([&] {
				static_cast<void>(vm.Invoke(function, std::span<const SquirrelValue>(complete).first(count)));
			},
			    "wrong number of parameters");
		}

		for (const auto& value : rejected) {
			const std::array<SquirrelValue, 2> arguments{std::int32_t{37}, value};
			RequireFailure([&] {
				static_cast<void>(vm.Invoke(function, arguments));
			},
			    "type mismatch");
		}

		const std::array<SquirrelValue, 2> scalar_receiver{std::int32_t{37}, std::string("text")};
		Require(StringText(vm.Invoke(function, scalar_receiver).Value) == "text", "global trim wildcard receiver must not become an instance-only method");
		vm.SetSlot(initial_root, name, marker);
	}

	Require(conversions == 0, "global trim must reject wrong types before invoking _tostring");
	const auto inherited = vm.CreateContext();
	for (const auto* name : kGlobalTrimNames) {
		Require(vm.GetSlot(vm.RootTableInContext(inherited), std::string(name)) == marker, "child base registration must preserve existing string-library overrides");
	}

	vm.ReleaseContext(inherited);
	const auto replacement = vm.CreateTable();
	vm.SetRootTable(replacement);
	const auto child = vm.CreateContext();
	for (std::size_t kind = 0; kind < kGlobalTrimNames.size(); ++kind) {
		Require(!vm.FindRawSlot(replacement, std::string(kGlobalTrimNames[kind])), "replacement-root child must not reinstall startup trim globals");
		const std::array<SquirrelValue, 2> arguments{replacement, std::string("text")};
		Require(StringText(vm.InvokeInContext(child, functions[kind], arguments).Value) == "text", "retained trim closure cannot execute in its actual child context");
	}

	vm.ReleaseContext(child);
}

static void TestGlobalTrimOutputLifetime() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	std::array<SquirrelValue, 2> arguments{vm.RootTable(), vm.CreateString(std::string("\t kept\0tail \r", 13))};
	const auto source_weak = vm.CreateWeakReference(arguments[1]);
	const auto source_watch = vm.Retain(source_weak);
	SquirrelValue result = vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("strip")), arguments).Value;
	auto output = vm.Retain(result);
	const auto weak = vm.CreateWeakReference(result);
	const auto output_watch = vm.Retain(weak);
	arguments = {};
	result = {};
	vm.ResetExecution();
	vm.SetRootTable(vm.CreateTable());
	static_cast<void>(vm.CollectGarbage());
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(source_watch.Value())) && StringText(output.Value()) == std::string("kept\0tail", 9), "trim output must own its complete new stored extent after source/root retirement");
	output = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(output_watch.Value())), "last trim output release must expire its canonical string identity");
	vm.RethrowReleaseFailure();
}

static std::uint32_t ReadTrimNativeWord(std::string_view bytes, std::size_t& position) {
	if (position > bytes.size() || 4 > bytes.size() - position) {
		throw std::runtime_error("trim native fixture word exceeds its input");
	}

	std::uint32_t value = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[position++])) << (index * 8);
	}

	return value;
}

static void ReplayGlobalTrimNative(const char* path) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input) {
		throw std::runtime_error("cannot open trim native fixture");
	}

	const auto extent = input.tellg();
	if (extent < 8 || extent > 1024 * 1024) {
		throw std::runtime_error("trim native fixture extent is outside its bounded contract");
	}

	std::string bytes(static_cast<std::size_t>(extent), '\0');
	input.seekg(0);
	input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(input.good() && std::string_view(bytes).substr(0, 4) == "STR1", "trim native fixture read or magic failed");
	std::size_t position = 4;
	const auto count = ReadTrimNativeWord(bytes, position);
	Require(count != 0 && count <= 4096, "trim native fixture count exceeds its bound");
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	for (std::uint32_t ordinal = 0; ordinal < count; ++ordinal) {
		Require(position < bytes.size(), "trim native fixture kind exceeds its input");
		const auto kind = static_cast<unsigned char>(bytes[position++]);
		const auto source_length = ReadTrimNativeWord(bytes, position);
		const auto result_length = ReadTrimNativeWord(bytes, position);
		Require(kind < kGlobalTrimNames.size() && source_length <= 4096 && result_length <= 4096 && source_length <= bytes.size() - position, "trim native fixture source range is invalid");
		const auto source = bytes.substr(position, source_length);
		position += source_length;
		Require(result_length <= bytes.size() - position, "trim native fixture result range is invalid");
		const auto expected = std::string_view(bytes).substr(position, result_length);
		position += result_length;
		if (InvokeGlobalTrim(vm, kind, source) != expected) {
			throw std::runtime_error("global trim native difference at case " + std::to_string(ordinal));
		}

		vm.ResetExecution();
	}

	Require(position == bytes.size(), "trim native fixture has trailing data");
	std::cout << "Global trim original byte/range replay passed cases=" << count << '\n';
}

static void TestWeakReferences() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto target = vm.CreateTable();
	const auto weak = vm.CreateWeakReference(target);
	Require(vm.Type(weak) == sgre::script::SquirrelValueType::WeakReference && vm.CreateWeakReference(target) == weak, "weak identity must be cached on its target");
	Require(vm.ResolveWeakReference(weak) == target && InvokeDelegate(vm, weak, "ref") == target, "weak ref must return its borrowed target");
	Require(InvokeDelegate(vm, std::int32_t{17}, "weakref") == SquirrelValue(std::int32_t{17}), "weakref must retain a non-reference scalar value");
	const auto owned_string = vm.CreateString("owned string");
	const auto string_weak = vm.CreateWeakReference(owned_string);
	Require(vm.ResolveWeakReference(string_weak) == owned_string && vm.CreateWeakReference(owned_string) == string_weak, "interned strings must retain a cached weak identity");
	const auto weak_of_weak = vm.CreateWeakReference(weak);
	Require(vm.ResolveWeakReference(weak_of_weak) == weak, "weak resolution must dereference only one level");
	const auto table = vm.CreateTable();
	vm.NewSlot(table, std::string("borrowed"), weak);
	vm.NewSlot(table, std::string("nested"), weak_of_weak);
	Require(vm.GetSlot(table, std::string("borrowed")) == target && vm.GetSlot(table, std::string("nested")) == weak, "table reads must dereference one stored weak value");
	const std::array<SquirrelValue, 1> raw_key{std::string("borrowed")};
	Require(InvokeDelegate(vm, table, "rawget", raw_key) == target, "raw table reads must resolve a stored weak reference");
	const std::array<SquirrelValue, 1> values{weak};
	const auto array = vm.CreateArray(values);
	Require(vm.GetSlot(array, std::int32_t{0}) == target && InvokeDelegate(vm, array, "top") == weak, "array indexed access and top have distinct native weak behavior");
	Require(InvokeDelegate(vm, array, "pop") == weak, "array pop must return the raw stored weak value");
	const auto removed = vm.CreateArray(values);
	const std::array<SquirrelValue, 1> index{std::int32_t{0}};
	Require(InvokeDelegate(vm, removed, "remove", index) == target, "array remove must resolve its removed weak value");
	const auto sliced = InvokeDelegate(vm, vm.CreateArray(values), "slice");
	Require(InvokeDelegate(vm, sliced, "top") == target, "array slice must materialize resolved values");
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("field"), weak);
	vm.NewSlot(class_value, std::string("static"), weak, true);
	Require(vm.GetSlot(class_value, std::string("field")) == target && vm.GetSlot(class_value, std::string("static")) == weak, "class ordinary fields resolve weak values while static slots retain them");
	const auto instance = vm.CreateInstance(class_value);
	Require(vm.GetSlot(instance, std::string("field")) == target, "instance field reads must resolve their stored weak reference");
	const auto instance_weak = vm.CreateWeakReference(instance);
	auto clone_program = Program({Instruction(10, 1, 1), Instruction(54, 2, 1), Instruction(19, 1, 2)}, 3);
	clone_program.MainFunction.Parameters.push_back(std::string("value"));
	const auto clone = vm.CreateClosure(std::move(clone_program));
	const std::array<SquirrelValue, 2> clone_arguments{vm.RootTable(), instance};
	const auto cloned = vm.Invoke(clone, clone_arguments).Value;
	Require(vm.CreateWeakReference(cloned) != instance_weak && vm.ResolveWeakReference(vm.CreateWeakReference(cloned)) == cloned, "clone must not inherit its source's weak cache");
	auto receiver = Program({Instruction(19, 1, 0)}, 1);
	const auto closure = vm.CreateClosure(std::move(receiver));
	const auto bound = vm.BindEnvironment(closure, target);
	Require(InvokeDelegate(vm, bound, "getenv") == weak, "getenv must return the actual cached weak reference");
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	Require(vm.Invoke(bound, arguments).Value == target, "bound call must resolve its weak environment into the receiver");
	Require(vm.CreateWeakReference(bound) != vm.CreateWeakReference(closure), "bound closure must own a distinct weak identity cache");
}

static void TestExceptionClassAndHandlers() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto exception_class = vm.CreateClass();
	vm.NewSlot(exception_class, std::string("message"), SquirrelValue{});
	vm.NewSlot(exception_class, std::string("origin"), SquirrelValue{});
	auto constructor = Program({Instruction(1, 2, 0), Instruction(13, 3, 0, 2, 1), Instruction(21, 2), Instruction(8, 3, 1, 2, 4), Instruction(2, 5, 2), Instruction(6, 6, 3, 4, 2), Instruction(1, 2, 2), Instruction(13, 3, 0, 2, 6), Instruction(19, 255)}, 7);
	constructor.MainFunction.Parameters.push_back(std::string("message"));
	constructor.MainFunction.Literals = {std::string("message"), std::string("getstackinfos"), std::string("origin")};
	vm.NewSlot(exception_class, std::string("constructor"), vm.CreateClosure(std::move(constructor)));
	const std::array<SquirrelValue, 2> setting{vm.RootTable(), exception_class};
	Require(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("setexceptionclass")), setting).Value == exception_class, "setexceptionclass must return the installed class");
	auto caught = Program({Instruction(56, 2, 3), Instruction(1, 1, 0), Instruction(58, 1), Instruction(19, 255), Instruction(19, 1, 2)});
	caught.MainFunction.Literals = {std::string("original message")};
	sgre::script::SquirrelLineInformation line;
	line.Line = 28;
	caught.MainFunction.LineInformation.push_back(line);
	sgre::script::SquirrelLocalVariable local;
	local.Name = std::string("this");
	local.EndInstruction = 4;
	caught.MainFunction.LocalVariables.push_back(local);
	const auto caught_error = Run(vm, std::move(caught));
	Require(vm.IsInstanceOf(caught_error, exception_class), "string throw must be constructed before entering a trap");
	Require(StringText(vm.GetSlot(caught_error, std::string("message"))) == "original message", "exception constructor did not receive the original message");
	const auto origin = vm.GetSlot(caught_error, std::string("origin"));
	Require(StringText(vm.GetSlot(origin, std::string("func"))) == "main" && std::get<std::int32_t>(vm.GetSlot(origin, std::string("line"))) == 28, "exception stack snapshot did not retain the throwing frame and recorded line");
	const auto locals = vm.GetSlot(origin, std::string("locals"));
	Require(vm.GetSlot(locals, std::string("this")) == vm.RootTable(), "stack snapshot lost an in-scope local register");
	Require(std::get<std::int32_t>(Run(vm, Program({Instruction(56, 2, 3), Instruction(2, 1, 91), Instruction(58, 1), Instruction(19, 255), Instruction(19, 1, 2)}))) == 91, "non-string throw must retain its original value");
	std::size_t calls = 0;
	SquirrelValue observed;
	const auto handler = vm.CreateNativeClosure("error_observer", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && arguments[0] == current.RootTable(), "error handler must receive root and error");
		++calls;
		observed = arguments[1];

		return sgre::script::SquirrelNativeResult{};
	});
	const std::array<SquirrelValue, 2> install_handler{vm.RootTable(), handler};
	static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("seterrorhandler")), install_handler));
	auto failure = Program({Instruction(2, 1, 83), Instruction(58, 1)});
	const auto failed = vm.CreateClosure(std::move(failure));
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	try {
		static_cast<void>(vm.Invoke(failed, receiver));
		throw std::runtime_error("unhandled throw unexpectedly completed");
	} catch (const sgre::script::SquirrelVmException& error) {
		Require(std::get<std::int32_t>(error.ThrownValue()) == 83, "error handler replaced the original thrown value");
	}

	Require(calls == 1 && std::get<std::int32_t>(observed) == 83, "unhandled error did not notify the registered handler once");
	const auto child = vm.CreateContext();
	try {
		static_cast<void>(vm.InvokeInContext(child, failed, receiver));
	} catch (const sgre::script::SquirrelVmException&) {
	}

	Require(calls == 2, "new child context did not inherit its parent's error handler");
	const std::array<SquirrelValue, 2> clear_handler{vm.RootTable(), SquirrelValue{}};
	static_cast<void>(vm.InvokeInContext(child, vm.GetSlot(vm.RootTable(), std::string("seterrorhandler")), clear_handler));
	try {
		static_cast<void>(vm.InvokeInContext(child, failed, receiver));
	} catch (const sgre::script::SquirrelVmException&) {
	}

	Require(calls == 2, "child handler clearing changed another context or failed to clear");
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, failed, "pcall", receiver));
	},
	    "unhandled Squirrel exception");
	Require(calls == 2, "pcall must suppress the error-handler callback while preserving failure");
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, failed, "call", receiver));
	},
	    "unhandled Squirrel exception");
	Require(calls == 3, "ordinary closure call must request the error-handler callback");
}

static SquirrelValue RunArguments(SquirrelVm& vm, BytecodeProgram program, std::span<const SquirrelValue> arguments) {
	const auto closure = vm.CreateClosure(std::move(program));

	return vm.Invoke(closure, arguments).Value;
}

static void AttachDelegate(SquirrelVm& vm, const SquirrelValue& object, const SquirrelValue& delegate) {
	auto program = Program({Instruction(53, 1, 1, 2), Instruction(19, 1, 1)});
	program.MainFunction.Parameters.push_back(std::string("object"));
	program.MainFunction.Parameters.push_back(std::string("delegate"));
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), object, delegate};
	Require(RunArguments(vm, std::move(program), arguments) == object, "delegate expression did not return receiver");
}

static SquirrelValue RunOperator(SquirrelVm& vm, std::uint8_t opcode, std::uint8_t operation, const SquirrelValue& left, const SquirrelValue& right) {
	auto program = Program({Instruction(opcode, 3, 2, 1, operation), Instruction(19, 1, 3)});
	program.MainFunction.Parameters.push_back(std::string("left"));
	program.MainFunction.Parameters.push_back(std::string("right"));
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), left, right};

	return RunArguments(vm, std::move(program), arguments);
}

static std::uint64_t DiagnosticObjectAddress(SquirrelVm& vm, const SquirrelValue& value) {
	const auto text = vm.ToString(value);
	const auto first = text.find("0x");
	Require(first != std::string::npos && first + 18 < text.size(), "object pointer diagnostic must provide the retained sixteen-digit allocation identity");
	std::uint64_t address = 0;
	const auto result = std::from_chars(text.data() + first + 2, text.data() + first + 18, address, 16);
	Require(result.ec == std::errc{} && result.ptr == text.data() + first + 18, "object pointer diagnostic contains invalid hexadecimal identity");

	return address;
}

static std::int32_t DiagnosticPointerOrder(SquirrelVm& vm, const SquirrelValue& left, const SquirrelValue& right) {
	const auto address_left = DiagnosticObjectAddress(vm, left);
	const auto address_right = DiagnosticObjectAddress(vm, right);

	return address_left == address_right ? 0 : (address_left < address_right ? -1 : 1);
}

static void TestNativePointerOrdering() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	const auto native = [](SquirrelVm&, std::span<const SquirrelValue>) {
		return sgre::script::SquirrelNativeResult{};
	};
	auto generator_program = Program({Instruction(19, 255)}, 2);
	generator_program.MainFunction.IsGenerator = true;
	const auto generator_closure = vm.CreateClosure(std::move(generator_program));
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	const std::vector<std::pair<SquirrelValue, SquirrelValue>> pairs{
	    {vm.CreateTable(), vm.CreateTable()},
	    {vm.CreateArray(), vm.CreateArray()},
	    {vm.CreateClosure(Program({Instruction(19, 255)}, 2)), vm.CreateClosure(Program({Instruction(19, 255)}, 2))},
	    {vm.CreateNativeClosure("pointer_order_one", native), vm.CreateNativeClosure("pointer_order_two", native)},
	    {vm.CreateClass(), vm.CreateClass()},
	    {vm.CreateInstance(class_value), vm.CreateInstance(class_value)},
	    {vm.CreateWeakReference(vm.RootTable()), vm.CreateWeakReference(vm.ConstantTable())},
	    {vm.Invoke(generator_closure, receiver).Value, vm.Invoke(generator_closure, receiver).Value},
	};
	for (const auto& [left, right] : pairs) {
		const auto order = DiagnosticPointerOrder(vm, left, right);
		Require(order != 0 && vm.CompareValues(left, right) == order && vm.CompareValues(right, left) == -order && vm.CompareValues(left, left) == 0, "same-type object ordering must compare actual stable owned allocation identities");
		Require(std::get<bool>(RunOperator(vm, 40, 3, left, right)) == (order < 0), "bytecode comparison must use the same recovered pointer fallback as native comparison adapters");
	}

	RequireFailure([&] {
		static_cast<void>(vm.CompareValues(true, std::int32_t{1}));
	},
	    "comparsion between 'bool' and 'integer'");
	RequireFailure([&] {
		static_cast<void>(vm.CompareValues(pairs[0].first, pairs[1].first));
	},
	    "comparsion between 'table' and 'array'");
	Require(vm.CompareValues({}, pairs[1].first) == -1 && vm.CompareValues(pairs[1].first, {}) == 1, "null ordering must remain valid across different types");
}

static void TestFailedComparatorPointerFallback() {
	SquirrelVm vm;
	std::size_t calls = 0;
	const auto failing_class = vm.CreateClass();
	vm.NewSlot(failing_class, std::string("_cmp"), vm.CreateNativeClosure("failed_pointer_cmp", [&](SquirrelVm&, std::span<const SquirrelValue>) -> sgre::script::SquirrelNativeResult {
		++calls;
		throw std::runtime_error("independent comparator failure");
	}));
	const auto left = vm.CreateInstance(failing_class);
	const auto right = vm.CreateInstance(failing_class);
	const auto order = DiagnosticPointerOrder(vm, left, right);
	Require(vm.CompareValues(left, right) == order && std::get<bool>(RunOperator(vm, 40, 3, left, right)) == (order < 0) && calls == 2, "failed native comparator must recover pointer order without escaping as a script error");
	const auto script_class = vm.CreateClass();
	auto throwing = Program({Instruction(2, 2, 97), Instruction(58, 2)}, 3);
	throwing.MainFunction.Parameters.push_back(std::string("other"));
	vm.NewSlot(script_class, std::string("_cmp"), vm.CreateClosure(std::move(throwing)));
	const auto script_left = vm.CreateInstance(script_class);
	const auto script_right = vm.CreateInstance(script_class);
	const auto script_order = DiagnosticPointerOrder(vm, script_left, script_right);
	Require(vm.CompareValues(script_left, script_right) == script_order && std::get<bool>(RunOperator(vm, 40, 3, script_left, script_right)) == (script_order < 0), "failed script comparator must recover pointer order through its unwound continuation");
	const auto invalid_class = vm.CreateClass();
	vm.NewSlot(invalid_class, std::string("_cmp"), std::int32_t{9});
	const auto invalid_left = vm.CreateInstance(invalid_class);
	const auto invalid_right = vm.CreateInstance(invalid_class);
	const auto invalid_order = DiagnosticPointerOrder(vm, invalid_left, invalid_right);
	Require(vm.CompareValues(invalid_left, invalid_right) == invalid_order && std::get<bool>(RunOperator(vm, 40, 3, invalid_left, invalid_right)) == (invalid_order < 0), "noncallable comparator must take the native failed-call pointer fallback");
	const auto wrong_result_class = vm.CreateClass();
	vm.NewSlot(wrong_result_class, std::string("_cmp"), vm.CreateNativeClosure("wrong_pointer_cmp", [](SquirrelVm&, std::span<const SquirrelValue>) {
		sgre::script::SquirrelNativeResult result;
		result.Value = 1.0f;

		return result;
	}));
	const auto wrong_left = vm.CreateInstance(wrong_result_class);
	const auto wrong_right = vm.CreateInstance(wrong_result_class);
	RequireFailure([&] {
		static_cast<void>(vm.CompareValues(wrong_left, wrong_right));
	},
	    "comparsion between 'instance' and 'instance'");
	RequireFailure([&] {
		static_cast<void>(RunOperator(vm, 40, 3, wrong_left, wrong_right));
	},
	    "comparsion between 'instance' and 'instance'");
}

static void TestMetamethodContinuations() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	vm.NewSlot(receiver, std::string("backing"), std::int32_t{17});
	const auto delegate = vm.CreateTable();
	auto getter_program = Program({Instruction(9, 2, 0, 0), Instruction(19, 1, 2)}, 3);
	getter_program.MainFunction.Parameters.push_back(std::string("key"));
	getter_program.MainFunction.Literals = {std::string("backing")};
	const auto getter = vm.CreateClosure(std::move(getter_program));
	vm.NewSlot(delegate, std::string("_get"), getter);
	AttachDelegate(vm, receiver, delegate);
	auto read = Program({Instruction(9, 2, 0, 1), Instruction(19, 1, 2)});
	read.MainFunction.Parameters.push_back(std::string("receiver"));
	read.MainFunction.Literals = {std::string("dynamic")};
	const std::array<SquirrelValue, 2> read_arguments{vm.RootTable(), receiver};
	Require(std::get<std::int32_t>(RunArguments(vm, std::move(read), read_arguments)) == 17, "script _get continuation lost its receiver or result");

	std::size_t setter_calls = 0;
	const auto setter = vm.CreateNativeClosure("set_dynamic", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 3 && arguments[0] == receiver, "_set native receiver/key/value convention changed");
		current.SetSlot(arguments[0], std::string("backing"), arguments[2]);
		++setter_calls;
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{-99};

		return result;
	});
	vm.NewSlot(delegate, std::string("_set"), setter);
	auto write = Program({Instruction(1, 2, 0), Instruction(2, 3, 42), Instruction(13, 4, 1, 2, 3), Instruction(19, 1, 4)});
	write.MainFunction.Parameters.push_back(std::string("receiver"));
	write.MainFunction.Literals = {std::string("dynamic")};
	Require(std::get<std::int32_t>(RunArguments(vm, std::move(write), read_arguments)) == 42 && setter_calls == 1, "_set return must retain assignment operand instead of callback result");
	Require(std::get<std::int32_t>(vm.GetSlot(receiver, std::string("backing"))) == 42, "_set native side effect was lost");

	auto add_program = Program({Instruction(2, 2, 100), Instruction(17, 3, 1, 2, '+'), Instruction(19, 1, 3)}, 4);
	add_program.MainFunction.Parameters.push_back(std::string("right"));
	vm.NewSlot(delegate, std::string("_add"), vm.CreateClosure(std::move(add_program)));
	Require(std::get<std::int32_t>(RunOperator(vm, 17, '+', receiver, std::int32_t{9})) == 109, "script arithmetic metamethod result was lost");

	const auto comparator = vm.CreateNativeClosure("ordering", [](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2, "_cmp argument count changed");
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{-7};

		return result;
	});
	vm.NewSlot(delegate, std::string("_cmp"), comparator);
	const auto other_receiver = vm.CreateTable();
	Require(std::get<bool>(RunOperator(vm, 40, 3, receiver, other_receiver)), "_cmp integer result did not reach relational continuation");
	Require(!std::get<bool>(RunOperator(vm, 40, 0, receiver, other_receiver)), "_cmp relation direction changed");
	Require(!std::get<bool>(RunOperator(vm, 15, 0, receiver, std::int32_t{0})), "table equality must not invoke instance-only _cmp");

	const auto callable = vm.CreateNativeClosure("call_object", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 3 && arguments[0] == receiver && arguments[1] == vm.RootTable(), "_call must prepend object before original call receiver");
		sgre::script::SquirrelNativeResult result;
		result.Value = arguments[2];

		return result;
	});
	vm.NewSlot(delegate, std::string("_call"), callable);
	auto call = Program({Instruction(10, 2, 0), Instruction(2, 3, 81), Instruction(6, 4, 1, 2, 2), Instruction(19, 1, 4)});
	call.MainFunction.Parameters.push_back(std::string("callable"));
	Require(std::get<std::int32_t>(RunArguments(vm, std::move(call), read_arguments)) == 81, "_call return continuation failed");
}

static void TestCompoundMetamethods() {
	SquirrelVm vm;
	const auto number_class = vm.CreateClass();
	auto addition = Program({Instruction(2, 2, 100), Instruction(17, 3, 1, 2, '+'), Instruction(19, 1, 3)}, 4);
	addition.MainFunction.Parameters.push_back(std::string("operand"));
	vm.NewSlot(number_class, std::string("_add"), vm.CreateClosure(std::move(addition)));
	const auto number = vm.CreateInstance(number_class);
	const auto receiver = vm.CreateTable();
	vm.NewSlot(receiver, std::string("backing"), number);
	const auto delegate = vm.CreateTable();
	auto getter = Program({Instruction(9, 2, 0, 0), Instruction(19, 1, 2)}, 3);
	getter.MainFunction.Parameters.push_back(std::string("key"));
	getter.MainFunction.Literals = {std::string("backing")};
	vm.NewSlot(delegate, std::string("_get"), vm.CreateClosure(std::move(getter)));
	std::size_t setter_calls = 0;
	vm.NewSlot(delegate, std::string("_set"), vm.CreateNativeClosure("compound_setter", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 3 && arguments[0] == receiver && StringText(arguments[1]) == "dynamic", "compound setter lost its receiver/key/value");
		++setter_calls;
		current.SetSlot(receiver, std::string("backing"), arguments[2]);
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{-99};

		return result;
	}));
	AttachDelegate(vm, receiver, delegate);
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), receiver};
	auto compound = Program({Instruction(1, 2, 0), Instruction(2, 3, 9), Instruction(34, 4, (1 << 16) | 3, 2, '+'), Instruction(19, 1, 4)});
	compound.MainFunction.Parameters.push_back(std::string("receiver"));
	compound.MainFunction.Literals = {std::string("dynamic")};
	Require(std::get<std::int32_t>(RunArguments(vm, std::move(compound), arguments)) == 109 && setter_calls == 1, "compound expression did not chain script _get/_add and native _set");
	Require(std::get<std::int32_t>(vm.GetSlot(receiver, std::string("backing"))) == 109, "compound setter did not receive computed value");
	for (const auto opcode : {std::uint8_t{36}, std::uint8_t{38}}) {
		auto increment = Program({Instruction(1, 2, 0), Instruction(opcode, 3, 1, 2, 1), Instruction(19, 1, 3)});
		increment.MainFunction.Parameters.push_back(std::string("receiver"));
		increment.MainFunction.Literals = {std::string("dynamic")};
		Require(std::get<std::int32_t>(RunArguments(vm, std::move(increment), arguments)) == 110, "compound pre/post slot increment returned incorrect old/new value");
	}

	Require(setter_calls == 3 && std::get<std::int32_t>(vm.GetSlot(receiver, std::string("backing"))) == 111, "compound increments lost setter mutations");
	for (const auto opcode : {std::uint8_t{35}, std::uint8_t{37}, std::uint8_t{39}}) {
		auto local = Program({Instruction(2, 2, 9), Instruction(opcode, 3, 1, 2, opcode == 35 ? '+' : 1), Instruction(31, 4), Instruction(32, 4, 1), Instruction(32, 4, 3), Instruction(19, 1, 4)});
		local.MainFunction.Parameters.push_back(std::string("number"));
		const std::array<SquirrelValue, 2> local_arguments{vm.RootTable(), number};
		const auto values = RunArguments(vm, std::move(local), local_arguments);
		Require(std::get<std::int32_t>(vm.GetSlot(values, std::int32_t{0})) == (opcode == 35 ? 109 : 101), "local compound arithmetic did not replace its source register");
		if (opcode == 39) {
			Require(vm.GetSlot(values, std::int32_t{1}) == number, "post local increment did not preserve its original object value");
		} else {
			Require(vm.GetSlot(values, std::int32_t{1}) == vm.GetSlot(values, std::int32_t{0}), "local compound result and changed register diverged");
		}
	}

	auto failed_setter = Program({Instruction(2, 3, 91), Instruction(58, 3)}, 4);
	failed_setter.MainFunction.Parameters = {std::string("this"), std::string("key"), std::string("value")};
	vm.NewSlot(delegate, std::string("_set"), vm.CreateClosure(std::move(failed_setter)));
	auto failed_write = Program({Instruction(1, 2, 0), Instruction(36, 3, 1, 2, 1), Instruction(19, 1, 3)});
	failed_write.MainFunction.Parameters.push_back(std::string("receiver"));
	failed_write.MainFunction.Literals = {std::string("dynamic")};
	Require(std::get<std::int32_t>(RunArguments(vm, std::move(failed_write), arguments)) == 112, "native compound helper ignores a failed setter status after computation");
	Require(std::get<std::int32_t>(vm.GetSlot(receiver, std::string("backing"))) == 111, "failed setter unexpectedly mutated the backing value");
}

static void TestExistMetamethodStatus() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	auto exists = Program({Instruction(22, 2, 0), Instruction(19, 1, 2)}, 3);
	exists.MainFunction.Parameters.push_back(std::string("key"));
	vm.NewSlot(class_value, std::string("_exist"), vm.CreateClosure(std::move(exists)));
	const auto instance = vm.CreateInstance(class_value);
	auto query = Program({Instruction(1, 2, 0), Instruction(41, 3, 1, 2), Instruction(19, 1, 3)});
	query.MainFunction.Parameters.push_back(std::string("receiver"));
	query.MainFunction.Literals = {std::string("absent")};
	const auto closure = vm.CreateClosure(std::move(query));
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), instance};
	Require(std::get<bool>(vm.Invoke(closure, arguments).Value), "_exist successful call status counts as true even when the returned value is false");
	const auto failed_class = vm.CreateClass();
	auto failed = Program({Instruction(2, 2, 83), Instruction(58, 2)}, 3);
	failed.MainFunction.Parameters.push_back(std::string("key"));
	vm.NewSlot(failed_class, std::string("_exist"), vm.CreateClosure(std::move(failed)));
	const std::array<SquirrelValue, 2> failed_arguments{vm.RootTable(), vm.CreateInstance(failed_class)};
	Require(!std::get<bool>(vm.Invoke(closure, failed_arguments).Value), "failed _exist call must return false without propagating its thrown value");
}

static void TestSharedExecutionContexts() {
	SquirrelVm vm;
	sgre::script::SquirrelContextHandle child_context;
	vm.RegisterNative("request_child", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 1 && current.CurrentContext() == vm.RootContext(), "host callback lost its active execution context");
		child_context = current.CreateContext();
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;
		result.Value = std::string("queued child");

		return result;
	});
	auto parent_program = Program({Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)});
	parent_program.MainFunction.Literals = {std::string("request_child")};
	const auto parent = vm.CreateClosure(std::move(parent_program));
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	const auto waiting = vm.Invoke(parent, receiver);
	Require(waiting.State == sgre::script::SquirrelRunState::Suspended && child_context != vm.RootContext(), "parent context did not retain a distinct queued child");
	Require(vm.ContextState(child_context) == sgre::script::SquirrelContextState::Idle, "new child context must remain idle until scheduled");

	auto child_program = Program({Instruction(1, 1, 0), Instruction(2, 2, 88), Instruction(11, 3, 0, 1, 2), Instruction(19, 1, 3)});
	child_program.MainFunction.Literals = {std::string("child_mutation")};
	const auto child = vm.CreateClosure(std::move(child_program));
	std::vector<sgre::script::SquirrelContextHandle> traces;
	vm.SetTraceCallback([&](const sgre::script::SquirrelTraceEvent& event) {
		traces.push_back(event.Context);
	});
	const auto completed = vm.InvokeInContext(child_context, child, receiver);
	Require(completed.Context == child_context && std::get<std::int32_t>(completed.Value) == 88, "scheduled child result or identity changed");
	Require(vm.ContextState(vm.RootContext()) == sgre::script::SquirrelContextState::Suspended, "running child modified the parent's suspension state");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.RootTable(), std::string("child_mutation"))) == 88, "contexts did not share root-table mutations");
	const auto resumed = vm.ResumeInContext(vm.RootContext(), completed.Value);
	Require(std::get<std::int32_t>(resumed.Value) == 88 && resumed.Context == vm.RootContext(), "child result did not reach parent suspension destination");
	Require(!traces.empty() && traces.front() == child_context && traces.back() == vm.RootContext(), "trace callback did not distinguish child and parent execution");

	vm.ReleaseContext(child_context);
	const auto replacement = vm.CreateContext();
	Require(replacement.Index == child_context.Index && replacement.Generation != child_context.Generation, "context slot reuse must change generation");
	RequireFailure([&] {
		static_cast<void>(vm.ContextState(child_context));
	},
	    "context handle is invalid");
	RequireFailure([&] {
		vm.ReleaseContext(vm.RootContext());
	},
	    "cannot release the root");

	auto failure_program = Program({Instruction(2, 1, 91), Instruction(58, 1)});
	const auto failure = vm.CreateClosure(std::move(failure_program));
	try {
		static_cast<void>(vm.InvokeInContext(replacement, failure, receiver));
		throw std::runtime_error("child throw was not propagated");
	} catch (const sgre::script::SquirrelVmException& error) {
		Require(std::get<std::int32_t>(error.ThrownValue()) == 91, "child exception lost its original script value");
	}

	Require(vm.ContextState(vm.RootContext()) == sgre::script::SquirrelContextState::Idle, "child failure contaminated another context");
}

static void TestMetamethodSuspensionBoundary() {
	SquirrelVm vm;
	const auto receiver = vm.CreateTable();
	const auto delegate = vm.CreateTable();
	const auto getter = vm.CreateNativeClosure("suspending_getter", [](SquirrelVm&, std::span<const SquirrelValue>) {
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	});
	vm.NewSlot(delegate, std::string("_get"), getter);
	AttachDelegate(vm, receiver, delegate);
	RequireFailure([&] {
		static_cast<void>(vm.ReadSlot(receiver, std::string("dynamic")));
	},
	    "the index doesn't exist");
	auto read = Program({Instruction(9, 2, 0, 1), Instruction(19, 1, 2)});
	read.MainFunction.Parameters.push_back(std::string("receiver"));
	read.MainFunction.Literals = {std::string("dynamic")};
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), receiver};
	RequireFailure([&] {
		static_cast<void>(RunArguments(vm, std::move(read), arguments));
	},
	    "does not exist");
	Require(!vm.IsSuspended(), "rejected metamethod suspension changed VM state");
}

static void TestSafetyLimits() {
	SquirrelVm vm;
	const auto closure = vm.CreateClosure(Program({Instruction(24, 0, -1)}));
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(closure, arguments, 32));
	},
	    "instruction budget exceeded");
	vm.ResetExecution();
	RequireFailure([&] {
		static_cast<void>(Run(vm, Program({Instruction(2, 250, 1), Instruction(19, 255)})));
	},
	    "register exceeds");
	RequireFailure([&] {
		static_cast<void>(Run(vm, Program({Instruction(24, 0, -2)})));
	},
	    "branch target exceeds");
	sgre::script::SquirrelVmLimits limits;
	limits.MaxObjects = 3;
	SquirrelVm bounded(limits);
	static_cast<void>(bounded.CreateTable());
	RequireFailure([&] {
		static_cast<void>(bounded.CreateTable());
	},
	    "object limit exceeded");
}

static void TestNativeInstanceBridge() {
	SquirrelVm vm;
	const auto base = vm.CreateClass();
	vm.NewSlot(base, std::string("base_field"), std::int32_t{12});
	const auto derived = vm.CreateClass(base);
	std::size_t constructor_calls = 0;
	vm.NewSlot(derived, std::string("constructor"), vm.CreateNativeClosure("host_constructor", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && current.Type(arguments[0]) == sgre::script::SquirrelValueType::Instance, "native constructor receiver is not an instance");
		current.SetSlot(arguments[0], std::string("base_field"), arguments[1]);
		++constructor_calls;
		sgre::script::SquirrelNativeResult result;
		result.Value = std::string("discarded constructor result");

		return result;
	}));
	const auto bare_instance = vm.CreateInstance(derived);
	Require(constructor_calls == 0 && std::get<std::int32_t>(vm.GetSlot(bare_instance, std::string("base_field"))) == 12, "create-instance invoked constructor or lost inherited defaults");
	RequireFailure([&] {
		vm.NewSlot(base, std::string("late_base_field"), std::int32_t{1});
	},
	    "class is locked");
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), std::int32_t{27}};
	const auto instance = vm.Invoke(derived, arguments).Value;
	Require(constructor_calls == 1 && vm.Type(instance) == sgre::script::SquirrelValueType::Instance && std::get<std::int32_t>(vm.GetSlot(instance, std::string("base_field"))) == 27, "top-level class call returned constructor value instead of initialized instance");
	const std::uint8_t type_tag = 0;
	sgre::script::SquirrelInstanceHostIdentity identity;
	identity.TypeTag = &type_tag;
	identity.Value = 901;
	vm.SetInstanceHostIdentity(instance, identity);
	Require(vm.InstanceHostIdentity(instance, &type_tag) == identity, "instance host identity was not retained");
	const std::uint8_t other_type_tag = 0;
	RequireFailure([&] {
		static_cast<void>(vm.InstanceHostIdentity(instance, &other_type_tag));
	},
	    "host type tag does not match");
	RequireFailure([&] {
		static_cast<void>(vm.CreateInstance(vm.RootTable()));
	},
	    "instance creation requires a class");
}

static void TestNestedNativeInvocations() {
	SquirrelVm vm;
	const auto child_context = vm.CreateContext();
	std::size_t callbacks = 0;
	vm.RegisterNative("invoke_on_base", [&](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2, "nested invocation callback argument count changed");
		const auto caller = current.CurrentContext();
		const std::array<SquirrelValue, 1> receiver{current.RootTable()};
		const auto nested = current.InvokeInContext(current.RootContext(), arguments[1], receiver);
		Require(nested.Context == current.RootContext() && nested.State == sgre::script::SquirrelRunState::Completed, "nested call did not return its own completed result");
		Require(current.CurrentContext() == caller, "nested call did not restore caller context");
		++callbacks;
		sgre::script::SquirrelNativeResult result;
		result.Value = nested.Value;

		return result;
	});
	auto inner_program = Program({Instruction(2, 1, 61), Instruction(19, 1, 1)});
	const auto inner = vm.CreateClosure(std::move(inner_program));
	auto outer_program = Program({Instruction(8, 2, 0, 0, 3), Instruction(10, 4, 1), Instruction(6, 5, 2, 3, 2), Instruction(2, 6, 1), Instruction(17, 7, 6, 5, '+'), Instruction(19, 1, 7)});
	outer_program.MainFunction.Parameters.push_back(std::string("inner"));
	outer_program.MainFunction.Literals = {std::string("invoke_on_base")};
	const auto outer = vm.CreateClosure(std::move(outer_program));
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), inner};
	Require(std::get<std::int32_t>(vm.Invoke(outer, arguments).Value) == 62, "same-context native reentry consumed caller frame or lost its result");
	Require(std::get<std::int32_t>(vm.InvokeInContext(child_context, outer, arguments).Value) == 62 && callbacks == 2, "cross-context native reentry lost base result or caller continuation");
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(outer, arguments, 5));
	},
	    "instruction budget exceeded");
	Require(vm.ContextState(vm.RootContext()) == sgre::script::SquirrelContextState::Idle, "nested budget exhaustion retained caller frames");

	const auto throwing = vm.CreateClosure(Program({Instruction(2, 1, 73), Instruction(58, 1)}));
	auto trapped_program = Program({Instruction(56, 7, 4), Instruction(8, 2, 0, 0, 3), Instruction(10, 4, 1), Instruction(6, 5, 2, 3, 2), Instruction(19, 1, 5), Instruction(19, 1, 7)});
	trapped_program.MainFunction.Parameters.push_back(std::string("inner"));
	trapped_program.MainFunction.Literals = {std::string("invoke_on_base")};
	const auto trapped = vm.CreateClosure(std::move(trapped_program));
	const std::array<SquirrelValue, 2> throwing_arguments{vm.RootTable(), throwing};
	Require(std::get<std::int32_t>(vm.Invoke(trapped, throwing_arguments).Value) == 73, "nested exception must return across native boundary before reaching caller trap");

	vm.InstallRecoveredBaseLibrary();
	auto suspend_program = Program({Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)});
	suspend_program.MainFunction.Literals = {std::string("suspend")};
	const auto suspend_callable = vm.CreateClosure(std::move(suspend_program));
	const std::array<SquirrelValue, 2> suspend_arguments{vm.RootTable(), suspend_callable};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(outer, suspend_arguments));
	},
	    "cannot suspend through native calls/metamethods");
	Require(vm.ContextState(vm.RootContext()) == sgre::script::SquirrelContextState::Idle, "rejected nested suspension retained an invocation");
}

static void TestIdleChildReleaseDuringParentExecution() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto child = vm.CreateContext();
	auto waiting = Program({Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)});
	waiting.MainFunction.Literals = {std::string("suspend")};
	const auto child_closure = vm.CreateClosure(std::move(waiting));
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	Require(vm.InvokeInContext(child, child_closure, receiver).State == sgre::script::SquirrelRunState::Suspended, "child release fixture must retain a suspended child frame");
	std::size_t releases = 0;
	const auto release_child = vm.CreateNativeClosure("release_idle_child", [&](SquirrelVm& current, const auto) {
		Require(current.CurrentContext() == current.RootContext(), "parent cleanup callback lost its executing context");
		RequireFailure([&] {
			current.ResetContext(current.CurrentContext());
		},
		    "executing context");
		current.ReleaseContext(child);
		++releases;
		sgre::script::SquirrelNativeResult result;
		result.Value = std::int32_t{17};

		return result;
	});
	Require(std::get<std::int32_t>(vm.Invoke(release_child, receiver).Value) == 17 && releases == 1, "idle child release must preserve the parent callback's return and native frame");
	RequireFailure([&] {
		static_cast<void>(vm.ContextState(child));
	},
	    "handle is invalid");
	const auto replacement = vm.CreateContext();
	Require(replacement.Index == child.Index && replacement.Generation != child.Generation, "released child context must recycle with a new generation");
	Require(vm.ContextState(vm.RootContext()) == sgre::script::SquirrelContextState::Idle, "child release must not reset the executing parent's state");
}

static void TestArrayJoinNativeBytesAndSignature() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto array = vm.CreateArray();
	const auto join = vm.GetSlot(array, std::string("join"));
	const auto info = InvokeDelegate(vm, join, "getinfos");
	const auto masks = vm.GetSlot(info, std::string("typecheck"));
	Require(std::get<std::int32_t>(vm.GetSlot(info, std::string("paramscheck"))) == 2 && StringText(vm.GetSlot(info, std::string("name"))) == "join" && vm.Length(masks) == 2 && std::get<std::int32_t>(vm.GetSlot(masks, std::int32_t{0})) == 64 && std::get<std::int32_t>(vm.GetSlot(masks, std::int32_t{1})) == 16, "Array.join must retain its receiver-inclusive2/as native registration");
	const auto joined = [&](std::initializer_list<SquirrelValue> values, std::string separator) {
		const auto source = vm.CreateArray(std::span<const SquirrelValue>(values.begin(), values.size()));
		const std::array<SquirrelValue, 1> arguments{std::move(separator)};

		return std::string(StringText(InvokeDelegate(vm, source, "join", arguments)));
	};
	Require(joined({}, ",").empty() && joined({std::string(""), std::string("a")}, ",") == "a" && joined({std::string("a"), std::string(""), std::string("b")}, ",") == "a,,b" && joined({std::string(""), std::string("")}, ",").empty(), "native join inserts a separator only after cumulative output becomes nonempty");
	Require(joined({std::string("a\0hidden", 8), std::string("b")}, std::string("::\0hidden", 9)) == "a::b" && joined({std::string("a"), std::string("b")}, std::string("\0hidden", 7)) == "ab", "native join uses C prefixes independently for elements and separator");
	Require(joined({std::string("\xc3\xa9", 2), std::string("\xe7\x95\x8c", 3)}, "|") == std::string("\xc3\xa9|\xe7\x95\x8c", 6), "string join must preserve UTF8 bytes");
	const std::array<SquirrelValue, 3> complete{array, std::string(","), std::int32_t{1}};
	for (const auto count : {std::size_t{0}, std::size_t{1}, std::size_t{3}}) {
		RequireFailure([&] {
			static_cast<void>(vm.Invoke(join, std::span<const SquirrelValue>(complete).first(count)));
		},
		    "wrong number of parameters");
	}
	std::size_t conversions = 0;
	const auto conversion_class = vm.CreateClass();
	vm.NewSlot(conversion_class, std::string("_tostring"), vm.CreateNativeClosure("forbidden_join_separator_conversion", [&](SquirrelVm&, const auto) {
		++conversions;

		return sgre::script::SquirrelNativeResult::ReturnValue(std::string(","));
	}));
	for (const auto& invalid : std::array<SquirrelValue, 5>{SquirrelValue{}, std::int32_t{1}, true, vm.CreateTable(), vm.CreateInstance(conversion_class)}) {
		RequireFailure([&] {
			static_cast<void>(vm.Invoke(join, std::array<SquirrelValue, 2>{array, invalid}));
		},
		    "type mismatch");
		RequireFailure([&] {
			static_cast<void>(vm.Invoke(join, std::array<SquirrelValue, 2>{invalid, std::string(",")}));
		},
		    "type mismatch");
	}
	Require(conversions == 0 && vm.Length(array) == 0, "join signature rejection must not coerce the separator or mutate its receiver");
}

static void TestArrayJoinScriptContextAndExplicitConversionGate() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	// Independently authored Array construction and tail-call shape; no extracted original bytecode.
	auto script = Program({Instruction(31, 1, 3), Instruction(1, 2, 0), Instruction(32, 1, 2), Instruction(1, 2, 1), Instruction(32, 1, 2), Instruction(1, 2, 2), Instruction(32, 1, 2), Instruction(8, 3, 3, 1, 4), Instruction(1, 5, 4), Instruction(5, 3, 3, 4, 2), Instruction(19, 1, 3)}, 6);
	script.MainFunction.Literals = {std::string("first"), std::string("second"), std::string("third"), std::string("join"), std::string("\n")};
	const auto callable = vm.CreateClosure(std::move(script));
	const auto child = vm.CreateContext();
	vm.SetRootTableInContext(child, vm.CreateTable());
	vm.SetLastErrorInContext(vm.RootContext(), std::string("root-sentinel"));
	vm.SetLastErrorInContext(child, std::string("child-sentinel"));
	const auto result = vm.InvokeInContext(child, callable, std::array<SquirrelValue, 1>{vm.RootTable()});
	Require(result.State == sgre::script::SquirrelRunState::Completed && StringText(result.Value) == "first\nsecond\nthird" && StringText(vm.LastErrorInContext(vm.RootContext())) == "root-sentinel" && StringText(vm.LastErrorInContext(child)) == "child-sentinel", "actual Array delegate script tail-call works in a child context without changing either error slot");
	std::size_t conversions = 0;
	const auto conversion_class = vm.CreateClass();
	vm.NewSlot(conversion_class, std::string("_tostring"), vm.CreateNativeClosure("forbidden_join_element_conversion", [&](SquirrelVm&, const auto) {
		++conversions;

		return sgre::script::SquirrelNativeResult::ReturnValue(std::string("replacement"));
	}));
	const auto instance = vm.CreateInstance(conversion_class);
	const auto instance_weak = vm.CreateWeakReference(instance);
	const std::array<SquirrelValue, 8> rejected{SquirrelValue{}, std::int32_t{7}, 1.5f, true, vm.CreateArray(), vm.CreateTable(), instance, instance_weak};
	for (const auto& value : rejected) {
		const auto source = vm.CreateArray(std::array<SquirrelValue, 2>{std::string("prefix"), value});
		RequireFailure([&] {
			static_cast<void>(InvokeDelegate(vm, source, "join", std::array<SquirrelValue, 1>{std::string(",")}));
		},
		    "Array.join nonstring conversion requires recovered shared scratch ownership");
		Require(vm.Length(source) == 2 && StringText(vm.GetSlot(source, std::int32_t{0})) == "prefix", "explicit unrecovered shared-scratch gate must preserve the actual source array");
	}
	Require(conversions == 0, "unrecovered nonstring join conversion must reject before invoking user tostring or its mutations");
	vm.ReleaseContext(child);
}

static void TestArrayJoinLivingWeakStringLifetimeAndBounds() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	SquirrelValue text = vm.CreateString("retained-source");
	auto text_root = vm.Retain(text);
	const auto weak = vm.CreateWeakReference(text);
	const auto weak_root = vm.Retain(weak);
	Require(vm.Type(weak) == sgre::script::SquirrelValueType::WeakReference, "positive weak-string test requires the genuine weak intern target");
	SquirrelValue source = vm.CreateArray(std::array<SquirrelValue, 2>{weak, std::string("tail")});
	auto source_root = vm.Retain(source);
	const auto source_weak = vm.CreateWeakReference(source);
	const auto source_weak_root = vm.Retain(source_weak);
	SquirrelValue result = InvokeDelegate(vm, source, "join", std::array<SquirrelValue, 1>{std::string("|")});
	auto output_root = vm.Retain(result);
	Require(StringText(result) == "retained-source|tail", "living weak-string elements must resolve to their actual string bytes");
	text_root = {};
	source_root = {};
	text = {};
	source = {};
	result = {};
	vm.ResetExecution();
	static_cast<void>(vm.CollectGarbage());
	Require(StringText(output_root.Value()) == "retained-source|tail" && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(source_weak_root.Value())) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_root.Value())), "joined output owns bytes while its source array and distinct weak intern target can retire");
	const auto expired_source = vm.CreateArray(std::array<SquirrelValue, 1>{weak_root.Value()});
	const auto expired_root = vm.Retain(expired_source);
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(vm, expired_source, "join", std::array<SquirrelValue, 1>{std::string(",")}));
	},
	    "Array.join nonstring conversion requires recovered shared scratch ownership");
	output_root = {};
	vm.RethrowReleaseFailure();

	sgre::script::SquirrelVmLimits bounded_limits;
	bounded_limits.MaxStringBytes = 32;
	SquirrelVm bounded(bounded_limits);
	bounded.InstallRecoveredBaseLibrary();
	const auto too_large = bounded.CreateArray(std::array<SquirrelValue, 2>{std::string(16, 'a'), std::string(16, 'b')});
	RequireFailure([&] {
		static_cast<void>(InvokeDelegate(bounded, too_large, "join", std::array<SquirrelValue, 1>{std::string(",")}));
	},
	    "string limit");
	Require(bounded.Length(too_large) == 2 && StringText(bounded.GetSlot(too_large, std::int32_t{0})) == std::string(16, 'a') && StringText(InvokeDelegate(bounded, too_large, "join", std::array<SquirrelValue, 1>{std::string("")})) == std::string(16, 'a') + std::string(16, 'b'), "checked string output bound must preserve inputs and allow an exact-limit retry");
}

static void TestTableChainCollisionGrowthDeletionAndOrder() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto table = vm.CreateTable();
	constexpr std::int32_t kEntries = 129;
	for (std::int32_t step = 0; step < kEntries; ++step) {
		const auto index = (step * 73 + 19) % kEntries;
		const auto key = index * 4096 + index % 7;
		vm.NewSlot(table, key, index + 1000);
	}

	for (std::int32_t index = 0; index < kEntries; ++index) {
		const auto key = index * 4096 + index % 7;
		Require(std::get<std::int32_t>(vm.GetSlot(table, key)) == index + 1000, "colliding table lookup must find every inserted integer after growth");
		vm.SetSlot(table, key, index + 2000);
	}

	const auto before = IteratorPairs(vm, table);
	for (std::int32_t index = 0; index < kEntries; ++index) {
		const auto key = index * 4096 + index % 7;
		Require(std::get<std::int32_t>(vm.GetSlot(table, key)) == index + 2000 && !vm.HasSlot(table, key + 1000001), "collision-chain replacement and missing-key lookup must stay distinct");
	}
	const auto after = IteratorPairs(vm, table);
	Require(vm.Length(before) == vm.Length(after), "readonly table lookup must preserve physical enumeration length");
	for (std::size_t index = 0; index < vm.Length(before); ++index) {
		Require(vm.GetSlot(before, static_cast<std::int32_t>(index)) == vm.GetSlot(after, static_cast<std::int32_t>(index)), "readonly table lookup must preserve every physical iteration key and value");
	}

	for (std::int32_t index = 0; index < kEntries; index += 2) {
		const auto key = index * 4096 + index % 7;
		Require(std::get<std::int32_t>(InvokeDelegate(vm, table, "rawdelete", std::array<SquirrelValue, 1>{key})) == index + 2000, "deletion must return the actual colliding table value");
	}
	for (std::int32_t index = 0; index < kEntries; ++index) {
		const auto key = index * 4096 + index % 7;
		Require(vm.HasSlot(table, key) == (index % 2 != 0), "deletion and shrink must preserve membership of every surviving collision");
		if (index % 2 != 0) {
			Require(std::get<std::int32_t>(vm.GetSlot(table, key)) == index + 2000, "shrink must preserve every surviving collision value");
		}
		vm.NewSlot(table, key, index + 3000);
	}
	for (std::int32_t index = 0; index < kEntries; ++index) {
		Require(std::get<std::int32_t>(vm.GetSlot(table, index * 4096 + index % 7)) == index + 3000, "reinserted collisions must retain independent values");
	}
}

static void TestTableChainTypedPayloadAndWeakValues() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto table = vm.CreateTable();
	const auto table_root = vm.Retain(table);
	const std::array<SquirrelValue, 10> keys{std::int32_t{0}, false, 0.0f, std::bit_cast<float>(std::uint32_t{0x80000000}), std::bit_cast<float>(std::uint32_t{0x7fc01234}), std::bit_cast<float>(std::uint32_t{0xffc05678}), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::string("embedded\0name", 13), std::string("embedded")};
	for (std::size_t index = 0; index < keys.size(); ++index) {
		vm.NewSlot(table, keys[index], static_cast<std::int32_t>(index));
	}
	for (std::size_t index = 0; index < keys.size(); ++index) {
		Require(std::get<std::int32_t>(vm.GetSlot(table, keys[index])) == static_cast<std::int32_t>(index), "table key types, signed zero, NaN payloads and embedded NUL bytes must remain distinct");
	}
	Require(!vm.HasSlot(table, SquirrelValue{}) && !vm.HasSlot(table, std::bit_cast<float>(std::uint32_t{0x7fc01235})), "null and a distinct NaN payload must remain missing table keys");

	const auto object_key = vm.CreateTable();
	const auto other_key = vm.CreateTable();
	vm.NewSlot(table, object_key, std::int32_t{900});
	Require(std::get<std::int32_t>(vm.GetSlot(table, object_key)) == 900 && !vm.HasSlot(table, other_key), "object keys must retain exact VM identity through the existing logical hash");

	SquirrelValue target = vm.CreateTable();
	const auto weak = vm.CreateWeakReference(target);
	vm.NewSlot(table, std::string("weak-value"), weak);
	Require(vm.GetSlot(table, std::string("weak-value")) == target, "successful table lookup must resolve the original living weak value");
	target = {};
	vm.ResetExecution();
	static_cast<void>(vm.CollectGarbage());
	Require(vm.HasSlot(table_root.Value(), std::string("weak-value")) && std::holds_alternative<std::monostate>(vm.GetSlot(table_root.Value(), std::string("weak-value"))), "a retained table must preserve its key while the unrooted weak value expires");
	vm.RethrowReleaseFailure();
}

static void TestTableChainClassIndexAndCurrentRoot() {
	SquirrelVm vm;
	const auto base = vm.CreateClass();
	for (std::int32_t index = 0; index < 96; ++index) {
		vm.NewSlot(base, std::string("field-") + std::to_string(index), index);
	}
	const auto derived = vm.CreateClass(base);
	vm.NewSlot(base, std::string("late-base-field"), std::int32_t{700});
	const auto instance = vm.CreateInstance(derived);
	for (std::int32_t index = 0; index < 96; ++index) {
		const auto name = std::string("field-") + std::to_string(index);
		Require(std::get<std::int32_t>(vm.ReadSlot(instance, name)) == index, "hashed class member index must resolve the actual copied instance field");
		vm.SetSlot(instance, name, index + 100);
		Require(std::get<std::int32_t>(vm.ReadSlot(instance, name)) == index + 100, "class member index lookup must preserve ordinary instance field assignment");
	}
	Require(!vm.HasSlot(instance, std::string("late-base-field")), "member-index lookup must not search late additions to the parent class");

	const auto child = vm.CreateContext();
	const auto child_root = vm.CreateTable();
	vm.NewSlot(vm.RootTable(), std::string("chain-root"), std::int32_t{11});
	vm.NewSlot(child_root, std::string("chain-root"), std::int32_t{29});
	vm.SetRootTableInContext(child, child_root);
	auto program = Program({Instruction(21, 1), Instruction(1, 2, 0), Instruction(9, 3, 0, 1), Instruction(19, 1, 3)});
	program.MainFunction.Literals = {std::string("chain-root")};
	const auto closure = vm.CreateClosure(std::move(program));
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	Require(std::get<std::int32_t>(vm.InvokeInContext(child, closure, receiver).Value) == 29 && std::get<std::int32_t>(vm.Invoke(closure, receiver).Value) == 11, "hash-chain reads must retain the executing context's actual root");
	vm.ReleaseContext(child);
}

static void TestTableChainSparseReadWorkDomain() {
	SquirrelVm vm;
	const auto table = Run(vm, Program({Instruction(30, 1, 4096), Instruction(19, 1, 1)}));
	for (std::int32_t index = 0; index < 32; ++index) {
		vm.NewSlot(table, index, index + 100);
	}
	for (std::int32_t index = 32; index < 132; ++index) {
		Require(!vm.HasSlot(table, index), "sparse table work domain must contain exactly the authored integer keys");
	}
	for (std::int32_t index = 0; index < 32; ++index) {
		Require(std::get<std::int32_t>(vm.GetSlot(table, index)) == index + 100, "sparse table work domain must preserve all existing values");
	}
}

static void TestTableChainCloneAfterCollisionsAndSourceRetirement() {
	for (const bool delete_before_clone : {false, true}) {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;
		SquirrelVm vm(limits);
		vm.InstallRecoveredBaseLibrary();

		SquirrelValue source = vm.CreateTable();
		auto source_root = vm.Retain(source);
		const auto source_weak_root = vm.Retain(vm.CreateWeakReference(source));
		constexpr std::int32_t kEntries = 129;
		for (std::int32_t step = 0; step < kEntries; ++step) {
			const auto index = (step * 73 + 19) % kEntries;
			vm.NewSlot(source, index * 4096 + index % 7, index + 1000);
		}

		if (delete_before_clone) {
			for (std::int32_t index = 0; index < kEntries; index += 2) {
				static_cast<void>(InvokeDelegate(vm, source, "rawdelete", std::array<SquirrelValue, 1>{index * 4096 + index % 7}));
			}
		}

		const auto object_key = vm.CreateTable();
		SquirrelValue payload = vm.CreateTable();
		vm.NewSlot(payload, std::string("value"), std::int32_t{77});
		const auto payload_weak_root = vm.Retain(vm.CreateWeakReference(payload));
		vm.NewSlot(source, object_key, payload);
		SquirrelValue weak_target = vm.CreateTable();
		auto weak_target_root = vm.Retain(weak_target);
		vm.NewSlot(source, std::string("weak-value"), vm.CreateWeakReference(weak_target));

		auto program = Program({Instruction(10, 1, 1), Instruction(54, 2, 1), Instruction(19, 1, 2)}, 3);
		program.MainFunction.Parameters.push_back(std::string("value"));
		const auto closure = vm.CreateClosure(std::move(program));
		std::array<SquirrelValue, 2> arguments{vm.RootTable(), source};
		SquirrelValue cloned = vm.Invoke(closure, arguments).Value;
		auto clone_root = vm.Retain(cloned);
		const auto clone_weak_root = vm.Retain(vm.CreateWeakReference(cloned));
		Require(cloned != source && vm.Length(cloned) == vm.Length(source), "actual CLONE must allocate an independent table with every surviving entry");
		for (std::int32_t index = 0; index < kEntries; ++index) {
			const auto key = index * 4096 + index % 7;
			const bool present = !delete_before_clone || index % 2 != 0;
			Require(vm.HasSlot(cloned, key) == present, "clone must reconstruct reachability of every collision and deleted hole");
			if (present) {
				Require(std::get<std::int32_t>(vm.GetSlot(cloned, key)) == index + 1000, "clone must preserve every surviving collision value");
			}
		}

		constexpr std::int32_t kPresentKey = 4097;
		vm.SetSlot(cloned, kPresentKey, std::int32_t{9001});
		Require(std::get<std::int32_t>(vm.GetSlot(source, kPresentKey)) == 1001, "clone slot replacement must not overwrite its source table");
		static_cast<void>(InvokeDelegate(vm, source, "rawdelete", std::array<SquirrelValue, 1>{kPresentKey}));
		Require(!vm.HasSlot(source, kPresentKey) && std::get<std::int32_t>(vm.GetSlot(cloned, kPresentKey)) == 9001, "source deletion must not unlink the clone collision chain");
		vm.NewSlot(cloned, std::int32_t{9876543}, std::int32_t{123});
		Require(!vm.HasSlot(source, std::int32_t{9876543}), "new clone entry must not become a source entry");
		Require(vm.GetSlot(cloned, object_key) == payload && vm.GetSlot(cloned, std::string("weak-value")) == weak_target, "clone must preserve raw object-key/value identity and living weak-value resolution");

		arguments = {};
		cloned = {};
		source_root = {};
		source = {};
		vm.ResetExecution();
		static_cast<void>(vm.CollectGarbage());
		Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(source_weak_root.Value())) && vm.ResolveWeakReference(payload_weak_root.Value()) == payload, "source-first collection must retire the source while actual cloned entries retain their values");
		Require(std::get<std::int32_t>(vm.GetSlot(clone_root.Value(), kPresentKey)) == 9001 && vm.GetSlot(clone_root.Value(), object_key) == payload && std::get<std::int32_t>(vm.GetSlot(payload, std::string("value"))) == 77, "all clone chains and transitive values must remain usable after source collection");
		for (std::int32_t index = 0; index < kEntries; ++index) {
			const auto key = index * 4096 + index % 7;
			const bool present = !delete_before_clone || index % 2 != 0;
			Require(vm.HasSlot(clone_root.Value(), key) == present, "source retirement must retain all cloned collision-chain membership");
			if (present) {
				const auto expected = key == kPresentKey ? std::int32_t{9001} : index + 1000;
				Require(std::get<std::int32_t>(vm.GetSlot(clone_root.Value(), key)) == expected, "source retirement must retain every cloned collision value");
			}
		}

		weak_target_root = {};
		weak_target = {};
		vm.ResetExecution();
		static_cast<void>(vm.CollectGarbage());
		Require(vm.HasSlot(clone_root.Value(), std::string("weak-value")) && std::holds_alternative<std::monostate>(vm.GetSlot(clone_root.Value(), std::string("weak-value"))), "cloning must retain the raw weak wrapper rather than a resolved strong target");

		payload = {};
		clone_root = {};
		vm.ResetExecution();
		static_cast<void>(vm.CollectGarbage());
		Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(clone_weak_root.Value())) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(payload_weak_root.Value())), "final clone retirement must release its transitive table values");
		vm.RethrowReleaseFailure();
	}
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: squirrel_vm_tests [native-trim.bin]");
		TestArithmetic();
		TestComparison();
		TestBitwise();
		TestBranchAndUnary();
		TestContainersAndOwnership();
		TestClosureCaptureAndDefaults();
		TestNativeSuspension();
		TestTraps();
		TestGenerator();
		TestClassesAndConstructors();
		TestClassHooksAndCachedMethods();
		TestIteratorsAndDelegates();
		TestGeneratorIteration();
		TestClassIteration();
		TestCustomIteration();
		TestBaseLibrary();
		TestSharedConstantTable();
		TestAdditionalDelegates();
		TestArrayJoinNativeBytesAndSignature();
		TestArrayJoinScriptContextAndExplicitConversionGate();
		TestArrayJoinLivingWeakStringLifetimeAndBounds();
		TestClassMemberCallReceivers();
		TestBuiltinSuspensionAndMetaErrorPolicy();
		TestFailedGetterFallbacks();
		TestNativeGeneralSlotReads();
		TestScopedHostRoots();
		TestNativePointerStringConversion();
		TestNativeArrayInsertionAndSort();
		TestNativeSortCallbackErrors();
		TestMissingIndexDiagnostics();
		TestNativeSignaturesAndReflection();
		TestSearchDelegates();
		TestStringReplacementAndMultibyteDelegates();
		TestGlobalSplitByteContractAndSignatures();
		TestGlobalSplitScriptAndTokenLifetime();
		TestGlobalSplitAllocationRollback();
		TestGlobalTrimNativeByteRanges();
		TestGlobalTrimSignaturesAndInitialPhase();
		TestGlobalTrimOutputLifetime();
		TestWeakReferences();
		TestExceptionClassAndHandlers();
		TestNativePointerOrdering();
		TestFailedComparatorPointerFallback();
		TestMetamethodContinuations();
		TestCompoundMetamethods();
		TestExistMetamethodStatus();
		TestSharedExecutionContexts();
		TestIdleChildReleaseDuringParentExecution();
		TestMetamethodSuspensionBoundary();
		TestNativeInstanceBridge();
		TestNestedNativeInvocations();
		TestSafetyLimits();
		TestTableChainCollisionGrowthDeletionAndOrder();
		TestTableChainTypedPayloadAndWeakValues();
		TestTableChainClassIndexAndCurrentRoot();
		TestTableChainSparseReadWorkDomain();
		TestTableChainCloneAfterCollisionsAndSourceRetirement();
		if (argc == 2) {
			ReplayGlobalTrimNative(argv[1]);
		}

		std::cout << "Squirrel VM execution checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
