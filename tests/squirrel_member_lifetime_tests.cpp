#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void SetMemberAttributes(SquirrelVm& vm, const SquirrelValue& type, const SquirrelValue& key, const SquirrelValue& attributes) {
	const std::array<SquirrelValue, 3> arguments{type, key, attributes};
	static_cast<void>(vm.Invoke(vm.GetSlot(type, std::string("setattributes")), arguments));
}

static void TestOnlyMemberIndexOwnsNamesAndClassOwnsAttributes() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::size_t releases = 0;
	bool key_expired = false;
	bool attributes_expired = false;
	bool value_alive = false;
	SquirrelValue key_weak;
	SquirrelValue attributes_weak;
	SquirrelValue value_weak;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto relations = vm.CreateTable();
	const auto type = vm.CreateClass({}, relations);
	SquirrelValue key = vm.CreateString("independent-member-key-lifetime");
	SquirrelValue attributes = vm.CreateString("independent-member-attributes-lifetime");
	SquirrelValue value = vm.CreateString("independent-member-value-lifetime");
	key_weak = vm.CreateWeakReference(key);
	attributes_weak = vm.CreateWeakReference(attributes);
	value_weak = vm.CreateWeakReference(value);
	vm.NewSlot(type, key, value);
	SetMemberAttributes(vm, type, key, attributes);
	const auto instance = vm.CreateInstance(type);
	vm.NewSlot(relations, std::string("instance"), instance);
	vm.NewSlot(vm.RootTable(), std::string("subject_class"), type);
	vm.SetInstanceReleaseHook(instance, [&](SquirrelVm& current, const SquirrelValue& receiver) {
		key_expired = std::holds_alternative<std::monostate>(current.ResolveWeakReference(key_weak));
		attributes_expired = std::holds_alternative<std::monostate>(current.ResolveWeakReference(attributes_weak));
		value_alive = !std::holds_alternative<std::monostate>(current.ResolveWeakReference(value_weak));
		Require(!current.HasSlot(receiver, std::string("independent-member-key-lifetime")), "retired native class metadata must no longer resolve an instance field name");
		++releases;
	});
	key = {};
	attributes = {};
	value = {};
	Require(vm.CollectGarbage() == 0, "the rooted class attributes relationship must retain its own native instance before Close");
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(releases == 1 && key_expired && attributes_expired && value_alive, "native Class retirement must expire member names/attributes while the surviving instance still owns its field value");
}

static void TestTupleAttributesReleaseBeforeValue() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::vector<std::int32_t> callbacks;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto owner_type = vm.CreateClass();
	SquirrelValue attribute_owner = vm.CreateInstance(owner_type);
	SquirrelValue value_owner = vm.CreateInstance(owner_type);
	const auto attribute_weak = vm.CreateWeakReference(attribute_owner);
	const auto value_weak = vm.CreateWeakReference(value_owner);
	vm.SetInstanceReleaseHook(attribute_owner, [&](SquirrelVm& current, const SquirrelValue&) {
		Require(!std::holds_alternative<std::monostate>(current.ResolveWeakReference(value_weak)), "native tuple attribute release must precede the value's last reference drop");
		callbacks.push_back(0);
	});
	vm.SetInstanceReleaseHook(value_owner, [&](SquirrelVm& current, const SquirrelValue&) {
		Require(std::holds_alternative<std::monostate>(current.ResolveWeakReference(attribute_weak)), "native tuple value release must observe the already expired attributes owner");
		callbacks.push_back(1);
	});
	SquirrelValue subject = vm.CreateClass();
	vm.NewSlot(subject, std::string("field"), value_owner);
	SetMemberAttributes(vm, subject, std::string("field"), attribute_owner);
	attribute_owner = {};
	value_owner = {};
	subject = {};
	vm.RethrowReleaseFailure();
	Require(callbacks == std::vector<std::int32_t>{0, 1}, "native class tuples must retire attributes then values in ascending member order");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestMemberIndexRetiresBeforeParentClass() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::size_t releases = 0;
	SquirrelValue key_weak;
	SquirrelVm vm(limits);
	const auto observer_type = vm.CreateClass();
	SquirrelValue observer = vm.CreateInstance(observer_type);
	vm.SetInstanceReleaseHook(observer, [&](SquirrelVm& current, const SquirrelValue&) {
		Require(std::holds_alternative<std::monostate>(current.ResolveWeakReference(key_weak)), "native parent Class release must observe the derived member index already retired");
		++releases;
	});
	SquirrelValue parent = vm.CreateClass({}, observer);
	observer = {};
	SquirrelValue subject = vm.CreateClass(parent);
	parent = {};
	SquirrelValue key = vm.CreateString("independent-derived-only-member-name");
	key_weak = vm.CreateWeakReference(key);
	vm.NewSlot(subject, key, std::int32_t(7));
	key = {};
	subject = {};
	vm.RethrowReleaseFailure();
	Require(releases == 1, "retiring the derived parent relation must release the base Class attributes owner exactly once");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first, std::int32_t second) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = first;
	instruction.Argument1 = second;

	return instruction;
}

static void TestInstanceCloneDoesNotCopyNativeCleanup() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::size_t releases = 0;
	SquirrelVm vm(limits);
	const auto type = vm.CreateClass();
	SquirrelValue source = vm.CreateInstance(type);
	const auto source_identity = vm.BorrowObjectIdentity(source);
	vm.SetInstanceReleaseHook(source, [&](SquirrelVm& current, const SquirrelValue& receiver) {
		Require(current.BorrowObjectIdentity(receiver) == source_identity, "native source cleanup must never run with a cloned allocation's identity");
		++releases;
	});
	sgre::script::BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-native-clone-fixture");
	program.MainFunction.Parameters = {std::string("this"), std::string("source")};
	program.MainFunction.StackSize = 3;
	program.MainFunction.Instructions = {Instruction(54, 2, 1), Instruction(19, 1, 2)};
	SquirrelValue cloned;
	{
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), source};
		cloned = vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
	}

	vm.ResetContext(vm.RootContext());
	cloned = {};
	vm.RethrowReleaseFailure();
	Require(releases == 0, "original instance clone264360 initializes an empty native release hook");
	source = {};
	vm.RethrowReleaseFailure();
	Require(releases == 1, "native cleanup must remain attached only to the source instance");
}

int main() {
	try {
		TestOnlyMemberIndexOwnsNamesAndClassOwnsAttributes();
		TestTupleAttributesReleaseBeforeValue();
		TestMemberIndexRetiresBeforeParentClass();
		TestInstanceCloneDoesNotCopyNativeCleanup();
		std::cout << "Squirrel native member ownership checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
