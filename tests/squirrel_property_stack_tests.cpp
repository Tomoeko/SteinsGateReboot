#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelPropertyReadKind;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void TestActualRawAndTypedReads() {
	SquirrelVm vm;
	const auto root = vm.RootContext();
	const auto table = vm.CreateTable();
	vm.NewSlot(table, std::string("nil"), {});
	vm.NewSlot(table, std::string("fraction"), 0.75f);
	vm.NewSlot(table, std::string("negative"), -2.75f);
	vm.NewSlot(table, std::string("boolean"), true);
	vm.NewSlot(table, std::string("string"), std::string("owned\0tail", 10));
	vm.NewSlot(table, std::string("object"), vm.CreateTable());
	Require(vm.ReadPropertyInContext(root, table, "nil", SquirrelPropertyReadKind::RawPresence).Present, "present null must remain present after its real result pop");
	Require(!vm.ReadPropertyInContext(root, table, "missing", SquirrelPropertyReadKind::RawPresence).Present, "raw missing lookup must return false");
	Require(!vm.ReadPropertyInContext(root, table, "fraction", SquirrelPropertyReadKind::Boolean).Boolean && vm.ReadPropertyInContext(root, table, "negative", SquirrelPropertyReadKind::Boolean).Boolean, "boolean conversion uses native integer truncation rather than float truthiness");
	Require(vm.ReadPropertyInContext(root, table, "negative", SquirrelPropertyReadKind::Integer).Integer == -2 && vm.ReadPropertyInContext(root, table, "boolean", SquirrelPropertyReadKind::Number).Number == 1, "native integer and numeric conversions remain distinct");
	Require(vm.ReadPropertyInContext(root, table, "string", SquirrelPropertyReadKind::String).String == std::string("owned\0tail", 10), "the copied string preserves bytes before later property-specific C-string conversion");
	Require(vm.ReadPropertyInContext(root, table, "object", SquirrelPropertyReadKind::Integer).Integer == 0 && vm.ReadPropertyInContext(root, table, "object", SquirrelPropertyReadKind::String).String.empty(), "unsupported converter types retain initialized zero/empty without ToString");
	vm.SetLastError(std::string("unchanged"));
	static_cast<void>(vm.ReadPropertyInContext(root, table, "missing", SquirrelPropertyReadKind::RawPresence));
	Require(sgre::script::StringText(vm.LastError()) == "unchanged", "a raw miss must preserve LastError");
	const auto failed = vm.ReadPropertyInContext(root, table, "missing", SquirrelPropertyReadKind::Integer);
	Require(!failed.Present && failed.Integer == 0 && sgre::script::StringText(vm.LastError()) == "the index doesn't exist", "general miss pops its key and replaces LastError with the native index failure");
	vm.NewSlot(vm.RootTable(), std::string("root-only"), std::int32_t{71});
	Require(!vm.ReadPropertyInContext(root, table, "root-only", SquirrelPropertyReadKind::Integer).Present, "native property reads never retry the context root");
	Require(!vm.HasSlot(table, std::string("len")), "bare fixture VM has not installed a default delegate");
	vm.InstallRecoveredBaseLibrary();
	const auto default_length = vm.ReadSlotInContext(root, table, std::string("len"));
	Require(vm.Type(default_length) == SquirrelValueType::NativeClosure && vm.Invoke(default_length, std::array<SquirrelValue, 1>{table}).Value == SquirrelValue{std::int32_t{6}}, "the installed actual table default delegate must return the real own-member count");
	Require(!vm.ReadPropertyInContext(root, table, "len", SquirrelPropertyReadKind::RawPresence).Present && vm.ReadPropertyInContext(root, table, "len", SquirrelPropertyReadKind::Integer).Present, "raw presence excludes the separately installed default delegate");
}

