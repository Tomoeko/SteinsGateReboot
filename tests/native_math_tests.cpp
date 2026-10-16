#include "sgre/script/native_math.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

static SquirrelValue InvokeMath(SquirrelVm& vm, std::string name, std::span<const SquirrelValue> parameters = {}) {
	std::vector<SquirrelValue> arguments{vm.RootTable()};
	arguments.insert(arguments.end(), parameters.begin(), parameters.end());

	return vm.Invoke(vm.GetSlot(vm.RootTable(), name), arguments).Value;
}

static void TestExactRandomState() {
	SquirrelVm vm;
	sgre::script::ScriptMathBindings math(vm);
	const std::array<std::int32_t, 6> expected{41, 18467, 6334, 26500, 19169, 15724};
	for (const auto value : expected) {
		Require(std::get<std::int32_t>(InvokeMath(vm, "rand")) == value, "native CRT random sequence changed");
	}

	const std::array<SquirrelValue, 1> seed{std::int32_t{1}};
	Require(std::holds_alternative<std::monostate>(InvokeMath(vm, "srand", seed)), "srand returns no value");
	Require(math.Random() == 41, "script seed did not reach shared host RNG owner");
	const auto context = vm.CreateContext();
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	const auto random = vm.GetSlot(vm.RootTable(), std::string("rand"));
	Require(std::get<std::int32_t>(vm.InvokeInContext(context, random, receiver).Value) == 18467, "child context did not share CRT RNG state");
	Require(math.Random() == 6334, "child RNG mutated a separate seed");
	const std::array<SquirrelValue, 1> boolean{true};
	RequireFailure([&] {
		static_cast<void>(InvokeMath(vm, "srand", boolean));
	},
	    "type mismatch");
}

static void TestNativeSignatureReflection() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	sgre::script::ScriptMathBindings math(vm);
	for (const auto& name : {std::string("sqrt"), std::string("pow"), std::string("rand"), std::string("min")}) {
		const auto callable = vm.GetSlot(vm.RootTable(), name);
		const std::array<SquirrelValue, 1> receiver{callable};
		const auto information = vm.Invoke(vm.GetSlot(callable, std::string("getinfos")), receiver).Value;
		Require(sgre::script::StringText(vm.GetSlot(information, std::string("name"))) == name, "math reflection lost the original registration name");
		const auto count = name == "pow" ? 3 : name == "rand" ? 1
		    : name == "min"                                   ? -2
		                                                      : 2;
		Require(std::get<std::int32_t>(vm.GetSlot(information, std::string("paramscheck"))) == count, "math reflection lost the exact signed argument count");
		const auto types = vm.GetSlot(information, std::string("typecheck"));
		if (name == "rand" || name == "min") {
			Require(std::holds_alternative<std::monostate>(types), "math registration without a mask must reflect null typecheck");
		} else {
			Require(vm.Length(types) == static_cast<std::size_t>(count) && std::get<std::int32_t>(vm.GetSlot(types, std::int32_t{0})) == -1 && std::get<std::int32_t>(vm.GetSlot(types, std::int32_t{count - 1})) == 6, "math type mask must retain wildcard receiver and integer-or-float parameters");
		}
	}

	const std::array<SquirrelValue, 2> wildcard_receiver{true, std::int32_t{9}};
	Require(std::get<float>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("sqrt")), wildcard_receiver).Value) == 3.0f, "math wildcard receiver must accept a non-table value");
	const std::array<SquirrelValue, 1> extra_random_argument{std::int32_t{7}};
	RequireFailure([&] {
		static_cast<void>(InvokeMath(vm, "rand", extra_random_argument));
	},
	    "wrong number of parameters");
	RequireFailure([&] {
		static_cast<void>(InvokeMath(vm, "min"));
	},
	    "wrong number of parameters");
}

