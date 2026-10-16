#include "sgre/render/sprite_pass.hpp"
#include "sgre/script/screen_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

	throw std::runtime_error("expected screen rejection: " + std::string(message));
}

static void TestAutomaticScreenLifetime(sgre::scene::LayerTree& tree, sgre::scene::ScreenRegistry& screens) {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	sgre::script::ScriptScreenBindings bindings(vm, objects, layers, screens);
	const auto derived = vm.CreateClass(bindings.ScreenClass());
	const auto class_root = vm.Retain(derived);
	vm.NewSlot(derived, std::string("cycle"), {});
	const std::array<sgre::script::SquirrelValue, 3> arguments{vm.RootTable(), std::int32_t(11), std::int32_t(7)};
	sgre::script::SquirrelValue screen = vm.Invoke(derived, arguments).Value;
	vm.SetSlot(screen, std::string("cycle"), screen);
	const auto handle = bindings.Handle(screen);
	const auto weak_screen = vm.CreateWeakReference(screen);
	sgre::script::SquirrelValue child;
	{
		const std::array<sgre::script::SquirrelValue, 2> child_arguments{vm.RootTable(), screen};
		child = vm.Invoke(layers.LayerFolderClass(), child_arguments).Value;
	}

	const auto child_handle = layers.Handle(child);
	auto child_root = vm.Retain(child);
	vm.ResetExecution();
	screen = {};
	static_cast<void>(vm.CollectGarbage());
	vm.RethrowReleaseFailure();
	Require(!screens.Contains(handle) && screens.Count() == 0 && std::holds_alternative<std::monostate>(vm.ResolveWeakReference(weak_screen)), "unrooted Screen cycle must release its owned Metal target during collection");
	Require(tree.Contains(child_handle) && !tree.State(child_handle).Attached && !tree.State(child_handle).ScreenOwner, "automatic Screen release must detach its surviving native folder");
	{
		const std::array<sgre::script::SquirrelValue, 1> receiver{child};
		Require(std::holds_alternative<std::monostate>(vm.Invoke(vm.GetSlot(child, std::string("getOwner")), receiver).Value), "automatic Screen release must clear the retained child's weak owner");
	}

	child = {};
	child_root = {};
	vm.RethrowReleaseFailure();
	Require(!tree.Contains(child_handle), "last retained folder release must reclaim its scene slot after Screen cleanup");
}

static void TestScreenCloseCleanup(sgre::scene::LayerTree& tree, sgre::scene::ScreenRegistry& screens) {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	sgre::script::ScriptScreenBindings bindings(vm, objects, layers, screens);
	const std::array<sgre::script::SquirrelValue, 3> arguments{vm.RootTable(), std::int32_t(13), std::int32_t(9)};
	const auto derived = vm.CreateClass(bindings.ScreenClass());
	const auto screen = vm.Invoke(derived, arguments).Value;
	const auto handle = bindings.Handle(screen);
	const std::array<sgre::script::SquirrelValue, 2> child_arguments{vm.RootTable(), screen};
	const auto child = vm.Invoke(layers.LayerFolderClass(), child_arguments).Value;
	const auto child_handle = layers.Handle(child);
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(vm.IsClosed() && !screens.Contains(handle) && screens.Count() == 0 && !tree.Contains(child_handle), "explicit VM shutdown must release actual screen targets and native layer owners before their services expire");
}

