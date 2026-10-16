#include "sgre/script/layer_bindings.hpp"

#include "raw_texture_fixture.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using SquirrelValue = sgre::script::SquirrelValue;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected raw common binding safety rejection");
}

static SquirrelValue Call(sgre::script::SquirrelVm& vm, const SquirrelValue& receiver, std::string_view name, std::initializer_list<SquirrelValue> tail = {}) {
	std::vector<SquirrelValue> arguments{receiver};
	arguments.insert(arguments.end(), tail.begin(), tail.end());

	return vm.Invoke(vm.GetSlot(receiver, std::string(name)), arguments).Value;
}

struct RawBindingFixture {
	sgre::scene::tests::TaskFixtureBackend Backend;
	sgre::scene::LayerTree Tree{16};
	sgre::scene::ParallelTaskSet Driver;
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptLayerBindings Layers;
	SquirrelValue Parent;
	SquirrelValue Instance;
	sgre::scene::ParallelTaskHandle Background;
	std::unique_ptr<sgre::scene::RawLayerOwner> Owner;
	const SquirrelValue* ClosingReceiver = nullptr;
	std::vector<std::string> Events;
	bool BackgroundReleased = false;

	static sgre::script::SquirrelVmLimits Limits() {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;

		return limits;
	}

	RawBindingFixture() : Vm(Limits()), Objects(Vm), Layers(Vm, Objects, Tree) {
		const std::array<SquirrelValue, 1> root{Vm.RootTable()};
		Parent = Vm.Invoke(Layers.LayerFolderClass(), root).Value;
		Instance = Vm.CreateInstance(Vm.CreateClass(Layers.LayerBaseClass()));
		sgre::scene::ParallelTaskCallbacks background;
		background.Release = [this](sgre::scene::ParallelTaskHandle) {
			BackgroundReleased = true;
		};
		Background = Driver.Append(std::move(background));
		sgre::scene::RawLayerRetirement retirement;
		retirement.DropParent = [this] {
			Require(ClosingReceiver != nullptr, "fixture retirement uses the actual borrowed native receiver");
			Require(Driver.State(Background).Status == sgre::scene::TaskStatus::Retire && !Tree.State(Owner->Handle()).Attached && Backend.Live, "native raw parent phase follows background retirement and unlink with texture live");
			Layers.DropRawTextureParent(*ClosingReceiver);
			Events.push_back("parent");
		};
		retirement.ReleaseObject = [this] {
			Require(ClosingReceiver != nullptr && !Backend.Live && Tree.Contains(Owner->Handle()), "Object phase uses retained common identity after actual texture release");
			Layers.ReleaseRawTextureObject(*ClosingReceiver);
			Events.push_back("Object");
		};
		Owner = std::make_unique<sgre::scene::RawLayerOwner>(Tree, std::make_unique<sgre::scene::RawTextureTask>(Backend, sgre::resource::MotionPoint{2, 2}), Driver, Background, std::move(retirement), Layers.RawTextureAttachment(Parent));
		Layers.AttachRawTextureInstance(Instance, Owner->Handle(), Parent, [this](bool smoothing) {
			Owner->TextureTask().SetFiltering(smoothing ? 1u : 0u, smoothing ? 1u : 0u);
		},
		    [this](const SquirrelValue& receiver) {
			    Close(receiver);
		    });
		// Retire the independent setup invocation's persistent VM temporary before weak-parent tests.
		Vm.ResetExecution();
	}

	~RawBindingFixture() {
		try {
			Vm.Close();
		} catch (...) {
		}

		try {
			Driver.Clear();
		} catch (...) {
		}
	}

	void Close(const SquirrelValue& receiver) {
		ClosingReceiver = &receiver;
		try {
			Owner->Close();
		} catch (...) {
			ClosingReceiver = nullptr;

			throw;
		}

		ClosingReceiver = nullptr;
	}
};

