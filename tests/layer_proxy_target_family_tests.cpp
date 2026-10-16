#include "sgre/script/layer_proxy_bindings.hpp"

#include "raw_texture_fixture.hpp"
#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/script/binary_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/raw_image_bindings.hpp"
#include "sgre/script/raw_texture_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iostream>
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

static sgre::script::SquirrelVmLimits VmLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static sgre::scene::LayerCameraFrameState CameraState() {
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {16, 8};
	state.RawScissor = {0, 0, 16, 8};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;

	return state;
}

class TargetFamilyFixture {
public:
	TargetFamilyFixture()
	    : Vm(VmLimits()), Objects(Vm), Layers(Vm, Objects, Tree), Binary(Vm, Objects), Images(Vm, Objects, Binary), Camera(CameraState(), Models, [](const auto&) {
	      }),
	      Raw(Vm, Objects, Layers, Images, Binary, Tree, Backend, Priority), Proxies(Vm, Objects, Layers, Tree, Services()) {
		Vm.InstallRecoveredBaseLibrary();
		BaseClass = Vm.CreateClass(Layers.LayerBaseClass());
		BaseClassRoot = Vm.Retain(BaseClass);
		Vm.NewSlot(BaseClass, std::string("constructor"), Vm.CreateNativeClosure("genuine family fixture Base constructor", [this](auto&, auto arguments) {
			Layers.ConstructBaseInstance(arguments);

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
	}

	~TargetFamilyFixture() {
		try {
			Close();
		} catch (const std::exception& error) {
			std::cerr << "LayerProxy target family cleanup: " << error.what() << '\n';
		}
	}

	void Close() {
		Vm.Close();
		Vm.RethrowReleaseFailure();
		Priority.Update();
		Require(Priority.Count() == 0 && !Backend.Live && Tree.ChildCount() == 0, "actual family fixture balances native layers, Raw texture and pending Priority task");
	}

	SquirrelValue Call(const SquirrelValue& receiver, std::string_view method, std::initializer_list<SquirrelValue> inputs = {}) {
		std::vector<SquirrelValue> arguments{receiver};
		arguments.insert(arguments.end(), inputs);
		const auto result = Vm.Invoke(Vm.GetSlot(receiver, std::string(method)), arguments).Value;
		Vm.ResetExecution();

		return result;
	}

	SquirrelValue Proxy(const SquirrelValue& target) {
		const auto proxy = Vm.Invoke(Proxies.LayerProxyClass(), std::array<SquirrelValue, 2>{Vm.RootTable(), target}).Value;
		Vm.ResetExecution();
		Layers.AttachBaseFrame(proxy, Camera);

		return proxy;
	}

	SquirrelValue Base(const SquirrelValue& parent) {
		const auto instance = Vm.Invoke(BaseClass, std::array<SquirrelValue, 2>{Vm.RootTable(), parent}).Value;
		Vm.ResetExecution();
		Layers.AttachBaseFrame(instance, Camera);

		return instance;
	}

	SquirrelValue Folder() {
		const auto instance = Vm.Invoke(Layers.LayerFolderClass(), std::array<SquirrelValue, 1>{Vm.RootTable()}).Value;
		Vm.ResetExecution();
		sgre::script::ScriptFolderFrameServices services;
		services.State = [this](const auto& receiver) {
			return Layers.FolderFrameState(receiver);
		};
		services.Children.State = [this](auto handle) {
			return State(handle);
		};
		services.Children.Update = [this](auto handle) {
			if (Tree.State(handle).Kind == sgre::scene::LayerKind::RawTexture) {
				Raw.Update(handle);
			} else {
				Layers.DispatchBaseFrame(handle, [](const auto&, auto& frame) {
					frame.Update();
				});
			}
		};
		services.Children.Draw = [this](auto handle, auto argument) {
			Draw(handle, argument);
		};
		services.Children.PrepareDraw = [] {
			throw std::runtime_error("native Folder must preserve its enclosing camera rather than reset renderer");
		};
		Layers.AttachFolderFrame(instance, Camera, std::move(services));

		return instance;
	}

	sgre::scene::LayerFrameProcessState State(sgre::scene::LayerHandle handle) {
		if (Tree.State(handle).Kind == sgre::scene::LayerKind::RawTexture) {
			++RawStateReads;

			return Raw.ProcessState(handle);
		}

		return Layers.ProcessState(handle);
	}

	void Draw(sgre::scene::LayerHandle handle, std::uint32_t argument) {
		if (Proxies.Owns(handle)) {
			Proxies.Draw(handle, argument);
		} else if (Tree.State(handle).Kind == sgre::scene::LayerKind::Folder) {
			++FolderDispatches;
			Layers.DispatchFolderFrame(handle, [argument](const auto&, auto& frame) {
				frame.Draw(argument);
			});
		} else if (Tree.State(handle).Kind == sgre::scene::LayerKind::RawTexture) {
			++RawDispatches;
			Raw.Draw(handle, argument);
		} else {
			Layers.DispatchBaseFrame(handle, [argument](const auto&, auto& frame) {
				frame.Draw(argument);
			});
		}
	}

	sgre::script::ScriptLayerProxyDrawServices Services() {
		sgre::script::ScriptLayerProxyDrawServices services;
		services.State = [this](auto handle) {
			return State(handle);
		};
		services.Draw = [this](auto handle, auto argument) {
			Draw(handle, argument);
		};

		return services;
	}

	sgre::scene::tests::TaskFixtureBackend Backend;
	sgre::scene::ParallelTaskSet Priority{16};
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::scene::LayerTree Tree{32};
	sgre::script::ScriptLayerBindings Layers;
	sgre::script::ScriptBinaryBindings Binary;
	sgre::script::ScriptRawImageBindings Images;
	std::array<sgre::scene::MotionScissorMatrix, 1> Models;
	sgre::scene::LayerCameraDrawScope Camera;
	sgre::script::ScriptRawTextureBindings Raw;
	sgre::script::ScriptLayerProxyBindings Proxies;
	SquirrelValue BaseClass;
	sgre::script::SquirrelHostRoot BaseClassRoot;
	std::size_t FolderDispatches = 0;
	std::size_t RawStateReads = 0;
	std::size_t RawDispatches = 0;
};

static void TestActualFolderManagerCameraAndIndependentRetirement() {
	for (const bool target_first : {false, true}) {
		TargetFamilyFixture fixture;
		const auto target = fixture.Folder();
		const auto leaf = fixture.Base(target);
		const auto leaf_handle = fixture.Layers.Handle(leaf);
		const auto proxy = fixture.Proxy(target);
		const auto handle = fixture.Layers.Handle(proxy);
		std::size_t target_updates = 0;
		std::size_t leaf_draws = 0;
		bool through_proxy = true;
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [&](auto) {
			++target_updates;
		};
		callbacks.Draw = [&](auto, auto argument) {
			Require(argument == 0xfedcba98u && fixture.Camera.Depth() == (through_proxy ? 4u : 3u), "actual Folder manager dispatches its genuine child Base camera beneath target and proxy cameras");
			const auto& model = fixture.Camera.CurrentModel().Coefficients;
			Require(model[0] == (through_proxy ? 10 : 5) && model[5] == (through_proxy ? 21 : 7) && model[3] == (through_proxy ? -29 : -11) && model[7] == (through_proxy ? -37 : -13), "Folder virtual draw preserves actual nested transforms without renderer reset");
			++leaf_draws;
		};
		static_cast<void>(fixture.Layers.BaseTasks(leaf).Children().Append(std::move(callbacks)));
		fixture.Call(proxy, "setScale", {2.0f, 3.0f});
		fixture.Call(proxy, "setOffset", {7.0f, -2.0f});
		fixture.Call(target, "setScale", {5.0f, 7.0f});
		fixture.Call(target, "setOffset", {11.0f, 13.0f});
		fixture.Layers.DispatchBaseFrame(handle, [](const auto&, auto& frame) {
			frame.Update();
		});
		Require(target_updates == 0, "proxy own update never advances the real Folder manager or its target children");
		fixture.Proxies.Draw(handle, 0xfedcba98u);
		Require(leaf_draws == 1 && fixture.FolderDispatches == 1 && fixture.Camera.Depth() == 1, "proxy calls genuine Folder virtual frame and restores both target and child cameras");
		if (target_first) {
			fixture.Layers.ReleaseInstance(target);
			Require(fixture.Tree.Contains(leaf_handle) && !fixture.Tree.State(leaf_handle).Attached && !fixture.Tree.State(leaf_handle).Owner, "Folder target retirement detaches independent retained child without reviving a root registration");
			fixture.Proxies.Draw(handle, 0xfedcba98u);
			Require(leaf_draws == 1 && fixture.FolderDispatches == 1, "native-null Folder target skips proxy without touching surviving detached child");
			fixture.Proxies.ReleaseInstance(proxy);
		} else {
			fixture.Proxies.ReleaseInstance(proxy);
			through_proxy = false;
			fixture.Layers.DispatchFolderFrame(fixture.Layers.Handle(target), [](const auto&, auto& frame) {
				frame.Draw(0xfedcba98u);
			});
			Require(leaf_draws == 2 && fixture.Tree.Contains(leaf_handle), "proxy-first retirement preserves actual Folder manager and independent child frame");
		}
		fixture.Close();
	}
}

static void TestActualRawFramePacketPriorityAndIndependentRetirement() {
	for (const bool target_first : {false, true}) {
		TargetFamilyFixture fixture;
		std::size_t packets = 0;
		bool through_proxy = true;
		std::size_t target_updates = 0;
		fixture.Raw.ConfigureFrameOwners(fixture.Camera, [&](const auto& receiver, auto& task, const auto& packet, auto argument) {
			Require(fixture.Tree.State(fixture.Layers.Handle(receiver)).Kind == sgre::scene::LayerKind::RawTexture && &task == &fixture.Raw.Owner(receiver).TextureTask(), "Raw target dispatch lends exact actual owner and its mutable texture task");
			Require(argument == 0xfedcba98u && fixture.Camera.Depth() == (through_proxy ? 3u : 2u), "Raw frame receives full draw argument beneath its own camera and proxy camera");
			Require(packet.Vertices[0].Position.X == 0 && packet.Vertices[1].Position.Y == 1 && packet.Vertices[2].Position.X == 2 && packet.Vertices[3].Uv.X == 1 && packet.Vertices[3].Uv.Y == 1 && packet.Color == 0xffffffffu && packet.TextureAttributes == 4, "real Raw task prepares the accepted bounded 2x1 XY/UV/color/attribute packet");
			task.Texture().Bind(0, sgre::scene::MutableTextureStage::Pixel);
			++packets;
		});
		const auto derived = fixture.Vm.CreateClass(fixture.Raw.RawTextureClass());
		fixture.Vm.NewSlot(derived, std::string("onProcess"), fixture.Vm.CreateNativeClosure("actual Raw target onProcess counter", [&](auto&, auto) {
			++target_updates;

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
		const auto target = fixture.Vm.Invoke(derived, std::array<SquirrelValue, 3>{fixture.Vm.RootTable(), std::int32_t{2}, std::int32_t{1}}).Value;
		fixture.Vm.ResetExecution();
		const auto proxy = fixture.Proxy(target);
		const auto handle = fixture.Layers.Handle(proxy);
		Require(fixture.Backend.Live && fixture.Priority.Count() == 1 && fixture.Vm.IsInstanceOf(target, fixture.Layers.LayerBaseClass()), "real Raw constructor owns bounded texture and actual deferred Priority entry as a genuine LayerBase target");
		fixture.Layers.DispatchBaseFrame(handle, [](const auto&, auto& frame) {
			frame.Update();
		});
		Require(target_updates == 0, "proxy own update never invokes genuine Raw onProcess");
		const std::array<std::uint8_t, 6> rgb{255, 0, 0, 0, 0, 255};
		fixture.Raw.Owner(target).TextureTask().RestoreRgb(rgb);
		fixture.Raw.Update(fixture.Layers.Handle(target));
		Require(target_updates == 1 && fixture.Backend.UploadCount == 0, "one explicit target update advances its real process while upload remains deferred to actual texture bind");
		fixture.Proxies.Draw(handle, 0xfedcba98u);
		Require(packets == 1 && fixture.RawDispatches == 1 && fixture.RawStateReads == 1 && fixture.Backend.BindCount == 1 && fixture.Backend.UploadCount == 1 && fixture.Backend.Uploaded.size() == 8 && fixture.Camera.Depth() == 1, "real Proxy selects Raw virtual dispatch rather than generic Base and balances nested frame state");
		if (target_first) {
			fixture.Raw.ReleaseInstance(target);
			Require(!fixture.Backend.Live && fixture.Priority.Count() == 1, "target-first native Raw retirement destroys actual texture but preserves pending Priority disposal");
			fixture.Proxies.Draw(handle, 0xfedcba98u);
			Require(packets == 1 && fixture.RawDispatches == 1 && fixture.RawStateReads == 1 && target_updates == 1, "native-null Raw target never touches retired texture, state service or target update");
			fixture.Proxies.ReleaseInstance(proxy);
			fixture.Priority.Update();
			Require(fixture.Priority.Count() == 0, "actual Priority traversal acknowledges source-first background retirement once");
		} else {
			fixture.Proxies.ReleaseInstance(proxy);
			through_proxy = false;
			fixture.Raw.Draw(fixture.Layers.Handle(target), 0xfedcba98u);
			Require(packets == 2 && fixture.Backend.Live && fixture.Priority.Count() == 1 && target_updates == 1, "proxy-first retirement preserves independent Raw packet/texture/background owner without advancing its process");
		}
		fixture.Close();
	}
}

int main() {
	try {
		TestActualFolderManagerCameraAndIndependentRetirement();
		TestActualRawFramePacketPriorityAndIndependentRetirement();
		std::cout << "PASS 2 actual LayerProxy Folder/Raw target-family groups with 4 retirement orders\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
