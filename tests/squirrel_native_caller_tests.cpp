#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

using sgre::script::SquirrelNativeResult;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void Reject(const auto& operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("expected native caller admission rejection");
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first, std::int32_t second, std::uint8_t third, std::uint8_t fourth) {
	sgre::script::SquirrelInstruction value;
	value.Opcode = opcode;
	value.Argument0 = first;
	value.Argument1 = second;
	value.Argument2 = third;
	value.Argument3 = fourth;

	return value;
}

static SquirrelValue Caller(SquirrelVm& vm, std::string source, std::string name, std::string called) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.SourceName = std::move(source);
	program.MainFunction.Name = std::move(name);
	program.MainFunction.Parameters = {std::string("this")};
	program.MainFunction.StackSize = 3;
	program.MainFunction.Literals = {std::move(called)};
	program.MainFunction.Instructions = {Instruction(8, 1, 0, 0, 2), Instruction(6, 1, 1, 2, 1), Instruction(19, 1, 1, 0, 0)};

	return vm.CreateClosure(std::move(program));
}

static void TestGenuineNativeCallbackAndExternalRejection() {
	SquirrelVm vm;
	Reject([&] {
		static_cast<void>(vm.NativeCallerMatches("caller.nut", "caller", 2));
	},
	    "live native callback");
	bool non_native_rejected = false;
	vm.SetTraceCallback([&](const sgre::script::SquirrelTraceEvent&) {
		if (!non_native_rejected) {
			Reject([&] {
				static_cast<void>(vm.NativeCallerMatches("caller.nut", "caller", 2));
			},
			    "active native frame");
			non_native_rejected = true;
		}
	});
	std::size_t native_calls = 0;
	vm.RegisterNative("probe", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		++native_calls;
		const auto context = active.CurrentContext();
		Require(arguments.size() == 1 && active.NativeCallerMatches("caller.nut", "caller", 2), "actual native callback must match its unchanged original script returnPC");
		Require(!active.NativeCallerMatches("wrong.nut", "caller", 2) && !active.NativeCallerMatches("caller.nut", "wrong", 2) && !active.NativeCallerMatches("caller.nut", "caller", 1), "wrong source/name/PC must not match another native callback scope");
		Reject([&] {
			static_cast<void>(active.ObserveContext(context));
		},
		    "serialized endpoint");
		Reject([&] {
			static_cast<void>(active.NativeCallerMatches("caller.nut", "caller", 2, 0));
		},
		    "bounded metadata");
		Require(active.CurrentContext() == context && active.NativeStackSize() == 1, "read-only matching must preserve current context and original physical operand extent");

		return SquirrelNativeResult::ReturnValue(std::int32_t{7});
	});
	const auto result = vm.Invoke(Caller(vm, "caller.nut", "caller", "probe"), std::array<SquirrelValue, 1>{vm.RootTable()});
	Require(result.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(result.Value) == 7 && native_calls == 1 && non_native_rejected, "real script/native invocation must preserve exact original output and reject running non-native observation");
	vm.SetTraceCallback({});
	const auto direct = vm.CreateNativeClosure("direct", [](SquirrelVm& active, std::span<const SquirrelValue>) {
		Require(!active.NativeCallerMatches("caller.nut", "caller", 2), "host-invoked genuine native callback without a script caller must not fabricate one");

		return SquirrelNativeResult::NoValues();
	});
	Require(vm.Invoke(direct, {}).State == sgre::script::SquirrelRunState::Completed, "direct native callback must remain usable after metadata-only queries");
	vm.Close();
	Reject([&] {
		static_cast<void>(vm.NativeCallerMatches("caller.nut", "caller", 2));
	},
	    "live native callback");
}

static void TestNestedMetadataBoundDoesNotRetainFrames() {
	SquirrelVm vm;
	std::size_t calls = 0;
	vm.RegisterNative("probe", [&](SquirrelVm& active, std::span<const SquirrelValue>) {
		++calls;
		Require(active.NativeCallerMatches("inner.nut", "inner", 2) && active.NativeCallerMatches("outer.nut", "outer", 2), "bounded current script metadata must include the real outer and inner callers");
		Require(!active.NativeCallerMatches("outer.nut", "outer", 2, 1) && !active.NativeCallerMatches("inner.nut", "inner", 2, 1), "a truncated stack domain must fail closed even if its newest frame would match");

		return SquirrelNativeResult::ReturnValue(true);
	});
	vm.NewSlot(vm.RootTable(), std::string("inner"), Caller(vm, "inner.nut", "inner", "probe"));
	const auto result = vm.Invoke(Caller(vm, "outer.nut", "outer", "inner"), std::array<SquirrelValue, 1>{vm.RootTable()});
	Require(result.State == sgre::script::SquirrelRunState::Completed && std::get<bool>(result.Value) && calls == 1, "native metadata query must not add invocations or alter nested call completion");
	const auto observation = vm.ObserveContext(vm.RootContext());
	Require(observation.FrameCount == 0 && observation.NativeFrameCount == 0, "metadata matcher must not retain a completed native or script frame");
}

int main() {
	try {
		TestGenuineNativeCallbackAndExternalRejection();
		TestNestedMetadataBoundDoesNotRetainFrames();
		std::cout << "Squirrel native caller metadata: two groups passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
