#include "sgre/script/affine_layer_bindings.hpp"

#include "sgre/scene/affine_layer_order_workspace.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/object_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
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
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected affine priority attachment failure");
}

struct Services {
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	sgre::scene::LayerTree Tree;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	sgre::script::ScriptLayerBindings Layers;
	sgre::script::ScriptAffineMatrixBindings Matrices;
	sgre::script::ScriptAffineLayerBindings Affine;

	static sgre::script::SquirrelVmLimits Limits() {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;

		return limits;
	}

	static sgre::script::ScriptLayerBindings& Configure(sgre::script::ScriptLayerBindings& layers, sgre::script::ScriptPsbBindings& psb, sgre::scene::MotionAssetCatalog& catalog, sgre::scene::TextureCache& cache) {
		layers.ConfigureMotionOwners(psb, catalog, cache);

		return layers;
	}

	Services()
	    : Vm(Limits()), Objects(Vm), Psb(Vm, Objects), Tree(32), Cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		      throw std::runtime_error("priority owner fixture unexpectedly uploaded an asset");
	      },
	                                                                 [](auto) {
		                                                                 throw std::runtime_error("priority owner fixture unexpectedly retired an uploaded asset");
	                                                                 }),
	      Catalog(Cache), Layers(Vm, Objects, Tree), Matrices(Vm), Affine(Vm, Objects, Configure(Layers, Psb, Catalog, Cache), Matrices, 16) {
		Vm.InstallRecoveredBaseLibrary();
	}

	~Services() {
		try {
			Vm.Close();
		} catch (...) {
		}
	}

	SquirrelValue Construct(const SquirrelValue& class_value, const SquirrelValue& owner = {}) {
		const auto instance = Vm.Invoke(class_value, std::array<SquirrelValue, 2>{Vm.RootTable(), owner}).Value;
		Vm.ResetExecution();

		return instance;
	}

	void Set(const SquirrelValue& instance, std::string_view method, const SquirrelValue& value) {
		static_cast<void>(Vm.Invoke(Vm.GetSlot(instance, std::string(method)), std::array<SquirrelValue, 2>{instance, value}));
		Vm.ResetExecution();
	}

	std::vector<LayerHandle> Order(const SquirrelValue& folder) {
		const auto handle = Layers.Handle(folder);
		Tree.PrepareChildren(handle);
		std::vector<LayerHandle> order(Tree.ChildCount(handle));
		static_cast<void>(Tree.WriteChildren(handle, order));

		return order;
	}

	void Close() {
		Vm.Close();
		Vm.RethrowReleaseFailure();
	}
};

