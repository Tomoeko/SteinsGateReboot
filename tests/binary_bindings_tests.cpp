#include "sgre/script/binary_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected Binary rejection: " + std::string(message));
}

static void TestReclaimedBinaryLifetime() {
	sgre::script::SquirrelVmLimits vm_limits;
	vm_limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(vm_limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptBinaryBindingLimits limits;
	limits.MaxInstances = 1;
	sgre::script::ScriptBinaryBindings binary(vm, objects, limits);
	const auto class_identity = vm.BorrowObjectIdentity(binary.BinaryClass());
	vm.SetSlot(vm.RootTable(), std::string("Binary"), {});
	static_cast<void>(vm.CollectGarbage());
	Require(vm.ResolveObjectIdentity(class_identity).has_value(), "Binary class metadata must retain an explicit host root");
	std::weak_ptr<const std::vector<std::uint8_t>> observed;
	std::vector<std::int32_t> events;
	bool observe_release = true;
	vm.NewSlot(binary.BinaryClass(), std::string("destructor"), vm.CreateNativeClosure("fixture.binary.destructor", [&](sgre::script::SquirrelVm& current, const auto arguments) {
		if (!observe_release) {
			return sgre::script::SquirrelNativeResult{};
		}
		Require(objects.HasInstance(arguments[0]) && !observed.expired(), "Binary script destructor must precede native backing release");
		Require(std::get<std::int32_t>(current.Invoke(current.GetSlot(arguments[0], std::string("size")), arguments).Value) == 3, "Binary script destructor lost its live native buffer");
		events.push_back(1);

		return sgre::script::SquirrelNativeResult{};
	}));
	auto backing = std::make_shared<const std::vector<std::uint8_t>>(3);
	observed = backing;
	auto instance = binary.WrapBuffer(backing);
	backing.reset();
	const auto identity = vm.BorrowObjectIdentity(instance);
	const auto weak = vm.CreateWeakReference(instance);
	const auto listener = vm.CreateInstance(objects.ObjectClass());
	objects.AttachInstance(listener);
	objects.Subscribe(instance, listener, [&](const auto&) {
		Require(observed.expired(), "Binary Object notification ran before native backing release");
		events.push_back(2);

		return false;
	});
	instance = {};
	vm.RethrowReleaseFailure();
	Require(events == std::vector<std::int32_t>{1, 2} && observed.expired() && !vm.ResolveObjectIdentity(identity) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak)), "Binary registry retained its script instance or weak target");
	observe_release = false;
	const auto replacement = binary.WrapBuffer(std::make_shared<const std::vector<std::uint8_t>>(1));
	const auto replacement_identity = vm.BorrowObjectIdentity(replacement);
	Require(replacement_identity.Index == identity.Index && replacement_identity != identity && !vm.ResolveObjectIdentity(identity), "Binary registry identity retargeted a recycled generation");
}

static void TestExpiredBinaryOwner() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::SquirrelValue instance;
	sgre::script::SquirrelValue callable;
	std::weak_ptr<const std::vector<std::uint8_t>> observed;
	{
		sgre::script::ScriptBinaryBindings binary(vm, objects);
		auto backing = std::make_shared<const std::vector<std::uint8_t>>(2);
		observed = backing;
		instance = binary.WrapBuffer(backing);
		callable = vm.GetSlot(instance, std::string("size"));
	}

	Require(observed.expired(), "expired Binary adapter retained native backing");
	const std::array<sgre::script::SquirrelValue, 1> receiver{instance};
	RequireError([&] {
		static_cast<void>(vm.Invoke(callable, receiver));
	},
	    "callbacks have expired");
}

static void TestBinaryClose() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptBinaryBindings binary(vm, objects);
	auto backing = std::make_shared<const std::vector<std::uint8_t>>(3);
	const std::weak_ptr<const std::vector<std::uint8_t>> observed = backing;
	const auto instance = binary.WrapBuffer(backing);
	backing.reset();
	const auto identity = vm.BorrowObjectIdentity(instance);
	vm.NewSlot(vm.RootTable(), std::string("fixture-binary"), instance);
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(observed.expired() && !vm.ResolveObjectIdentity(identity), "Class finalization prevented Binary native buffer/base cleanup");
}

int main() {
	try {
		TestReclaimedBinaryLifetime();
		TestExpiredBinaryOwner();
		TestBinaryClose();
		sgre::script::SquirrelVm vm;
		sgre::script::ScriptObjectBindings objects(vm);
		sgre::script::ScriptBinaryBindings binary(vm, objects);
		const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
		RequireError([&] {
			static_cast<void>(vm.Invoke(binary.BinaryClass(), root_receiver));
		},
		    "can't create instance");
		auto backing = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{0x80, 0xff, 0, 42});
		const auto instance = binary.WrapBuffer(backing);
		backing.reset();
		Require(vm.IsInstanceOf(instance, objects.ObjectClass()) && objects.HasInstance(instance), "Binary did not retain native Object inheritance");
		const std::array<sgre::script::SquirrelValue, 1> receiver{instance};
		Require(std::get<std::int32_t>(vm.Invoke(vm.GetSlot(instance, std::string("size")), receiver).Value) == 4, "Binary size differs from retained bytes");
		const auto at = vm.GetSlot(instance, std::string("at"));
		const auto index_value = [&](sgre::script::SquirrelValue index) {
			const std::array<sgre::script::SquirrelValue, 2> arguments{instance, std::move(index)};

			return std::get<std::int32_t>(vm.Invoke(at, arguments).Value);
		};
		Require(index_value(std::int32_t(0)) == 128 && index_value(std::int32_t(1)) == 255, "Binary at sign-extended original byte values");
		Require(index_value(std::int32_t(-1)) == 0 && index_value(std::int32_t(4)) == 0, "Binary at failed its native out-of-range zero contract");
		Require(index_value(3.9f) == 42 && index_value(true) == 255 && index_value(std::string("invalid")) == 128, "Binary native integer conversion/default differs");
		Require(std::get<std::int32_t>(vm.Invoke(at, receiver).Value) == 128, "Binary missing index did not preserve its native initialized zero");
		const auto empty = binary.WrapBuffer(std::make_shared<const std::vector<std::uint8_t>>());
		const std::array<sgre::script::SquirrelValue, 1> empty_receiver{empty};
		Require(std::get<std::int32_t>(vm.Invoke(vm.GetSlot(empty, std::string("size")), empty_receiver).Value) == 0 && std::get<std::int32_t>(vm.Invoke(vm.GetSlot(empty, std::string("at")), empty_receiver).Value) == 0, "empty Binary owner did not preserve bounded reads");
		RequireError([&] {
			static_cast<void>(binary.WrapBuffer({}));
		},
		    "absent");
		std::cout << "Binary construction restrictions, owned bytes, native size, and indexed reads passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
