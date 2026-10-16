#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using sgre::script::SquirrelHostRoot;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t first, std::int32_t second) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = first;
	instruction.Argument1 = second;

	return instruction;
}

static SquirrelValue MakeOwnedClosure(SquirrelVm& vm, std::span<const SquirrelValue, 4> owners, const SquirrelValue& cycle = {}) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-closure-lifetime-fixture");
	program.MainFunction.Parameters = {std::string("this"), std::string("capture0"), std::string("capture1"), std::string("default0"), std::string("default1"), std::string("cycle")};
	program.MainFunction.StackSize = 7;
	program.MainFunction.Instructions = {Instruction(48, 6, 0), Instruction(19, 1, 6)};
	sgre::script::SquirrelFunction child;
	child.SourceName = std::string("independent-closure-lifetime-fixture");
	child.Name = std::string("owned_closure");
	child.Parameters = {std::string("this"), std::string("default0"), std::string("default1")};
	child.StackSize = 3;
	child.DefaultParameters = {3, 4};
	child.Instructions = {Instruction(19, 1, 1)};
	for (const auto source : {1, 2, 5}) {
		sgre::script::SquirrelOuterBinding binding;
		binding.Type = 0;
		binding.Source = static_cast<std::int32_t>(source);
		binding.Name = std::string("owned_capture");
		child.OuterBindings.push_back(binding);
	}

	program.MainFunction.Functions.push_back(std::move(child));
	const std::array<SquirrelValue, 6> arguments{vm.RootTable(), owners[0], owners[1], owners[2], owners[3], cycle};

	return vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
}

static void InstallOwnerHooks(SquirrelVm& vm, std::span<const SquirrelValue, 4> owners, const SquirrelValue& closure_weak, const SquirrelValue& source_weak, const std::optional<sgre::script::SquirrelObjectIdentity>& environment_identity, std::vector<std::size_t>& calls, std::array<SquirrelHostRoot, 2>& default_roots) {
	for (std::size_t index = 0; index < owners.size(); ++index) {
		vm.SetInstanceReleaseHook(owners[index], [&, index](SquirrelVm& current, const SquirrelValue&) {
			Require(!std::holds_alternative<std::monostate>(current.ResolveWeakReference(closure_weak)), "native closure weak identity must survive every owned default/capture release callback");
			Require(!std::holds_alternative<std::monostate>(current.ResolveWeakReference(source_weak)), "native closure prototype metadata must remain owned throughout default/capture callbacks");
			if (environment_identity) {
				Require(current.ResolveObjectIdentity(*environment_identity).has_value(), "native bound environment wrapper must remain owned throughout default/capture callbacks");
			}

			if (index == 0) {
				default_roots[0] = {};
				default_roots[1] = {};
			}

			calls.push_back(index);
		});
	}
}

static std::array<SquirrelValue, 4> CreateOwners(SquirrelVm& vm, const SquirrelValue& owner_type) {
	return {vm.CreateInstance(owner_type), vm.CreateInstance(owner_type), vm.CreateInstance(owner_type), vm.CreateInstance(owner_type)};
}

static void ClearOwners(std::span<SquirrelValue, 4> owners) {
	for (auto& owner : owners) {
		owner = {};
	}
}

