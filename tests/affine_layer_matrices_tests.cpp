#include "sgre/script/affine_layer_matrices.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void CheckDistinctRootsAndRetainedWrapper() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptAffineMatrixBindings matrices(vm);
	sgre::script::ScriptAffineLayerMatrices owner(vm, matrices);
	const auto composite = owner.Wrapper(sgre::script::AffineLayerMatrixSlot::Composite);
	const auto local = owner.Wrapper(sgre::script::AffineLayerMatrixSlot::Local);
	const auto image = owner.Wrapper(sgre::script::AffineLayerMatrixSlot::Image);
	Require(composite != local && composite != image && local != image && matrices.InstanceCount() == 3, "each slot requires a distinct genuine owned matrix wrapper");
	for (const auto& wrapper : {composite, local, image}) {
		Require(vm.IsInstanceOf(wrapper, matrices.MatrixClass()) && vm.RetainedReferenceCount(wrapper) == 1, "each genuine matrix has exactly one external AffineLayer root");
	}
	owner.Matrix(sgre::script::AffineLayerMatrixSlot::Local).Rows[3] = 17;
	Require(matrices.Matrix(local).Rows[3] == 17 && matrices.Matrix(composite).Rows[3] == 0 && matrices.Matrix(image).Rows[3] == 0, "borrowed slot access mutates only its separately owned matrix");
	owner.Close();
	owner.Close();
	Require(owner.IsClosed() && vm.RetainedReferenceCount(local) == 0 && matrices.Matrix(local).Rows[3] == 17 && matrices.InstanceCount() == 3, "script copies survive idempotent external root retirement");
	try {
		static_cast<void>(owner.Matrix(sgre::script::AffineLayerMatrixSlot::Local));
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()).find("roots have retired") != std::string::npos, "retired owner access fails before borrowing a stale matrix");
		vm.Close();
		vm.RethrowReleaseFailure();

		return;
	}

	throw std::runtime_error("closed AffineLayer matrix root owner allowed access");
}

static void CheckNativeReverseReleaseOrder() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptAffineMatrixBindings matrices(vm);
	vm.NewSlot(matrices.MatrixClass(), std::string("probe"), std::monostate{});
	std::vector<std::uint8_t> order;
	sgre::script::ScriptAffineLayerMatrices owner(vm, matrices);
	const std::array<sgre::script::AffineLayerMatrixSlot, 3> slots{sgre::script::AffineLayerMatrixSlot::Composite, sgre::script::AffineLayerMatrixSlot::Local, sgre::script::AffineLayerMatrixSlot::Image};
	for (std::uint8_t index = 0; index < slots.size(); ++index) {
		const std::array<std::uint8_t, 1> bytes{index};
		const auto probe = vm.CreateUserData(bytes);
		vm.SetUserDataReleaseHook(probe, [&](auto&, const auto&, std::span<std::uint8_t> payload) {
			order.push_back(payload[0]);
			Require(matrices.InstanceCount() == 2 - order.size() + 1, "native matrix hook retires before its arbitrary script members");
		});
		vm.SetSlot(owner.Wrapper(slots[index]), std::string("probe"), probe);
	}
	owner.Close();
	Require(order == std::vector<std::uint8_t>{2, 1, 0} && matrices.InstanceCount() == 0, "native image/local/composite root retirement order preserves real instance/member disposal");
	vm.RethrowReleaseFailure();
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void CheckPartialConstructionAndVmClose() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptAffineMatrixBindings bounded(vm, 2);
	try {
		sgre::script::ScriptAffineLayerMatrices owner(vm, bounded);
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()).find("instance limit") != std::string::npos && bounded.InstanceCount() == 0, "bounded partial construction retires all previously acquired roots");
		sgre::script::ScriptAffineMatrixBindings matrices(vm);
		sgre::script::ScriptAffineLayerMatrices owner(vm, matrices);
		vm.Close();
		vm.RethrowReleaseFailure();
		Require(matrices.InstanceCount() == 0, "VM Close deletes genuine matrix payloads despite external owner roots");
		owner.Close();
		Require(owner.IsClosed(), "external roots safely retire after the VM completes shutdown");

		return;
	}

	throw std::runtime_error("bounded three-matrix construction should reject its third instance");
}

int main() {
	try {
		CheckDistinctRootsAndRetainedWrapper();
		CheckNativeReverseReleaseOrder();
		CheckPartialConstructionAndVmClose();
		std::cout << "Three AffineLayer matrix owner fixture groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
