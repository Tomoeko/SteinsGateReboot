#include "sgre/script/native_format.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using sgre::script::FormatNativeString;
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

static void TestIntegerAndStringFields() {
	SquirrelVm vm;
	const std::array<SquirrelValue, 2> values{std::int32_t{-31}, std::int32_t{0}};
	Require(FormatNativeString(vm, "%d %s %%", values) == "-31 0 %", "integer string coercion or literal percent changed");
	Require(FormatNativeString(vm, "%#08x %+.3d", values) == "0xffffffe1 +000", "hex alternate prefix or integer precision changed");
	Require(FormatNativeString(vm, "%08.3d %5.0d", values) == "    -031      ", "precision must disable zero field padding and zero digits");
	const std::array<SquirrelValue, 3> fields{std::string("abcdef"), std::int32_t{10}, std::int32_t{0}};
	Require(FormatNativeString(vm, "%-8.3s%#o:%#.0o", fields) == "abc     012:0", "string truncation, alignment, or alternate octal changed");
	const std::array<SquirrelValue, 1> minimum{std::numeric_limits<std::int32_t>::min()};
	Require(FormatNativeString(vm, "%d", minimum) == "-2147483648", "INT_MIN formatted magnitude overflowed");
	const std::array<SquirrelValue, 1> fractional{7.75f};
	Require(FormatNativeString(vm, "%d", fractional) == "7", "native float-to-integer format must truncate");
	const std::array<SquirrelValue, 1> boolean{true};
	Require(FormatNativeString(vm, "%d", boolean) == "1", "native printf integer conversion accepts boolean values");
	const std::array<SquirrelValue, 1> indefinite{std::numeric_limits<float>::infinity()};
	Require(FormatNativeString(vm, "%d", indefinite) == "-2147483648", "native CVTTSS2SI integer-indefinite conversion changed");
	Require(vm.ToString(1.23456789f) == "1.23457", "native tostring %g uses six significant digits");
	const std::array<SquirrelValue, 1> absent{SquirrelValue{}};
	Require(FormatNativeString(vm, "%s", absent) == "(null : 0x0000000000000000)", "native %s null conversion must preserve the recovered pointer diagnostic");
	const auto object = vm.CreateInstance(vm.CreateClass());
	const std::array<SquirrelValue, 1> object_field{object};
	Require(FormatNativeString(vm, "owner=%s", object_field) == "owner=" + vm.ToString(object), "native %s object conversion must retain actual allocation identity");
}

static void TestMetamethodPreflight() {
	SquirrelVm vm;
	const auto type = vm.CreateClass();
	std::size_t conversions = 0;
	vm.NewSlot(type, std::string("_tostring"), vm.CreateNativeClosure("fixture_string", [&](SquirrelVm&, const auto) {
		++conversions;
		sgre::script::SquirrelNativeResult result;
		result.Value = std::string("owned");

		return result;
	}));
	const auto instance = vm.CreateInstance(type);
	const std::array<SquirrelValue, 2> values{std::string("invalid numeric"), instance};
	RequireFailure([&] {
		static_cast<void>(FormatNativeString(vm, "%d %s", values));
	},
	    "numeric format requires");
	Require(conversions == 1, "%s preflight must happen before numeric validation");
}

static void TestFloatingFields() {
	SquirrelVm vm;
	const std::array<SquirrelValue, 1> midpoint{2.5f};
	Require(FormatNativeString(vm, "%.0f", midpoint) == "2", "native floating output must round midpoint to even");
	const std::array<SquirrelValue, 1> zero{std::bit_cast<float>(std::uint32_t{0x80000000})};
	Require(FormatNativeString(vm, "%012.3f", zero) == "-0000000.000", "floating zero padding lost sign or precision");
	const std::array<SquirrelValue, 1> indefinite{std::bit_cast<float>(std::uint32_t{0xffc00000})};
	Require(FormatNativeString(vm, "%G", indefinite) == "-NAN(IND)", "negative native indefinite NaN spelling changed");
	const std::array<SquirrelValue, 1> infinity{std::numeric_limits<float>::infinity()};
	Require(FormatNativeString(vm, "%012.3f", infinity) == "         inf", "native infinity ignores zero padding");
	const std::array<SquirrelValue, 1> fraction{0.1f};
	Require(FormatNativeString(vm, "%#g", fraction) == "0.100000", "alternate general format did not retain significant zeros");
	const std::array<SquirrelValue, 1> maximum{std::numeric_limits<float>::max()};
	Require(FormatNativeString(vm, "%.6g", maximum) == "3.40282e+38", "promoted float32 maximum formatting changed");
}

static void TestBoundedHostPrinting() {
	SquirrelVm vm;
	std::vector<std::string> output;
	std::vector<bool> errors;
	sgre::script::InstallNativePrinting(vm, [&](std::string_view text, bool error) {
		output.emplace_back(text);
		errors.push_back(error);
	},
	    100);
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), std::string("count:%s\n"), std::int32_t{4}};
	const auto result = vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("printf")), arguments);
	Require(std::holds_alternative<std::monostate>(result.Value) && output == std::vector<std::string>{"count:4\n"} && !errors[0], "printf must return null and send exact bytes to host log");
	const std::array<SquirrelValue, 2> error_arguments{vm.RootTable(), std::string("problem")};
	static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("errprint")), error_arguments));
	Require(errors.back() && output.back() == "problem", "errprint routing or newline behavior changed");
	const std::array<SquirrelValue, 3> nul_arguments{vm.RootTable(), std::string("before%cafter"), std::int32_t{0}};
	static_cast<void>(vm.Invoke(vm.GetSlot(vm.RootTable(), std::string("printf")), nul_arguments));
	Require(output.back() == "before", "native print host callback must stop at embedded NUL");
}

static void TestFormatRejections() {
	SquirrelVm vm;
	const std::array<SquirrelValue, 1> value{std::int32_t{1}};
	RequireFailure([&] {
		static_cast<void>(FormatNativeString(vm, "%100d", value));
	},
	    "width format too long");
	RequireFailure([&] {
		static_cast<void>(FormatNativeString(vm, "%.100d", value));
	},
	    "precision format too long");
	RequireFailure([&] {
		static_cast<void>(FormatNativeString(vm, "%n", value));
	},
	    "invalid format");
	RequireFailure([&] {
		static_cast<void>(FormatNativeString(vm, "%s %s", value));
	},
	    "not enough paramters");
	RequireFailure([&] {
		static_cast<void>(FormatNativeString(vm, "%99d", value, 10));
	},
	    "output exceeds limit");
	Require(FormatNativeString(vm, "%f", value) == "1.000000", "native floating format promoted integer incorrectly");
}

int main() {
	try {
		TestIntegerAndStringFields();
		TestMetamethodPreflight();
		TestFloatingFields();
		TestBoundedHostPrinting();
		TestFormatRejections();
		std::cout << "Native formatting checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
