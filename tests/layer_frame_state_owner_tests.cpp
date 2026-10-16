#include "sgre/script/layer_bindings.hpp"

#include "sgre/scene/graphics_model_stack.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("required actual frame-state owner rejection absent");
}

struct Fixture {
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	sgre::scene::LayerTree Tree;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	sgre::scene::GraphicsModelStack Models;
	sgre::scene::LayerCameraFrameState Frame;
	sgre::scene::LayerCameraDrawScope Scope;
	sgre::script::ScriptLayerBindings Layers;

	static sgre::scene::LayerCameraFrameState CameraState() {
		sgre::scene::LayerCameraFrameState state;
		state.Viewport = {100, 100};
		state.RawScissor = {0, 0, 100, 100};
		state.RoundedScissor = state.RawScissor;
		state.AppliedRoundedScissor = state.RawScissor;

		return state;
	}

	Fixture() : Objects(Vm), Psb(Vm, Objects), Tree(16), Cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		            throw std::runtime_error("frame-state fixture unexpectedly requested asset upload");
	            },
	                                                         [](auto) {
		                                                         throw std::runtime_error("frame-state fixture unexpectedly released an uploaded asset");
	                                                         }),
	            Catalog(Cache), Models(8), Frame(CameraState()), Scope(Models, Frame, [](const auto&) {
		            throw std::runtime_error("disabled-clip fixture unexpectedly requested physical submission");
	            }),
	            Layers(Vm, Objects, Tree) {
		Models.Reset();
		Layers.ConfigureMotionOwners(Psb, Catalog, Cache);
	}

	~Fixture() noexcept {
		try {
			Vm.Close();
			Vm.RethrowReleaseFailure();
		} catch (...) {
			std::terminate();
		}
	}

	sgre::script::SquirrelValue Folder(const sgre::script::SquirrelValue& owner = {}) {
		const auto value = Vm.Invoke(Layers.LayerFolderClass(), std::array<sgre::script::SquirrelValue, 2>{Vm.RootTable(), owner}).Value;
		Vm.ResetExecution();

		return value;
	}

	void Call(const sgre::script::SquirrelValue& receiver, std::string_view name, std::vector<sgre::script::SquirrelValue> arguments = {}) {
		arguments.insert(arguments.begin(), receiver);
		static_cast<void>(Vm.Invoke(Vm.GetSlot(receiver, std::string(name)), arguments));
		Vm.ResetExecution();
	}

	void AttachFolder(const sgre::script::SquirrelValue& receiver) {
		sgre::script::ScriptFolderFrameServices services;
		services.State = [this](const auto& actual_receiver) {
			return Layers.FolderFrameState(actual_receiver);
		};
		services.Children.State = [this](auto handle) {
			return Layers.ProcessState(handle);
		};
		services.Children.Update = [this](auto handle) {
			Layers.DispatchFolderFrame(handle, [](const auto&, auto& frame) {
				frame.Update();
			});
		};
		services.Children.Draw = [this](auto handle, auto argument) {
			Layers.DispatchFolderFrame(handle, [argument](const auto&, auto& frame) {
				frame.Draw(argument);
			});
		};
		Layers.AttachFolderFrame(receiver, Scope, std::move(services));
	}
};

static void VerifyCommonState(const sgre::scene::LayerCameraParameters& camera, const sgre::scene::MotionBounds& base) {
	Require(camera.Pivot.X == 2 && camera.Pivot.Y == -3 && camera.Offset.X == 5 && camera.Offset.Y == -6, "frame state did not read actual center/offset setters");
	Require(camera.Scale.X == .5f && camera.Scale.Y == -2 && camera.RotationRadians == .375f, "frame state did not read actual scale/radian setters");
	Require(base.MinimumX == 10 && base.MinimumY == 20 && base.MaximumX == 13 && base.MaximumY == 24, "frame state changed the actual base clip endpoints");
	Require(camera.Clip.MinimumX == 1 && camera.Clip.MinimumY == 2 && camera.Clip.MaximumX == 4 && camera.Clip.MaximumY == -2, "camera state changed the independent reversed clip");
}

