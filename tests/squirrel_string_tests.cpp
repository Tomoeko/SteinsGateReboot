#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

using sgre::script::SquirrelString;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
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

	throw std::runtime_error("expected string ownership failure");
}

static sgre::script::SquirrelVmLimits RecoveredLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static bool SameString(const SquirrelValue& left, const SquirrelValue& right) {
	return std::get<SquirrelString>(left).SharesIdentity(std::get<SquirrelString>(right));
}

static void TestInternedIdentityAndExtents() {
	SquirrelVm vm(RecoveredLimits());
	const SquirrelValue host_literal = "first-party literal";
	Require(StringText(host_literal) == "first-party literal", "C++ string literals must retain the runtime string alternative");
	const auto first = vm.CreateString(std::string("a\0b", 3));
	const auto equal = vm.CreateString(std::string("a\0b", 3));
	const auto different = vm.CreateString(std::string("a\0c", 3));
	Require(SameString(first, equal) && !SameString(first, different), "interning must compare the complete immutable byte extent");
	Require(StringText(first) == std::string("a\0b", 3) && vm.Length(first) == 3, "interning must preserve embedded NUL bytes");
	const auto weak = vm.CreateWeakReference(first);
	Require(vm.CreateWeakReference(equal) == weak && SameString(vm.ResolveWeakReference(weak), first), "equal interned strings must share one cached weak wrapper");
	const auto builtin_weak = vm.CreateWeakReference(vm.CreateString("_cmp"));
	Require(StringText(vm.ResolveWeakReference(builtin_weak)) == "_cmp", "native shared-state metadata must retain its pooled strings before delegate installation");
	const auto table = vm.CreateTable();
	vm.NewSlot(table, first, std::int32_t{11});
	vm.NewSlot(table, different, std::int32_t{29});
	Require(std::get<std::int32_t>(vm.GetSlot(table, equal)) == 11 && vm.Length(table) == 2, "full string keys must remain distinct across embedded NUL bytes");
	for (std::size_t length : {std::size_t{0}, std::size_t{31}, std::size_t{32}, std::size_t{33}, std::size_t{63}, std::size_t{64}, std::size_t{65}, std::size_t{1024}}) {
		std::string bytes(length, '\0');
		for (std::size_t index = 0; index < length; ++index) {
			bytes[index] = static_cast<char>(static_cast<std::uint8_t>(index * 37));
		}

		const auto value = vm.CreateString(bytes);
		const auto copy = vm.CreateString(bytes);
		Require(SameString(value, copy) && StringText(value) == bytes, "signed-byte hash sampling must preserve all measured lengths and bytes");
	}
}

static void TestCopiesAndImmediateWeakExpiration() {
	SquirrelVm vm(RecoveredLimits());
	SquirrelValue value = std::string("owned lifetime");
	SquirrelValue copy = value;
	const auto weak = vm.CreateWeakReference(value);
	Require(SameString(value, copy), "copies made before adoption must share the adopted native identity");
	value = {};
	Require(StringText(vm.ResolveWeakReference(weak)) == "owned lifetime", "a copied immutable string must retain its native identity");
	copy = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)) && vm.CollectGarbage() == 0, "last string ownership must expire immediately without a collector iteration");
	const auto replacement = vm.CreateString("owned lifetime");
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)) && vm.CreateWeakReference(replacement) != weak, "reinsertion of equal bytes must never revive an expired weak target");
}

static void TestWeakWrapperRetirementAndOneLevelResolution() {
	SquirrelVm vm(RecoveredLimits());
	const auto text = vm.CreateString("cached wrapper");
	SquirrelValue weak = vm.CreateWeakReference(text);
	const auto identity = vm.BorrowObjectIdentity(weak);
	weak = {};
	const auto replacement = vm.CreateWeakReference(text);
	Require(!vm.ResolveObjectIdentity(identity) && vm.ResolveWeakReference(replacement) == text, "weak retirement must clear a live string's cached wrapper");
	const auto nested = vm.CreateWeakReference(replacement);
	Require(vm.ResolveWeakReference(nested) == replacement, "weak wrappers of string weak wrappers must resolve only one level");
	const auto table = vm.CreateTable();
	vm.NewSlot(table, std::string("text"), replacement);
	vm.NewSlot(table, std::string("wrapper"), nested);
	Require(SameString(vm.GetSlot(table, std::string("text")), text) && vm.GetSlot(table, std::string("wrapper")) == replacement, "stored weak string reads must retain native one-level resolution");
}

