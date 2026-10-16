#include "sgre/script/squirrel_vm.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestPayload(std::size_t byte_count, bool collection_first) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelValue weak;
	std::vector<std::uint8_t> expected(byte_count);
	std::int32_t callbacks = 0;
	std::int32_t tag = 0;
	SquirrelVm vm(limits);
	for (std::size_t index = 0; index < expected.size(); ++index) {
		expected[index] = static_cast<std::uint8_t>(index * 7 + 19);
	}

	SquirrelValue userdata = vm.CreateUserData(expected);
	weak = vm.CreateWeakReference(userdata);
	Require(vm.Type(userdata) == SquirrelValueType::UserData && vm.TypeName(userdata) == "userdata", "native userdata must have its genuine runtime type");
	Require(vm.Length(userdata) == byte_count && vm.UserDataBytes(userdata).size() == byte_count, "native userdata size and payload extent must agree");
	Require(vm.UserDataTypeTag(userdata) == nullptr, "new userdata must start without a type tag");
	vm.SetUserDataTypeTag(userdata, &tag);
	Require(vm.UserDataTypeTag(userdata) == &tag, "userdata type tag must preserve opaque borrowed identity");
	vm.SetUserDataReleaseHook(userdata, [&](SquirrelVm& active, const SquirrelValue& receiver, std::span<std::uint8_t> payload) {
		Require(active.Type(active.ResolveWeakReference(weak)) == SquirrelValueType::UserData, "userdata weak self must remain alive during its last-reference payload hook");
		Require(active.Length(receiver) == byte_count && active.UserDataTypeTag(receiver) == &tag, "userdata metadata must remain readable throughout native cleanup");
		Require(std::equal(payload.begin(), payload.end(), expected.begin(), expected.end()), "userdata hook must receive its owned initialized payload unchanged");
		++callbacks;
	});
	if (collection_first) {
		Require(vm.CollectGarbage() == 1 && callbacks == 0, "userdata finalization must preserve its payload/hook while an unrooted external strong owner survives");
		Require(vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::UserData, "collection finalization must not expire a still-owned userdata identity");
	}

	userdata = {};
	vm.RethrowReleaseFailure();
	Require(callbacks == 1 && vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null, "userdata last-reference deletion must invoke one hook before weak-self expiration");
	vm.Close();
	vm.RethrowReleaseFailure();
}

enum class CycleRoot {
	None,
	UserData,
	Table
};

