#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using sgre::resource::PsbArray;
using sgre::resource::PsbDocument;
using sgre::resource::PsbObject;
using sgre::resource::PsbValue;
using sgre::script::ScriptObjectBindings;
using sgre::script::ScriptPsbBindings;
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

template <typename Value>
static PsbValue Atom(Value value, std::uint8_t tag = 0) {
	PsbValue result;
	result.SerializedTag = tag;
	result.Data = std::move(value);

	return result;
}

static SquirrelValue InvokeMethod(SquirrelVm& vm, const SquirrelValue& instance, std::string name, std::span<const SquirrelValue> parameters = {}) {
	std::vector<SquirrelValue> arguments{instance};
	arguments.insert(arguments.end(), parameters.begin(), parameters.end());

	return vm.Invoke(vm.GetSlot(instance, std::move(name)), arguments).Value;
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t argument1 = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
	sgre::script::SquirrelInstruction result;
	result.Opcode = opcode;
	result.Argument0 = destination;
	result.Argument1 = argument1;
	result.Argument2 = argument2;
	result.Argument3 = argument3;

	return result;
}

static SquirrelValue Evaluate(SquirrelVm& vm, std::vector<sgre::script::SquirrelInstruction> instructions, std::span<const SquirrelValue> arguments) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.Name = std::string("fixture");
	program.MainFunction.SourceName = std::string("independent-psb-fixture.nut");
	program.MainFunction.Parameters = {std::string("this"), std::string("value"), std::string("key")};
	program.MainFunction.StackSize = 8;
	program.MainFunction.Instructions = std::move(instructions);

	return vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
}

static bool Contains(SquirrelVm& vm, const SquirrelValue& object, const SquirrelValue& key) {
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), object, key};

	return std::get<bool>(Evaluate(vm, {Instruction(41, 3, 1, 2), Instruction(19, 1, 3)}, arguments));
}

static std::string TypeName(SquirrelVm& vm, const SquirrelValue& value) {
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), value, SquirrelValue{}};

	return sgre::script::StringText(Evaluate(vm, {Instruction(55, 3, 1), Instruction(19, 1, 3)}, arguments));
}

static SquirrelValue IteratorPairs(SquirrelVm& vm, const SquirrelValue& iterable) {
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), iterable, SquirrelValue{}};

	return Evaluate(vm, {Instruction(31, 6), Instruction(51, 1, 4, 2), Instruction(52, 1, 4, 2), Instruction(32, 6, 2), Instruction(32, 6, 3), Instruction(24, 0, -5), Instruction(19, 1, 6)}, arguments);
}

