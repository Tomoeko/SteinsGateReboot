#include "sgre/script/affine_matrix_bindings.hpp"

#include "sgre/script/object_bindings.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static sgre::script::SquirrelValue Construct(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& class_value, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), vm.RootTable());

	return vm.Invoke(class_value, arguments).Value;
}

static sgre::script::SquirrelValue Method(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, std::string name, std::vector<sgre::script::SquirrelValue> arguments = {}) {
	arguments.insert(arguments.begin(), receiver);

	return vm.Invoke(vm.GetSlot(receiver, name), arguments).Value;
}

static void CheckGenuineClassAndConstructor() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptAffineMatrixBindings bindings(vm);
	const auto source = bindings.WrapMatrix();
	bindings.Matrix(source).Rows[3] = 12;
	Require(vm.GetSlot(vm.RootTable(), std::string("AffineMatrix")) == bindings.MatrixClass() && !vm.IsInstanceOf(source, objects.ObjectClass()), "AffineMatrix is its genuine root class without Object ancestry");
	const auto zero = Construct(vm, bindings.MatrixClass());
	const auto one = Construct(vm, bindings.MatrixClass(), {source});
	const auto two = Construct(vm, bindings.MatrixClass(), {source, std::monostate{}});
	Require(bindings.Matrix(zero).Rows[3] == 0 && bindings.Matrix(one).Rows[3] == 0 && bindings.Matrix(two).Rows[3] == 12, "factory ignores one argument and copies only at native top three or greater");
	const auto fallback = Construct(vm, bindings.MatrixClass(), {std::string("wrong type"), true});
	Require(std::get<bool>(Method(vm, fallback, "isIdentity")), "untyped first argument falls back to native identity");
	const auto subclass = vm.CreateClass(bindings.MatrixClass());
	const auto derived = Construct(vm, subclass, {source, true});
	Require(vm.IsInstanceOf(derived, bindings.MatrixClass()) && bindings.Matrix(derived).Rows == bindings.Matrix(source).Rows, "derived receiver shares the genuine native matrix registry");
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(bindings.InstanceCount() == 0, "VM Close retires all actual matrix owners");
}

static void CheckCapturedNumericThunks() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptAffineMatrixBindings bindings(vm);
	const auto matrix = bindings.WrapMatrix();
	Require(std::holds_alternative<std::monostate>(Method(vm, matrix, "translate", {3.0f, std::int32_t{4}, true})), "native three-float mutator returns status zero");
	Require(bindings.Matrix(matrix).Rows[3] == 3 && bindings.Matrix(matrix).Rows[7] == 4 && bindings.Matrix(matrix).Rows[11] == 1, "float integer and boolean inputs use actual native conversions");
	static_cast<void>(Method(vm, matrix, "reset"));
	static_cast<void>(Method(vm, matrix, "scale", {2.0f, 3.0f}));
	Require(bindings.Matrix(matrix).Rows[0] == 2 && bindings.Matrix(matrix).Rows[5] == 3 && bindings.Matrix(matrix).Rows[10] == 0, "two-argument script scale reads captured userdata as its missing Z, producing zero");
	static_cast<void>(Method(vm, matrix, "reset"));
	const std::array<sgre::script::SquirrelValue, 2> captures{19.75f, 3.5f};
	const auto seed = vm.CreateNativeClosureWithCaptures("matrix inactive cell seed", [](auto&, auto) {
		return sgre::script::SquirrelNativeResult::NoValues();
	},
	    captures);
	const std::array<sgre::script::SquirrelValue, 1> arguments{std::int32_t{17}};
	static_cast<void>(vm.Invoke(seed, arguments));
	static_cast<void>(Method(vm, matrix, "translate"));
	Require(bindings.Matrix(matrix).Rows[3] == 0 && bindings.Matrix(matrix).Rows[7] == 19.75f, "zero-user method reads its actual capture and retained inactive coordinate cell");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void CheckTypedCopyAndMultiply() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptAffineMatrixBindings bindings(vm);
	const auto left = bindings.WrapMatrix();
	const auto right = bindings.WrapMatrix();
	static_cast<void>(Method(vm, left, "translate", {10, 20, 0}));
	static_cast<void>(Method(vm, right, "scale", {2, 3, 1}));
	static_cast<void>(Method(vm, left, "multiply", {right}));
	Require(bindings.Matrix(left).Rows[0] == 2 && bindings.Matrix(left).Rows[5] == 3 && bindings.Matrix(left).Rows[3] == 10 && bindings.Matrix(left).Rows[7] == 20, "script multiplication preserves original composition order");
	static_cast<void>(Method(vm, right, "copy", {left}));
	Require(bindings.Matrix(right).Rows == bindings.Matrix(left).Rows, "copy overwrites the complete matrix");
	static_cast<void>(Method(vm, right, "copy", {right}));
	static_cast<void>(Method(vm, right, "multiply", {right}));
	Require(bindings.Matrix(right).Rows[0] == 4 && bindings.Matrix(right).Rows[3] == 30 && bindings.Matrix(right).Rows[5] == 9 && bindings.Matrix(right).Rows[7] == 80, "aliased copy and multiplication retain actual original operands");
	try {
		static_cast<void>(Method(vm, right, "copy", {true}));
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("dereference null") != std::string_view::npos, "invalid typed source reports native safety boundary");
		vm.Close();
		vm.RethrowReleaseFailure();

		return;
	}

	throw std::runtime_error("untyped matrix source was incorrectly accepted");
}

