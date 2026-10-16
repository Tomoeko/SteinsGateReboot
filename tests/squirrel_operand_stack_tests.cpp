#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using sgre::script::BytecodeProgram;
using sgre::script::SquirrelInstruction;
using sgre::script::SquirrelValue;
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

static BytecodeProgram Program(std::uint32_t count, std::vector<SquirrelInstruction> instructions) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-physical-stack-fixture");
	program.MainFunction.Name = std::string("main");
	program.MainFunction.Parameters = {std::string("this")};
	program.MainFunction.StackSize = count;
	program.MainFunction.Instructions = std::move(instructions);

	return program;
}

static SquirrelValue Run(SquirrelVm& vm, BytecodeProgram program) {
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};

	return vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
}

static void TestChildAliasesCallerCellsAndKeepsUninitializedValues(std::uint32_t child_size, std::uint8_t returned_register) {
	SquirrelVm vm;
	auto program = Program(8, {Instruction(48, 1), Instruction(2, 2, 102), Instruction(2, 3, 103), Instruction(2, 4, 104), Instruction(6, 6, 1, 2, 2), Instruction(19, 1, returned_register)});
	sgre::script::SquirrelFunction child;
	child.SourceName = std::string("independent-physical-stack-fixture");
	child.Name = std::string("child");
	child.Parameters = {std::string("this"), std::string("value")};
	child.StackSize = child_size;
	child.Instructions = {Instruction(2, 1, 999), Instruction(19, 1, 2)};
	program.MainFunction.Functions.push_back(std::move(child));
	Require(std::get<std::int32_t>(Run(vm, std::move(program))) == (returned_register == 3 ? 999 : 104), "supplied StartCall offset must alias the caller argument cells and preserve the existing nonparameter child register");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestBoundNativeCallReplacesPhysicalCallerReceiver() {
	SquirrelVm vm;
	const auto environment = vm.CreateTable();
	const auto callback = vm.CreateNativeClosure("bound-stack-callback", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 1 && arguments[0] == environment, "native callback must receive its resolved weak environment in argument zero");

		return sgre::script::SquirrelNativeResult{};
	});
	vm.NewSlot(vm.RootTable(), std::string("bound"), vm.BindEnvironment(callback, environment));
	auto program = Program(4, {Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 2)});
	program.MainFunction.Literals = {std::string("bound")};
	Require(Run(vm, std::move(program)) == environment, "native weak-environment assignment must modify the caller's actual receiver cell");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCallerOwnsChildTemporaryUntilCallerOverwrite() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto type = vm.CreateClass();
	std::size_t last_instruction = 0;
	std::string last_function;
	std::vector<std::size_t> releases;
	vm.SetTraceCallback([&](const sgre::script::SquirrelTraceEvent& event) {
		last_instruction = event.Instruction;
		last_function = event.Function;
	});
	vm.NewSlot(vm.RootTable(), std::string("factory"), vm.CreateNativeClosure("independent-owner-factory", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		sgre::script::SquirrelNativeResult result;
		result.Value = current.CreateInstance(type);
		current.SetInstanceReleaseHook(result.Value, [&](SquirrelVm& during_release, const SquirrelValue&) {
			Require(last_function == "main" && last_instruction == 7, "short child return must retain its copied temporary inside the caller frame until the caller overwrites that cell");
			releases.push_back(last_instruction);
			const auto nested = Program(700, {Instruction(2, 200, 777), Instruction(19, 1, 200)});
			Require(std::get<std::int32_t>(Run(during_release, nested)) == 777, "nested release invocation must grow physical storage while preserving the surrounding offset-based register view");
		});

		return result;
	}));
	auto program = Program(8, {Instruction(8, 1, 0, 0, 2), Instruction(6, 4, 1, 2, 1), Instruction(48, 1), Instruction(10, 2, 0), Instruction(2, 3, 103), Instruction(6, 6, 1, 2, 3), Instruction(2, 7, 17), Instruction(20, 5, 1), Instruction(19, 1, 6)});
	program.MainFunction.Literals = {std::string("factory")};
	sgre::script::SquirrelFunction child;
	child.SourceName = std::string("independent-physical-stack-fixture");
	child.Name = std::string("child");
	child.Parameters = {std::string("this"), std::string("value"), std::string("owner")};
	child.StackSize = 4;
	child.Instructions = {Instruction(10, 3, 2), Instruction(20, 2, 1), Instruction(2, 1, 999), Instruction(19, 1, 1)};
	program.MainFunction.Functions.push_back(std::move(child));
	Require(std::get<std::int32_t>(Run(vm, std::move(program))) == 999, "nested physical stack growth must preserve the return destination in the surrounding caller");
	vm.RethrowReleaseFailure();
	Require(releases == std::vector<std::size_t>{7}, "physical caller-owned temporary must release exactly once at its actual overwrite");
	vm.SetTraceCallback({});
	vm.Close();
	vm.RethrowReleaseFailure();
}