static void TestActualSettersAndReceivers() {
	Fixture fixture;
	const auto folder = fixture.Folder();
	const auto motion = fixture.Vm.Invoke(fixture.Layers.LayerClass(), std::array<sgre::script::SquirrelValue, 1>{fixture.Vm.RootTable()}).Value;
	fixture.Vm.ResetExecution();
	for (const auto& receiver : {folder, motion}) {
		fixture.Call(receiver, "setCenter", {2.0f, -3.0f});
		fixture.Call(receiver, "setOffset", {5.0f, -6.0f});
		fixture.Call(receiver, "setScale", {.5f, -2.0f});
		fixture.Call(receiver, "setAngleRad", {.375f});
		fixture.Call(receiver, "setBaseClip", {10.0f, 20.0f, 3.0f, 4.0f});
		fixture.Call(receiver, "setClip", {1.0f, 2.0f, 3.0f, -4.0f});
	}
	const auto folder_state = fixture.Layers.FolderFrameState(folder);
	VerifyCommonState(folder_state.Camera, folder_state.BaseClip);
	fixture.Call(motion, "setObjectClip", {8.0f, 9.0f, -2.0f, 4.0f});
	const auto motion_state = fixture.Layers.MotionFrameState(motion);
	VerifyCommonState(motion_state.Camera, motion_state.BaseClip);
	Require(motion_state.ObjectClip.MinimumX == 8 && motion_state.ObjectClip.MinimumY == 9 && motion_state.ObjectClip.MaximumX == 6 && motion_state.ObjectClip.MaximumY == 13, "Motion object clip was replaced by inherited camera clipping");
	fixture.Call(motion, "resetObjectClip");
	Require(fixture.Layers.MotionFrameState(motion).ObjectClip.MaximumX == -1 && motion_state.ObjectClip.MaximumX == 6, "frame state value retained a mutable owner borrow");
	fixture.Vm.DispatchBorrowedReceiver(fixture.Vm.BorrowObjectIdentity(folder), [&](const auto& receiver) {
		VerifyCommonState(fixture.Layers.FolderFrameState(receiver).Camera, folder_state.BaseClip);
	});
	RequireFailure([&] {
		static_cast<void>(fixture.Layers.FolderFrameState(motion));
	});
	RequireFailure([&] {
		static_cast<void>(fixture.Layers.MotionFrameState(folder));
	});
	fixture.Layers.ReleaseInstance(folder);
	RequireFailure([&] {
		static_cast<void>(fixture.Layers.FolderFrameState(folder));
	});
}

static void TestRealNestedFolderDispatch() {
	Fixture fixture;
	const auto parent = fixture.Folder();
	const auto child = fixture.Folder(parent);
	const auto grandchild = fixture.Folder(child);
	fixture.Call(parent, "setOffset", {2.0f, 0.0f});
	fixture.Call(child, "setOffset", {3.0f, 0.0f});
	fixture.AttachFolder(parent);
	fixture.AttachFolder(child);
	fixture.AttachFolder(grandchild);
	std::size_t draws = 0;
	sgre::scene::ParallelTaskCallbacks observer;
	observer.Draw = [&](auto, auto argument) {
		Require(argument == 0xfedcba98 && fixture.Models.Current().Coefficients[3] == -5 && fixture.Scope.Depth() == 3, "nested real Folder dispatch did not consume its own current camera state");
		++draws;
	};
	static_cast<void>(fixture.Layers.FolderTasks(grandchild).CommonFirst().Append(std::move(observer)));
	fixture.Layers.DispatchFolderFrame(fixture.Layers.Handle(parent), [](const auto&, auto& frame) {
		frame.Draw(0xfedcba98);
	});
	Require(draws == 1 && fixture.Models.Depth() == 1, "actual nested state readers did not restore the surrounding graphics model");
}

int main() {
	try {
		TestActualSettersAndReceivers();
		TestRealNestedFolderDispatch();
		std::cout << "Actual Folder/Motion state setters, scoped receivers, independent clips and nested Folder cameras passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
