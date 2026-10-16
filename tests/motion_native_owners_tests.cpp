#include "sgre/script/motion_native_owners.hpp"

#include "sgre/core/frame_clock.hpp"
#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected Motion wrapper rejection: " + std::string(message));
}

static sgre::script::SquirrelVmLimits Limits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

class Fixture {
public:
	Fixture() : Vm(Limits()), Objects(Vm), Psb(Vm, Objects), Cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		            throw std::runtime_error("root-only wrapper fixture unexpectedly uploaded a texture");
	            },
	                                                             [](auto) {
		                                                             throw std::runtime_error("root-only wrapper fixture unexpectedly released a texture");
	                                                             }),
	            Assets(Cache), Tree(8), Layers(Vm, Objects, Tree), Clock(0), Class(Vm.CreateClass(Objects.ObjectClass())) {
		Layers.ConfigureMotionOwners(Psb, Assets, Cache);
		Leaf = Vm.Invoke(Layers.LayerClass(), std::array<sgre::script::SquirrelValue, 1>{Vm.RootTable()}).Value;
		// Discard the invocation result root; this fixture keeps its explicit Leaf handle.
		Vm.ResetExecution();
		for (std::size_t index = 0; index < Seed.size(); ++index) {
			Seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17;
		}
		sgre::scene::MotionManagerRuntimeServices services;
		services.ReadSourceAliases = [this](auto project) {
			return Layers.MotionResources(Leaf).SourceAliases(project);
		};
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Layers.MotionResources(Leaf).Secondary(), Clock, Seed, std::move(services), 16);
		Layers.AttachMotionRuntime(Leaf, *Runtime);
		Owners = std::make_unique<sgre::script::ScriptMotionNativeOwners>(Vm, Objects, Layers, Class, 2);
		Require(!Vm.HasSlot(Vm.RootTable(), std::string("Motion")), "an ownership fixture must not publish a partial Motion class");
	}

	~Fixture() {
		Owners->Close();
		if (std::holds_alternative<sgre::script::SquirrelObjectHandle>(Leaf)) {
			Layers.ReleaseInstance(Leaf);
		}
		Runtime->Close();
		Vm.Close();
		Cache.Close();
	}

	[[nodiscard]] sgre::script::SquirrelValue New(sgre::scene::MotionTaskInsertion insertion = sgre::scene::MotionTaskInsertion::Append) {
		auto instance = Vm.CreateInstance(Class);
		Owners->ConstructNew(instance, Leaf, *Runtime, false, insertion);

		return instance;
	}

	[[nodiscard]] sgre::scene::ParallelTaskSet& Second() {
		return Layers.MotionTasks(Leaf).Group(sgre::scene::MotionTaskGroup::Second);
	}

	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Assets;
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers;
	sgre::core::FrameClock Clock;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	sgre::script::SquirrelValue Class;
	sgre::script::SquirrelValue Leaf;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	std::unique_ptr<sgre::script::ScriptMotionNativeOwners> Owners;
};

static void TestWeakReceiverAndDeferredRetirement() {
	Fixture fixture;
	auto instance = fixture.New();
	const auto identity = fixture.Vm.BorrowObjectIdentity(instance);
	auto task = fixture.Owners->Task(instance);
	Require(fixture.Owners->InstanceCount() == 1 && fixture.Second().Count() == 1 && fixture.Runtime->PlayerCount() == 1 && !fixture.Owners->EventEnabled(instance), "actual new wrapper starts with one Second payload and forwarding disabled");
	fixture.Second().Update();
	task.PostUpdate();
	fixture.Second().Draw(0xfedcba98);
	Require(task.Player().Rows().empty() && fixture.Runtime->Context().Size() == 0, "genuine unselected root update and Draw must produce its actual empty queue");
	instance = {};
	fixture.Vm.RethrowReleaseFailure();
	Require(!fixture.Vm.ResolveObjectIdentity(identity) && fixture.Owners->InstanceCount() == 0, "weak wrapper registry must not retain the VM receiver");
	Require(task.IsLive() && fixture.Second().Count() == 1 && fixture.Second().State(task.Handle()).Status == sgre::scene::TaskStatus::Retire, "wrapper destruction marks status2 while the actual group retains the player");
	RequireFailure([&] {
		task.PostUpdate();
	},
	    "pending retirement");
	fixture.Second().Update();
	Require(!task.IsLive() && fixture.Runtime->PlayerCount() == 0 && fixture.Second().Count() == 0, "real status2 traversal retires concrete ownership before any later weak view access");
}

