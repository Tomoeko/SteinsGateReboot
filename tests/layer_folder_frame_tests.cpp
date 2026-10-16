#include "sgre/script/layer_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using SquirrelValue = sgre::script::SquirrelValue;
using LayerHandle = sgre::scene::LayerHandle;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected actual Folder frame rejection");
}

struct Services {
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::scene::LayerTree Tree;
	std::array<sgre::scene::MotionScissorMatrix, 1> Models;
	std::vector<sgre::scene::MotionIntegerScissor> Scissors;
	sgre::scene::LayerCameraDrawScope Renderer;
	sgre::script::ScriptLayerBindings Layers;

	static sgre::script::SquirrelVmLimits Limits() {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;

		return limits;
	}

	static sgre::scene::LayerCameraFrameState CameraState() {
		sgre::scene::LayerCameraFrameState state;
		state.Viewport = {100, 100};
		state.RawScissor = {0, 0, 100, 100};
		state.RoundedScissor = state.RawScissor;
		state.AppliedRoundedScissor = state.RawScissor;

		return state;
	}

	Services()
	    : Vm(Limits()), Objects(Vm), Tree(16), Renderer(CameraState(), Models, [this](const auto& scissor) {
		      Scissors.push_back(scissor);
	      }),
	      Layers(Vm, Objects, Tree) {
	}

	~Services() {
		try {
			Vm.Close();
		} catch (...) {
		}
	}

	SquirrelValue Folder(const SquirrelValue& owner = {}) {
		const auto instance = Vm.Invoke(Layers.LayerFolderClass(), std::array<SquirrelValue, 2>{Vm.RootTable(), owner}).Value;
		Vm.ResetExecution();

		return instance;
	}

	sgre::script::ScriptFolderFrameServices EmptyChildrenServices(LayerHandle handle) {
		sgre::script::ScriptFolderFrameServices services;
		services.State = [this, handle](const auto& receiver) {
			Require(Layers.Handle(receiver) == handle, "state reads the existing actual Folder receiver");

			return sgre::scene::LayerFolderFrameState{};
		};
		services.Children.State = [](auto) -> sgre::scene::LayerFrameProcessState {
			throw std::runtime_error("empty actual child manager unexpectedly requested child state");
		};
		services.Children.Update = [](auto) {
			throw std::runtime_error("empty actual child manager unexpectedly updated a child");
		};
		services.Children.Draw = [](auto, auto) {
			throw std::runtime_error("empty actual child manager unexpectedly drew a child");
		};

		return services;
	}

	void Close() {
		Vm.Close();
		Vm.RethrowReleaseFailure();
	}
};