static void TestExplicitHostRootsAndBorrowedRootValues() {
	SquirrelVm vm(RecoveredLimits());
	SquirrelValue text = vm.CreateString("rooted string");
	const auto weak = vm.CreateWeakReference(text);
	auto first = vm.Retain(text);
	auto second = vm.Retain(vm.CreateString("rooted string"));
	Require(vm.RetainedReferenceCount(text) == 2 && SameString(first.Value(), second.Value()), "duplicate external roots must count separately while sharing interned bytes");
	text = {};
	first = {};
	Require(StringText(second.Value()) == "rooted string", "the remaining external root must protect the immutable string");
	SquirrelValue copied_root = second.Value();
	second = {};
	Require(StringText(vm.ResolveWeakReference(weak)) == "rooted string", "copying a borrowed root value must acquire actual ownership");
	copied_root = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "borrowed scoped-root metadata must not secretly retain the string");
}

static void TestArrayReleaseBeforeFollowingOwnerCallback() {
	SquirrelVm vm(RecoveredLimits());
	SquirrelValue text = vm.CreateString("first array member");
	const auto weak = vm.CreateWeakReference(text);
	const auto class_value = vm.CreateClass();
	SquirrelValue instance = vm.CreateInstance(class_value);
	SquirrelValue array = vm.CreateArray();
	vm.AppendArray(array, text);
	vm.AppendArray(array, instance);
	bool called = false;
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue&) {
		Require(std::holds_alternative<std::monostate>(current.ResolveWeakReference(weak)), "ascending array release must retire prior string ownership before the next native owner callback");
		Require(current.Length(array) == 2, "array release callbacks must retain the original logical extent");
		RequireFailure([&] {
			static_cast<void>(current.GetSlot(array, std::int32_t{0}));
		},
		    "string identity expired");
		called = true;
	});
	text = {};
	instance = {};
	vm.ClearArray(array);
	vm.RethrowReleaseFailure();
	Require(called && vm.Length(array) == 0, "array clearing must finish its native callback and empty the array");
}

static void TestCollectionRetiresContainedStrings() {
	SquirrelVm vm(RecoveredLimits());
	SquirrelValue table = vm.CreateTable();
	SquirrelValue text = vm.CreateString("cycle-owned string");
	const auto weak = vm.CreateWeakReference(text);
	vm.NewSlot(table, std::string("self"), table);
	vm.NewSlot(table, std::string("text"), text);
	text = {};
	table = {};
	Require(StringText(vm.ResolveWeakReference(weak)) == "cycle-owned string", "the container cycle must keep the string alive until native finalization");
	Require(vm.CollectGarbage() == 1 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "collection must release contained strings without treating strings as collectible nodes");
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t argument = 0) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = destination;
	instruction.Argument1 = argument;

	return instruction;
}

static void TestPrototypeOwnershipIsDirected() {
	SquirrelVm vm(RecoveredLimits());
	sgre::script::BytecodeProgram program;
	program.MainFunction.Parameters.push_back(std::string("this"));
	program.MainFunction.StackSize = 2;
	program.MainFunction.Literals.push_back(std::string("parent-only literal"));
	program.MainFunction.Instructions = {Instruction(48, 1), Instruction(19, 1, 1)};
	sgre::script::SquirrelFunction child;
	child.Parameters.push_back(std::string("this"));
	child.StackSize = 2;
	child.Literals.push_back(std::string("child-only literal"));
	child.Instructions = {Instruction(1, 1), Instruction(19, 1, 1)};
	program.MainFunction.Functions.push_back(std::move(child));
	SquirrelValue parent = vm.CreateClosure(std::move(program));
	const auto parent_text_weak = vm.CreateWeakReference(vm.CreateString("parent-only literal"));
	const auto child_text_weak = vm.CreateWeakReference(vm.CreateString("child-only literal"));
	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	SquirrelValue callable = vm.Invoke(parent, receiver).Value;
	vm.ResetExecution();
	parent = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(parent_text_weak)), "a retained child prototype must not retain its parent's unrelated string literals");
	Require(StringText(vm.ResolveWeakReference(child_text_weak)) == "child-only literal", "a retained child closure must own its immutable native prototype literals");
	Require(SameString(vm.Invoke(callable, receiver).Value, vm.ResolveWeakReference(child_text_weak)), "literal dispatch must return its prototype's canonical string identity");
	vm.ResetExecution();
	callable = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(child_text_weak)), "last child closure release must retire its own prototype string ownership");
}