static void TestLiveScreenCleanupPreflight(sgre::scene::LayerTree& tree, sgre::scene::ScreenRegistry& screens) {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	sgre::script::ScriptScreenBindings bindings(vm, objects, layers, screens);
	const auto screen = vm.Invoke(bindings.ScreenClass(), std::array<sgre::script::SquirrelValue, 3>{vm.RootTable(), std::int32_t(9), std::int32_t(7)}).Value;
	const auto child = vm.Invoke(layers.LayerFolderClass(), std::array<sgre::script::SquirrelValue, 2>{vm.RootTable(), screen}).Value;
	const auto handle = bindings.Handle(screen);
	const auto root = screens.State(handle).LayerRoot;
	const auto child_handle = layers.Handle(child);
	const auto owner = vm.GetSlot(child, std::string("getOwner"));
	tree.VisitScreenChildren(root, [&](auto current) {
		Require(current == child_handle, "actual Screen traversal must visit its own retained child");
		RequireError([&] {
			screens.RequireCanRelease(handle);
		},
		    "clear during child traversal");
		RequireError([&] {
			bindings.ReleaseInstance(screen);
		},
		    "clear during child traversal");
		RequireError([&] {
			layers.DetachScreenOwners(screen);
		},
		    "clear during child traversal");
		Require(screens.Contains(handle) && objects.HasInstance(screen) && tree.ContainsScreenRoot(root) && tree.State(child_handle).Attached, "Screen preflight must precede failure-completing Object and target cleanup");
		Require(vm.Invoke(owner, std::array<sgre::script::SquirrelValue, 1>{child}).Value == screen, "rejected Screen detach must preserve the actual weak parent relation");
	});
	screens.RequireCanRelease(handle);
	bindings.ReleaseInstance(screen);
	Require(!screens.Contains(handle) && !objects.HasInstance(screen) && !tree.ContainsScreenRoot(root) && !tree.State(child_handle).Attached, "ordinary Screen cleanup must release target/Object roots after traversal ends");
	Require(std::holds_alternative<std::monostate>(vm.Invoke(owner, std::array<sgre::script::SquirrelValue, 1>{child}).Value), "ordinary Screen cleanup must clear the surviving child's weak owner");
	layers.ReleaseInstance(child);
	vm.Close();
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("screens_tests SPRITE_SHADER");
		}

		sgre::scene::LayerTree tree(16);
		sgre::scene::ScreenRegistryLimits limits;
		limits.MaxScreens = 3;
		limits.MaxDimension = 64;
		limits.MaxPixels = 10000;
		std::size_t allocations = 0;
		sgre::scene::ScreenRegistry screens(tree, [&](const sgre::scene::ScreenDescription& description) {
			++allocations;
			sgre::render::SpritePassDescription target;
			target.Width = description.Width;
			target.Height = description.Height;
			target.MaxVertices = 3;
			target.MaxIndices = 3;
			target.MaxDraws = 1;
			target.MaxTextures = 1;
			target.MaxPipelines = 1;
			target.MaxTextureBytes = 4;
			target.MaxTargetBytes = 64 * 1024;
			sgre::scene::ScreenTargetAllocation allocation;
			allocation.Renderer = std::make_shared<sgre::render::SpritePassRenderer>(target, argv[1]);
			const auto actual = allocation.Renderer->Description();
			allocation.Width = actual.Width;
			allocation.Height = actual.Height;

			return allocation;
		},
		    limits);
		sgre::scene::ScreenDescription description;
		description.Width = 64;
		description.Height = 32;
		const auto first = screens.Create(description);
		description.Width = 33;
		description.Height = 17;
		const auto second = screens.Create(description);
		description.Width = 57;
		description.Height = 13;
		description.InsertBefore = true;
		const auto third = screens.Create(description);
		std::array<sgre::scene::ScreenHandle, 3> order;
		Require(screens.WriteOrder(order) == 3 && order[0] == third && order[1] == first && order[2] == second, "screen task front/tail insertion order differs");
		RequireError([&] {
			static_cast<void>(screens.Create(description));
		},
		    "capacity");
		Require(allocations == 3, "capacity rejection allocated an extra native target");
		const auto bounds = screens.Bounds(second);
		Require(bounds.Left == -16.5f && bounds.Top == -8.5f && bounds.Right == 16.5f && bounds.Bottom == 8.5f && bounds.Width == 33 && bounds.Height == 17, "native centered screen bounds lost odd dimensions");
		Require(screens.State(second).Visible && screens.State(second).BackgroundColor == std::array<std::int32_t, 4>{}, "native screen defaults differ");
		screens.SetVisible(second, false);
		screens.SetBackgroundColor(second, {128, 64, 0, 255});
		Require(!screens.State(second).Visible && screens.State(second).BackgroundColor[0] == 128, "screen task state setters failed");
		const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
		auto& renderer = screens.Renderer(second);
		const auto submission = renderer.Submit({}, identity, {128.0f / 255.0f, 64.0f / 255.0f, 0, 1});
		const auto pixels = renderer.Capture(submission);
		Require(pixels.Width == 33 && pixels.Height == 17, "Screen did not own the requested actual Metal extent");
		for (std::size_t offset = 0; offset < pixels.Rgba.size(); offset += 4) {
			Require(pixels.Rgba[offset] == 128 && pixels.Rgba[offset + 1] == 64 && pixels.Rgba[offset + 2] == 0 && pixels.Rgba[offset + 3] == 255, "owned offscreen Screen clear differs");
		}

		screens.Release(first);
		screens.Release(second);
		screens.Release(third);
		Require(screens.Count() == 0 && !screens.Contains(second), "screen release left a live target or task entry");
		sgre::script::SquirrelVm vm;
		vm.InstallRecoveredBaseLibrary();
		sgre::script::ScriptObjectBindings objects(vm);
		sgre::script::ScriptLayerBindings layers(vm, objects, tree);
		sgre::script::ScriptScreenBindings bindings(vm, objects, layers, screens);
		const std::array<sgre::script::SquirrelValue, 3> constructor{vm.RootTable(), std::int32_t(31), 17.9f};
		const auto screen = vm.Invoke(bindings.ScreenClass(), constructor).Value;
		Require(vm.IsInstanceOf(screen, objects.ObjectClass()) && !vm.IsInstanceOf(screen, layers.LayerBaseClass()), "Screen must be an Object-owned target with a separate layer manager");
		const auto handle = bindings.Handle(screen);
		const auto call = [&](std::string name, std::vector<sgre::script::SquirrelValue> arguments = {}) {
			arguments.insert(arguments.begin(), screen);

			return vm.Invoke(vm.GetSlot(screen, std::move(name)), arguments).Value;
		};
		const auto table = call("getBounds");
		Require(std::get<float>(vm.GetSlot(table, std::string("left"))) == -15.5f && std::get<float>(vm.GetSlot(table, std::string("height"))) == 17, "Screen VM bounds or native integer conversion differs");
		vm.AssignSlot(screen, std::string("visible"), false);
		Require(!std::get<bool>(vm.ReadSlot(screen, std::string("visible"))), "Screen Object property did not reach visibility state");
		static_cast<void>(call("setBackgroundColor", {std::int32_t(255), 128.9f, std::string("invalid"), true}));
		Require(screens.State(handle).BackgroundColor == std::array<std::int32_t, 4>{255, 128, 0, 1}, "native Screen integer color conversion/default differs");
		const std::array<sgre::script::SquirrelValue, 2> child_arguments{vm.RootTable(), screen};
		const auto child = vm.Invoke(layers.LayerFolderClass(), child_arguments).Value;
		const auto layer_handle = layers.Handle(child);
		Require(tree.State(layer_handle).ScreenOwner == screens.State(handle).LayerRoot && tree.ChildCount() == 0, "folder owner did not select the native Screen's separate manager");
		bindings.ReleaseInstance(screen);
		const std::array<sgre::script::SquirrelValue, 1> child_receiver{child};
		Require(!screens.Contains(handle) && !tree.State(layer_handle).Attached && std::holds_alternative<std::monostate>(vm.Invoke(vm.GetSlot(child, std::string("getOwner")), child_receiver).Value), "Screen lifetime failed to detach retained children and VM owners");
		layers.ReleaseInstance(child);
		TestAutomaticScreenLifetime(tree, screens);
		TestScreenCloseCleanup(tree, screens);
		TestLiveScreenCleanupPreflight(tree, screens);
		std::cout << "Screen target ownership, task order, centered bounds, state, VM bindings, and actual offscreen Metal clear passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