static void TestDelegateAndInstanceMembership() {
	SquirrelVm vm;
	const auto delegate = vm.CreateTable();
	vm.NewSlot(delegate, std::string("shared"), std::int32_t{19});
	const auto userdata = vm.CreateUserData({});
	vm.SetUserDataDelegate(userdata, delegate);
	Require(vm.ReadPropertyInContext(vm.RootContext(), userdata, "shared", SquirrelPropertyReadKind::RawPresence).Present && vm.ReadPropertyInContext(vm.RootContext(), userdata, "shared", SquirrelPropertyReadKind::Integer).Integer == 19, "raw userdata lookup follows its actual table delegate before typed general reading");
	std::uint32_t getters = 0;
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("ordinary"), std::int32_t{37});
	vm.NewSlot(class_value, std::string("_get"), vm.CreateNativeClosure("virtual-property", [&](SquirrelVm&, auto) {
		++getters;
		return SquirrelNativeResult::ReturnValue(std::int32_t{53});
	}));
	const auto instance = vm.CreateInstance(class_value);
	Require(vm.ReadPropertyInContext(vm.RootContext(), class_value, "ordinary", SquirrelPropertyReadKind::Integer).Integer == 37 && vm.ReadPropertyInContext(vm.RootContext(), instance, "ordinary", SquirrelPropertyReadKind::Integer).Integer == 37, "real Class and Instance encoded member indices resolve through their owned tuples");
	Require(!vm.ReadPropertyInContext(vm.RootContext(), instance, "virtual", SquirrelPropertyReadKind::RawPresence).Present && getters == 0, "raw instance probing cannot invoke its virtual getter");
	Require(vm.ReadPropertyInContext(vm.RootContext(), instance, "virtual", SquirrelPropertyReadKind::Integer).Integer == 53 && getters == 1, "the separate general read invokes the actual instance getter once");
	Require(!vm.ReadPropertyInContext(vm.RootContext(), class_value, "virtual", SquirrelPropertyReadKind::Integer).Present && getters == 1, "a Class does not use the Instance virtual-property fallback");
	const auto failing_class = vm.CreateClass();
	vm.NewSlot(failing_class, std::string("_get"), vm.CreateNativeClosure("failing-virtual-property", [](SquirrelVm&, auto) -> SquirrelNativeResult {
		throw std::runtime_error("original getter failure");
	}));
	const auto failed = vm.ReadPropertyInContext(vm.RootContext(), vm.CreateInstance(failing_class), "virtual", SquirrelPropertyReadKind::Integer);
	Require(!failed.Present && failed.Integer == 0 && sgre::script::StringText(vm.LastError()) == "the index doesn't exist", "failed MetaCall must unwind before native222a80 replaces the failure and supplies typed zero");
	const auto weak = vm.CreateWeakReference(instance);
	Require(vm.ReadPropertyInContext(vm.RootContext(), weak, "ordinary", SquirrelPropertyReadKind::Integer).Integer == 37, "ObjectInfo push resolves the supplied actual weak receiver");
}

static void TestPhysicalAliasSurvivesActualStackGrowth() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("_get"), vm.CreateNativeClosure("aliased-key-stack-growth", [&](SquirrelVm& active, auto arguments) {
		Require(arguments.size() == 2 && sgre::script::StringText(active.NativeAllocatedStackValue(-3)) == "grow", "native MetaCall must see the real outer key cell immediately below its two actual arguments");
		for (std::uint32_t index = 0; index < 32768; ++index) {
			active.PushNativeValue(static_cast<std::int32_t>(index));
		}
		return SquirrelNativeResult::ReturnValue(std::int32_t{89});
	}));
	const auto instance = vm.CreateInstance(class_value);
	Require(vm.ReadPropertyInContext(vm.RootContext(), instance, "grow", SquirrelPropertyReadKind::Integer).Integer == 89, "physical output must be re-resolved by index after actual operand vector relocation");
	Require(vm.ObserveContext(vm.RootContext()).NativeFrameCount == 0 && vm.ContextState(vm.RootContext()) == sgre::script::SquirrelContextState::Idle, "the scratch getter restores its actual frames and receiving context");
}

