#include "sgre/script/squirrel_vm.hpp"

#include <array>
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
using sgre::script::SquirrelValueType;
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

static bool Alive(SquirrelVm& vm, const SquirrelValue& weak) {
	return vm.Type(vm.ResolveWeakReference(weak)) != SquirrelValueType::Null;
}

static SquirrelValue Getter(SquirrelVm& vm) {
	return vm.CreateNativeClosure("independent-Class-meta-only-getter", [](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2, "the independent getter must receive instance and requested key");

		return SquirrelNativeResult::ReturnValue(std::int32_t{17});
	});
}

static void TestMetamethodFunctionIsNotOrdinaryMember() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("_get"), Getter(vm));
	Require(!vm.HasSlot(class_value, std::string("_get")), "function-valued metamethod names belong only to the separate native meta array");
	const auto instance = vm.CreateInstance(class_value);
	Require(!vm.HasSlot(instance, std::string("_get")), "native instance member lookup must not expose the separate Class meta array");
	Require(std::get<std::int32_t>(vm.ReadSlot(instance, std::string("independent-property"))) == 17, "ordinary missing-property lookup must still invoke the separately owned meta getter");
}

static void TestStaticScalarAndMetamethodHaveIndependentStorage() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("_get"), std::int32_t{79}, true);
	vm.NewSlot(class_value, std::string("_get"), Getter(vm));
	Require(std::get<std::int32_t>(vm.GetSlot(class_value, std::string("_get"))) == 79, "a metamethod function must preserve a separately indexed same-name static scalar");
	const auto instance = vm.CreateInstance(class_value);
	Require(std::get<std::int32_t>(vm.GetSlot(instance, std::string("_get"))) == 79, "same-name ordinary method storage must remain readable independently of the virtual meta getter");
	Require(std::get<std::int32_t>(vm.ReadSlot(instance, std::string("independent-property"))) == 17, "same-name ordinary scalar storage must not replace the virtual meta getter");
}

static void TestExistingFieldFunctionDoesNotRegisterMetamethod() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("_get"), std::int32_t{31});
	const auto method = Getter(vm);
	vm.NewSlot(class_value, std::string("_get"), method);
	Require(vm.GetSlot(class_value, std::string("_get")) == method, "replacing an existing Class field with a function must preserve its field index");
	const auto instance = vm.CreateInstance(class_value);
	Require(vm.GetSlot(instance, std::string("_get")) == method, "instance storage must copy a function-valued ordinary field");
	bool missing = false;
	try {
		static_cast<void>(vm.ReadSlot(instance, std::string("independent-property")));
	} catch (const sgre::script::SquirrelVmException& error) {
		missing = sgre::script::StringText(error.ThrownValue()) == "the index doesn't exist";
	}

	Require(missing, "existing field replacement must not create a separate native metamethod registration");
}