static void TestDelegateCycle(bool userdata_first, CycleRoot root_kind) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelValue userdata_weak;
	SquirrelValue table_weak;
	SquirrelValue key_weak;
	std::int32_t callbacks = 0;
	SquirrelVm vm(limits);
	SquirrelValue table;
	SquirrelValue userdata;
	const std::array<std::uint8_t, 8> payload{73, 0, 0, 0, 0, 0, 0, 0};
	if (!userdata_first) {
		table = vm.CreateTable();
	}

	userdata = vm.CreateUserData(payload);
	if (userdata_first) {
		table = vm.CreateTable();
	}

	userdata_weak = vm.CreateWeakReference(userdata);
	table_weak = vm.CreateWeakReference(table);
	SquirrelValue key = vm.CreateString("independent-userdata-cycle-key");
	key_weak = vm.CreateWeakReference(key);
	vm.SetUserDataDelegate(userdata, table);
	vm.NewSlot(table, key, userdata);
	vm.SetUserDataReleaseHook(userdata, [&](SquirrelVm& active, const SquirrelValue&, std::span<std::uint8_t> bytes) {
		Require(bytes.size() == 8 && bytes[0] == 73, "delegate-cycle cleanup must keep userdata payload intact");
		Require(active.Type(active.ResolveWeakReference(userdata_weak)) == SquirrelValueType::UserData, "cycle cleanup must retain userdata weak self during its actual hook");
		Require(active.Type(active.ResolveWeakReference(key_weak)) == SquirrelValueType::Null, "genuine table key must expire before its userdata value hook");
		const bool table_alive = active.Type(active.ResolveWeakReference(table_weak)) == SquirrelValueType::Table;
		const bool expected_alive = root_kind == CycleRoot::Table || (root_kind == CycleRoot::None && userdata_first);
		Require(table_alive == expected_alive, "userdata delegate retirement must reflect original typed marker/list order and last-release phase");
		++callbacks;
	});
	auto root = root_kind == CycleRoot::None ? sgre::script::SquirrelHostRoot{} : vm.Retain(root_kind == CycleRoot::UserData ? userdata : table);
	key = {};
	table = {};
	userdata = {};
	if (root_kind != CycleRoot::None) {
		Require(vm.CollectGarbage() == 0 && callbacks == 0, "rooted userdata/table cycle must remain traced through the real userdata delegate edge");
		root = {};
	}

	Require(vm.CollectGarbage() == 1, "one native delegate cycle must be deleted through one explicit finalization iteration");
	vm.RethrowReleaseFailure();
	Require(callbacks == 1 && vm.Type(vm.ResolveWeakReference(userdata_weak)) == SquirrelValueType::Null && vm.Type(vm.ResolveWeakReference(table_weak)) == SquirrelValueType::Null, "typed delegate-cycle retirement must expire both owners after one payload hook");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCapturedData(bool finalize_closure_first) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::vector<std::size_t> events;
	std::array<SquirrelValue, 2> weak;
	SquirrelValue closure_weak;
	SquirrelVm vm(limits);
	const std::array<std::uint8_t, 8> first{9, 0, 0, 0, 0, 0, 0, 0};
	const std::array<std::uint8_t, 8> second{23, 0, 0, 0, 0, 0, 0, 0};
	std::array<SquirrelValue, 2> captures{vm.CreateUserData(first), vm.CreateUserData(second)};
	for (std::size_t index = 0; index < captures.size(); ++index) {
		weak[index] = vm.CreateWeakReference(captures[index]);
		vm.SetUserDataReleaseHook(captures[index], [&, index](SquirrelVm& active, const SquirrelValue&, std::span<std::uint8_t> payload) {
			Require(active.Type(active.ResolveWeakReference(weak[index])) == SquirrelValueType::UserData, "captured userdata hook must retain its weak self through payload cleanup");
			Require(active.Type(active.ResolveWeakReference(closure_weak)) == SquirrelValueType::Null, "inactive physical capture ownership must outlive native closure destruction");
			Require(payload[0] == (index == 0 ? 9 : 23), "native capture cleanup must preserve the real stored payload");
			Require(active.NativeStackSize() == (index == 1 ? 2 : 3), "native hidden-cell growth must publish its new top before retiring captured userdata");
			events.push_back(index);
		});
	}

	SquirrelValue closure = vm.CreateNativeClosureWithCaptures("userdata_captures", [](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 3 && std::get<std::int32_t>(arguments[0]) == 17, "actual native userdata captures must follow the original user receiver");
		Require(active.Type(arguments[1]) == SquirrelValueType::UserData && active.UserDataBytes(arguments[1])[0] == 23 && active.UserDataBytes(arguments[2])[0] == 9, "native userdata capture order must reverse original construction pushes");

		return SquirrelNativeResult::NoValues();
	},
	    captures);
	closure_weak = vm.CreateWeakReference(closure);
	captures = {};
	const std::array<SquirrelValue, 1> arguments{std::int32_t{17}};
	static_cast<void>(vm.Invoke(closure, arguments));
	const auto clear_result = vm.CreateNativeClosure("clear_result", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::NoValues();
	});
	const auto clear_root = vm.Retain(clear_result);
	static_cast<void>(vm.Invoke(clear_result, arguments));
	if (finalize_closure_first) {
		Require(vm.CollectGarbage() == 1, "native closure finalization must leave its actual inactive userdata captures marked in physical cells");
	}

	closure = {};
	Require(events.empty() && vm.Type(vm.ResolveWeakReference(weak[0])) == SquirrelValueType::UserData && vm.Type(vm.ResolveWeakReference(weak[1])) == SquirrelValueType::UserData, "closure retirement must not release userdata still owned by actual hidden native cells");
	const auto grow = vm.CreateNativeClosure("overwrite_captured_userdata", [](SquirrelVm& active, std::span<const SquirrelValue>) {
		active.SetNativeStackSize(3);

		return SquirrelNativeResult::NoValues();
	});
	static_cast<void>(vm.Invoke(grow, arguments));
	vm.RethrowReleaseFailure();
	Require(events == std::vector<std::size_t>{1, 0}, "userdata payload hooks must follow ascending physical cells and reversed capture push order");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNativeTypeAndGet() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const std::array<std::uint8_t, 1> payload{71};
	const auto userdata = vm.CreateUserData(payload);
	const auto delegate = vm.CreateTable();
	vm.NewSlot(delegate, std::string("direct"), std::int32_t{73});
	const auto userdata_identity = vm.BorrowObjectIdentity(userdata);
	vm.NewSlot(delegate, std::string("_get"), vm.CreateNativeClosure("userdata_getter", [userdata_identity](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(active.BorrowObjectIdentity(arguments[0]) == userdata_identity && sgre::script::StringText(arguments[1]) == "missing", "userdata getter must receive the original userdata and unresolved key");
		Require(active.UserDataBytes(arguments[0])[0] == 71, "userdata getter must access its actual initialized native wrapper payload");

		return SquirrelNativeResult::ReturnValue(std::int32_t{89});
	}));
	vm.SetUserDataDelegate(userdata, delegate);
	Require(std::get<std::int32_t>(vm.ReadSlot(userdata, std::string("direct"))) == 73 && std::get<std::int32_t>(vm.ReadSlot(userdata, std::string("missing"))) == 89, "userdata general get must search the actual delegate before its _get metamethod");
	vm.NewSlot(delegate, std::string("_set"), vm.CreateNativeClosure("userdata_setter", [](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(sgre::script::StringText(arguments[1]) == "direct", "native userdata setter must receive its original existing delegate key");
		active.UserDataBytes(arguments[0])[0] = static_cast<std::uint8_t>(std::get<std::int32_t>(arguments[2]));

		return SquirrelNativeResult::NoValues();
	}));
	vm.AssignSlot(userdata, std::string("direct"), std::int32_t{89});
	Require(vm.UserDataBytes(userdata)[0] == 89 && std::get<std::int32_t>(vm.GetSlot(delegate, std::string("direct"))) == 73, "userdata assignment must call _set instead of mutating an existing delegate table slot");
	const auto table_default = vm.ReadSlot(vm.CreateTable(), std::string("tostring"));
	Require(vm.ReadSlot(userdata, std::string("tostring")) == table_default, "userdata's real table delegate must expose ordinary table defaults before the userdata's own _get");
	sgre::script::SquirrelNativeSignature signature;
	signature.ParameterCheck = 1;
	signature.TypeCheck = {128};
	const auto typed = vm.CreateNativeClosure("userdata_signature", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::NoValues();
	},
	    signature);
	const std::array<SquirrelValue, 1> arguments{userdata};
	static_cast<void>(vm.Invoke(typed, arguments));
	std::int32_t new_slot_calls = 0;
	vm.NewSlot(delegate, std::string("_newslot"), vm.CreateNativeClosure("forbidden_userdata_newslot", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		++new_slot_calls;

		return SquirrelNativeResult::NoValues();
	}));
	sgre::script::BytecodeProgram new_slot_program;
	new_slot_program.MainFunction.SourceName = std::string("independent-userdata-newslot-fixture");
	new_slot_program.MainFunction.Parameters = {std::string("this"), std::string("key"), std::string("value")};
	new_slot_program.MainFunction.StackSize = 3;
	sgre::script::SquirrelInstruction new_slot;
	new_slot.Opcode = 11;
	new_slot.Argument1 = 0;
	new_slot.Argument2 = 1;
	new_slot.Argument3 = 2;
	new_slot_program.MainFunction.Instructions = {new_slot};
	const std::array<SquirrelValue, 3> new_slot_arguments{userdata, std::string("new"), std::int32_t{99}};
	bool new_slot_rejected = false;
	try {
		static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(new_slot_program)), new_slot_arguments));
	} catch (const sgre::script::SquirrelVmException& error) {
		new_slot_rejected = std::string(error.what()).find("newslot requires a table or class") != std::string::npos;
	}

	Require(new_slot_rejected && new_slot_calls == 0, "native userdata does not support newslot or invoke an invented userdata _newslot hook");
	vm.SetUserDataDelegate(userdata, {});
	bool missing = false;
	try {
		static_cast<void>(vm.ReadSlot(userdata, std::string("tostring")));
	} catch (const sgre::script::SquirrelVmException& error) {
		missing = std::string(error.what()).find("the index doesn't exist") != std::string::npos;
	}

	Require(missing, "userdata must not receive an invented default type delegate");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCapturedUserDataAtFixedIndices(std::size_t user_count) {
	SquirrelVm vm;
	const std::array<std::uint8_t, 8> payload{61, 0, 0, 0, 0, 0, 0, 0};
	const auto userdata = vm.CreateUserData(payload);
	const std::array<SquirrelValue, 1> captures{userdata};
	const auto closure = vm.CreateNativeClosureWithCaptures("userdata_fixed_indices", [user_count](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == user_count + 2 && active.NativeStackSize() == arguments.size(), "unchecked native adapter calls must append actual userdata after the supplied user arguments");
		Require(active.Type(active.NativeStackValue(-1)) == SquirrelValueType::UserData && active.UserDataBytes(arguments.back())[0] == 61, "a native adapter's final capture must remain its actual userdata payload");
		if (user_count == 0) {
			Require(active.Type(active.NativeStackValue(2)) == SquirrelValueType::UserData, "a fixed second native stack index must select captured userdata when the first user parameter is missing");
		} else {
			Require(std::get<float>(active.NativeStackValue(2)) == 3.5F, "a supplied first numeric parameter must stay ahead of the actual userdata capture");
		}

		if (user_count == 1) {
			Require(active.Type(active.NativeStackValue(3)) == SquirrelValueType::UserData, "a fixed third native stack index must select captured userdata when the second user parameter is missing");
		} else if (user_count == 2) {
			Require(std::get<float>(active.NativeStackValue(3)) == 7.25F, "two supplied numeric parameters must retain their native fixed indices");
		}

		return SquirrelNativeResult::NoValues();
	},
	    captures);
	std::vector<SquirrelValue> arguments{std::int32_t{17}};
	if (user_count >= 1) {
		arguments.emplace_back(3.5F);
	}

	if (user_count >= 2) {
		arguments.emplace_back(7.25F);
	}

	static_cast<void>(vm.Invoke(closure, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestRetainedCallbackIdentityExpires() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelValue retained;
	SquirrelValue weak;
	SquirrelVm vm(limits);
	const std::array<std::uint8_t, 1> payload{37};
	SquirrelValue userdata = vm.CreateUserData(payload);
	const auto identity = vm.BorrowObjectIdentity(userdata);
	weak = vm.CreateWeakReference(userdata);
	vm.SetUserDataReleaseHook(userdata, [&](SquirrelVm& active, const SquirrelValue&, std::span<std::uint8_t>) {
		retained = active.ResolveWeakReference(weak);
	});
	userdata = {};
	Require(vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null && !vm.ResolveObjectIdentity(identity), "native userdata deletion must ignore callback self retention and expire its identity");
	bool rejected = false;
	try {
		static_cast<void>(vm.UserDataBytes(retained));
	} catch (const std::runtime_error& error) {
		rejected = std::string(error.what()).find("object handle does not belong") != std::string::npos;
	}

	Require(rejected, "portable retained userdata aliases must reject safely after the native deletion boundary");
	const auto replacement = vm.CreateUserData(payload);
	Require(!vm.ResolveObjectIdentity(identity) && vm.UserDataBytes(replacement)[0] == 37, "retired userdata generation must never retarget a recycled arena allocation");
	retained = {};
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestBudgetAndOwnerValidation() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxUserDataBytes = 4;
	SquirrelVm vm(limits);
	const std::array<std::uint8_t, 3> payload{1, 2, 3};
	const std::array<std::uint8_t, 2> too_large{4, 5};
	SquirrelValue first = vm.CreateUserData(payload);
	bool rejected = false;
	try {
		static_cast<void>(vm.CreateUserData(too_large));
	} catch (const std::runtime_error& error) {
		rejected = std::string(error.what()).find("payload byte limit") != std::string::npos;
	}

	Require(rejected, "userdata must enforce a live aggregate payload budget before arena allocation");
	first = {};
	const auto replacement = vm.CreateUserData(payload);
	Require(vm.UserDataBytes(replacement).size() == payload.size(), "retired userdata bytes must return to the live payload budget");
	SquirrelVm foreign;
	rejected = false;
	try {
		static_cast<void>(foreign.UserDataBytes(replacement));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "userdata payload access must validate its actual VM owner");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestThrowingHookStillCleansDelegate() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelValue weak;
	SquirrelValue delegate_weak;
	SquirrelVm vm(limits);
	const std::array<std::uint8_t, 1> payload{1};
	SquirrelValue userdata = vm.CreateUserData(payload);
	SquirrelValue delegate = vm.CreateTable();
	weak = vm.CreateWeakReference(userdata);
	delegate_weak = vm.CreateWeakReference(delegate);
	vm.SetUserDataDelegate(userdata, delegate);
	vm.SetUserDataReleaseHook(userdata, [](SquirrelVm&, const SquirrelValue&, std::span<std::uint8_t>) {
		throw std::runtime_error("independent-userdata-cleanup-failure");
	});
	delegate = {};
	userdata = {};
	bool recorded = false;
	try {
		vm.RethrowReleaseFailure();
	} catch (const std::runtime_error& error) {
		recorded = std::string(error.what()) == "independent-userdata-cleanup-failure";
	}

	Require(recorded && vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null && vm.Type(vm.ResolveWeakReference(delegate_weak)) == SquirrelValueType::Null, "authored host cleanup errors must be deferred while userdata/delegate deletion still completes");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const auto size : {0U, 1U, 16U, 63U}) {
			TestPayload(size, false);
			TestPayload(size, true);
		}

		for (const auto userdata_first : {false, true}) {
			for (const auto root : {CycleRoot::None, CycleRoot::UserData, CycleRoot::Table}) {
				TestDelegateCycle(userdata_first, root);
			}
		}

		TestCapturedData(false);
		TestCapturedData(true);
		TestNativeTypeAndGet();
		for (const auto user_count : {0U, 1U, 2U}) {
			TestCapturedUserDataAtFixedIndices(user_count);
		}

		TestRetainedCallbackIdentityExpires();
		TestBudgetAndOwnerValidation();
		TestThrowingHookStillCleansDelegate();
		std::cout << "twenty-three independent userdata lifetime and capture fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