static void TestContainersAndLifetime() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	ScriptObjectBindings objects(vm);
	ScriptPsbBindings bindings(vm, objects);
	auto document = std::make_shared<PsbDocument>();
	PsbArray numbers{Atom(std::int64_t{7}), Atom(std::int64_t{11}), Atom(std::int64_t{19})};
	sgre::resource::PsbChunkReference chunk;
	PsbObject root;
	root.emplace_back("numbers", Atom(std::move(numbers), 32));
	root.emplace_back("second", Atom(std::string("text\0tail", 9), 21));
	root.emplace_back("chunk", Atom(chunk, 25));
	root.emplace_back("wide", Atom(std::int64_t{0x100000001}, 12));
	root.emplace_back("fraction", Atom(1.234567891234, 31));
	root.emplace_back("flag", Atom(true, 2));
	root.emplace_back("absent", PsbValue{});
	document->Root = Atom(std::move(root), 33);
	const auto owner = bindings.WrapDocument(document);
	const auto weak_owner = std::weak_ptr<const PsbDocument>(document);
	document.reset();
	Require(objects.HasInstance(owner) && vm.IsInstanceOf(owner, bindings.ObjectClass()), "PSBObject must retain real native Object base state");
	const auto value = vm.ReadSlot(owner, std::string("root"));
	Require(!weak_owner.expired() && vm.IsInstanceOf(value, bindings.ValueClass()) && TypeName(vm, value) == "table", "root table must be a typed PSBValue retaining its document owner");
	const auto array = vm.ReadSlot(value, std::string("numbers"));
	Require(TypeName(vm, array) == "array" && std::get<std::int32_t>(InvokeMethod(vm, array, "len")) == 3, "PSB array type and length changed");
	Require(std::get<std::int32_t>(vm.ReadSlot(array, 1.9f)) == 11 && std::get<std::int32_t>(vm.ReadSlot(array, true)) == 11, "PSB indexed access must use native numeric integer coercion");
	Require(Contains(vm, array, std::int32_t{1}) && Contains(vm, array, true) && !Contains(vm, array, 1.0f), "PSB existence has a distinct native exclusion for float keys");
	Require(Contains(vm, value, std::string("absent")) && !Contains(vm, value, std::string("missing")), "PSB existence must distinguish a present null atom from a missing key");
	Require(sgre::script::StringText(vm.ReadSlot(value, std::string("second\0ignored", 14))) == "text", "PSB lookup and string conversion must preserve native C-string termination");
	Require(std::get<std::int32_t>(vm.ReadSlot(value, std::string("wide"))) == 1, "PSB signed 64-bit atoms must narrow to native integer low bits");
	Require(std::get<float>(vm.ReadSlot(value, std::string("fraction"))) == static_cast<float>(1.234567891234), "PSB double atoms must narrow to native float32");
	Require(std::get<bool>(vm.ReadSlot(value, std::string("flag"))), "PSB bool atom conversion changed");
	const auto binary = vm.ReadSlot(value, std::string("chunk"));
	Require(vm.IsInstanceOf(binary, bindings.ValueClass()) && TypeName(vm, binary) == "null" && std::holds_alternative<std::monostate>(vm.ReadSlot(binary, std::string("any"))), "native nested chunk is a noniterable PSBValue whose type and lookup are null");
	const auto array_pairs = IteratorPairs(vm, array);
	Require(vm.Length(array_pairs) == 6 && std::get<std::int32_t>(vm.GetSlot(array_pairs, std::int32_t{4})) == 2 && std::get<std::int32_t>(vm.GetSlot(array_pairs, std::int32_t{5})) == 19, "PSB array iterator key/value order changed");
	const auto object_pairs = IteratorPairs(vm, value);
	Require(vm.Length(object_pairs) == 14 && sgre::script::StringText(vm.GetSlot(object_pairs, std::int32_t{0})) == "numbers" && sgre::script::StringText(vm.GetSlot(object_pairs, std::int32_t{2})) == "second", "PSB table enumeration must retain its original key-index order");
	const std::array<SquirrelValue, 1> invalid_index{std::int32_t{-1}};
	Require(!vm.HasSlot(array, std::string("_get")), "native PSB meta getter must remain separate from its ordinary getValue entry point");
	RequireFailure([&] {
		static_cast<void>(InvokeMethod(vm, array, "getValue", invalid_index));
	},
	    "index does not exist");
	RequireFailure([&] {
		static_cast<void>(vm.ReadSlot(array, std::int32_t{-1}));
	},
	    "the index doesn't exist");
	RequireFailure([&] {
		static_cast<void>(InvokeMethod(vm, value, "len"));
	},
	    "requires an array");
	RequireFailure([&] {
		static_cast<void>(IteratorPairs(vm, binary));
	},
	    "_nexti failed");
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(bindings.ObjectClass(), receiver));
	},
	    "can't create instance");
}