static void TestNativeMetadataAndInvocationOwnership() {
	SquirrelVm vm(RecoveredLimits());
	sgre::script::SquirrelNativeSignature signature;
	signature.Name = "native-owned-name";
	SquirrelValue closure = vm.CreateNativeClosure("native-owned-name", [](SquirrelVm& current, const auto arguments) {
		Require(SameString(arguments[1], current.CreateString("native argument")), "native callbacks must receive the canonical adopted string identity");
		sgre::script::SquirrelNativeResult result;
		result.Value = std::string("native return");

		return result;
	},
	    signature);
	const auto name_weak = vm.CreateWeakReference(vm.CreateString("native-owned-name"));
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), std::string("native argument")};
	Require(SameString(vm.Invoke(closure, arguments).Value, vm.CreateString("native return")), "native return text must be adopted before exposure");
	closure = {};
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(name_weak)), "last native closure release must retire its retained name string");
}

static void TestForeignAndClosedOwners() {
	SquirrelVm first(RecoveredLimits());
	SquirrelVm second(RecoveredLimits());
	const auto text = first.CreateString("first owner");
	RequireFailure([&] {
		static_cast<void>(second.CreateWeakReference(text));
	},
	    "does not belong to this VM");
	RequireFailure([&] {
		static_cast<void>(second.Length(text));
	},
	    "does not belong to this VM");
	const auto copied_bytes = second.CreateString(StringText(text));
	Require(!SameString(text, copied_bytes), "distinct shared states must not intern strings into one mutable global pool");
	first.Close();
	Require(StringText(text) == "first owner", "the immutable host byte view must remain memory-safe after its VM closes");
	RequireFailure([&] {
		static_cast<void>(first.CreateWeakReference(text));
	},
	    "owner expired");
}

static void TestAggregateBoundsAndPoolRetirement() {
	auto limits = RecoveredLimits();
	limits.MaxInternedStrings = 37;
	limits.MaxInternedStringBytes = 229;
	SquirrelVm vm(limits);
	SquirrelValue first = vm.CreateString("abc");
	SquirrelValue second = vm.CreateString("de");
	Require(SameString(first, vm.CreateString("abc")), "duplicate identities must not consume aggregate pool capacity");
	RequireFailure([&] {
		static_cast<void>(vm.CreateString("f"));
	},
	    "aggregate interned string limit");
	first = {};
	for (std::size_t index = 0; index < 1000; ++index) {
		const auto transient = vm.CreateString(std::to_string(index));
		Require(!StringText(transient).empty(), "retired pool entries must free count and byte budget for new identities");
	}

	second = {};
	const auto binary = vm.CreateString(std::string("a\0b\0c", 5));
	RequireFailure([&] {
		static_cast<void>(vm.CreateString("d"));
	},
	    "aggregate interned string limit");
	Require(vm.Length(binary) == 5, "aggregate byte accounting must include the complete embedded-NUL extent");
}

static void TestSharedConstructorNameSurvivesRemainingFinalizers() {
	SquirrelVm vm(RecoveredLimits());
	const auto constructor_weak = vm.CreateWeakReference(vm.CreateString("constructor"));
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("self"), {});
	const auto class_root = vm.Retain(class_value);
	SquirrelValue instance = vm.CreateInstance(class_value);
	vm.SetSlot(instance, std::string("self"), instance);
	bool observed = false;
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue&) {
		const auto constructor = current.ResolveWeakReference(constructor_weak);
		Require(current.Type(constructor) == sgre::script::SquirrelValueType::String && StringText(constructor) == "constructor", "shared constructor name must survive the remaining-GC-list finalization pass");
		observed = true;
	});
	instance = {};
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(observed, "the remaining instance finalizer must observe shared constructor identity before shared-slot retirement");
}

int main() {
	try {
		TestInternedIdentityAndExtents();
		TestCopiesAndImmediateWeakExpiration();
		TestWeakWrapperRetirementAndOneLevelResolution();
		TestExplicitHostRootsAndBorrowedRootValues();
		TestArrayReleaseBeforeFollowingOwnerCallback();
		TestCollectionRetiresContainedStrings();
		TestPrototypeOwnershipIsDirected();
		TestNativeMetadataAndInvocationOwnership();
		TestForeignAndClosedOwners();
		TestAggregateBoundsAndPoolRetirement();
		TestSharedConstructorNameSurvivesRemainingFinalizers();
		std::cout << "Squirrel string identity and lifetime checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
