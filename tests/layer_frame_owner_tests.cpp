#include "sgre/script/layer_bindings.hpp"

#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
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
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected Layer frame ownership rejection");
}

static sgre::scene::LayerCameraFrameState CameraState() {
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {1280, 720};
	state.RawScissor = {0, 0, 1280, 720};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;

	return state;
}

struct Services {
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	sgre::scene::LayerTree Tree;
	std::vector<sgre::scene::MotionIntegerScissor> Submissions;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	sgre::scene::LayerCameraDrawScope Renderer;
	sgre::script::ScriptLayerBindings Layers;

	static sgre::script::SquirrelVmLimits Limits() {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;

		return limits;
	}

	Services()
	    : Vm(Limits()), Objects(Vm), Psb(Vm, Objects), Tree(16), Cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		      throw std::runtime_error("owner fixture unexpectedly requested an asset upload");
	      },
	                                                                 [](auto) {
		                                                                 throw std::runtime_error("owner fixture unexpectedly retired an uploaded asset");
	                                                                 }),
	      Catalog(Cache), Renderer(CameraState(), {}, [this](const auto& scissor) {
		      Submissions.push_back(scissor);
	      }),
	      Layers(Vm, Objects, Tree) {
		Layers.ConfigureMotionOwners(Psb, Catalog, Cache);
	}

	~Services() {
		try {
			Vm.Close();
		} catch (...) {
		}
	}

	SquirrelValue Folder() {
		const auto instance = Vm.Invoke(Layers.LayerFolderClass(), std::array<SquirrelValue, 1>{Vm.RootTable()}).Value;
		Vm.ResetExecution();

		return instance;
	}

	SquirrelValue Motion() {
		const auto instance = Vm.Invoke(Layers.LayerClass(), std::array<SquirrelValue, 1>{Vm.RootTable()}).Value;
		Vm.ResetExecution();

		return instance;
	}

	void Close() {
		Vm.Close();
		Vm.RethrowReleaseFailure();
	}
};

