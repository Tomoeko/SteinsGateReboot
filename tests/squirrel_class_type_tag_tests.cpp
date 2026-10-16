#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

static constexpr std::array<std::uint8_t, 4> kTagA{17, 41, 73, 89};
static constexpr std::array<std::uint8_t, 4> kSameBytesAsTagA{17, 41, 73, 89};
static constexpr std::array<std::uint8_t, 4> kTagB{31, 53, 79, 97};
static constexpr std::array<std::uint8_t, 4> kTagC{43, 61, 83, 101};
static constexpr std::array<std::uint8_t, 4> kHostTag{47, 67, 103, 109};

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireRejected(Operation&& operation, const char* message) {
	try {
		std::forward<Operation>(operation)();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error(message);
}

static void TestOwnNullAndLiveAncestry() {
	SquirrelVm vm;
	const auto base = vm.CreateClass();
	Require(vm.ClassTypeTag(base) == nullptr, "new root class must start with a null tag");
	vm.SetClassTypeTag(base, &kTagA);
	const auto child = vm.CreateClass(base);
	const auto grandchild = vm.CreateClass(child);
	Require(vm.ClassTypeTag(child) == nullptr && vm.ClassTypeTag(grandchild) == nullptr, "NewClass must not copy the parent's class tag");
	const auto instance = vm.CreateInstance(grandchild);
	Require(vm.InstanceClassTypeTag(instance) == nullptr, "direct instance tag lookup must not search ancestors");
	Require(vm.InstanceClassHasTypeTag(instance, &kTagA) && !vm.InstanceClassHasTypeTag(instance, &kTagB), "typed instance admission must search actual live ancestors");
	Require(vm.InstanceClassHasTypeTag(instance, nullptr), "a null requested tag must bypass matching for an actual live instance");
	vm.SetClassTypeTag(child, &kTagB);
	vm.SetClassTypeTag(grandchild, &kTagC);
	Require(vm.InstanceClassTypeTag(instance) == &kTagC && vm.InstanceClassHasTypeTag(instance, &kTagA) && vm.InstanceClassHasTypeTag(instance, &kTagB), "all three live class tag identities must remain independently queryable");
	vm.SetClassTypeTag(grandchild, nullptr);
	vm.SetClassTypeTag(base, &kTagC);
	Require(vm.InstanceClassTypeTag(instance) == nullptr && !vm.InstanceClassHasTypeTag(instance, &kTagA) && vm.InstanceClassHasTypeTag(instance, &kTagC), "existing instances must observe ancestor reassignment without a cached tag snapshot");
}

static void TestLockAndDistinctPayloadMetadata() {
	SquirrelVm vm;
	const auto class_value = vm.CreateClass();
	const auto instance = vm.CreateInstance(class_value);
	RequireRejected([&] {
		vm.NewSlot(class_value, std::string("late_field"), std::int32_t{3});
	},
	    "the fixture must exercise an actual locked class");
	vm.SetInstanceHostIdentity(instance, {&kHostTag, 73});
	const auto class_references = vm.RetainedReferenceCount(class_value);
	const auto instance_references = vm.RetainedReferenceCount(instance);
	vm.SetClassTypeTag(class_value, &kTagA);
	Require(vm.InstanceClassHasTypeTag(instance, &kTagA), "tag assignment must remain permitted after the real class locks");
	Require(vm.InstanceHostIdentity(instance, &kHostTag).Value == 73, "a class tag must not replace the independent host payload identity");
	RequireRejected([&] {
		static_cast<void>(vm.InstanceHostIdentity(instance, &kTagA));
	},
	    "class ancestry must not masquerade as native instance payload admission");
	vm.SetClassTypeTag(class_value, &kTagB);
	vm.SetClassTypeTag(class_value, nullptr);
	Require(vm.ClassTypeTag(class_value) == nullptr && vm.InstanceClassTypeTag(instance) == nullptr, "nullable reassignment must preserve the actual own class field");
	Require(vm.RetainedReferenceCount(class_value) == class_references && vm.RetainedReferenceCount(instance) == instance_references, "tag reads/writes must not acquire any VM object root");
	Require(vm.InstanceHostIdentity(instance, &kHostTag).Value == 73, "class tag clearing must leave the host payload untouched");
	const std::array<std::uint8_t, 3> bytes{7, 11, 13};
	const auto userdata = vm.CreateUserData(bytes);
	vm.SetUserDataTypeTag(userdata, &kTagC);
	Require(vm.UserDataTypeTag(userdata) == &kTagC && vm.ClassTypeTag(class_value) == nullptr, "existing userdata tags must remain in their separate storage domain");
}

static void TestCollisionAndCheckedDomains() {
	SquirrelVm vm;
	SquirrelVm foreign;
	const auto first = vm.CreateClass();
	const auto unrelated = vm.CreateClass();
	vm.SetClassTypeTag(first, &kTagA);
	vm.SetClassTypeTag(unrelated, &kTagA);
	const auto instance = vm.CreateInstance(unrelated);
	Require(vm.InstanceClassHasTypeTag(instance, &kTagA) && !vm.IsInstanceOf(instance, first), "native tag matching uses pointer equality, not class object identity");
	Require(kTagA == kSameBytesAsTagA && !vm.InstanceClassHasTypeTag(instance, &kSameBytesAsTagA), "equal descriptor contents must not replace the native opaque identity comparison");
	const auto table = vm.CreateTable();
	const auto foreign_class = foreign.CreateClass();
	RequireRejected([&] {
		vm.SetClassTypeTag(table, &kTagB);
	},
	    "table must not accept a class tag");
	RequireRejected([&] {
		vm.SetClassTypeTag(instance, &kTagB);
	},
	    "instance payload must not accept a class tag");
	RequireRejected([&] {
		vm.SetClassTypeTag(foreign_class, &kTagB);
	},
	    "foreign VM class identity must reject before tag mutation");
	Require(foreign.ClassTypeTag(foreign_class) == nullptr, "foreign rejection must leave the real sibling owner unchanged");
	RequireRejected([&] {
		static_cast<void>(vm.ClassTypeTag(instance));
	},
	    "class-only getter must not silently read an instance class");
	RequireRejected([&] {
		static_cast<void>(vm.InstanceClassTypeTag(first));
	},
	    "instance getter requires an actual instance");
	RequireRejected([&] {
		static_cast<void>(vm.InstanceClassHasTypeTag(table, nullptr));
	},
	    "a null requested tag must not admit a non-instance");
	RequireRejected([&] {
		static_cast<void>(vm.InstanceClassHasTypeTag(std::int32_t{17}, &kTagA));
	},
	    "primitive values must not masquerade as class instances");
	Require(vm.ClassTypeTag(first) == &kTagA && vm.ClassTypeTag(unrelated) == &kTagA, "invalid operations must preserve both actual class owners");
	vm.Close();
	RequireRejected([&] {
		static_cast<void>(vm.ClassTypeTag(first));
	},
	    "closed VM class identities must reject safely");
	RequireRejected([&] {
		vm.SetClassTypeTag(first, &kTagC);
	},
	    "closed VM tag writes must reject safely");
}

static void TestTagDoesNotKeepClassAlive() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue class_value = vm.CreateClass();
	vm.SetClassTypeTag(class_value, &kTagA);
	const auto weak = vm.CreateWeakReference(class_value);
	vm.NewSlot(class_value, std::string("cycle"), class_value);
	class_value = {};
	Require(vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Class, "the fixture must contain a genuine otherwise unrooted class cycle");
	static_cast<void>(vm.CollectGarbage());
	Require(vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null, "opaque tag metadata must not become a collector root or preserve the class cycle");
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestOwnNullAndLiveAncestry();
		TestLockAndDistinctPayloadMetadata();
		TestCollisionAndCheckedDomains();
		TestTagDoesNotKeepClassAlive();
		std::cout << "squirrel class type tag tests passed (4 fixture groups)\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
