#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
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

enum class Binding {
	Unbound,
	Live,
	Expired
};

static void TestCaptureOverwritePrecedesReceiverBinding(Binding binding) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	SquirrelValue previous = vm.CreateInstance(vm.CreateClass());
	std::vector<std::int32_t> events;
	vm.SetInstanceReleaseHook(previous, [&](SquirrelVm& active, const SquirrelValue&) {
		Require(active.NativeStackSize() == 2 && active.Type(active.NativeStackValue(1)) == SquirrelValueType::Integer && std::get<std::int32_t>(active.NativeStackValue(1)) == 17, "capture cell release must observe the original receiver before weak binding");
		Require(std::get<std::int32_t>(active.NativeStackValue(-1)) == 23, "capture replacement must be installed before the old cell release callback");
		events.push_back(1);
		active.SetNativeStackSize(0);
		active.PushNativeValue(std::int32_t{99});
		active.PushNativeValue(std::int32_t{23});
		events.push_back(2);
	});
	const auto populate = vm.CreateNativeClosure("populate_hidden_capture_cell", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		active.PushNativeValue(previous);

		return SquirrelNativeResult::NoValues();
	});
	const std::array<SquirrelValue, 1> root_arguments{vm.RootTable()};
	static_cast<void>(vm.Invoke(populate, root_arguments));
	previous = {};
	SquirrelValue environment = vm.CreateTable();
	const auto environment_identity = vm.BorrowObjectIdentity(environment);
	const std::array<SquirrelValue, 1> pushed{std::int32_t{23}};
	SquirrelValue closure = vm.CreateNativeClosureWithCaptures("capture_receiver", [&, binding](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(events == std::vector<std::int32_t>{1, 2}, "capture overwrite callback must finish before native invocation");
		Require(arguments.size() == 2 && active.NativeStackSize() == 2 && std::get<std::int32_t>(arguments[1]) == 23, "native callback must expose its current capture cell after reentry");
		Require(arguments[0] == active.NativeStackValue(1), "native callback argument view must snapshot the current receiver cell after reentry");
		if (binding == Binding::Unbound) {
			Require(std::get<std::int32_t>(arguments[0]) == 99, "unbound native invocation must preserve receiver mutation from capture overwrite");
		} else if (binding == Binding::Live) {
			const auto expected = active.ResolveObjectIdentity(environment_identity);
			Require(expected && arguments[0] == *expected, "live weak receiver binding must replace the capture callback's mutation");
		} else {
			Require(active.Type(arguments[0]) == SquirrelValueType::Null && !active.ResolveObjectIdentity(environment_identity), "expired weak receiver binding must replace the mutation with actual null");
		}

		events.push_back(3);

		return SquirrelNativeResult::StackStatus(1);
	},
	    pushed);
	if (binding != Binding::Unbound) {
		closure = vm.BindEnvironment(closure, environment);
	}

	if (binding == Binding::Expired) {
		environment = {};
	}

	const std::array<SquirrelValue, 1> arguments{std::int32_t{17}};
	Require(std::get<std::int32_t>(vm.Invoke(closure, arguments).Value) == 23, "native result handling must consume the post-reentry physical last capture");
	vm.RethrowReleaseFailure();
	Require(events == std::vector<std::int32_t>{1, 2, 3}, "capture, mutation and invocation must execute in recovered native order");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const auto binding : {Binding::Unbound, Binding::Live, Binding::Expired}) {
			TestCaptureOverwritePrecedesReceiverBinding(binding);
		}

		std::cout << "three independent capture receiver reentry fixtures pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