static void TestMethodToFieldKeepsPreviousTuple() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue weak;
	SquirrelValue class_value = vm.CreateClass();
	{
		const auto method = vm.CreateNativeClosure("independent-retained-old-Class-method", [](SquirrelVm&, std::span<const SquirrelValue>) {
			return SquirrelNativeResult::NoValues();
		});
		weak = vm.CreateWeakReference(method);
		vm.NewSlot(class_value, std::string("ordinary-method"), method);
		vm.NewSlot(class_value, std::string("ordinary-method"), std::int32_t{67});
	}

	Require(Alive(vm, weak), "redirecting an ordinary method name to a field must retain the original native method tuple");
	Require(std::get<std::int32_t>(vm.GetSlot(class_value, std::string("ordinary-method"))) == 67, "the redirected name must resolve to its new field value");
	const auto instance = vm.CreateInstance(class_value);
	Require(std::get<std::int32_t>(vm.GetSlot(instance, std::string("ordinary-method"))) == 67, "new instance fields must use the new field index rather than the retained unreachable method tuple");
	class_value = {};
	Require(Alive(vm, weak), "the actual instance Class ownership must preserve its retained old method tuple");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestDerivedSnapshotKeepsRedirectedMethodTuple() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue weak;
	SquirrelValue derived;
	{
		const auto base = vm.CreateClass();
		const auto method = vm.CreateNativeClosure("independent-inherited-retained-method", [](SquirrelVm&, std::span<const SquirrelValue>) {
			return SquirrelNativeResult::NoValues();
		});
		weak = vm.CreateWeakReference(method);
		vm.NewSlot(base, std::string("ordinary-method"), method);
		vm.NewSlot(base, std::string("ordinary-method"), std::int32_t{67});
		derived = vm.CreateClass(base);
	}

	Require(Alive(vm, weak), "native derived Class snapshots must copy the retained unreachable method tuple");
	Require(std::get<std::int32_t>(vm.GetSlot(derived, std::string("ordinary-method"))) == 67, "the copied member index must still select its actual new field tuple");
	derived = {};
	Require(!Alive(vm, weak), "dropping the final copied tuple owners must retire the original ordinary method identity");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestMetaOnlyAttributesAreNotRetained() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	SquirrelValue weak;
	{
		const auto attributes = vm.CreateString("independent-unindexed-meta-attributes");
		weak = vm.CreateWeakReference(attributes);
		BytecodeProgram program;
		program.MainFunction.SourceName = std::string("independent-meta-attribute-fixture");
		program.MainFunction.Parameters = {std::string("this"), std::string("class"), std::string("method"), std::string("attributes")};
		program.MainFunction.StackSize = 5;
		program.MainFunction.Literals = {std::string("_get")};
		program.MainFunction.Instructions = {Instruction(1, 4, 0), Instruction(60, 1, 1, 4, 2), Instruction(19, 255)};
		const std::array<SquirrelValue, 4> arguments{vm.RootTable(), class_value, Getter(vm), attributes};
		static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(program)), arguments));
	}

	Require(!Alive(vm, weak), "native newmember ignores the failed attribute assignment when a meta-only function has no ordinary member index");
	const auto instance = vm.CreateInstance(class_value);
	Require(std::get<std::int32_t>(vm.ReadSlot(instance, std::string("independent-property"))) == 17, "ignored unindexed attributes must preserve the actual meta function registration");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestIntermediateLookupCanRedirectAfterRelease(bool instance_receiver, bool redirect, const SquirrelValue& redirected_index = std::int32_t{0x01000000}) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::size_t release_calls = 0;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("field"), std::int32_t{41});
	vm.NewSlot(class_value, std::string("redirected-method"), std::int32_t{77}, true);
	const SquirrelValue receiver = instance_receiver ? vm.CreateInstance(class_value) : class_value;
	SquirrelValue old_weak;
	SquirrelValue array;
	{
		const auto old = vm.CreateInstance(vm.CreateClass());
		old_weak = vm.CreateWeakReference(old);
		vm.SetInstanceReleaseHook(old, [&](SquirrelVm& current, const SquirrelValue&) {
			++release_calls;
			if (redirect) {
				BytecodeProgram nested;
				nested.MainFunction.SourceName = std::string("independent-index-redirection-callback");
				nested.MainFunction.Parameters = {std::string("this")};
				nested.MainFunction.StackSize = 2;
				nested.MainFunction.Literals = {std::holds_alternative<std::int32_t>(redirected_index) ? sgre::script::SquirrelLiteral(std::get<std::int32_t>(redirected_index)) : sgre::script::SquirrelLiteral(std::string("invalid-encoded-index"))};
				nested.MainFunction.Instructions = {Instruction(1, 1, 0), Instruction(19, 1, 1)};
				const std::array<SquirrelValue, 1> arguments{current.RootTable()};
				static_cast<void>(current.InvokeInContext(current.CurrentContext(), current.CreateClosure(std::move(nested)), arguments));
			}
		});
		const std::array<SquirrelValue, 1> values{old};
		array = vm.CreateArray(values);
	}

	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-index-redirection-fixture");
	program.MainFunction.Parameters = {std::string("this"), std::string("array"), std::string("receiver")};
	program.MainFunction.StackSize = 6;
	program.MainFunction.Literals = {std::string("field")};
	program.MainFunction.Instructions = {Instruction(2, 3, 0), Instruction(14, 4, 1, 3), Instruction(20, 4, 1), Instruction(13, 5, 1, 3, 4), Instruction(9, 4, 0, 2), Instruction(19, 1, 4)};
	const std::array<SquirrelValue, 3> arguments{vm.RootTable(), array, receiver};
	const bool invalid_redirect = redirect && (redirected_index != SquirrelValue(std::int32_t{0x01000000}));
	bool failed = false;
	try {
		const auto result = vm.Invoke(vm.CreateClosure(std::move(program)), arguments);
		Require(!invalid_redirect && std::get<std::int32_t>(result.Value) == (redirect ? 77 : 41), "Class/Instance get must select the current encoded index after its prior temporary release callback");
	} catch (const std::runtime_error& error) {
		const std::string expected = std::holds_alternative<std::int32_t>(redirected_index) ? "Squirrel class lookup temporary index exceeds owned tuples" : "Squirrel class lookup temporary is not a bounded encoded integer";
		failed = invalid_redirect && std::string(error.what()) == expected;
		if (!failed) {
			throw;
		}
	}

	Require(failed == invalid_redirect, "invalid reentry output must fail the explicit encoded-index safety check without selecting unowned storage");
	Require(release_calls == 1 && !Alive(vm, old_weak), "the previous temporary-only owner must retire exactly once before indexed tuple selection");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestClassDoesNotExposeLength() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("field"), std::int32_t{41});
	bool failed = false;
	try {
		static_cast<void>(vm.Length(class_value));
	} catch (const std::runtime_error& error) {
		failed = std::string(error.what()) == "Squirrel value has no length";
	}

	Require(failed, "native sq_getsize does not count Class field/method tuples");
}

