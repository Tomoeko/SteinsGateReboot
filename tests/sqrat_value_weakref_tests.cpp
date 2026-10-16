#include "sgre/script/psb_bindings.hpp"

#include "sgre/core/frame_clock.hpp"
#include "sgre/script/binary_bindings.hpp"
#include "sgre/script/struct_bindings.hpp"
#include "sgre/script/system_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t argument1 = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = destination;
	instruction.Argument1 = argument1;
	instruction.Argument2 = argument2;
	instruction.Argument3 = argument3;

	return instruction;
}

static sgre::script::SquirrelValue InvokeWeakref(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, std::optional<sgre::script::SquirrelValue> final_argument = {}) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.Name = std::string("actual weakref member call");
	program.MainFunction.Parameters = {std::string("this"), std::string("actual"), std::string("extra")};
	program.MainFunction.StackSize = 6;
	program.MainFunction.Literals = {std::string("weakref")};
	program.MainFunction.Instructions.push_back(Instruction(8, 3, 0, 1, 4));
	if (final_argument) {
		program.MainFunction.Instructions.push_back(Instruction(10, 5, 2));
	}
	program.MainFunction.Instructions.push_back(Instruction(6, 3, 3, 4, final_argument ? std::uint8_t{2} : std::uint8_t{1}));
	program.MainFunction.Instructions.push_back(Instruction(19, 1, 3));
	const auto closure = vm.CreateClosure(std::move(program));
	const std::array<sgre::script::SquirrelValue, 3> arguments{vm.RootTable(), receiver, final_argument.value_or(sgre::script::SquirrelValue{})};

	return vm.Invoke(closure, arguments).Value;
}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, const char* method) {
	const std::array<sgre::script::SquirrelValue, 1> arguments{receiver};

	return vm.Invoke(vm.GetSlot(receiver, std::string(method)), arguments).Value;
}

static sgre::script::SquirrelValue Construct(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& actual_class) {
	const std::array<sgre::script::SquirrelValue, 1> arguments{vm.RootTable()};

	return vm.Invoke(actual_class, arguments).Value;
}

static void CheckActualMember(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, const sgre::script::SquirrelValue& final_object) {
	Require(vm.HasSlot(receiver, std::string("weakref")), "actual Sqrat class is missing its ordinary weakref member");
	const auto method = vm.ReadSlot(receiver, std::string("weakref"));
	Require(vm.Type(method) == sgre::script::SquirrelValueType::NativeClosure && !vm.HasBoundEnvironment(method), "payload _get intercepted or rebound an ordinary weakref member");
	const auto expected_receiver = vm.Type(receiver) == sgre::script::SquirrelValueType::Class ? vm.RootTable() : receiver;
	Require(vm.ResolveWeakReference(InvokeWeakref(vm, receiver)) == expected_receiver, "bytecode member call must preserve native class-current-this or instance receiver selection");
	Require(vm.ResolveWeakReference(Call(vm, receiver, "weakref")) == receiver, "direct native weakref must use its actual physical class or instance argument");
	Require(vm.ResolveWeakReference(InvokeWeakref(vm, receiver, final_object)) == final_object, "native weakref ignored the last physical argument");
	Require(InvokeWeakref(vm, receiver, std::int32_t{47}) == sgre::script::SquirrelValue(std::int32_t{47}), "native weakref changed a final non-reference-counted scalar");
}

static std::shared_ptr<const sgre::resource::PsbDocument> StructSchema() {
	auto schema = std::make_shared<sgre::resource::PsbDocument>();
	const sgre::resource::PsbObject fields{{"weakref", Atom(sgre::resource::PsbArray{Atom(std::int64_t{2})})}};
	schema->Root = Atom(sgre::resource::PsbObject{{"root", Atom(fields)}});

	return schema;
}

static void TestActualNativeClasses() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	sgre::script::ScriptStructBindings structures(vm, objects, psb, binary);
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	const auto final_object = vm.CreateTable();
	const auto final_root = vm.Retain(final_object);

	auto input = std::make_shared<sgre::resource::PsbDocument>();
	input->Root = Atom(sgre::resource::PsbObject{{"weakref", Atom(std::int64_t{99})}});
	const auto document = psb.WrapDocument(input);
	const auto document_root = vm.Retain(document);
	const auto psb_value = Call(vm, document, "getRoot");
	CheckActualMember(vm, psb_value, final_object);

	const auto schema = psb.WrapDocument(StructSchema());
	const auto schema_root = vm.Retain(schema);
	const std::array<sgre::script::SquirrelValue, 2> struct_arguments{vm.RootTable(), schema};
	const auto structure = vm.Invoke(structures.StructClass(), struct_arguments).Value;
	const auto structure_root = vm.Retain(structure);
	const auto struct_value = Call(vm, structure, "getRoot");
	CheckActualMember(vm, struct_value, final_object);
	Require(structures.BorrowStructStorage(structure).Serialize() == std::vector<std::uint8_t>{0}, "generic weakref mutated actual Struct bytes");

	CheckActualMember(vm, system.SystemClass(), final_object);
	CheckActualMember(vm, Construct(vm, system.SystemClass()), final_object);
	CheckActualMember(vm, Construct(vm, psb.ValueClass()), final_object);
	CheckActualMember(vm, Construct(vm, structures.ValueClass()), final_object);

	sgre::script::SquirrelValue orphan = Construct(vm, psb.ValueClass());
	const auto weak = InvokeWeakref(vm, orphan);
	const auto weak_root = vm.Retain(weak);
	orphan = {};
	Require(vm.Type(vm.ResolveWeakReference(weak)) == sgre::script::SquirrelValueType::Null, "ordinary weakref retained an actual CopyOnly PSBValue instance");
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestActualNativeClasses();
		std::cout << "PSBValue, StructValue and System ordinary weakref precedence, final stack argument, CopyOnly payload independence and expiration passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