static void CheckReturnedCoordinatesAndArrays() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptAffineMatrixBindings bindings(vm);
	const auto matrix = bindings.WrapMatrix();
	bindings.Matrix(matrix).Rows = {2, 3, 0, 7, -1, 4, 0, 11, 0, 0, 1, 0, 0, 0, 0, 1};
	Require(std::get<float>(Method(vm, matrix, "transformX", {5, 6, 0})) == 35 && std::get<float>(Method(vm, matrix, "transformY", {5, 6, 0})) == 30, "native point methods return exactly one float");
	const auto x = Method(vm, matrix, "transformAreaX", {5, 6});
	const auto y = Method(vm, matrix, "transformAreaY", {5, 6});
	const std::array<float, 4> expected_x{7, 17, 25, 35};
	const std::array<float, 4> expected_y{11, 6, 35, 30};
	Require(vm.Type(x) == sgre::script::SquirrelValueType::Array && vm.Length(x) == 4 && vm.Length(y) == 4, "native area methods return a four-element array");
	for (std::int32_t index = 0; index < 4; ++index) {
		Require(std::get<float>(vm.GetSlot(x, index)) == expected_x[static_cast<std::size_t>(index)] && std::get<float>(vm.GetSlot(y, index)) == expected_y[static_cast<std::size_t>(index)], "native corner result order is preserved by VM array construction");
	}
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void CheckLifetimeAndWeakReference() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptAffineMatrixBindings bindings(vm, 1);
	auto matrix = bindings.WrapMatrix();
	const auto weak = Method(vm, matrix, "weakref");
	Require(vm.ResolveWeakReference(weak) == matrix && vm.RetainedReferenceCount(matrix) == 0, "matrix weakref targets its real wrapper without a host root");
	Require(std::get<std::int32_t>(Method(vm, matrix, "weakref", {17})) == 17, "captureless native weakref uses actual topmost argument");
	matrix = {};
	static_cast<void>(vm.CollectGarbage());
	Require(bindings.InstanceCount() == 0 && vm.Type(vm.ResolveWeakReference(weak)) == sgre::script::SquirrelValueType::Null, "actual release hook retires matrix owner and weak target");
	const auto recycled = bindings.WrapMatrix();
	Require(bindings.InstanceCount() == 1 && vm.Type(vm.ResolveWeakReference(weak)) == sgre::script::SquirrelValueType::Null, "recycled VM arena slot never retargets an old weak identity");
	static_cast<void>(recycled);
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(bindings.InstanceCount() == 0, "VM Close completes remaining matrix native release");
}

static std::uint32_t ReadWord(std::ifstream& stream) {
	std::uint32_t word = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = stream.get();
		if (byte == std::char_traits<char>::eof()) {
			throw std::runtime_error("truncated native matrix constructor fixture");
		}
		word |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(byte)) << shift;
	}

	return word;
}

static void CompareNativeConstructors(const char* plan_path, const char* expected_path) {
	std::ifstream plan(plan_path, std::ios::binary);
	std::ifstream expected(expected_path, std::ios::binary);
	Require(plan && expected && ReadWord(plan) == 0x43414753, "valid bounded matrix constructor vector streams");
	const auto count = ReadWord(plan);
	Require(count <= 1000, "matrix constructor native vector work bound");
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptAffineMatrixBindings bindings(vm);
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto top = ReadWord(plan);
		const auto mode = ReadWord(plan);
		Require(top >= 1 && top <= 5 && mode <= 2, "matrix constructor plan domain");
		sgre::scene::AffineMatrix original;
		for (auto& value : original.Rows) {
			value = std::bit_cast<float>(ReadWord(plan));
		}
		const auto source = bindings.WrapMatrix(original);
		std::vector<sgre::script::SquirrelValue> arguments(top - 1);
		if (!arguments.empty()) {
			arguments[0] = mode == 1 ? source : sgre::script::SquirrelValue{};
		}
		const auto result = Construct(vm, bindings.MatrixClass(), std::move(arguments));
		for (const auto value : bindings.Matrix(result).Rows) {
			Require(std::bit_cast<std::uint32_t>(value) == ReadWord(expected), "original constructor matrix word mismatch");
		}
		Require(ReadWord(expected) == 0, "native constructor returns status zero");
	}
	Require(plan.peek() == std::char_traits<char>::eof() && expected.peek() == std::char_traits<char>::eof(), "matrix constructor streams consumed exactly");
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(bindings.InstanceCount() == 0, "constructor vector owners all retire at VM Close");
	std::cout << "Native " << count << " constructors: " << count * 17 << " words, zero differences\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 3, "optional constructor plan and expected paths");
		CheckGenuineClassAndConstructor();
		CheckCapturedNumericThunks();
		CheckTypedCopyAndMultiply();
		CheckReturnedCoordinatesAndArrays();
		CheckLifetimeAndWeakReference();
		std::cout << "Five genuine AffineMatrix binding fixture groups passed\n";
		if (argc == 3) {
			CompareNativeConstructors(argv[1], argv[2]);
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
