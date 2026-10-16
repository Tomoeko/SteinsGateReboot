#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>

using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static bool LogicalReadRejects(SquirrelVm& vm, std::int32_t index) {
	try {
		static_cast<void>(vm.NativeStackValue(index));
	} catch (const std::runtime_error& error) {
		return std::string(error.what()).find("logical extent") != std::string::npos;
	}

	return false;
}

static SquirrelValue CreateSeed(SquirrelVm& vm, std::span<const SquirrelValue> captures = {}) {
	return vm.CreateNativeClosureWithCaptures("seed_inactive_cells", [](SquirrelVm&, std::span<const SquirrelValue>) {
		return SquirrelNativeResult::NoValues();
	},
	    captures);
}

static SquirrelValue CreateMetadata(SquirrelVm& vm) {
	const std::array<std::uint8_t, 8> payload{61, 0, 0, 0, 0, 0, 0, 0};

	return vm.CreateUserData(payload);
}

static void TestInactiveCoordinate(bool seeded) {
	SquirrelVm vm;
	if (seeded) {
		const std::array<SquirrelValue, 2> captures{19.75F, 3.5F};
		const std::array<SquirrelValue, 1> arguments{std::int32_t{17}};
		static_cast<void>(vm.Invoke(CreateSeed(vm, captures), arguments));
	}

	const std::array<SquirrelValue, 1> captures{CreateMetadata(vm)};
	const auto reader = vm.CreateNativeClosureWithCaptures("allocated_coordinate", [seeded](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && active.NativeStackSize() == 2, "zero-user coordinate call must expose only receiver and actual userdata capture");
		Require(active.Type(active.NativeAllocatedStackValue(2)) == SquirrelValueType::UserData, "native fixed second index must preserve captured userdata type");
		Require(LogicalReadRejects(active, 3), "existing logical stack API must keep rejecting the inactive third cell");
		const auto next = active.NativeAllocatedStackValue(3);
		if (seeded) {
			Require(std::get<float>(next) == 19.75F, "allocated native index three must read a genuine inactive float left by the preceding call");
		} else {
			Require(active.Type(next) == SquirrelValueType::Null, "fresh initialized allocated storage must remain null behind a short native call");
		}

		Require(active.NativeStackSize() == 2, "allocated inspection must not grow or normalize logical native top");

		return SquirrelNativeResult::NoValues();
	},
	    captures);
	const std::array<SquirrelValue, 1> arguments{std::int32_t{17}};
	static_cast<void>(vm.Invoke(reader, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestInactiveRawWeakValue() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue target = CreateMetadata(vm);
	const auto weak = vm.CreateWeakReference(target);
	const std::array<SquirrelValue, 2> seed_captures{weak, 3.5F};
	const std::array<SquirrelValue, 1> seed_arguments{std::int32_t{17}};
	static_cast<void>(vm.Invoke(CreateSeed(vm, seed_captures), seed_arguments));
	target = {};
	Require(vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null, "seeded weak wrapper must not retain its userdata target");
	const std::array<SquirrelValue, 1> captures{CreateMetadata(vm)};
	const auto reader = vm.CreateNativeClosureWithCaptures("allocated_raw_weak", [&weak](SquirrelVm& active, std::span<const SquirrelValue>) {
		const auto cell = active.NativeAllocatedStackValue(3);
		Require(cell == weak && active.Type(cell) == SquirrelValueType::WeakReference, "allocated inspection must return the actual weak wrapper rather than resolve it");
		Require(active.Type(active.ResolveWeakReference(cell)) == SquirrelValueType::Null && active.NativeStackSize() == 2, "raw inactive inspection must preserve weak expiration and native top");

		return SquirrelNativeResult::NoValues();
	},
	    captures);
	const std::array<SquirrelValue, 1> arguments{std::int32_t{17}};
	static_cast<void>(vm.Invoke(reader, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNestedAbsoluteNegativeIndex() {
	SquirrelVm vm;
	const auto child = vm.CreateNativeClosure("nested_allocated_child", [](SquirrelVm& active, std::span<const SquirrelValue>) {
		Require(active.NativeStackSize() == 2 && std::get<std::int32_t>(active.NativeAllocatedStackValue(1)) == 17 && std::get<std::int32_t>(active.NativeAllocatedStackValue(2)) == 34, "positive allocated indices must start at the actual nested native frame base");
		Require(std::get<std::int32_t>(active.NativeAllocatedStackValue(-1)) == 34, "negative allocated indices must start at actual logical native top");
		Require(std::get<std::int32_t>(active.NativeAllocatedStackValue(-4)) == 404, "an allocated negative index before the nested base must inspect the actual retained parent cell");
		Require(active.Type(active.NativeAllocatedStackValue(-3)) == SquirrelValueType::NativeClosure, "allocated cells before the nested base must retain the actual host-call closure placement");
		Require(LogicalReadRejects(active, -4), "logical native API must retain its narrower current-frame extent");

		return SquirrelNativeResult::NoValues();
	});
	const auto parent = vm.CreateNativeClosure("nested_allocated_parent", [&child](SquirrelVm& active, std::span<const SquirrelValue>) {
		const std::array<SquirrelValue, 2> arguments{std::int32_t{17}, std::int32_t{34}};
		static_cast<void>(active.Invoke(child, arguments));
		Require(active.NativeStackSize() == 4 && std::get<std::int32_t>(active.NativeAllocatedStackValue(-1)) == 404, "nested allocated inspection must restore the parent's unchanged native base/top");

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 4> arguments{std::int32_t{101}, std::int32_t{202}, std::int32_t{303}, std::int32_t{404}};
	static_cast<void>(vm.Invoke(parent, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestContextIsolation() {
	SquirrelVm vm;
	const auto child = vm.CreateContext();
	const std::array<SquirrelValue, 2> seed_captures{19.75F, 3.5F};
	const std::array<SquirrelValue, 1> seed_arguments{std::int32_t{17}};
	static_cast<void>(vm.Invoke(CreateSeed(vm, seed_captures), seed_arguments));
	const std::array<SquirrelValue, 1> captures{CreateMetadata(vm)};
	const auto reader = vm.CreateNativeClosureWithCaptures("context_allocated_reader", [&child](SquirrelVm& active, std::span<const SquirrelValue>) {
		const auto third = active.NativeAllocatedStackValue(3);
		if (active.CurrentContext() == child) {
			Require(active.Type(third) == SquirrelValueType::Null, "a new child must inspect its own initialized inactive cells instead of the parent's saved float");
		} else {
			Require(std::get<float>(third) == 19.75F, "root context must retain its own prior inactive cell across child execution");
		}

		return SquirrelNativeResult::NoValues();
	},
	    captures);
	const std::array<SquirrelValue, 1> arguments{std::int32_t{17}};
	static_cast<void>(vm.InvokeInContext(child, reader, arguments));
	static_cast<void>(vm.Invoke(reader, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestInactiveOwnershipIsUnchanged() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::int32_t callbacks = 0;
	SquirrelValue weak;
	SquirrelVm vm(limits);
	SquirrelValue owner = CreateMetadata(vm);
	weak = vm.CreateWeakReference(owner);
	vm.SetUserDataReleaseHook(owner, [&](SquirrelVm&, const SquirrelValue&, std::span<std::uint8_t>) {
		++callbacks;
	});
	std::array<SquirrelValue, 2> seed_captures{owner, 3.5F};
	const std::array<SquirrelValue, 1> seed_arguments{std::int32_t{17}};
	static_cast<void>(vm.Invoke(CreateSeed(vm, seed_captures), seed_arguments));
	seed_captures = {};
	owner = {};
	const std::array<SquirrelValue, 1> captures{CreateMetadata(vm)};
	const auto reader = vm.CreateNativeClosureWithCaptures("inactive_owned_reader", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		Require(active.Type(active.NativeAllocatedStackValue(3)) == SquirrelValueType::UserData, "inactive allocated userdata owner must remain accessible as its actual raw value");
		Require(callbacks == 0 && active.NativeStackSize() == 2 && active.CollectGarbage() == 0, "read-only inspection must preserve inactive-cell ownership and native collector tracing");

		return SquirrelNativeResult::NoValues();
	},
	    captures);
	const std::array<SquirrelValue, 1> arguments{std::int32_t{17}};
	static_cast<void>(vm.Invoke(reader, arguments));
	Require(vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::UserData && callbacks == 0, "returning from allocated inspection must not clear or transfer the hidden owner");
	const auto grow = vm.CreateNativeClosure("retire_inactive_owner", [](SquirrelVm& active, std::span<const SquirrelValue>) {
		active.SetNativeStackSize(3);

		return SquirrelNativeResult::NoValues();
	});
	static_cast<void>(vm.Invoke(grow, arguments));
	vm.RethrowReleaseFailure();
	Require(callbacks == 1 && vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null, "actual native growth must retire the untouched hidden owner exactly once");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestAllocatedBounds() {
	SquirrelVm vm;
	const auto reader = vm.CreateNativeClosure("allocated_bounds", [](SquirrelVm& active, std::span<const SquirrelValue>) {
		Require(active.Type(active.NativeAllocatedStackValue(1023)) == SquirrelValueType::Null, "last initialized allocated cell must remain readable at the one-based native frame offset");
		Require(active.Type(active.NativeAllocatedStackValue(-2)) == SquirrelValueType::NativeClosure, "negative index exactly equal to absolute top must read physical cell zero");
		for (const auto index : {0, 1024, -3, std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::min()}) {
			bool rejected = false;
			try {
				static_cast<void>(active.NativeAllocatedStackValue(index));
			} catch (const std::runtime_error& error) {
				rejected = std::string(error.what()).find("allocated extent") != std::string::npos;
			}

			Require(rejected && active.NativeStackSize() == 1, "invalid native allocated indices must reject before reading or mutating physical storage");
		}

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 1> arguments{std::int32_t{17}};
	static_cast<void>(vm.Invoke(reader, arguments));
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestNoActiveFrame() {
	SquirrelVm vm;
	bool rejected = false;
	try {
		static_cast<void>(vm.NativeAllocatedStackValue(1));
	} catch (const std::runtime_error& error) {
		rejected = std::string(error.what()).find("active native frame") != std::string::npos;
	}

	Require(rejected, "allocated native cell access must require a real active native callback frame");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestInactiveCoordinate(false);
		TestInactiveCoordinate(true);
		TestInactiveRawWeakValue();
		TestNestedAbsoluteNegativeIndex();
		TestContextIsolation();
		TestInactiveOwnershipIsUnchanged();
		TestAllocatedBounds();
		TestNoActiveFrame();
		std::cout << "eight independent allocated native cell fixture groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
