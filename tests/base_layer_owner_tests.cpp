#include "sgre/script/layer_bindings.hpp"

#include <array>
#include <cstdint>
#include <functional>
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
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected genuine SQLayerBase owner rejection");
}

static sgre::scene::LayerCameraFrameState CameraState() {
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {100, 100};
	state.RawScissor = {0, 0, 100, 100};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;

	return state;
}

class FixtureCleanup {
public:
	explicit FixtureCleanup(std::function<void()> cleanup)
	    : m_Cleanup(std::move(cleanup)) {
	}
	~FixtureCleanup() {
		try {
			m_Cleanup();
		} catch (const std::exception& error) {
			std::cerr << "fixture retirement: " << error.what() << '\n';
		}
	}

private:
	std::function<void()> m_Cleanup;
};

static void TestActualBaseOwner() {
	using sgre::script::SquirrelValue;
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::scene::LayerTree tree(8);
	sgre::script::ScriptLayerBindings layers(vm, objects, tree);
	std::array<sgre::scene::MotionScissorMatrix, 1> models;
	sgre::scene::LayerCameraDrawScope renderer(CameraState(), models, [](const auto&) {
	});
	const auto derived = vm.CreateClass(layers.LayerBaseClass());
	const auto constructor = vm.CreateNativeClosure("native diagnostic SQLayerBase constructor", [&](auto&, const auto arguments) {
		layers.ConstructBaseInstance(arguments);

		return sgre::script::SquirrelNativeResult::NoValues();
	});
	vm.NewSlot(derived, std::string("constructor"), constructor);
	std::size_t script_process_calls = 0;
	vm.NewSlot(derived, std::string("onProcess"), vm.CreateNativeClosure("unused script onProcess", [&](auto&, auto) {
		++script_process_calls;

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	const auto parent = vm.Invoke(layers.LayerFolderClass(), std::array<SquirrelValue, 1>{vm.RootTable()}).Value;
	vm.ResetExecution();
	const auto instance = vm.Invoke(derived, std::array<SquirrelValue, 2>{vm.RootTable(), parent}).Value;
	vm.ResetExecution();
	const auto handle = layers.Handle(instance);
	Require(tree.State(handle).Kind == sgre::scene::LayerKind::Base && tree.State(handle).Owner == layers.Handle(parent), "derived factory uses the genuine common leaf and original owner");
	RequireError([&] {
		static_cast<void>(tree.CreateFolder(handle));
	},
	    "folder child manager");
	layers.AttachBaseFrame(instance, renderer);
	Require(layers.CommonTasks(instance).Parallel().Count() == 3, "native common root has its actual three children");
	bool can_close = false;
	std::vector<int> events;
	auto& groups = layers.BaseTasks(instance);
	std::array<sgre::scene::ParallelTaskSet*, 3> native_groups{&groups.CommonFirst(), &groups.CommonSecond(), &groups.Children()};
	for (std::size_t index = 0; index < native_groups.size(); ++index) {
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [&, index](auto) {
			events.push_back(static_cast<int>(index) + 1);
			RequireError([&] {
				layers.ReleaseInstance(instance);
			},
			    "traversal");
			Require(tree.Contains(handle), "active traversal rejects retirement before concrete state changes");
		};
		callbacks.Draw = [&, index](auto, std::uint32_t argument) {
			Require(argument == 0xfedcba98 && renderer.Depth() == (index == 2 ? 2 : 1), "common groups precede the actual native camera scope");
			events.push_back(static_cast<int>(index) + 11);
		};
		callbacks.Release = [&, index](auto) {
			Require(!tree.State(handle).Attached, "native base unlinks its manager before parallel deletion");
			events.push_back(static_cast<int>(index) + 21);
		};
		static_cast<void>(native_groups[index]->Append(std::move(callbacks)));
	}
	FixtureCleanup cleanup([&] {
		can_close = true;
		vm.Close();
		vm.RethrowReleaseFailure();
	});
	layers.DispatchBaseFrame(handle, [](const auto&, auto& frame) {
		frame.Update();
	});
	Require(events == std::vector<int>{1, 2, 3} && script_process_calls == 0, "native base update preserves physical order and its inert native callback");
	events.clear();
	layers.DispatchBaseFrame(handle, [](const auto&, auto& frame) {
		frame.Draw(0xfedcba98);
	});
	Require(events == std::vector<int>{11, 12, 13} && renderer.Depth() == 1, "base draw restores the actual containing camera");
	layers.ConfigureBaseRetirement(
	    instance, [&](const auto& receiver) {
		    Require(vm.BorrowObjectIdentity(receiver) == vm.BorrowObjectIdentity(instance), "preflight receives the genuine native receiver");
		    if (!can_close) {
			    throw std::runtime_error("actual derived resource remains borrowed");
		    }
	    },
	    [&](const auto& receiver) {
		    Require(vm.BorrowObjectIdentity(receiver) == vm.BorrowObjectIdentity(instance) && tree.State(handle).Attached && objects.HasInstance(receiver), "derived cleanup precedes the common base destructor");
		    events.push_back(20);
	    });
	events.clear();
	RequireError([&] {
		layers.ReleaseInstance(instance);
	},
	    "derived resource");
	Require(events.empty() && tree.State(handle).Attached && groups.Children().Count() == 1, "failed derived preflight preserves native graph and membership");
	can_close = true;
	layers.ReleaseInstance(instance);
	Require(events == std::vector<int>{20, 21, 22, 23} && !tree.Contains(handle) && !objects.HasInstance(instance), "derived cleanup and common parallel phases precede Object retirement");
	layers.ReleaseInstance(parent);
	vm.Close();
	vm.RethrowReleaseFailure();
}

int main() {
	try {
		TestActualBaseOwner();
		std::cout << "PASS genuine SQLayerBase derived ownership, frame order/camera, native inert callback, traversal and derived retirement guards\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