static void TestScalarRootsAndSafety() {
	SquirrelVm vm;
	ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindingLimits limits;
	limits.MaxDocuments = 8;
	limits.MaxValues = 1;
	ScriptPsbBindings bindings(vm, objects, limits);
	auto integer = std::make_shared<PsbDocument>();
	integer->Root = Atom(std::int64_t{-19}, 5);
	const auto scalar_owner = bindings.WrapDocument(integer);
	Require(std::get<std::int32_t>(InvokeMethod(vm, scalar_owner, "getRoot")) == -19, "scalar PSB root must return directly");
	RequireFailure([&] {
		static_cast<void>(InvokeMethod(vm, scalar_owner, "totalStreamEntitySize"));
	},
	    "complete retained 32-bit backing");
	auto complete = std::make_shared<PsbDocument>();
	complete->Header.DataOffset = 21;
	complete->SerializedSize = 28;
	const auto complete_owner = bindings.WrapDocument(complete, std::make_shared<const std::vector<std::uint8_t>>(32));
	const auto shared_asset = bindings.DocumentAsset(complete_owner);
	Require(shared_asset.Document == complete && shared_asset.Backing && shared_asset.Backing->size() == 32, "native PSB asset access must preserve original immutable document and complete backing ownership");
	Require(std::get<std::int32_t>(InvokeMethod(vm, complete_owner, "totalStreamEntitySize")) == 11, "native stream size must use original complete backing length including unreferenced padding");
	auto chunk = std::make_shared<PsbDocument>();
	chunk->Root = Atom(sgre::resource::PsbChunkReference{}, 25);
	Require(std::holds_alternative<std::monostate>(InvokeMethod(vm, bindings.WrapDocument(chunk), "getRoot")), "root chunk has no native getRoot value result");
	auto invalid = std::make_shared<PsbDocument>();
	invalid->Root = Atom(std::int64_t{4}, 13);
	const auto invalid_owner = bindings.WrapDocument(invalid);
	RequireFailure([&] {
		static_cast<void>(InvokeMethod(vm, invalid_owner, "getRoot"));
	},
	    "packed-array header");
	auto array = std::make_shared<PsbDocument>();
	array->Root = Atom(PsbArray{}, 32);
	const auto owner = bindings.WrapDocument(array);
	static_cast<void>(InvokeMethod(vm, owner, "getRoot"));
	RequireFailure([&] {
		static_cast<void>(InvokeMethod(vm, owner, "getRoot"));
	},
	    "value binding limit exceeded");
	auto short_backing = std::make_shared<PsbDocument>();
	short_backing->SerializedSize = 17;
	RequireFailure([&] {
		static_cast<void>(bindings.WrapDocument(short_backing, std::make_shared<const std::vector<std::uint8_t>>(3)));
	},
	    "backing is shorter");
	RequireFailure([&] {
		static_cast<void>(bindings.WrapDocument({}));
	},
	    "document is absent");
}

static void TestCallbackOwnerExpiry() {
	SquirrelVm vm;
	ScriptObjectBindings objects(vm);
	SquirrelValue callable;
	SquirrelValue instance;
	{
		ScriptPsbBindings bindings(vm, objects);
		auto document = std::make_shared<PsbDocument>();
		document->Root = Atom(true, 2);
		instance = bindings.WrapDocument(document);
		callable = vm.GetSlot(instance, std::string("getRoot"));
	}

	const std::array<SquirrelValue, 1> receiver{instance};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(callable, receiver));
	},
	    "bindings have expired");
}

static void TestReclaimedProxyOwner() {
	sgre::script::SquirrelVmLimits vm_limits;
	vm_limits.ReclaimObjects = true;
	SquirrelVm vm(vm_limits);
	ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindingLimits limits;
	limits.MaxDocuments = 1;
	limits.MaxValues = 2;
	ScriptPsbBindings bindings(vm, objects, limits);
	const auto class_identity = vm.BorrowObjectIdentity(bindings.ValueClass());
	vm.SetSlot(vm.RootTable(), std::string("PSBValue"), {});
	vm.SetSlot(vm.RootTable(), std::string("PSBObject"), {});
	static_cast<void>(vm.CollectGarbage());
	Require(vm.ResolveObjectIdentity(class_identity).has_value(), "PSB class metadata lost its explicit host roots");
	auto document = std::make_shared<PsbDocument>();
	document->Root = Atom(PsbObject{{"items", Atom(PsbArray{Atom(std::int64_t(17))}, 32)}}, 33);
	const std::weak_ptr<const PsbDocument> document_weak = document;
	auto owner = bindings.WrapDocument(document);
	document.reset();
	const auto owner_identity = vm.BorrowObjectIdentity(owner);
	const auto owner_weak = vm.CreateWeakReference(owner);
	auto root = vm.ReadSlot(owner, std::string("root"));
	auto child = vm.ReadSlot(root, std::string("items"));
	vm.NewSlot(vm.RootTable(), std::string("fixture-child"), child);
	const auto child_identity = vm.BorrowObjectIdentity(child);
	Require(vm.RetainedReferenceCount(owner) == 2, "each native PSBValue must explicitly retain its script document owner");
	owner = {};
	root = {};
	static_cast<void>(vm.CollectGarbage());
	Require(!document_weak.expired() && vm.ResolveObjectIdentity(owner_identity).has_value() && std::get<std::int32_t>(vm.ReadSlot(child, std::int32_t(0))) == 17, "nested PSBValue failed to preserve its document during collection");
	{
		const auto retained = vm.ResolveObjectIdentity(owner_identity);
		Require(retained && vm.RetainedReferenceCount(*retained) == 1, "reclaimed PSBValue left an extra native document root");
	}

	vm.SetSlot(vm.RootTable(), std::string("fixture-child"), {});
	child = {};
	vm.RethrowReleaseFailure();
	Require(document_weak.expired() && !vm.ResolveObjectIdentity(owner_identity) && !vm.ResolveObjectIdentity(child_identity) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(owner_weak)), "last PSBValue release stranded its native document or script owner");
	auto next_document = std::make_shared<PsbDocument>();
	next_document->Root = Atom(PsbArray{}, 32);
	const auto replacement = bindings.WrapDocument(next_document);
	const auto replacement_root = vm.ReadSlot(replacement, std::string("root"));
	Require(vm.BorrowObjectIdentity(replacement) != owner_identity && vm.BorrowObjectIdentity(replacement_root) != child_identity && !vm.ResolveObjectIdentity(owner_identity) && !vm.ResolveObjectIdentity(child_identity), "PSB registry resolved a previous generation after slot reuse");
}