static void TestInheritedPropertiesAndOrderedObjectTail() {
	RawBindingFixture fixture;
	const auto handle = fixture.Owner->Handle();
	Require(fixture.Layers.Handle(fixture.Instance) == handle && fixture.Tree.State(handle).Kind == sgre::scene::LayerKind::RawTexture && fixture.Objects.HasInstance(fixture.Instance), "actual raw leaf registers its common native state without a motion alias");
	Require(Call(fixture.Vm, fixture.Instance, "getOwner") == fixture.Parent, "raw common getter resolves original weak parent");
	static_cast<void>(Call(fixture.Vm, fixture.Instance, "setSmoothing", {true}));
	Require(fixture.Tree.State(handle).Smoothing && fixture.Owner->TextureTask().Texture().State().DesiredSampler.FilterWord == 21, "native raw smoothing updates common flag and actual mutable texture filters");
	static_cast<void>(Call(fixture.Vm, fixture.Instance, "setOffset", {3.0f, 4.0f}));
	static_cast<void>(Call(fixture.Vm, fixture.Instance, "setVisible", {false}));
	Require(fixture.Tree.State(handle).Transform.Offset == sgre::scene::LayerPoint{3, 4} && !fixture.Tree.State(handle).Visible, "inherited raw properties act on the genuine common tree leaf");
	const auto listener = fixture.Vm.CreateInstance(fixture.Objects.ObjectClass());
	fixture.Objects.AttachInstance(listener);
	const auto notifications = std::make_shared<std::size_t>(0);
	fixture.Objects.Subscribe(fixture.Instance, listener, [&, handle, notifications](const SquirrelValue&) {
		Require(!fixture.Backend.Live && fixture.Tree.Contains(handle) && !fixture.Tree.State(handle).Attached, "raw Object notifications follow actual texture and parent disposal");
		RequireError([&] {
			static_cast<void>(Call(fixture.Vm, fixture.Instance, "setOwner", {fixture.Parent}));
		},
		    "during native retirement");
		++*notifications;

		return true;
	});
	fixture.Layers.ReleaseInstance(fixture.Instance);
	Require(*notifications == 1 && fixture.Owner->IsClosed() && !fixture.Tree.Contains(handle) && !fixture.Objects.HasInstance(fixture.Instance), "native common hook completes actual ownership and Object registry cleanup");
	Require(fixture.Events == std::vector<std::string>{"parent", "Object"} && !fixture.BackgroundReleased && fixture.Driver.Contains(fixture.Background), "background remains driver owned after wrapper retirement");
	fixture.Driver.Update();
	Require(fixture.BackgroundReleased && !fixture.Driver.Contains(fixture.Background), "later actual driver dispatch deletes its retired background entry");
}

static void TestWeakParentAndAutomaticNativeRelease() {
	RawBindingFixture fixture;
	const auto handle = fixture.Owner->Handle();
	const auto parent_handle = fixture.Layers.Handle(fixture.Parent);
	const auto weak_parent = fixture.Vm.CreateWeakReference(fixture.Parent);
	fixture.Parent = {};
	fixture.Vm.RethrowReleaseFailure();
	Require(!fixture.Tree.Contains(parent_handle) && fixture.Tree.Contains(handle) && !fixture.Tree.State(handle).Attached && std::holds_alternative<std::monostate>(fixture.Vm.ResolveWeakReference(weak_parent)), "raw common registration retains weak parent wrapper, not its target");
	Require(std::holds_alternative<std::monostate>(Call(fixture.Vm, fixture.Instance, "getOwner")), "parent retirement clears actual raw common weak owner");
	const auto weak_raw = fixture.Vm.CreateWeakReference(fixture.Instance);
	fixture.Instance = {};
	fixture.Vm.RethrowReleaseFailure();
	Require(fixture.Owner->IsClosed() && !fixture.Tree.Contains(handle) && std::holds_alternative<std::monostate>(fixture.Vm.ResolveWeakReference(weak_raw)), "automatic singular native hook retires unrooted raw ownership");
}