static void TestActualContainingOrderAndTaskPhase() {
	std::size_t observer_calls = 0;
	std::size_t draws = 0;
	Services services;
	const auto driver_layer = services.Construct(services.Layers.LayerClass());
	const auto folder = services.Construct(services.Layers.LayerFolderClass());
	const auto first = services.Construct(services.Affine.AffineLayerClass(), folder);
	const auto second = services.Construct(services.Affine.AffineLayerClass(), folder);
	const auto third = services.Construct(services.Affine.AffineLayerClass(), folder);
	const auto first_handle = services.Layers.Handle(first);
	const auto second_handle = services.Layers.Handle(second);
	const auto third_handle = services.Layers.Handle(third);
	services.Set(first, "setZpos", 1.0f);
	services.Set(second, "setZpos", 3.0f);
	services.Set(third, "setZpos", 2.0f);
	Require(services.Order(folder) == std::vector<LayerHandle>{first_handle, second_handle, third_handle}, "script property dirty does not publish priority before the native global manager phase");
	auto& driver = services.Layers.MotionTasks(driver_layer).Group(sgre::scene::MotionTaskGroup::CommonFirst);
	services.Affine.AttachPriorityTask(driver);
	RequireError([&] {
		services.Affine.AttachPriorityTask(driver);
	},
	    "one stable actual default SQDriver parallel owner");
	sgre::scene::ParallelTaskCallbacks observer;
	observer.Update = [&](auto) {
		++observer_calls;
		Require(!services.Affine.PriorityOrderDirty(), "the earlier actual priority task publishes before later CommonFirst work");
		RequireError([&] {
			services.Affine.DetachPriorityTask();
		},
		    "during traversal");
		if (observer_calls == 1) {
			Require(services.Tree.State(second_handle).Priority == 0 && services.Tree.State(third_handle).Priority == 1 && services.Tree.State(first_handle).Priority == 2, "actual Layer fields receive native affine ranks through their existing owner handles");
			services.Set(first, "setZpos", 5.0f);
		}
	};
	observer.Draw = [&](auto, std::uint32_t argument) {
		Require(argument == 0xfedcba98, "actual sibling task still receives the full draw argument");
		++draws;
	};
	static_cast<void>(driver.Append(std::move(observer)));
	driver.Update();
	Require(services.Affine.PriorityOrderDirty() && services.Order(folder) == std::vector<LayerHandle>{second_handle, third_handle, first_handle}, "later task callback mutation stays dirty while actual containing order reflects the completed priority pass");
	driver.Update();
	Require(!services.Affine.PriorityOrderDirty() && services.Order(folder) == std::vector<LayerHandle>{first_handle, second_handle, third_handle}, "next actual task update consumes callback mutation and invalidates the containing Layer manager sort");
	services.Set(first, "setAbsoluteBase", 8.0f);
	driver.Draw(0xfedcba98);
	Require(draws == 1 && services.Affine.PriorityOrderDirty(), "native empty priority Draw does not evaluate priority or invent renderer work");
	services.Affine.DetachPriorityTask();
	Require(driver.Count() == 1 && services.Affine.CanDetachPriorityTask(), "stable detach removes only the borrowed global-manager task adapter");
	driver.Clear();
	services.Close();
}

static void TestLinkOrderAbsoluteBaseAndPersistentTies() {
	Services services;
	const auto folder = services.Construct(services.Layers.LayerFolderClass());
	const auto parent = services.Construct(services.Affine.AffineLayerClass(), folder);
	const auto child = services.Construct(services.Affine.AffineLayerClass(), folder);
	const auto sibling = services.Construct(services.Affine.AffineLayerClass(), folder);
	const auto parent_handle = services.Layers.Handle(parent);
	const auto child_handle = services.Layers.Handle(child);
	const auto sibling_handle = services.Layers.Handle(sibling);
	services.Set(child, "setOrder", std::int32_t{-1});
	services.Set(child, "setLink", parent);
	services.Set(parent, "setAbsoluteBase", -10.0f);
	services.Set(child, "setAbsoluteBase", 20.0f);
	services.Affine.UpdatePriorityOrder();
	Require(services.Tree.State(child_handle).Priority == 20 && services.Tree.State(parent_handle).Priority == -9 && services.Tree.State(sibling_handle).Priority == 2, "signed link ordering ranks before per-owner absolute bases are added");
	Require(services.Order(folder) == std::vector<LayerHandle>{parent_handle, sibling_handle, child_handle}, "actual containing Layer manager sorts published values rather than substituting affine membership order");

	services.Set(child, "setLink", {});
	services.Set(child, "setOrder", std::int32_t{0});
	for (const auto& instance : {parent, child, sibling}) {
		services.Set(instance, "setAbsoluteBase", 16777216.0f);
	}
	services.Affine.UpdatePriorityOrder();
	Require(services.Tree.State(child_handle).Priority == 16777216.0f && services.Tree.State(parent_handle).Priority == 16777216.0f && services.Tree.State(sibling_handle).Priority == 16777218.0f, "equivalent affine roots retain the prior sorted membership and native rank-to-binary32 rounding");
	Require(services.Order(folder) == std::vector<LayerHandle>{parent_handle, child_handle, sibling_handle}, "equal published priority preserves actual manager's existing tie order");
	services.Affine.ReleaseInstance(child);
	Require(services.Affine.PriorityOrderDirty(), "actual affine membership removal marks native global order dirty");
	services.Affine.UpdatePriorityOrder();
	Require(services.Tree.State(parent_handle).Priority == 16777216.0f && services.Tree.State(sibling_handle).Priority == 16777216.0f, "later manager update closes rank gaps after explicit native membership removal");
	services.Close();
}