static void TestAbsoluteAndSelection() {
	SquirrelVm vm;
	sgre::script::ScriptMathBindings math(vm);
	const std::array<SquirrelValue, 1> minimum{std::numeric_limits<std::int32_t>::min()};
	Require(std::get<std::int32_t>(InvokeMath(vm, "iabs", minimum)) == std::numeric_limits<std::int32_t>::min(), "wrapping native INT_MIN absolute value changed");
	const std::array<SquirrelValue, 1> fraction{-7.75f};
	Require(std::get<std::int32_t>(InvokeMath(vm, "iabs", fraction)) == 7, "iabs did not truncate float input");
	Require(std::get<float>(InvokeMath(vm, "abs", fraction)) == 7.75f, "abs did not retain float result");
	const std::array<SquirrelValue, 1> negative_nan{std::bit_cast<float>(std::uint32_t{0xffc05678})};
	Require(std::bit_cast<std::uint32_t>(std::get<float>(InvokeMath(vm, "fabs", negative_nan))) == 0x7fc05678, "fabs must clear only the sign bit, preserving NaN payload");
	const std::array<SquirrelValue, 3> mixed{std::int32_t{3}, 3.0f, std::int32_t{7}};
	Require(std::holds_alternative<std::int32_t>(InvokeMath(vm, "min", mixed)), "min equality must retain first original type");
	Require(std::get<std::int32_t>(InvokeMath(vm, "max", mixed)) == 7, "max selected incorrect value");
	const std::array<SquirrelValue, 2> wrapped{std::numeric_limits<std::int32_t>::min(), std::int32_t{1}};
	Require(std::get<std::int32_t>(InvokeMath(vm, "min", wrapped)) == 1, "native min must use wrapping integer subtraction ordering");
	const std::array<SquirrelValue, 2> null_order{std::int32_t{1}, SquirrelValue{}};
	Require(std::holds_alternative<std::monostate>(InvokeMath(vm, "min", null_order)), "native ordering places null before non-null");
}

static void TestDoublePromotionAndConstants() {
	SquirrelVm vm;
	sgre::script::ScriptMathBindings math(vm);
	const std::array<SquirrelValue, 1> input{2.0f};
	Require(std::bit_cast<std::uint32_t>(std::get<float>(InvokeMath(vm, "sqrt", input))) == 0x3fb504f3, "sqrt did not narrow promoted double result to float32");
	const std::array<SquirrelValue, 1> negative{-2.25f};
	Require(std::get<float>(InvokeMath(vm, "floor", negative)) == -3.0f && std::get<float>(InvokeMath(vm, "ceil", negative)) == -2.0f, "floor/ceil retained incorrect result type or sign");
	const std::array<SquirrelValue, 2> power{2.0f, 3.0f};
	Require(std::get<float>(InvokeMath(vm, "pow", power)) == 8.0f, "pow input ordering changed");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.RootTable(), std::string("RAND_MAX"))) == 32767, "RAND_MAX native constant changed");
	Require(std::bit_cast<std::uint32_t>(std::get<float>(vm.GetSlot(vm.RootTable(), std::string("PI")))) == 0x40490fdb, "PI must use exact native float32 payload");
	const std::array<SquirrelValue, 1> invalid{-1.0f};
	RequireFailure([&] {
		static_cast<void>(InvokeMath(vm, "sqrt", invalid));
	},
	    "domain/overflow result parity is not recovered");
}

static void TestMeasuredFiniteResults() {
	SquirrelVm vm;
	sgre::script::ScriptMathBindings math(vm);
	struct Vector {
		std::string_view Name;
		std::uint32_t Input;
		std::uint32_t Expected;
	};
	// Compact independent samples from the exact initialized game's double CRT wrappers.
	const std::array<Vector, 11> vectors{
	    Vector{"sin", 0x3dcccccd, 0x3dcc7577},
	    Vector{"sin", 0x40490fdb, 0xb3bbbd2e},
	    Vector{"cos", 0x3dcccccd, 0x3f7eb898},
	    Vector{"asin", 0x3dcccccd, 0x3dcd2494},
	    Vector{"acos", 0x3dcccccd, 0x3fbc3d91},
	    Vector{"log", 0x3dcccccd, 0xc0135d8e},
	    Vector{"log10", 0x40490fdb, 0x3efe8a6e},
	    Vector{"tan", 0x3dcccccd, 0x3dcd7c44},
	    Vector{"atan", 0x40490fdb, 0x3fa19dc5},
	    Vector{"exp", 0x3dcccccd, 0x3f8d763e},
	    Vector{"exp", 0x40490fdb, 0x41b92025}};
	for (const auto& vector : vectors) {
		const std::array<SquirrelValue, 1> arguments{std::bit_cast<float>(vector.Input)};
		const auto result = InvokeMath(vm, std::string(vector.Name), arguments);
		Require(std::bit_cast<std::uint32_t>(std::get<float>(result)) == vector.Expected, "measured finite math result changed");
	}
}

int main() {
	try {
		TestExactRandomState();
		TestNativeSignatureReflection();
		TestAbsoluteAndSelection();
		TestDoublePromotionAndConstants();
		TestMeasuredFiniteResults();
		std::cout << "Native math binding checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