static void TestCallbackPreflightAndVmClose() {
	RawBindingFixture fixture;
	const auto handle = fixture.Owner->Handle();
	bool rejected = false;
	sgre::scene::ParallelTaskCallbacks callback;
	callback.Update = [&](sgre::scene::ParallelTaskHandle) {
		RequireError([&] {
			fixture.Layers.ReleaseInstance(fixture.Instance);
		},
		    "during an active task callback");
		rejected = true;
	};
	const auto task = fixture.Owner->Group(sgre::scene::RawLayerTaskGroup::CommonFirst).Append(std::move(callback));
	fixture.Owner->Group(sgre::scene::RawLayerTaskGroup::CommonFirst).Update();
	Require(rejected && fixture.Backend.Live && fixture.Tree.State(handle).Attached && fixture.Objects.HasInstance(fixture.Instance), "failed preflight leaves complete common/Object/texture ownership usable");
	static_cast<void>(Call(fixture.Vm, fixture.Instance, "setSmoothing", {true}));
	fixture.Owner->Group(sgre::scene::RawLayerTaskGroup::CommonFirst).SetStatus(task, sgre::scene::TaskStatus::Retire);
	fixture.Vm.Close();
	fixture.Vm.RethrowReleaseFailure();
	Require(fixture.Vm.IsClosed() && fixture.Owner->IsClosed() && !fixture.Tree.Contains(handle), "VM Close keeps real services live through finalized-class raw retirement");
}

static void TestRegistrationAndStageGuards() {
	RawBindingFixture fixture;
	const auto handle = fixture.Owner->Handle();
	RequireError([&] {
		fixture.Layers.DropRawTextureParent(fixture.Instance);
	},
	    "follow actual manager unlink");
	RequireError([&] {
		fixture.Layers.ReleaseRawTextureObject(fixture.Instance);
	},
	    "completed common parent");
	RequireError([&] {
		fixture.Layers.AttachRawTextureInstance(fixture.Instance, handle, fixture.Parent, [](bool) {
		},
		    [](const SquirrelValue&) {
		    });
	},
	    "already attached");
	const auto unregistered = fixture.Vm.CreateInstance(fixture.Vm.CreateClass(fixture.Layers.LayerBaseClass()));
	RequireError([&] {
		fixture.Layers.AttachRawTextureInstance(unregistered, fixture.Layers.Handle(fixture.Parent), {}, [](bool) {
		},
		    [](const SquirrelValue&) {
		    });
	},
	    "genuine LayerBase");
	Require(!fixture.Objects.HasInstance(unregistered) && fixture.Tree.Contains(handle), "invalid leaf registration leaves all actual owners unchanged");
	const auto screen_class = fixture.Vm.CreateClass(fixture.Objects.ObjectClass());
	const auto screen = fixture.Vm.CreateInstance(screen_class);
	const auto screen_root = fixture.Tree.CreateScreenRoot();
	fixture.Layers.ConfigureScreenOwners(screen_class, [&](const SquirrelValue& candidate) {
		Require(candidate == screen, "Screen resolver receives exact original owner identity");

		return screen_root;
	});
	static_cast<void>(Call(fixture.Vm, fixture.Instance, "setOwner", {screen}));
	Require(fixture.Tree.State(handle).ScreenOwner == screen_root && Call(fixture.Vm, fixture.Instance, "getOwner") == screen, "actual raw inherited setOwner can move to a genuine Screen manager");
	fixture.Layers.DetachScreenOwners(screen);
	Require(!fixture.Tree.State(handle).Attached && std::holds_alternative<std::monostate>(Call(fixture.Vm, fixture.Instance, "getOwner")), "Screen cleanup detaches raw common leaf and clears its weak owner");
	fixture.Layers.ReleaseInstance(fixture.Instance);
	fixture.Tree.ReleaseScreenRoot(screen_root);
}

int main() {
	try {
		TestInheritedPropertiesAndOrderedObjectTail();
		TestWeakParentAndAutomaticNativeRelease();
		TestCallbackPreflightAndVmClose();
		TestRegistrationAndStageGuards();
		std::cout << "PASS raw common script ownership: four independent fixture groups\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