static void CheckCommonOwnerDefaultsAndRetirementOrder(bool motion) {
	Services services;
	const auto instance = motion ? services.Motion() : services.Folder();
	const auto handle = services.Layers.Handle(instance);
	auto& process = services.Layers.CommonTasks(instance);
	const auto state = services.Layers.ProcessState(handle);
	Require(state.Status == sgre::scene::TaskStatus::Active && !state.ThreadedUpdate && process.Serial().Status() == sgre::scene::TaskStatus::Active && process.Parallel().Status() == sgre::scene::TaskStatus::Active, "actual Folder record owns native default process and both common groups");
	Require(!process.IsDrawPaused(sgre::scene::CommonProcessGroup::Serial) && !process.IsDrawPaused(sgre::scene::CommonProcessGroup::Parallel), "embedded common draw pause starts clear independently of Layer visibility");
	std::vector<int> releases;
	sgre::scene::SerialTaskCallbacks serial;
	serial.Release = [&] {
		Require(services.Tree.Contains(handle) && services.Objects.HasInstance(instance), "serial retirement precedes actual common Object/tree cleanup");
		releases.push_back(1);
	};
	static_cast<void>(process.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	sgre::scene::ParallelTaskCallbacks parallel;
	parallel.Release = [&](auto) {
		Require(services.Tree.Contains(handle) && services.Objects.HasInstance(instance), "parallel retirement precedes actual common Object/tree cleanup");
		releases.push_back(2);
	};
	static_cast<void>(process.Parallel().Append(std::move(parallel)));
	services.Layers.ReleaseInstance(instance);
	Require(releases == (motion ? std::vector<int>{1, 2} : std::vector<int>{2, 1}) && !services.Tree.Contains(handle) && !services.Objects.HasInstance(instance), "actual Folder uses parallel then serial; SQLayer preserves serial then parallel");
	RequireError([&] {
		static_cast<void>(services.Layers.ProcessState(handle));
	},
	    "no live native scene owner");
	services.Close();
}

static void TestCommonOwnerDefaultsAndRetirementOrder() {
	CheckCommonOwnerDefaultsAndRetirementOrder(false);
	CheckCommonOwnerDefaultsAndRetirementOrder(true);
}

static void TestActiveCommonPreflightBeforeRootCleanup() {
	Services services;
	const auto instance = services.Folder();
	const auto handle = services.Layers.Handle(instance);
	auto& process = services.Layers.CommonTasks(instance);
	std::size_t calls = 0;
	std::size_t releases = 0;
	const auto check = [&] {
		RequireError([&] {
			services.Layers.ReleaseInstance(instance);
		},
		    "common process traversal");
		Require(services.Tree.Contains(handle) && services.Objects.HasInstance(instance) && releases == 0, "active common preflight must reject before any derived or Object root drops");
		++calls;
	};
	sgre::scene::SerialTaskCallbacks serial;
	serial.Update = [&](auto&) {
		check();
	};
	serial.Release = [&] {
		++releases;
	};
	static_cast<void>(process.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	sgre::scene::ParallelTaskCallbacks parallel;
	parallel.Update = [&](auto) {
		check();
	};
	parallel.Release = [&](auto) {
		++releases;
	};
	static_cast<void>(process.Parallel().Append(std::move(parallel)));
	process.Serial().Update();
	process.Parallel().Update();
	Require(calls == 2 && releases == 0, "both genuine common task sets reject unsafe owner cleanup");
	services.Layers.ReleaseInstance(instance);
	Require(releases == 2, "stable owner retirement closes each common task exactly once");
	services.Close();
}

static void TestFailureCompletingCommonCleanup() {
	Services services;
	const auto instance = services.Folder();
	const auto handle = services.Layers.Handle(instance);
	auto& process = services.Layers.CommonTasks(instance);
	std::vector<int> releases;
	sgre::scene::SerialTaskCallbacks serial;
	serial.Release = [&] {
		releases.push_back(1);
		throw std::runtime_error("serial release sentinel");
	};
	static_cast<void>(process.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	sgre::scene::ParallelTaskCallbacks parallel;
	parallel.Release = [&](auto) {
		releases.push_back(2);
		throw std::runtime_error("parallel release sentinel");
	};
	static_cast<void>(process.Parallel().Append(std::move(parallel)));
	RequireError([&] {
		services.Layers.ReleaseInstance(instance);
	},
	    "parallel release sentinel");
	Require(releases == std::vector<int>{2, 1} && !services.Tree.Contains(handle) && !services.Objects.HasInstance(instance), "stable failure cleanup attempts both groups and base owners");
	services.Close();
}

static void TestExistingReceiverFrameServicesAndNoRoot() {
	Services services;
	SquirrelValue instance = services.Motion();
	const auto handle = services.Layers.Handle(instance);
	const auto identity = services.Vm.BorrowObjectIdentity(instance);
	const auto weak = services.Vm.CreateWeakReference(instance);
	const SquirrelValue* active_receiver = nullptr;
	std::size_t state_reads = 0;
	std::vector<int> source_calls;
	bool fail_primary = false;
	sgre::script::ScriptMotionFrameServices frame_services;
	frame_services.State = [&](const auto& receiver) {
		Require(&receiver == active_receiver && services.Layers.Handle(receiver) == handle && services.Layers.ProcessState(handle).Status == sgre::scene::TaskStatus::Active, "frame state consumes actual existing record and const operation receiver without redispatch");
		++state_reads;
		sgre::scene::MotionLayerFrameState state;
		state.ObjectClip = {-0.25f, -0.25f, 0.25f, 0.25f};

		return state;
	};
	frame_services.SecondaryDraw = [&](const auto& receiver, std::uint32_t argument) {
		Require(&receiver == active_receiver && services.Vm.BorrowObjectIdentity(receiver) == identity && argument == 0xfedcba98 && services.Renderer.Depth() == 1, "secondary concrete boundary borrows existing checked receiver inside actual camera scope");
		source_calls.push_back(1);
	};
	frame_services.PrimaryDraw = [&](const auto& receiver, std::uint32_t argument) {
		Require(&receiver == active_receiver && services.Vm.BorrowObjectIdentity(receiver) == identity && argument == 0xfedcba98 && services.Renderer.Depth() == 1, "primary concrete boundary shares the same receiver scope after object clipping");
		source_calls.push_back(2);
		if (fail_primary) {
			throw std::runtime_error("primary drawing sentinel");
		}
	};
	services.Layers.AttachMotionFrame(instance, services.Renderer, std::move(frame_services));
	const sgre::script::ScriptLayerBindings::MotionFrameOperation draw = [&](const auto& receiver, auto& frame) {
		active_receiver = &receiver;
		RequireError([&] {
			services.Layers.ReleaseInstance(receiver);
		},
		    "reenter its release");
		RequireError([&] {
			services.Layers.DispatchMotionFrame(receiver, [](const auto&, auto&) {
				throw std::runtime_error("reentrant frame operation unexpectedly ran");
			});
		},
		    "non-reentrant operation");
		frame.Draw(0xfedcba98);
		active_receiver = nullptr;
	};
	services.Layers.DispatchMotionFrame(handle, draw);
	services.Vm.DispatchBorrowedReceiver(identity, [&](const auto& existing_receiver) {
		services.Layers.DispatchMotionFrame(existing_receiver, draw);
	});
	Require(state_reads == 6 && source_calls == std::vector<int>{1, 2, 1, 2} && services.Submissions.size() == 4 && services.Renderer.Depth() == 0, "handle borrow and outer-manager borrow both use real scoped state/source services and restore renderer state");
	fail_primary = true;
	RequireError([&] {
		services.Vm.DispatchBorrowedReceiver(identity, [&](const auto& existing_receiver) {
			services.Layers.DispatchMotionFrame(existing_receiver, draw);
		});
	},
	    "primary drawing sentinel");
	active_receiver = nullptr;
	fail_primary = false;
	services.Layers.DispatchMotionFrame(handle, draw);
	Require(services.Renderer.Depth() == 0, "callback failure restores both receiver operation and camera scope for subsequent dispatch");
	instance = {};
	Require(std::holds_alternative<std::monostate>(services.Vm.ResolveWeakReference(weak)) && !services.Tree.Contains(handle), "configured frame services and checked dispatch must add no hidden receiver root");
	services.Close();
}

static void TestStateValidationAndRequiredServices() {
	Services services;
	const auto instance = services.Motion();
	const auto handle = services.Layers.Handle(instance);
	RequireError([&] {
		services.Layers.AttachMotionFrame(instance, services.Renderer, {});
	},
	    "real state/source services");
	Require(services.Layers.CommonTasks(instance).Parallel().Count() == 0, "missing concrete services reject before attaching root adapters");
	sgre::scene::LayerFrameProcessState state;
	state.Status = static_cast<sgre::scene::TaskStatus>(3);
	RequireError([&] {
		services.Layers.SetProcessState(instance, state);
	},
	    "0..2 domain");
	Require(services.Layers.ProcessState(handle).Status == sgre::scene::TaskStatus::Active, "invalid native process state cannot mutate actual containing record");
	state.Status = sgre::scene::TaskStatus::Complete;
	state.ThreadedUpdate = true;
	services.Layers.SetProcessState(instance, state);
	Require(services.Layers.ProcessState(handle).Status == sgre::scene::TaskStatus::Complete && services.Layers.ProcessState(handle).ThreadedUpdate, "containing status/thread metadata remains distinct from each owned task group");
	services.Layers.ReleaseInstance(instance);
	services.Close();
}

int main() {
	try {
		TestCommonOwnerDefaultsAndRetirementOrder();
		TestActiveCommonPreflightBeforeRootCleanup();
		TestFailureCompletingCommonCleanup();
		TestExistingReceiverFrameServicesAndNoRoot();
		TestStateValidationAndRequiredServices();
		std::cout << "5 ordinary Layer common/frame ownership groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