static void TestResultRetiresBeforeReceiverAndCallerRestores() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto root = vm.RootContext();
	const auto child = vm.CreateContext();
	const auto child_table = vm.CreateTable();
	vm.SetRootTableInContext(child, child_table);
	const auto class_value = vm.CreateClass();
	std::uint32_t released = 0;
	SquirrelValue produced_weak;
	SquirrelValue instance;
	vm.NewSlot(class_value, std::string("_get"), vm.CreateNativeClosure("actual-transient-result", [&](SquirrelVm& active, auto) {
		Require(active.CurrentContext() == root, "the selected property receiving context must own its actual getter");
		auto result = active.CreateInstance(active.CreateClass());
		produced_weak = active.CreateWeakReference(result);
		active.SetInstanceReleaseHook(result, [&](SquirrelVm& retiring, const SquirrelValue&) {
			++released;
			Require(retiring.CurrentContext() == root, "typed output retirement must run on its receiving context");
			const auto observe = retiring.CreateNativeClosure("observe-outer-receiver-after-result-pop", [&](SquirrelVm& observing, auto) {
				// Host Invoke adds its callable and one argument after the still-live outer receiver.
				Require(observing.NativeAllocatedStackValue(-3) == instance, "the key/result cell retires while the actual receiver remains on its physical stack");
				return SquirrelNativeResult::NoValues();
			});
			const std::array<SquirrelValue, 1> arguments{retiring.RootTable()};
			static_cast<void>(retiring.Invoke(observe, arguments));
		});
		return SquirrelNativeResult::ReturnValue(std::move(result));
	}));
	instance = vm.CreateInstance(class_value);
	const auto call = vm.CreateNativeClosure("child-property-caller", [&](SquirrelVm& active, auto) {
		Require(active.CurrentContext() == child, "fixture must enter its real child native frame");
		const auto result = active.ReadPropertyInContext(root, instance, "temporary", SquirrelPropertyReadKind::Integer);
		Require(result.Integer == 0 && result.Present && released == 1 && active.Type(active.ResolveWeakReference(produced_weak)) == SquirrelValueType::Null, "no VM result owner may escape conversion and the exact physical pop");
		Require(active.CurrentContext() == child && active.RootTable() == child_table, "base-context property work must restore the actual child caller");
		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 1> arguments{child_table};
	static_cast<void>(vm.InvokeInContext(child, call, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestInvalidGetterStatusRemainsSafetyFailure() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	vm.NewSlot(class_value, std::string("_get"), vm.CreateNativeClosure("invalid-status-suspension", [](SquirrelVm&, const auto) {
		auto result = SquirrelNativeResult::StackStatus(-1);
		result.Suspend = true;

		return result;
	}));
	const auto instance = vm.CreateInstance(class_value);
	bool failed = false;
	try {
		static_cast<void>(vm.FindPropertyInContext(vm.RootContext(), instance, "invalid"));
	} catch (const sgre::script::SquirrelVmException&) {
		throw std::runtime_error("invalid callback state must not become ignored native missing status");
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()) == "Squirrel explicit native status cannot use compatibility suspension", "invalid callback state must retain its separate safety diagnostic");
		failed = true;
	}

	Require(failed && vm.ObserveContext(vm.RootContext()).NativeFrameCount == 0 && vm.ContextState(vm.RootContext()) == sgre::script::SquirrelContextState::Idle, "native status recovery must preserve callback safety validation and unwind physical frames");
}

static void TestOwnedPropertyResultAndMissingStatus() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto root = vm.RootContext();
	const auto child = vm.CreateContext();
	const auto table = vm.CreateTable();
	vm.NewSlot(table, std::string("nil"), SquirrelValue{});
	const auto nil = vm.FindPropertyInContext(root, table, "nil");
	Require(nil.has_value() && vm.Type(*nil) == SquirrelValueType::Null, "owned native get must distinguish a present null from failed lookup");
	vm.SetLastError(std::string("before-miss"));
	Require(!vm.FindPropertyInContext(root, table, "missing") && sgre::script::StringText(vm.LastError()) == "the index doesn't exist", "a native get miss must return status and retain the original index diagnostic");
	const std::string binary_key("key\0tail", 8);
	vm.NewSlot(table, binary_key, std::int32_t{67});
	Require(vm.FindPropertyInContext(root, table, binary_key) == std::optional<SquirrelValue>{std::int32_t{67}}, "general property API must preserve key bytes; C-string truncation belongs to its original native caller");

	const auto class_value = vm.CreateClass();
	std::size_t released = 0;
	SquirrelValue produced_weak;
	vm.NewSlot(class_value, std::string("_get"), vm.CreateNativeClosure("owned-property-output", [&](SquirrelVm& active, const auto) {
		Require(active.CurrentContext() == root, "owned general get must execute in the selected receiving context");
		auto result = active.CreateInstance(active.CreateClass());
		produced_weak = active.CreateWeakReference(result);
		active.SetInstanceReleaseHook(result, [&](SquirrelVm&, const SquirrelValue&) {
			++released;
		});

		return SquirrelNativeResult::ReturnValue(std::move(result));
	}));
	const auto instance = vm.CreateInstance(class_value);
	const auto caller = vm.CreateNativeClosure("child-owned-property-caller", [&](SquirrelVm& active, const auto) {
		auto result = active.FindPropertyInContext(root, instance, "temporary");
		Require(result.has_value() && active.Type(*result) == SquirrelValueType::Instance && released == 0 && active.CurrentContext() == child, "owned result must survive its physical result/receiver pops and restore the actual child caller");
		result.reset();
		Require(released == 1 && active.Type(active.ResolveWeakReference(produced_weak)) == SquirrelValueType::Null, "only the escaped owner may keep a transient property result alive");

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTableInContext(child)};
	static_cast<void>(vm.InvokeInContext(child, caller, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestActualRawAndTypedReads();
		TestDelegateAndInstanceMembership();
		TestPhysicalAliasSurvivesActualStackGrowth();
		TestResultRetiresBeforeReceiverAndCallerRestores();
		TestOwnedPropertyResultAndMissingStatus();
		TestInvalidGetterStatusRemainsSafetyFailure();
		std::cout << "Squirrel native property-stack checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