static void TestParentFirstDetachAndRuntimeClose() {
	Fixture fixture;
	auto instance = fixture.New();
	auto task = fixture.Owners->Task(instance);
	fixture.Owners->SetEventEnabled(instance, true);
	const auto leaf_identity = fixture.Vm.BorrowObjectIdentity(fixture.Leaf);
	fixture.Leaf = {};
	fixture.Vm.RethrowReleaseFailure();
	Require(!fixture.Vm.ResolveObjectIdentity(leaf_identity) && fixture.Owners->IsDetached(instance) && fixture.Owners->EventEnabled(instance), "Layer destruction expires its weak parent and marks the actual surviving wrapper detached");
	Require(!task.IsLive() && fixture.Runtime->PlayerCount() == 0, "actual Layer retires the Second payload before runtime/source owners");
	RequireFailure([&] {
		static_cast<void>(fixture.Owners->Task(instance));
	},
	    "expired");
	RequireFailure([&] {
		static_cast<void>(fixture.Runtime->Manager());
	},
	    "closed");
	fixture.Owners->ReleaseInstance(instance);
	Require(fixture.Owners->InstanceCount() == 0, "detached wrapper release does not dereference the expired player or source manager");
	instance = {};
	fixture.Vm.RethrowReleaseFailure();
}

static void TestRegistrationAndConstructionFailures() {
	Fixture fixture;
	auto instance = fixture.Vm.CreateInstance(fixture.Class);
	RequireFailure([&] {
		fixture.Owners->ConstructNew(instance, fixture.Leaf, *fixture.Runtime, true);
	},
	    "synchronous mode");
	sgre::scene::MotionManagerRuntimeServices services;
	services.ReadSourceAliases = [&fixture](auto project) {
		return fixture.Layers.MotionResources(fixture.Leaf).SourceAliases(project);
	};
	sgre::scene::MotionManagerRuntime unregistered(fixture.Layers.MotionResources(fixture.Leaf).Primary(), fixture.Clock, fixture.Seed, std::move(services));
	RequireFailure([&] {
		fixture.Owners->ConstructNew(instance, fixture.Leaf, unregistered, false);
	},
	    "registered Motion runtime preflight");
	Require(fixture.Owners->InstanceCount() == 0 && !fixture.Objects.HasInstance(instance) && fixture.Second().Count() == 0 && fixture.Runtime->PlayerCount() == 0, "unsupported async/unregistered runtime must leave no committed VM Object/Layer/task ownership");
	auto first = fixture.New();
	auto second = fixture.New(sgre::scene::MotionTaskInsertion::Prepend);
	const auto first_task = fixture.Owners->Task(first);
	const auto second_task = fixture.Owners->Task(second);
	std::array<sgre::scene::ParallelTaskHandle, 2> order;
	Require(fixture.Second().WriteOrder(order) == 2 && order[0] == second_task.Handle() && order[1] == first_task.Handle(), "actual append/prepend uses only the real Second group");
	RequireFailure([&] {
		fixture.Owners->ConstructNew(instance, fixture.Leaf, *fixture.Runtime, false);
	},
	    "bounded capacity");
	first = {};
	second = {};
	fixture.Vm.RethrowReleaseFailure();
	fixture.Second().Update();
}

static void TestScopedResourceBorrowBeforeMutation() {
	Fixture fixture;
	const auto& resources = fixture.Layers.MotionResources(fixture.Leaf);
	{
		const auto borrowed = resources.BorrowEventDelivery();
		Require(!resources.CanClose(), "queued-string delivery borrow must block all containing source-owner close preflight");
		RequireFailure([&] {
			fixture.Layers.RequireCanReleaseInstance(fixture.Leaf);
		},
		    "source owner");
		const std::array<sgre::script::SquirrelValue, 2> arguments{fixture.Leaf, std::int32_t{1}};
		RequireFailure([&] {
			static_cast<void>(fixture.Vm.Invoke(fixture.Vm.GetSlot(fixture.Leaf, std::string("unregisterMotionResource")), arguments));
		},
		    "event delivery");
		Require(resources.Count() == 0 && fixture.Runtime->PlayerCount() == 0 && fixture.Second().Count() == 0, "blocked unregister and close perform no preceding source/task mutation");
	}
	Require(resources.CanClose(), "delivery completion releases only the observational borrow guard");
}

int main() {
	try {
		TestWeakReceiverAndDeferredRetirement();
		TestParentFirstDetachAndRuntimeClose();
		TestRegistrationAndConstructionFailures();
		TestScopedResourceBorrowBeforeMutation();
		std::cout << "Motion native new-only wrapper lifecycle source fixtures PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