static void TestImmutableValueAssets() {
	SquirrelVm vm;
	ScriptObjectBindings objects(vm);
	ScriptPsbBindings bindings(vm, objects);
	auto document = std::make_shared<PsbDocument>();
	document->Chunks.push_back({0, 4});
	document->Root = Atom(PsbObject{{"first", Atom(sgre::resource::PsbChunkReference{0, false}, 25)}, {"second", Atom(sgre::resource::PsbChunkReference{0, false}, 25)}}, 33);
	auto backing = std::make_shared<const std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{7, 8, 9, 10});
	const std::weak_ptr<const PsbDocument> document_weak = document;
	const std::weak_ptr<const std::vector<std::uint8_t>> backing_weak = backing;
	const auto owner = bindings.WrapDocument(document, backing);
	const auto root = vm.ReadSlot(owner, std::string("root"));
	const auto first = vm.ReadSlot(root, std::string("first"));
	const auto second = vm.ReadSlot(root, std::string("second"));
	const auto reference_count = vm.RetainedReferenceCount(owner);
	auto asset = bindings.ValueAsset(first);
	{
		const auto other = bindings.ValueAsset(second);
		Require(asset.Owner.Document == document && asset.Owner.Backing == backing && asset.Value == document->Root.Find("first"), "immutable native asset preserves original document/backing/node identity");
		Require(other.Value != asset.Value && other.Value == document->Root.Find("second") && std::get<sgre::resource::PsbChunkReference>(other.Value->Data).Index == 0, "equal PSB values retain distinct native node identities");
		Require(vm.RetainedReferenceCount(owner) == reference_count, "sharing a C++ asset must not add native script roots");
	}
	RequireFailure([&] {
		static_cast<void>(bindings.ValueAsset(owner));
	},
	    "no retained value");
	RequireFailure([&] {
		static_cast<void>(bindings.ValueAsset(vm.CreateInstance(bindings.ValueClass())));
	},
	    "no retained value");
	RequireFailure([&] {
		static_cast<void>(bindings.ValueAsset(std::int32_t(0)));
	},
	    "requires a PSBValue");
	document.reset();
	backing.reset();
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(!document_weak.expired() && !backing_weak.expired() && asset.Value->SerializedTag == 25 && asset.Owner.Backing->at(3) == 10, "owned immutable asset remains readable after native wrappers and VM retire");
	asset = {};
	Require(document_weak.expired() && backing_weak.expired(), "last C++ asset releases its document and backing without a stranded root");
}

int main() {
	try {
		TestContainersAndLifetime();
		TestScalarRootsAndSafety();
		TestCallbackOwnerExpiry();
		TestReclaimedProxyOwner();
		TestImmutableValueAssets();
		std::cout << "Native PSB binding checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