static void TestNestedActualFolderScopesAndNativePhases() {
	std::vector<int> calls;
	bool fail_draw = false;
	Services services;
	const auto root = services.Folder();
	const auto child = services.Folder(root);
	const auto root_handle = services.Layers.Handle(root);
	const auto child_handle = services.Layers.Handle(child);
	auto& root_groups = services.Layers.FolderTasks(root);
	auto& child_groups = services.Layers.FolderTasks(child);
	Require(&root_groups.CommonFirst() != &root_groups.CommonSecond(), "Folder owns two distinct native +118/+120 task groups");
	const auto child_identity = services.Vm.BorrowObjectIdentity(child);
	const SquirrelValue* active_root = nullptr;
	const SquirrelValue* active_child = nullptr;
	std::size_t root_state_reads = 0;
	std::size_t child_state_reads = 0;
	auto child_services = services.EmptyChildrenServices(child_handle);
	child_services.State = [&](const auto& receiver) {
		Require(&receiver == active_child && services.Vm.BorrowObjectIdentity(receiver) == child_identity, "nested Folder state consumes its existing checked scope without another borrow or root");
		++child_state_reads;

		return sgre::scene::LayerFolderFrameState{};
	};
	services.Layers.AttachFolderFrame(child, services.Renderer, std::move(child_services));
	sgre::script::ScriptFolderFrameServices root_services;
	root_services.State = [&](const auto& receiver) {
		Require(&receiver == active_root && services.Layers.Handle(receiver) == root_handle, "outer Folder state consumes its own live operation receiver");
		++root_state_reads;

		return sgre::scene::LayerFolderFrameState{};
	};
	root_services.Children.State = [&](auto handle) {
		Require(handle == child_handle, "native child manager traverses the existing Folder Tree list");

		return services.Layers.ProcessState(handle);
	};
	root_services.Children.Update = [&](auto handle) {
		services.Layers.DispatchFolderFrame(handle, [&](const auto& receiver, auto& frame) {
			active_child = &receiver;
			frame.Update();
			active_child = nullptr;
		});
	};
	root_services.Children.Draw = [&](auto handle, auto argument) {
		services.Layers.DispatchFolderFrame(handle, [&](const auto& receiver, auto& frame) {
			active_child = &receiver;
			frame.Draw(argument);
			active_child = nullptr;
		});
	};
	// PrepareDraw is absent: native Folder manager flag0 preserves the actual enclosing camera.
	services.Layers.AttachFolderFrame(root, services.Renderer, std::move(root_services));
	for (auto* group : {&root_groups.CommonFirst(), &root_groups.CommonSecond()}) {
		const auto number = group == &root_groups.CommonFirst() ? 1 : 2;
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [&, number](auto) {
			calls.push_back(number);
		};
		callbacks.Draw = [&, number](auto, std::uint32_t argument) {
			Require(argument == 0xfedcba98 && services.Renderer.Depth() == 1, "common Folder groups draw before its own camera scope");
			calls.push_back(number + 10);
		};
		static_cast<void>(group->Append(std::move(callbacks)));
	}
	sgre::scene::ParallelTaskCallbacks child_task;
	child_task.Update = [&](auto) {
		calls.push_back(3);
		RequireError([&] {
			services.Layers.ReleaseInstance(root);
		},
		    "traversal");
		Require(services.Tree.Contains(root_handle) && services.Objects.HasInstance(root), "active child traversal rejects before the actual containing Folder drops native roots");
	};
	child_task.Draw = [&](auto, std::uint32_t argument) {
		Require(argument == 0xfedcba98 && services.Renderer.Depth() == 2, "actual nested child groups retain the parent Folder camera without an invented reset");
		calls.push_back(13);
		if (fail_draw) {
			throw std::runtime_error("nested Folder drawing sentinel");
		}
	};
	static_cast<void>(child_groups.CommonFirst().Append(std::move(child_task)));
	const sgre::script::ScriptLayerBindings::FolderFrameOperation update = [&](const auto& receiver, auto& frame) {
		active_root = &receiver;
		frame.Update();
		active_root = nullptr;
	};
	const sgre::script::ScriptLayerBindings::FolderFrameOperation draw = [&](const auto& receiver, auto& frame) {
		active_root = &receiver;
		frame.Draw(0xfedcba98);
		active_root = nullptr;
	};
	services.Layers.DispatchFolderFrame(root_handle, update);
	Require(calls == std::vector<int>{1, 2, 3} && root_state_reads == 0 && child_state_reads == 0, "ordinary native Folder Update traverses common groups then actual children without renderer or script onProcess work");
	calls.clear();
	services.Vm.DispatchBorrowedReceiver(services.Vm.BorrowObjectIdentity(root), [&](const auto& outer_receiver) {
		services.Layers.DispatchFolderFrame(outer_receiver, draw);
	});
	Require(calls == std::vector<int>{11, 12, 13} && root_state_reads == 2 && child_state_reads == 2 && services.Renderer.Depth() == 1, "distinct nested borrowed managers traverse clip/camera scopes and restore actual renderer state");
	fail_draw = true;
	RequireError([&] {
		services.Layers.DispatchFolderFrame(root_handle, draw);
	},
	    "nested Folder drawing sentinel");
	Require(services.Renderer.Depth() == 1, "nested frame failure restores both actual camera model scopes");
	fail_draw = false;
	services.Layers.DispatchFolderFrame(root_handle, draw);
	services.Close();
}

