#include "sgre/script/struct_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
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

static std::shared_ptr<const sgre::resource::PsbDocument> Schema() {
	auto schema = std::make_shared<sgre::resource::PsbDocument>();
	const sgre::resource::PsbObject child = {{"number", Atom(sgre::resource::PsbArray{Atom(std::int64_t{2})})}};
	const sgre::resource::PsbObject root = {{"child", Atom(sgre::resource::PsbArray{Atom(std::int64_t{8}), Atom(std::string("child"))})}, {"flags", Atom(sgre::resource::PsbArray{Atom(std::int64_t{9}), Atom(std::int64_t{10})})}, {"float", Atom(sgre::resource::PsbArray{Atom(std::int64_t{5})})}, {"text", Atom(sgre::resource::PsbArray{Atom(std::int64_t{6}), Atom(std::int64_t{3})})}};
	schema->Root = Atom(sgre::resource::PsbObject{{"root", Atom(root)}, {"child", Atom(child)}});

	return schema;
}

static sgre::script::SquirrelValue Call(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& instance, std::string_view method, std::span<const sgre::script::SquirrelValue> inputs = {}) {
	std::vector<sgre::script::SquirrelValue> arguments{instance};
	arguments.insert(arguments.end(), inputs.begin(), inputs.end());

	try {
		return vm.Invoke(vm.GetSlot(instance, std::string(method)), arguments).Value;
	} catch (const std::exception& error) {
		throw std::runtime_error("Struct fixture method " + std::string(method) + ": " + error.what());
	}
}

static sgre::script::SquirrelValue Construct(sgre::script::SquirrelVm& vm, sgre::script::ScriptStructBindings& bindings, const sgre::script::SquirrelValue& source) {
	const auto instance = vm.CreateInstance(bindings.StructClass());
	const std::array<sgre::script::SquirrelValue, 1> inputs{source};
	static_cast<void>(Call(vm, instance, "constructor", inputs));

	return instance;
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error(std::string(message));
}

static sgre::script::SquirrelVmLimits VmLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static bool Exists(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& receiver, const sgre::script::SquirrelValue& key) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.Name = std::string("Struct exists fixture");
	program.MainFunction.Parameters = {std::string("this"), std::string("receiver"), std::string("key")};
	program.MainFunction.StackSize = 4;
	sgre::script::SquirrelInstruction query;
	query.Opcode = 41;
	query.Argument0 = 3;
	query.Argument1 = 1;
	query.Argument2 = 2;
	sgre::script::SquirrelInstruction returned;
	returned.Opcode = 19;
	returned.Argument0 = 1;
	returned.Argument1 = 3;
	program.MainFunction.Instructions = {query, returned};
	const auto closure = vm.CreateClosure(std::move(program));
	const std::array<sgre::script::SquirrelValue, 3> arguments{vm.RootTable(), receiver, key};

	return std::get<bool>(vm.Invoke(closure, arguments).Value);
}

static void TestActualBindingsAndCoercion() {
	sgre::script::SquirrelVm vm(VmLimits());
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	sgre::script::ScriptStructBindings bindings(vm, objects, psb, binary);
	const auto source = psb.WrapDocument(Schema());
	const auto document = Construct(vm, bindings, source);
	const auto root = Call(vm, document, "getRoot");
	Require(vm.IsInstanceOf(document, bindings.StructClass()) && vm.IsInstanceOf(root, bindings.ValueClass()), "actual schema constructors must create native tagged instances");
	Require(vm.RetainedReferenceCount(document) == 1 && vm.RetainedReferenceCount(source) == 1, "Struct root/value must retain their real script owners once");
	const auto child = vm.ReadSlot(root, std::string("child"));
	vm.AssignSlot(child, std::string("number"), std::int32_t{255});
	Require(std::get<std::int32_t>(vm.ReadSlot(child, std::string("number"))) == -1, "native fixed-width signed byte coercion differs");
	const auto flags = vm.ReadSlot(root, std::string("flags"));
	vm.AssignSlot(flags, std::int32_t{0}, std::int32_t{256});
	vm.AssignSlot(flags, std::int32_t{9}, 0.5f);
	Require(std::get<bool>(vm.ReadSlot(flags, std::int32_t{0})) && !std::get<bool>(vm.ReadSlot(flags, std::int32_t{9})), "native full-integer/CVTTSS2SI boolean adapter differs");
	vm.AssignSlot(flags, std::int32_t{9}, 1.5f);
	Require(std::get<std::int32_t>(Call(vm, flags, "countFlags")) == 2, "flags actual binding did not observe packed bits");
	vm.AssignSlot(root, std::string("float"), true);
	vm.AssignSlot(root, std::string("text"), std::int32_t{123});
	Require(std::get<float>(vm.ReadSlot(root, std::string("float"))) == 1 && sgre::script::StringText(vm.ReadSlot(root, std::string("text"))).empty(), "native scalar defaults and string-only conversion differ");
	Require(!Exists(vm, flags, std::int32_t{0}), "native exact Integer _exist branch unexpectedly succeeded");
	Require(Exists(vm, flags, 0.0f), "native float _exist branch must succeed with zero stack results");
	const auto bytes = Call(vm, document, "serialize");
	Require(binary.Bytes(bytes).size() == 11 && binary.Bytes(bytes)[0] == 255 && binary.Bytes(bytes)[1] == 1 && binary.Bytes(bytes)[2] == 2, "serialize must copy genuine packed root bytes into Binary owner");
	const auto duplicate = Call(vm, document, "dup");
	static_cast<void>(Call(vm, document, "purge"));
	Require(std::get<std::int32_t>(vm.ReadSlot(child, std::string("number"))) == 0, "purge must keep actual retained view identity over new zero backing");
	const std::array<sgre::script::SquirrelValue, 1> serialized{bytes};
	static_cast<void>(Call(vm, document, "unserialize", serialized));
	Require(std::get<std::int32_t>(vm.ReadSlot(child, std::string("number"))) == -1, "unserialize must update the existing real view");
	const auto copy_root = Call(vm, duplicate, "getRoot");
	Require(std::get<std::int32_t>(vm.ReadSlot(vm.ReadSlot(copy_root, std::string("child")), std::string("number"))) == -1, "duplicate must preserve independent native schema ownership");
	const auto invalid = vm.CreateInstance(bindings.ValueClass());
	Require(std::holds_alternative<std::monostate>(Call(vm, invalid, "constructor")), "native CopyOnly constructor must succeed with zero results and no value payload");
	RequireFailure([&] {
		static_cast<void>(Call(vm, invalid, "len"));
	},
	    "payload-dependent StructValue method unexpectedly fabricated storage");
	const std::array<sgre::script::SquirrelValue, 2> absent_payload{invalid, std::int32_t{0}};
	Require(std::holds_alternative<std::monostate>(vm.Invoke(vm.GetSlot(bindings.ValueClass(), std::string("getValue")), absent_payload).Value), "static native getValue must return zero results for a payload-less receiver");
	std::cout << "actual Struct/StructValue constructors, coercion, exact stack statuses and Binary serialization PASS\n";
}

