#include "sgre/script/layer_bindings.hpp"

#include <array>
#include <iostream>
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

	throw std::runtime_error("expected layer binding rejection: " + std::string(message));
}

static void TestAutomaticFolderLifetime() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::scene::LayerTree tree(8);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	const auto derived = vm.CreateClass(layers.LayerFolderClass());
	const auto class_root = vm.Retain(derived);
	vm.NewSlot(derived, std::string("cycle"), {});
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	sgre::script::SquirrelValue parent = vm.Invoke(layers.LayerFolderClass(), receiver).Value;
	sgre::script::SquirrelValue child;
	{
		const std::array<sgre::script::SquirrelValue, 2> arguments{vm.RootTable(), parent};
		child = vm.Invoke(derived, arguments).Value;
	}
	vm.SetSlot(child, std::string("cycle"), child);

	const auto parent_handle = layers.Handle(parent);
	const auto child_handle = layers.Handle(child);
	const auto weak_parent = vm.CreateWeakReference(parent);
	parent = {};
	vm.RethrowReleaseFailure();
	Require(!tree.Contains(parent_handle) && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_parent)), "folder parent target must expire after its last strong reference");
	Require(tree.Contains(child_handle) && !tree.State(child_handle).Attached && !tree.State(child_handle).Owner, "native folder release must detach and retain its surviving child");
	{
		const std::array<sgre::script::SquirrelValue, 1> child_receiver{child};
		Require(std::holds_alternative<std::monostate>(vm.Invoke(vm.GetSlot(child, std::string("getOwner")), child_receiver).Value), "native folder release must clear the surviving child's weak owner wrapper");
	}

	for (std::size_t iteration = 0; iteration < 32; ++iteration) {
		const auto transient = vm.Invoke(layers.LayerFolderClass(), receiver).Value;
		Require(tree.Contains(layers.Handle(transient)), "reused identity must receive a live native folder slot");
	}
	vm.RethrowReleaseFailure();

	auto root = vm.Retain(child);
	const auto child_identity = vm.BorrowObjectIdentity(child);
	child = {};
	static_cast<void>(vm.CollectGarbage());
	Require(tree.Contains(child_handle), "externally rooted folder must survive collection");
	root = {};
	static_cast<void>(vm.CollectGarbage());
	vm.RethrowReleaseFailure();
	Require(!tree.Contains(child_handle) && !vm.ResolveObjectIdentity(child_identity), "unrooted folder must release scene state during collection");
}

static void TestFolderCloseCleanup() {
	sgre::scene::LayerTree tree(4);
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	const auto derived = vm.CreateClass(layers.LayerFolderClass());
	const auto parent = vm.Invoke(derived, receiver).Value;
	const std::array<sgre::script::SquirrelValue, 2> arguments{vm.RootTable(), parent};
	const auto child = vm.Invoke(layers.LayerFolderClass(), arguments).Value;
	const auto parent_handle = layers.Handle(parent);
	const auto child_handle = layers.Handle(child);
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(vm.IsClosed() && !tree.Contains(parent_handle) && !tree.Contains(child_handle) && tree.ChildCount() == 0, "explicit VM shutdown must release native scene state while the layer owner/tree remain live");
}