static void TestPhysicalParallelCleanupAndFailureCompletion() {
	std::vector<int> releases;
	Services services;
	const auto parent = services.Folder();
	const auto instance = services.Folder(parent);
	const auto child = services.Folder(instance);
	const auto handle = services.Layers.Handle(instance);
	const auto child_handle = services.Layers.Handle(child);
	auto frame_services = services.EmptyChildrenServices(handle);
	services.Layers.AttachFolderFrame(instance, services.Renderer, std::move(frame_services));
	auto& groups = services.Layers.FolderTasks(instance);
	sgre::scene::ParallelTaskCallbacks first;
	first.Release = [&](auto) {
		Require(!services.Tree.State(handle).Attached && services.Objects.HasInstance(instance), "native base unlink precedes parallel cleanup while common Object remains alive");
		const auto owner = services.Vm.Invoke(services.Vm.GetSlot(instance, std::string("getOwner")), std::array<SquirrelValue, 1>{instance}).Value;
		Require(std::holds_alternative<std::monostate>(owner), "native weak parent drops before first common group destruction");
		services.Vm.ResetExecution();
		releases.push_back(1);
		throw std::runtime_error("first Folder group release sentinel");
	};
	static_cast<void>(groups.CommonFirst().Append(std::move(first)));
	sgre::scene::ParallelTaskCallbacks second;
	second.Release = [&](auto) {
		Require(services.Tree.State(child_handle).Owner == handle, "second native common group retires before child-manager clear");
		releases.push_back(2);
	};
	static_cast<void>(groups.CommonSecond().Append(std::move(second)));
	auto& process = services.Layers.CommonTasks(instance);
	sgre::scene::ParallelTaskCallbacks tail;
	tail.Release = [&](auto) {
		Require(!services.Tree.State(child_handle).Attached && services.Objects.HasInstance(child), "real clip/camera child-manager retirement precedes later native parallel tasks and preserves child objects");
		releases.push_back(3);
	};
	static_cast<void>(process.Parallel().Append(std::move(tail)));
	sgre::scene::SerialTaskCallbacks serial;
	serial.Release = [&] {
		Require(!services.Tree.State(child_handle).Attached && services.Objects.HasInstance(instance), "native Folder serial deletion follows the complete concrete parallel graph");
		releases.push_back(4);
	};
	static_cast<void>(process.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	RequireError([&] {
		services.Layers.ReleaseInstance(instance);
	},
	    "first Folder group release sentinel");
	Require(releases == std::vector<int>{1, 2, 3, 4} && !services.Tree.Contains(handle) && services.Tree.Contains(child_handle) && !services.Tree.State(child_handle).Attached, "failure-completing native-order cleanup retires every actual owner once without inventing deferred finalization");
	services.Close();
}

static void TestAttachmentServicesNoHiddenRootAndGroupPreflight() {
	Services services;
	SquirrelValue instance = services.Folder();
	const auto handle = services.Layers.Handle(instance);
	const auto weak = services.Vm.CreateWeakReference(instance);
	RequireError([&] {
		services.Layers.AttachFolderFrame(instance, services.Renderer, {});
	},
	    "actual state/child services");
	Require(services.Layers.CommonTasks(instance).Parallel().Count() == 0, "missing real services reject before partial physical graph publication");
	services.Layers.AttachFolderFrame(instance, services.Renderer, services.EmptyChildrenServices(handle));
	Require(services.Layers.CommonTasks(instance).Parallel().Count() == 3, "actual Folder common parallel owns exactly common-first, common-second and base-clip roots");
	auto& groups = services.Layers.FolderTasks(instance);
	sgre::scene::ParallelTaskCallbacks callback;
	callback.Update = [&](auto) {
		RequireError([&] {
			services.Layers.ReleaseInstance(instance);
		},
		    "common groups");
		Require(services.Tree.Contains(handle) && services.Objects.HasInstance(instance), "standalone actual common-group preflight precedes script native-root cleanup");
	};
	static_cast<void>(groups.CommonFirst().Append(std::move(callback)));
	groups.CommonFirst().Update();
	instance = {};
	Require(std::holds_alternative<std::monostate>(services.Vm.ResolveWeakReference(weak)) && !services.Tree.Contains(handle), "genuine frame state/manager attachments retain no script Folder instance");
	services.Close();
}

static void TestStableDetachAndPreserveRendererMode() {
	sgre::scene::LayerTree tree(4);
	const auto owner = tree.CreateFolder();
	const auto child = tree.CreateMotion(owner);
	sgre::scene::CommonProcessTasks process;
	sgre::scene::LayerFolderTasks groups;
	std::array<sgre::scene::MotionScissorMatrix, 1> models;
	std::vector<sgre::scene::MotionIntegerScissor> scissor;
	sgre::scene::LayerCameraDrawScope renderer(Services::CameraState(), models, [&](const auto& submitted) {
		scissor.push_back(submitted);
	});
	std::size_t updates = 0;
	std::size_t draws = 0;
	std::size_t resets = 0;
	sgre::scene::LayerManagerFrameOperations operations;
	operations.State = [&](auto handle) {
		Require(handle == child, "preserved manager resolves the actual child owner");

		return sgre::scene::LayerFrameProcessState{};
	};
	operations.Update = [&](auto) {
		++updates;
	};
	operations.Draw = [&](auto, auto) {
		Require(renderer.Depth() == 2, "preserved manager draws inside its actual Folder camera");
		++draws;
	};
	operations.PrepareDraw = [&] {
		++resets;
	};
	sgre::scene::LayerFolderFrame frame(process, groups, tree, owner, renderer, [] {
		return sgre::scene::LayerFolderFrameState{};
	},
	    std::move(operations), [&] {
		    tree.Clear(owner);
	    });
	frame.Update();
	frame.Draw(17);
	Require(updates == 1 && draws == 1 && resets == 0 && renderer.Depth() == 1, "native flag0 invokes no fabricated renderer reset while real child update/draw services run");
	frame.Close();
	Require(process.Parallel().Count() == 0 && tree.State(child).Attached, "stable attachment detach transfers no concrete child ownership or retirement");
	RequireError([&] {
		frame.Update();
	},
	    "attachment has retired");
	tree.Release(owner);
	tree.Release(child);
}

int main() {
	try {
		TestNestedActualFolderScopesAndNativePhases();
		TestPhysicalParallelCleanupAndFailureCompletion();
		TestAttachmentServicesNoHiddenRootAndGroupPreflight();
		TestStableDetachAndPreserveRendererMode();
		std::cout << "4 actual Folder graph/ownership groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
