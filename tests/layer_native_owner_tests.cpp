#include "sgre/script/layer_bindings.hpp"

#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using SquirrelValue = sgre::script::SquirrelValue;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected native Layer ownership failure: " + std::string(expected));
}

struct Services {
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	sgre::scene::LayerTree Tree;
	std::function<void()> OnUpload;
	std::uint32_t Uploads = 0;
	std::uint32_t Releases = 0;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	sgre::script::ScriptLayerBindings Layers;

	static sgre::script::SquirrelVmLimits Limits() {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;

		return limits;
	}

	Services() : Vm(Limits()), Objects(Vm), Psb(Vm, Objects), Tree(16), Cache([this](auto, const auto&) {
		             if (OnUpload) {
			             OnUpload();
		             }

		             return sgre::render::SpriteTextureHandle{++Uploads, 1, &Uploads};
	             },
	                                                                        [this](auto) {
		                                                                        ++Releases;
	                                                                        }),
	             Catalog(Cache), Layers(Vm, Objects, Tree) {
		Vm.InstallRecoveredBaseLibrary();
		Layers.ConfigureMotionOwners(Psb, Catalog, Cache);
	}

	~Services() {
		try {
			Vm.Close();
		} catch (...) {
		}
	}

	SquirrelValue Leaf() {
		return Vm.Invoke(Layers.LayerClass(), std::array<SquirrelValue, 1>{Vm.RootTable()}).Value;
	}
};

static void CheckDirectDerivedConstruction() {
	Services services;
	const auto folder = services.Vm.Invoke(services.Layers.LayerFolderClass(), std::array<SquirrelValue, 1>{services.Vm.RootTable()}).Value;
	const auto derived_class = services.Vm.CreateClass(services.Layers.LayerClass());
	bool called = false;
	const auto constructor = services.Vm.CreateNativeClosure("derived common Layer constructor fixture", [&](auto& vm, const auto arguments) {
		Require(arguments.size() == 2 && vm.NativeStackSize() == 2 && vm.NativeStackValue(1) == arguments[0] && vm.NativeStackValue(2) == folder, "derived factory enters with its actual receiver/owner native frame");
		services.Layers.ConstructMotionInstance(arguments);
		Require(vm.NativeStackSize() == 2 && services.Objects.HasInstance(arguments[0]) && services.Tree.State(services.Layers.Handle(arguments[0])).Owner == services.Layers.Handle(folder), "common construction adds no nested constructor frame or result cell");
		called = true;

		return sgre::script::SquirrelNativeResult::NoValues();
	});
	services.Vm.NewSlot(derived_class, std::string("constructor"), constructor);
	const auto instance = services.Vm.Invoke(derived_class, std::array<SquirrelValue, 2>{services.Vm.RootTable(), folder}).Value;
	Require(called && services.Vm.IsInstanceOf(instance, derived_class) && services.Vm.IsInstanceOf(instance, services.Layers.LayerClass()) && services.Layers.MotionResources(instance).Count() == 0, "genuine derived instance preserves actual Layer/common resources and inheritance");
	services.Layers.RequireCanReleaseInstance(instance);
	services.Layers.ReleaseInstance(instance);
	Require(!services.Objects.HasInstance(instance), "direct common owner retires through its established native bridge");
	RequireFailure([&] {
		services.Layers.ConstructMotionInstance({});
	},
	    "receiver");
}

static void CheckAllActiveTaskGroups() {
	Services services;
	for (const auto kind : {sgre::scene::MotionTaskGroup::CommonFirst, sgre::scene::MotionTaskGroup::CommonSecond, sgre::scene::MotionTaskGroup::First, sgre::scene::MotionTaskGroup::Second}) {
		const auto instance = services.Leaf();
		const auto handle = services.Layers.Handle(instance);
		auto& group = services.Layers.MotionTasks(instance).Group(kind);
		std::size_t guarded = 0;
		std::size_t released = 0;
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [&](auto) {
			RequireFailure([&] {
				services.Layers.RequireCanReleaseInstance(instance);
			},
			    "task groups is active");
			RequireFailure([&] {
				services.Layers.ReleaseInstance(instance);
			},
			    "task groups is active");
			Require(services.Tree.Contains(handle) && services.Objects.HasInstance(instance) && released == 0, "active update preflight preserves every native owner before cleanup begins");
			++guarded;
		};
		callbacks.Draw = [&](auto, std::uint32_t argument) {
			Require(argument == 9, "task draw forwards its actual native argument");
			RequireFailure([&] {
				services.Layers.ReleaseInstance(instance);
			},
			    "task groups is active");
			++guarded;
		};
		callbacks.Release = [&](auto) {
			++released;
		};
		static_cast<void>(group.Append(std::move(callbacks)));
		group.Update();
		group.Draw(9);
		Require(guarded == 2 && released == 0, "both active traversal modes retain borrowed task state");
		services.Layers.RequireCanReleaseInstance(instance);
		services.Layers.ReleaseInstance(instance);
		Require(released == 1 && !services.Tree.Contains(handle) && !services.Objects.HasInstance(instance), "inactive native release still retires each concrete task and common owner exactly once");
	}
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue atom;
	atom.Data = std::move(value);

	return atom;
}