int main() {
	try {
		TestAutomaticFolderLifetime();
		TestFolderCloseCleanup();
		sgre::script::SquirrelVm vm;
		vm.InstallRecoveredBaseLibrary();
		sgre::script::ScriptObjectBindings objects(vm);
		sgre::scene::LayerTree tree(16);
		sgre::script::ScriptLayerBindings layers(vm, objects, tree);
		const std::array<sgre::script::SquirrelValue, 1> root_receiver{vm.RootTable()};
		RequireError([&] {
			static_cast<void>(vm.Invoke(layers.LayerBaseClass(), root_receiver));
		},
		    "can't create instance");
		const auto folder = vm.Invoke(layers.LayerFolderClass(), root_receiver).Value;
		const std::array<sgre::script::SquirrelValue, 2> child_arguments{vm.RootTable(), folder};
		const auto child = vm.Invoke(layers.LayerFolderClass(), child_arguments).Value;
		Require(vm.IsInstanceOf(child, layers.LayerBaseClass()) && vm.IsInstanceOf(child, objects.ObjectClass()) && objects.HasInstance(child), "native folder inheritance/base attachment differs");
		const auto call = [&](const sgre::script::SquirrelValue& instance, std::string name, std::vector<sgre::script::SquirrelValue> values = {}) {
			values.insert(values.begin(), instance);

			return vm.Invoke(vm.GetSlot(instance, std::move(name)), values).Value;
		};
		Require(call(child, "getOwner") == folder && std::get<bool>(call(child, "getVisible")) && !std::get<bool>(call(child, "getSmoothing")), "native folder defaults differ");
		static_cast<void>(call(child, "setOffset", {std::int32_t(6), true}));
		Require(std::get<float>(call(child, "getOffsetX")) == 6 && std::get<float>(call(child, "getOffsetY")) == 1, "native float conversion from integer/boolean differs");
		static_cast<void>(call(child, "setOffset", {std::string("invalid")}));
		Require(std::get<float>(call(child, "getOffsetX")) == 0 && std::get<float>(call(child, "getOffsetY")) == 0, "failed/missing numeric conversions did not preserve initialized zero");
		static_cast<void>(call(child, "setVisible", {std::int32_t(0)}));
		Require(!std::get<bool>(vm.ReadSlot(child, std::string("visible"))), "native Object property did not use LayerBase getter");
		vm.AssignSlot(child, std::string("visible"), std::string(""));
		Require(std::get<bool>(call(child, "getVisible")), "native string truthiness differs");
		vm.AssignSlot(child, std::string("priority"), 13.5f);
		Require(std::get<float>(vm.ReadSlot(child, std::string("priority"))) == 13.5f, "native Object priority property did not reach scene state");
		static_cast<void>(call(child, "setClip", {1.0f, 2.0f, 3.0f, -4.0f}));
		Require(tree.State(layers.Handle(child)).Clip == sgre::scene::LayerClip{1, 2, 4, -2}, "VM clip arguments lost native endpoint addition");
		static_cast<void>(call(child, "setOffset", {2.0f, 3.0f}));
		static_cast<void>(call(child, "setScale", {2.0f, 4.0f}));
		const auto world = call(child, "convertLocalToWorld", {3.0f, 5.0f});
		Require(std::get<float>(vm.GetSlot(world, std::string("x"))) == 4 && std::get<float>(vm.GetSlot(world, std::string("y"))) == 17, "native two-argument point conversion failed");
		const auto local = call(child, "convertWorldToLocal", {world});
		Require(std::get<float>(vm.GetSlot(local, std::string("x"))) == 3 && std::get<float>(vm.GetSlot(local, std::string("y"))) == 5, "native point-table conversion failed");
		RequireError([&] {
			static_cast<void>(call(child, "convertLocalToWorld", {std::string("bad"), 0.0f}));
		},
		    "numeric");
		const auto derived_class = vm.CreateClass(layers.LayerFolderClass());
		const auto derived = vm.Invoke(derived_class, child_arguments).Value;
		Require(tree.State(layers.Handle(derived)).Owner == layers.Handle(folder), "derived folder constructor lost native receiver or ownership");
		static_cast<void>(call(folder, "clear"));
		Require(std::holds_alternative<std::monostate>(call(child, "getOwner")) && !tree.State(layers.Handle(child)).Attached && tree.ChildCount() == 1, "clear destroyed or root-attached child state");
		static_cast<void>(call(child, "setOwner", {sgre::script::SquirrelValue{}}));
		Require(tree.State(layers.Handle(child)).Attached && tree.ChildCount() == 2, "explicit null did not reattach a cleared child");
		RequireError([&] {
			static_cast<void>(call(child, "setOwner"));
		},
		    "requires one owner");
		RequireError([&] {
			static_cast<void>(call(child, "setOwner", {std::int32_t(3)}));
		},
		    "LayerBase instance");
		const auto stale_handle = layers.Handle(child);
		layers.ReleaseInstance(child);
		Require(!tree.Contains(stale_handle) && !objects.HasInstance(child), "explicit folder lifetime did not release scene/Object state");
		RequireError([&] {
			static_cast<void>(call(child, "getOwner"));
		},
		    "no live native scene state");
		std::cout << "LayerBase/Folder construction, ownership, native properties, transforms, and explicit lifetime passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