static void TestWeakFieldConstructorResolvesTarget(bool bytecode_call, bool live_target) {
	std::size_t calls = 0;
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	SquirrelValue constructor;
	{
		const auto method = vm.CreateNativeClosure("independent-weak-field-constructor", [&calls](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
			Require(arguments.size() == 1 && current.Type(arguments[0]) == SquirrelValueType::Instance, "resolved weak-field constructor must receive the actual newly created instance");
			++calls;

			return SquirrelNativeResult::NoValues();
		});
		vm.NewSlot(class_value, std::string("constructor"), vm.CreateWeakReference(method));
		if (live_target) {
			constructor = method;
		}
	}

	const std::array<SquirrelValue, 1> receiver{vm.RootTable()};
	SquirrelValue result;
	if (bytecode_call) {
		BytecodeProgram program;
		program.MainFunction.SourceName = std::string("independent-weak-field-constructor-call");
		program.MainFunction.Parameters = {std::string("this"), std::string("class")};
		program.MainFunction.StackSize = 4;
		program.MainFunction.Instructions = {Instruction(10, 2, 0), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)};
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), class_value};
		result = vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
	} else {
		result = vm.Invoke(class_value, receiver).Value;
	}

	Require(vm.Type(result) == SquirrelValueType::Instance && calls == (live_target ? 1U : 0U), "Class factory must resolve weak field constructors and skip expired targets in both generic and opcode call paths");
}

static void TestWeakMemberRead(bool instance_receiver, bool field, bool live_target) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto class_value = vm.CreateClass();
	SquirrelValue target;
	SquirrelValue weak;
	{
		const auto method = vm.CreateNativeClosure("independent-weak-Class-member", [](SquirrelVm&, std::span<const SquirrelValue>) {
			return SquirrelNativeResult::NoValues();
		});
		weak = vm.CreateWeakReference(method);
		vm.NewSlot(class_value, std::string("member"), weak, !field);
		if (live_target) {
			target = method;
		}
	}

	const SquirrelValue receiver = instance_receiver ? vm.CreateInstance(class_value) : class_value;
	const auto expected = field ? target : weak;
	Require(vm.GetSlot(receiver, std::string("member")) == expected, "Class/Instance field weak reads must resolve once while method-domain reads preserve the wrapper");
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-weak-Class-member-GET");
	program.MainFunction.Parameters = {std::string("this"), std::string("receiver")};
	program.MainFunction.StackSize = 3;
	program.MainFunction.Literals = {std::string("member")};
	program.MainFunction.Instructions = {Instruction(9, 2, 0, 1), Instruction(19, 1, 2)};
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), receiver};
	Require(vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value == expected, "primary encoded-index GET must preserve the same native field/method weak distinction");
}

int main() {
	try {
		TestMetamethodFunctionIsNotOrdinaryMember();
		TestStaticScalarAndMetamethodHaveIndependentStorage();
		TestExistingFieldFunctionDoesNotRegisterMetamethod();
		TestMethodToFieldKeepsPreviousTuple();
		TestDerivedSnapshotKeepsRedirectedMethodTuple();
		TestMetaOnlyAttributesAreNotRetained();
		TestIntermediateLookupCanRedirectAfterRelease(false, false);
		TestIntermediateLookupCanRedirectAfterRelease(false, true);
		TestIntermediateLookupCanRedirectAfterRelease(true, false);
		TestIntermediateLookupCanRedirectAfterRelease(true, true);
		TestIntermediateLookupCanRedirectAfterRelease(false, true, std::int32_t{0x01000002});
		TestIntermediateLookupCanRedirectAfterRelease(true, true, std::int32_t{0x02000002});
		TestIntermediateLookupCanRedirectAfterRelease(false, true, std::string("invalid-index"));
		TestIntermediateLookupCanRedirectAfterRelease(true, true, std::string("invalid-index"));
		TestClassDoesNotExposeLength();
		TestWeakFieldConstructorResolvesTarget(false, false);
		TestWeakFieldConstructorResolvesTarget(false, true);
		TestWeakFieldConstructorResolvesTarget(true, false);
		TestWeakFieldConstructorResolvesTarget(true, true);
		for (const bool instance_receiver : {false, true}) {
			for (const bool field : {false, true}) {
				for (const bool live_target : {false, true}) {
					TestWeakMemberRead(instance_receiver, field, live_target);
				}
			}
		}

		std::cout << "Squirrel Class member index checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