static std::shared_ptr<sgre::resource::PsbDocument> TextureDocument() {
	using PsbArray = sgre::resource::PsbArray;
	using PsbObject = sgre::resource::PsbObject;
	auto texture = Atom(PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{1})}, {"height", Atom(std::int64_t{1})}, {"truncated_width", Atom(std::int64_t{1})}, {"truncated_height", Atom(std::int64_t{1})}, {"pixel", Atom(sgre::resource::PsbChunkReference{})}});
	texture.SerializedOffset = 64;
	auto source = Atom(PsbObject{{"texture", std::move(texture)}, {"icon", Atom(PsbObject{})}});
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 127;
	document->SerializedSize = 128;
	document->Chunks.push_back({127, 1});
	document->Root = Atom(PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Synthetic"))}, {"easing", Atom(PsbArray{})}, {"source", Atom(PsbObject{{"source", std::move(source)}})}, {"object", Atom(PsbObject{})}});
	document->Root.SerializedOffset = document->Header.RootOffset;

	return document;
}

static void CheckMutationPreflight() {
	Services services;
	const auto instance = services.Leaf();
	const auto handle = services.Layers.Handle(instance);
	const auto document = TextureDocument();
	auto backing = std::make_shared<std::vector<std::uint8_t>>(128);
	backing->back() = 128;
	const auto psb = services.Psb.WrapDocument(document, backing);
	std::size_t guarded = 0;
	services.OnUpload = [&] {
		Require(!services.Layers.MotionResources(instance).CanClose(), "source upload occurs inside actual script resource mutation");
		RequireFailure([&] {
			services.Layers.RequireCanReleaseInstance(instance);
		},
		    "source owner is mutating");
		RequireFailure([&] {
			services.Layers.ReleaseInstance(instance);
		},
		    "source owner is mutating");
		Require(services.Tree.Contains(handle) && services.Objects.HasInstance(instance), "nested upload retirement rejects before dropping native state");
		++guarded;
	};
	static_cast<void>(services.Vm.Invoke(services.Vm.GetSlot(instance, std::string("registerMotionResource")), std::array<SquirrelValue, 2>{instance, psb}));
	Require(guarded == 1 && services.Uploads == 1 && services.Releases == 0 && services.Layers.MotionResources(instance).CanClose(), "registration completes after guarded callback and restores close eligibility");
	services.Layers.ReleaseInstance(instance);
	Require(services.Releases == 1 && !services.Tree.Contains(handle), "ordinary source cleanup remains complete after blocked reentrant release");
}

static void CheckLiveManagerOwnerPreflight() {
	Services services;
	const auto folder = services.Vm.Invoke(services.Layers.LayerFolderClass(), std::array<SquirrelValue, 1>{services.Vm.RootTable()}).Value;
	const auto leaf = services.Vm.Invoke(services.Layers.LayerClass(), std::array<SquirrelValue, 2>{services.Vm.RootTable(), folder}).Value;
	const auto folder_handle = services.Layers.Handle(folder);
	const auto leaf_handle = services.Layers.Handle(leaf);
	const auto clear = services.Vm.GetSlot(folder, std::string("clear"));
	const auto owner = services.Vm.GetSlot(leaf, std::string("getOwner"));
	std::size_t visits = 0;
	services.Tree.VisitChildren(folder_handle, [&](auto current) {
		Require(current == leaf_handle, "actual manager must visit its registered Layer");
		RequireFailure([&] {
			services.Layers.ReleaseInstance(leaf);
		},
		    "current traversal member");
		RequireFailure([&] {
			services.Layers.ReleaseInstance(folder);
		},
		    "owner cannot retire");
		RequireFailure([&] {
			static_cast<void>(services.Vm.Invoke(clear, std::array<SquirrelValue, 1>{folder}));
		},
		    "clear during child traversal");
		Require(services.Objects.HasInstance(folder) && services.Objects.HasInstance(leaf) && services.Tree.State(leaf_handle).Attached, "rejected release must preserve Object roots and actual manager membership");
		Require(services.Vm.Invoke(owner, std::array<SquirrelValue, 1>{leaf}).Value == folder, "clear rejection must precede dropping the child's weak parent root");
		++visits;
	});
	Require(visits == 1, "guarded manager traversal must finish without losing its current member");
	static_cast<void>(services.Vm.Invoke(clear, std::array<SquirrelValue, 1>{folder}));
	Require(services.Tree.ChildCount(folder_handle) == 0 && std::holds_alternative<std::monostate>(services.Vm.Invoke(owner, std::array<SquirrelValue, 1>{leaf}).Value), "ordinary clear must still detach the retained child and its weak owner");
	services.Layers.ReleaseInstance(leaf);
	services.Layers.ReleaseInstance(folder);
	Require(!services.Tree.Contains(leaf_handle) && !services.Tree.Contains(folder_handle), "guard rollback must permit complete later owner release");
}

int main() {
	try {
		CheckDirectDerivedConstruction();
		CheckAllActiveTaskGroups();
		CheckMutationPreflight();
		CheckLiveManagerOwnerPreflight();
		std::cout << "Four native Layer owner fixture groups passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