static void TestPositionOnlyGetterAndLeafComparatorOffsets() {
	Services services;
	const auto folder = services.Construct(services.Layers.LayerFolderClass());
	const auto parent = services.Construct(services.Affine.AffineLayerClass(), folder);
	const auto child = services.Construct(services.Affine.AffineLayerClass(), folder);
	services.Set(parent, "setZpos", 10.0f);
	services.Set(parent, "setOffsetz", 2.0f);
	services.Set(parent, "setOriginz", 3.0f);
	services.Set(child, "setZpos", 4.0f);
	services.Set(child, "setOffsetz", 5.0f);
	services.Set(child, "setOriginz", 6.0f);
	services.Set(child, "setLink", parent);
	const auto getter = services.Vm.GetSlot(child, std::string("getCalczpos"));
	const auto calculated = services.Vm.Invoke(getter, std::array<SquirrelValue, 1>{child}).Value;
	Require(calculated == SquirrelValue{14.0f}, "native getCalczpos sums ancestor position only, excluding both own and parent offset/origin");
	services.Vm.ResetExecution();
	services.Affine.UpdatePriorityOrder();
	Require(services.Tree.State(services.Layers.Handle(child)).Priority == 0 && services.Tree.State(services.Layers.Handle(parent)).Priority == 1, "native comparator adds only each current owner's own offset/origin to its position-only recursive value");
	services.Close();
}

static void TestActualReparentAndDetachedNativeRecord() {
	Services services;
	const auto first_folder = services.Construct(services.Layers.LayerFolderClass());
	const auto second_folder = services.Construct(services.Layers.LayerFolderClass());
	const auto first = services.Construct(services.Affine.AffineLayerClass(), first_folder);
	const auto second = services.Construct(services.Affine.AffineLayerClass(), second_folder);
	const auto first_handle = services.Layers.Handle(first);
	const auto second_handle = services.Layers.Handle(second);
	services.Set(second, "setZpos", 1.0f);
	services.Affine.UpdatePriorityOrder();
	services.Set(second, "setOwner", first_folder);
	Require(!services.Affine.PriorityOrderDirty() && services.Order(first_folder) == std::vector<LayerHandle>{second_handle, first_handle}, "real scene-owner reparenting consumes existing published priority independently from affine link/order dirty state");
	services.Tree.Detach(second_handle);
	services.Set(second, "setAbsoluteBase", 12.0f);
	services.Affine.UpdatePriorityOrder();
	Require(!services.Tree.State(second_handle).Attached && services.Tree.State(second_handle).Priority == 12.0f && services.Order(first_folder) == std::vector<LayerHandle>{first_handle}, "global affine membership can publish to a live detached native record without inventing manager attachment");
	services.Set(second, "setOwner", second_folder);
	Require(services.Order(second_folder) == std::vector<LayerHandle>{second_handle}, "actual reattachment uses the retained native priority field");
	services.Close();
}

static void TestLiveLookupMutationAndMembershipPreflight() {
	std::size_t callbacks = 0;
	Services services;
	const auto folder = services.Construct(services.Layers.LayerFolderClass());
	const auto first = services.Construct(services.Affine.AffineLayerClass(), folder);
	const auto second = services.Construct(services.Affine.AffineLayerClass(), folder);
	const auto first_handle = services.Layers.Handle(first);
	const auto second_handle = services.Layers.Handle(second);
	const auto diagnostic_class = services.Vm.CreateClass();
	SquirrelValue diagnostic = services.Vm.CreateInstance(diagnostic_class);
	services.Vm.SetInstanceReleaseHook(diagnostic, [&](auto& vm, const auto&) {
		++callbacks;
		RequireError([&] {
			services.Affine.UpdatePriorityOrder();
		},
		    "cannot reenter");
		RequireError([&] {
			services.Affine.ReleaseInstance(first);
		},
		    "active priority sort");
		Require(services.Tree.Contains(first_handle) && services.Objects.HasInstance(first), "explicit membership cleanup rejects before native matrix/common roots change");
		static_cast<void>(vm.Invoke(vm.GetSlot(second, std::string("setZpos")), std::array<SquirrelValue, 2>{second, 7.0f}));
	});
	services.Vm.SetLastError(diagnostic);
	diagnostic = {};
	services.Affine.UpdatePriorityOrder();
	services.Vm.ResetExecution();
	Require(callbacks == 1 && services.Tree.State(second_handle).Priority == 0 && services.Tree.State(first_handle).Priority == 1 && !services.Affine.PriorityOrderDirty(), "real VM lookup release effects remain visible to live native comparison; manager clears dirty after publication");
	services.Close();
}