static void TestLastNestedViewLifetime() {
	sgre::script::SquirrelVm vm(VmLimits());
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	sgre::script::ScriptStructBindings bindings(vm, objects, psb, binary);
	auto schema = Schema();
	const std::weak_ptr<const sgre::resource::PsbDocument> observed = schema;
	auto source = psb.WrapDocument(schema);
	schema.reset();
	auto document = Construct(vm, bindings, source);
	const auto identity = vm.BorrowObjectIdentity(document);
	const auto weak = vm.CreateWeakReference(document);
	auto root = Call(vm, document, "getRoot");
	auto child = vm.ReadSlot(root, std::string("child"));
	auto retained = vm.Retain(child);
	document = {};
	source = {};
	root = {};
	static_cast<void>(vm.CollectGarbage());
	Require(vm.ResolveObjectIdentity(identity) && !observed.expired(), "last nested view must retain actual Struct and immutable PSB owner");
	vm.AssignSlot(child, std::string("number"), std::int32_t{7});
	Require(std::get<std::int32_t>(vm.ReadSlot(child, std::string("number"))) == 7, "nested view lost its original root storage");
	child = {};
	retained = {};
	static_cast<void>(vm.CollectGarbage());
	vm.RethrowReleaseFailure();
	Require(!vm.ResolveObjectIdentity(identity) && observed.expired() && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "last view retirement must release root storage and actual PSB script owner once");
	std::cout << "last nested view and exact native root/source retirement PASS\n";
}

static void TestExplicitRetirementAndVmClose() {
	sgre::script::SquirrelVm vm(VmLimits());
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	sgre::script::ScriptStructBindings bindings(vm, objects, psb, binary);
	auto source = psb.WrapDocument(Schema());
	auto document = Construct(vm, bindings, source);
	auto root = Call(vm, document, "getRoot");
	const auto identity = vm.BorrowObjectIdentity(root);
	objects.ReleaseInstance(document);
	RequireFailure([&] {
		static_cast<void>(vm.ReadSlot(root, std::string("child")));
	},
	    "explicitly invalidated Struct receiver left a silently usable shared C++ view");
	source = {};
	document = {};
	vm.Close();
	Require(vm.IsClosed() && !vm.ResolveObjectIdentity(identity), "VM.Close must forcibly finalize root-owned StructValue records");
	RequireFailure([&] {
		static_cast<void>(vm.ReadSlot(root, std::string("child")));
	},
	    "closed VM left a usable StructValue view");
	std::cout << "explicit native receiver retirement and forced VM.Close rejection PASS\n";
}

static void TestLiveDocumentForcedFinalization() {
	sgre::script::SquirrelVm vm(VmLimits());
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	sgre::script::ScriptStructBindings bindings(vm, objects, psb, binary);
	auto schema = Schema();
	const std::weak_ptr<const sgre::resource::PsbDocument> observed = schema;
	auto source = psb.WrapDocument(schema);
	schema.reset();
	auto document = Construct(vm, bindings, source);
	auto root = Call(vm, document, "getRoot");
	auto child = vm.ReadSlot(root, std::string("child"));
	auto retained = vm.Retain(child);
	source = {};
	document = {};
	root = {};
	Require(!observed.expired(), "live nested view must retain the actual source before forced close");
	vm.Close();
	Require(observed.expired(), "forced VM.Close must release the actual schema source despite live nested C++ values and host roots");
	RequireFailure([&] {
		static_cast<void>(vm.ReadSlot(child, std::string("number")));
	},
	    "forced VM.Close left a silently usable shared storage view");
	std::cout << "live schema document and nested host-root forced finalization PASS\n";
}

int main() {
	try {
		TestActualBindingsAndCoercion();
		TestLastNestedViewLifetime();
		TestExplicitRetirementAndVmClose();
		TestLiveDocumentForcedFinalization();

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