static void TestOrdinaryDeletionReleasesDefaultsBeforeCaptures(bool bound) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::vector<std::size_t> calls;
	std::array<SquirrelHostRoot, 2> default_roots;
	SquirrelValue closure_weak;
	SquirrelValue source_weak;
	std::optional<sgre::script::SquirrelObjectIdentity> environment_identity;
	SquirrelVm vm(limits);
	const auto owner_type = vm.CreateClass();
	auto owners = CreateOwners(vm, owner_type);
	SquirrelValue closure = MakeOwnedClosure(vm, owners);
	const auto environment = vm.CreateTable();
	if (bound) {
		closure = vm.BindEnvironment(closure, environment);
		SquirrelValue environment_wrapper = vm.CreateWeakReference(environment);
		environment_identity = vm.BorrowObjectIdentity(environment_wrapper);
		environment_wrapper = {};
	}

	closure_weak = vm.CreateWeakReference(closure);
	source_weak = vm.CreateWeakReference(vm.CreateString("independent-closure-lifetime-fixture"));
	InstallOwnerHooks(vm, owners, closure_weak, source_weak, environment_identity, calls, default_roots);
	ClearOwners(owners);
	vm.ResetContext(vm.RootContext());
	closure = {};
	vm.RethrowReleaseFailure();
	Require(calls == std::vector<std::size_t>{2, 3, 0, 1}, "native ordinary closure deletion must retire defaults ascending before captures ascending");
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(closure_weak)), "native closure weak identity must expire only after all owned child releases finish");
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(source_weak)), "completed closure deletion must release its last prototype metadata owner");
	if (environment_identity) {
		Require(!vm.ResolveObjectIdentity(*environment_identity), "completed closure deletion must release its cached environment wrapper");
	}

	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestCollectionKeepsDefaultsUntilDeletion(bool cyclic) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	std::vector<std::size_t> calls;
	std::array<SquirrelHostRoot, 2> default_roots;
	SquirrelValue closure_weak;
	SquirrelValue source_weak;
	std::optional<sgre::script::SquirrelObjectIdentity> environment_identity;
	SquirrelVm vm(limits);
	const auto owner_type = vm.CreateClass();
	const auto type_root = vm.Retain(owner_type);
	auto owners = CreateOwners(vm, owner_type);
	default_roots[0] = vm.Retain(owners[2]);
	default_roots[1] = vm.Retain(owners[3]);
	SquirrelValue cycle = cyclic ? vm.CreateTable() : SquirrelValue{};
	SquirrelValue closure = MakeOwnedClosure(vm, owners, cycle);
	closure_weak = vm.CreateWeakReference(closure);
	source_weak = vm.CreateWeakReference(vm.CreateString("independent-closure-lifetime-fixture"));
	InstallOwnerHooks(vm, owners, closure_weak, source_weak, environment_identity, calls, default_roots);
	if (cyclic) {
		vm.NewSlot(cycle, std::string("closure"), closure);
		cycle = {};
		closure = {};
	}

	const auto first_default_weak = vm.CreateWeakReference(owners[2]);
	ClearOwners(owners);
	vm.ResetContext(vm.RootContext());
	Require(vm.CollectGarbage() == 1, "one native closure finalization must account for its reference-released owned descendants");
	vm.RethrowReleaseFailure();
	if (!cyclic) {
		Require(calls == std::vector<std::size_t>{0, 1}, "collection finalization must retire captures without destroying still-owned defaults");
		Require(!std::holds_alternative<std::monostate>(vm.ResolveWeakReference(first_default_weak)), "a finalized surviving closure must still own its default argument value");
		closure = {};
		vm.RethrowReleaseFailure();
	}

	Require(calls == std::vector<std::size_t>{0, 1, 2, 3}, "native collection followed by deletion must retire captures first and defer defaults to actual destruction");
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(closure_weak)) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(first_default_weak)), "completed native closure deletion must retire its weak identity and all owned default lifetimes");
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(source_weak)), "completed closure deletion must release its last prototype metadata owner");
	if (environment_identity) {
		Require(!vm.ResolveObjectIdentity(*environment_identity), "completed closure deletion must release its cached environment wrapper");
	}

	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestOrdinaryDeletionReleasesDefaultsBeforeCaptures(false);
		TestOrdinaryDeletionReleasesDefaultsBeforeCaptures(true);
		TestCollectionKeepsDefaultsUntilDeletion(false);
		TestCollectionKeepsDefaultsUntilDeletion(true);
		std::cout << "Squirrel closure retirement phase checks passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