static void TestStableDriverReleaseAndNoHiddenInstanceRoot() {
	Services services;
	const auto driver_layer = services.Construct(services.Layers.LayerClass());
	auto& driver = services.Layers.MotionTasks(driver_layer).Group(sgre::scene::MotionTaskGroup::CommonFirst);
	services.Affine.AttachPriorityTask(driver);
	SquirrelValue instance = services.Construct(services.Affine.AffineLayerClass());
	const auto handle = services.Layers.Handle(instance);
	const auto weak = services.Vm.CreateWeakReference(instance);
	driver.Update();
	instance = {};
	Require(std::holds_alternative<std::monostate>(services.Vm.ResolveWeakReference(weak)) && !services.Tree.Contains(handle) && services.Affine.PriorityOrderDirty(), "registered global task retains no script affine receiver while stable native retirement removes membership");
	driver.Update();
	Require(!services.Affine.PriorityOrderDirty(), "empty dirty membership consumes the native manager phase without a fake resource operation");
	std::array<sgre::scene::ParallelTaskHandle, 1> order;
	static_cast<void>(driver.WriteOrder(order));
	auto detached = driver.Extract(order[0]);
	Require(!services.Affine.CanDetachPriorityTask(), "detached adapter ownership remains active until the concrete lease releases it");
	RequireError([&] {
		services.Affine.DetachPriorityTask();
	},
	    "transferred adapter remains alive");
	detached.Release();
	Require(services.Affine.CanDetachPriorityTask(), "actual lease retirement records completed adapter release without a script self root");
	services.Affine.DetachPriorityTask();
	services.Affine.AttachPriorityTask(driver);
	services.Layers.ReleaseInstance(driver_layer);
	Require(services.Affine.CanDetachPriorityTask(), "actual group-owner cleanup reports released task before group storage expires");
	services.Affine.DetachPriorityTask();
	services.Close();
}

static void TestLiveWorkspaceCallbacksAndGuardRestoration() {
	sgre::scene::AffineLayerOrderWorkspace workspace(8);
	std::array<sgre::scene::AffineLayerOrderNode, 3> graph;
	graph[0].Order = 2;
	graph[1].Order = 1;
	graph[2].Order = 0;
	std::array<std::uint32_t, 3> order{0, 1, 2};
	std::size_t calls = 0;
	const sgre::scene::AffineLayerOrderWorkspace::Comparison compare = [&](auto left, auto right) {
		++calls;
		RequireError([&] {
			workspace.Sort(graph, order);
		},
		    "cannot reenter");

		return sgre::scene::CompareAffineLayerOrder(graph, left, right);
	};
	workspace.Sort(order, compare);
	Require(order == std::array<std::uint32_t, 3>{2, 1, 0} && calls != 0, "live callback sort uses the existing native list merge while recursive workspace mutation rejects");
	RequireError([&] {
		workspace.Sort(order, [](auto, auto) -> bool {
			throw std::runtime_error("live comparison sentinel");
		});
	},
	    "live comparison sentinel");
	workspace.Sort(graph, order);
	Require(order == std::array<std::uint32_t, 3>{2, 1, 0}, "comparison failure restores the workspace before independent immutable graph reuse");
}

int main() {
	try {
		TestActualContainingOrderAndTaskPhase();
		TestLinkOrderAbsoluteBaseAndPersistentTies();
		TestPositionOnlyGetterAndLeafComparatorOffsets();
		TestActualReparentAndDetachedNativeRecord();
		TestLiveLookupMutationAndMembershipPreflight();
		TestStableDriverReleaseAndNoHiddenInstanceRoot();
		TestLiveWorkspaceCallbacksAndGuardRestoration();
		std::cout << "7 actual affine priority attachment groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