static SquirrelValue ResumeOnce(SquirrelVm& vm, const SquirrelValue& generator) {
	auto program = Program(3, {Instruction(50, 2, 1), Instruction(19, 1, 2)});
	program.MainFunction.Parameters.push_back(std::string("generator"));
	const std::array<SquirrelValue, 2> arguments{vm.RootTable(), generator};

	return vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
}

static void TestGeneratorResumeRetainsNonzeroSavedCellsUntilKill() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto type = vm.CreateClass();
	SquirrelValue owner = vm.CreateInstance(type);
	const auto weak = vm.CreateWeakReference(owner);
	std::size_t observations = 0;
	vm.NewSlot(vm.RootTable(), std::string("inspect"), vm.CreateNativeClosure("independent-generator-snapshot-observer", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		Require(!std::holds_alternative<std::monostate>(current.ResolveWeakReference(weak)), "native resume retains saved registers one and two after both corresponding active registers are overwritten");
		Require(current.CollectGarbage() == 0 && !std::holds_alternative<std::monostate>(current.ResolveWeakReference(weak)), "the still-retained saved registers must mark their owners while the generator is actively executing");
		++observations;

		return sgre::script::SquirrelNativeResult{};
	}));
	auto program = Program(4, {Instruction(10, 2, 1), Instruction(49, 0, 255), Instruction(20, 1, 2), Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 255)});
	program.MainFunction.Parameters.push_back(std::string("owner"));
	program.MainFunction.IsGenerator = true;
	program.MainFunction.Literals = {std::string("inspect")};
	SquirrelValue generator;
	{
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), owner};
		generator = vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
	}

	owner = {};
	vm.ResetExecution();
	static_cast<void>(ResumeOnce(vm, generator));
	Require(!std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "first yield must own the two copied saved cells");
	static_cast<void>(ResumeOnce(vm, generator));
	Require(observations == 1 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "Kill must finally release the retained saved values after the active registers were cleared");
	generator = {};
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestDeadGeneratorKeepsSavedCallClosureUntilActualDeletion() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto type = vm.CreateClass();
	SquirrelValue owner = vm.CreateInstance(type);
	const auto owner_weak = vm.CreateWeakReference(owner);
	SquirrelValue generator_weak;
	std::size_t releases = 0;
	vm.SetInstanceReleaseHook(owner, [&](SquirrelVm& current, const SquirrelValue&) {
		Require(!std::holds_alternative<std::monostate>(current.ResolveWeakReference(generator_weak)), "saved CallInfo closure must release its captured owner before the generator weak identity expires");
		++releases;
	});
	auto creator = Program(3, {Instruction(48, 2), Instruction(19, 1, 2)});
	creator.MainFunction.Parameters.push_back(std::string("owner"));
	sgre::script::SquirrelFunction child;
	child.SourceName = std::string("independent-physical-stack-fixture");
	child.Name = std::string("capturing_generator");
	child.Parameters = {std::string("this")};
	child.StackSize = 1;
	child.IsGenerator = true;
	child.Instructions = {Instruction(19, 255)};
	sgre::script::SquirrelOuterBinding outer;
	outer.Type = 0;
	outer.Source = std::int32_t{1};
	outer.Name = std::string("owner");
	child.OuterBindings.push_back(outer);
	creator.MainFunction.Functions.push_back(child);
	SquirrelValue callable;
	{
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), owner};
		callable = vm.Invoke(vm.CreateClosure(std::move(creator)), arguments).Value;
	}

	owner = {};
	SquirrelValue generator;
	{
		const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
		generator = vm.Invoke(callable, arguments).Value;
	}

	callable = {};
	vm.ResetExecution();
	generator_weak = vm.CreateWeakReference(generator);
	static_cast<void>(ResumeOnce(vm, generator));
	Require(releases == 0 && !std::holds_alternative<std::monostate>(vm.ResolveWeakReference(owner_weak)), "dead generator Kill drops its own closure while retaining the separate saved CallInfo closure");
	vm.ResetExecution();
	generator = {};
	vm.RethrowReleaseFailure();
	Require(releases == 1 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(owner_weak)) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(generator_weak)), "actual generator deletion must retire the saved closure and captured owner before clearing generator weak identity");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestRejectedNestedFramePopsItsHostArguments() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	limits.MaxRegisters = 8;
	SquirrelVm vm(limits);
	const auto type = vm.CreateClass();
	SquirrelValue weak;
	std::size_t observations = 0;
	vm.NewSlot(vm.RootTable(), std::string("attempt"), vm.CreateNativeClosure("independent-rejected-frame", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		{
			SquirrelValue owner = current.CreateInstance(type);
			weak = current.CreateWeakReference(owner);
			auto program = Program(2, {Instruction(19, 255)});
			program.MainFunction.Parameters.push_back(std::string("owner"));
			const std::array<SquirrelValue, 2> arguments{current.RootTable(), owner};
			bool rejected = false;
			try {
				static_cast<void>(current.Invoke(current.CreateClosure(std::move(program)), arguments));
			} catch (const sgre::script::SquirrelVmException& error) {
				Require(std::string(error.what()).find("aggregate register limit") != std::string::npos, "independent nested frame must fail at its exact bounded setup guard");
				rejected = true;
			}

			Require(rejected, "nested frame must exceed the existing aggregate register bound");
		}

		Require(std::holds_alternative<std::monostate>(current.ResolveWeakReference(weak)), "failed host frame setup must pop the exact appended argument cells before returning to its native caller");
		++observations;

		return sgre::script::SquirrelNativeResult{};
	}));
	auto program = Program(8, {Instruction(8, 1, 0, 0, 2), Instruction(6, 7, 1, 2, 1), Instruction(19, 255)});
	program.MainFunction.Literals = {std::string("attempt")};
	static_cast<void>(Run(vm, std::move(program)));
	Require(observations == 1, "rejected nested setup must recover into its surrounding ordinary call");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestShortFailureRetainsHigherCallerCellsUntilOverwrite() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	const auto type = vm.CreateClass();
	vm.NewSlot(vm.RootTable(), std::string("owner_type"), type);
	SquirrelValue weak;
	vm.NewSlot(vm.RootTable(), std::string("factory"), vm.CreateNativeClosure("independent-unwind-owner", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		sgre::script::SquirrelNativeResult result;
		result.Value = current.CreateInstance(type);
		weak = current.CreateWeakReference(result.Value);

		return result;
	}));
	auto program = Program(16, {Instruction(8, 1, 0, 0, 2), Instruction(6, 15, 1, 2, 1), Instruction(48, 1), Instruction(10, 2, 0), Instruction(6, 3, 1, 2, 1), Instruction(19, 255)});
	program.MainFunction.Literals = {std::string("factory")};
	sgre::script::SquirrelFunction child;
	child.SourceName = std::string("independent-physical-stack-fixture");
	child.Name = std::string("short_failure");
	child.Parameters = {std::string("this")};
	child.StackSize = 4;
	child.Instructions = {Instruction(2, 1, 91), Instruction(58, 1)};
	program.MainFunction.Functions.push_back(std::move(child));
	bool failed = false;
	try {
		static_cast<void>(Run(vm, std::move(program)));
	} catch (const sgre::script::SquirrelVmException& error) {
		Require(std::get<std::int32_t>(error.ThrownValue()) == 91, "short-frame unwinding must preserve the exact typed thrown value");
		failed = true;
	}

	Require(failed && std::get<std::int32_t>(vm.LastError()) == 91, "original failure and last-error identity must survive record retirement");
	Require(!std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "unwind must clear only from the captured short failing top, leaving the higher caller cell physically retained");
	Require(vm.CollectGarbage() == 0 && !std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "all allocated stack cells must remain native mark roots even when they lie above the restored logical top");
	static_cast<void>(Run(vm, Program(16, {Instruction(19, 255)})));
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "a later actual ordinary return extent must overwrite the previously retained inactive caller cell");
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestSecondYieldKeepsActiveMetadataDuringNestedOwnerRelease() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	vm.InstallRecoveredBaseLibrary();
	const auto type = vm.CreateClass();
	SquirrelValue owner = vm.CreateInstance(type);
	SquirrelValue generator;
	std::size_t callbacks = 0;
	vm.SetInstanceReleaseHook(owner, [&](SquirrelVm& current, const SquirrelValue&) {
		const std::array<SquirrelValue, 1> receiver{generator};
		Require(sgre::script::StringText(current.Invoke(current.GetSlot(generator, std::string("getstatus")), receiver).Value) == "running", "native Yield retires overwritten saved owners while its generator is still active");
		Require(std::get<std::int32_t>(Run(current, Program(700, {Instruction(2, 200, 777), Instruction(19, 1, 200)}))) == 777, "saved-register overwrite callbacks must safely grow the surrounding physical stack");
		++callbacks;
	});
	auto program = Program(4, {Instruction(10, 2, 1), Instruction(49, 0, 255), Instruction(20, 1, 2), Instruction(49, 0, 255), Instruction(19, 255)});
	program.MainFunction.Parameters.push_back(std::string("owner"));
	program.MainFunction.IsGenerator = true;
	{
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), owner};
		generator = vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
	}

	owner = {};
	vm.ResetExecution();
	static_cast<void>(ResumeOnce(vm, generator));
	Require(callbacks == 0, "first yield must retain the two saved owners");
	static_cast<void>(ResumeOnce(vm, generator));
	vm.RethrowReleaseFailure();
	Require(callbacks == 1, "next yield must overwrite both old saved owners and finish their nested release before becoming suspended");
	static_cast<void>(ResumeOnce(vm, generator));
	generator = {};
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestGeneratorTrapExtentFailurePreservesItsSavedFrame() {
	SquirrelVm vm;
	auto program = Program(4, {Instruction(56, 3, 4), Instruction(49, 0, 255), Instruction(2, 1, 91), Instruction(58, 1), Instruction(57, 1), Instruction(19, 1, 3)});
	program.MainFunction.IsGenerator = true;
	SquirrelValue generator;
	{
		const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
		generator = vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
	}

	static_cast<void>(ResumeOnce(vm, generator));
	bool rejected = false;
	try {
		auto wider = Program(5, {Instruction(50, 2, 1), Instruction(19, 1, 2)});
		wider.MainFunction.Parameters.push_back(std::string("generator"));
		const std::array<SquirrelValue, 2> arguments{vm.RootTable(), generator};
		static_cast<void>(vm.Invoke(vm.CreateClosure(std::move(wider)), arguments));
	} catch (const sgre::script::SquirrelVmException& error) {
		Require(std::string(error.what()).find("generator trap relocation across different resumer extents") != std::string::npos, "unsupported native absolute-trap movement must report its precise recovery gap");
		rejected = true;
	}

	Require(rejected, "a generator's original absolute trap extent must not be silently relocated into a different caller");
	Require(std::get<std::int32_t>(ResumeOnce(vm, generator)) == 91, "rejected relocation must leave the saved generator and trap usable at their original extent");
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		for (const auto size : {std::uint32_t{4}, std::uint32_t{12}}) {
			TestChildAliasesCallerCellsAndKeepsUninitializedValues(size, 3);
			TestChildAliasesCallerCellsAndKeepsUninitializedValues(size, 6);
		}

		TestBoundNativeCallReplacesPhysicalCallerReceiver();
		TestRejectedNestedFramePopsItsHostArguments();
		TestShortFailureRetainsHigherCallerCellsUntilOverwrite();
		TestCallerOwnsChildTemporaryUntilCallerOverwrite();
		TestGeneratorResumeRetainsNonzeroSavedCellsUntilKill();
		TestDeadGeneratorKeepsSavedCallClosureUntilActualDeletion();
		TestSecondYieldKeepsActiveMetadataDuringNestedOwnerRelease();
		TestGeneratorTrapExtentFailurePreservesItsSavedFrame();
		std::cout << "Squirrel physical operand stack checks passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
