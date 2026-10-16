#include "sgre/script/raw_texture_bindings.hpp"

#include "raw_texture_fixture.hpp"
#include "resource_meter_fixture.hpp"
#include "sgre/resource/resource_manager_process.hpp"
#include "sgre/scene/bitmap_font_assets.hpp"
#include "sgre/scene/default_driver_scene.hpp"
#include "sgre/scene/resource_meter_process.hpp"
#include "sgre/script/binary_bindings.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/raw_image_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using sgre::scene::DefaultDriverTaskGroup;
using sgre::scene::TaskStatus;
using sgre::script::SquirrelValue;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected actual RawTex Priority owner rejection");
}

static sgre::script::SquirrelVmLimits FixtureVmLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

class ActualPriorityFixture {
public:
	explicit ActualPriorityFixture(sgre::script::ScriptRawTextureLimits limits = {}) : Manager(Mounts, Background), Textures(std::bind_front(&ActualPriorityFixture::UploadFont, this), std::bind_front(&ActualPriorityFixture::RetireFont, this)), Catalog(Textures), Asset(Catalog.Prepare(Document(), Backing())), Meter(Hooks, Textures, Asset, {}), Vm(FixtureVmLimits()), Objects(Vm), Layers(Vm, Objects, Tree), Binary(Vm, Objects), Images(Vm, Objects, Binary), Raw(Vm, Objects, Layers, Images, Binary, Tree, Backend, std::bind_front(&ActualPriorityFixture::ResolvePriority, this), limits) {
		Require(Resolutions == 0 && Controller.Parallel().Count() == 0 && !Backend.Live, "actual class registration must not resolve or construct the deferred native Priority owner");
	}

	sgre::render::SpriteTextureHandle UploadFont(sgre::resource::PixelFormat, const sgre::resource::RgbaImage& pixels) {
		Require(pixels.Width == 1 && pixels.Height == 1 && pixels.Pixels.size() == 4 && pixels.Pixels[0] == 0 && pixels.Pixels[1] == 0 && pixels.Pixels[2] == 0 && (pixels.Pixels[3] == 128 || pixels.Pixels[3] == 64), "actual synthetic font input must decode its independently authored A8 bytes");

		return sgre::render::SpriteTextureHandle{++Uploads, 1, &Uploads};
	}

	void RetireFont(sgre::render::SpriteTextureHandle) {
		++TextureRetirements;
	}

	sgre::scene::ParallelTaskSet& ResolvePriority() {
		++Resolutions;
		if (!Scene) {
			throw std::runtime_error("RawTex actual DefaultDriverScene Priority is not constructed");
		}
		if (DuringResolution) {
			DuringResolution();
		}

		return Scene->Group(DefaultDriverTaskGroup::Priority);
	}

	~ActualPriorityFixture() noexcept {
		try {
			Vm.Close();
			if (Scene) {
				Scene->Close();
			} else {
				Manager.Close();
				Meter.Close();
			}
			Background.Close();
			Hooks.Close();
		} catch (...) {
			std::terminate();
		}
	}
	ActualPriorityFixture(const ActualPriorityFixture&) = delete;
	ActualPriorityFixture& operator=(const ActualPriorityFixture&) = delete;

	void PublishScene() {
		const auto resolutions_before = Resolutions;
		Controller.Update([this] {
			Require(!Scene && Controller.Serial().Count() == 0 && Controller.Parallel().Count() == 0, "actual scene publication requires its original unused embedded driver groups");
			sgre::scene::DefaultDriverSceneServices services;
			services.ResourceManager.Update = [this] {
				Manager.Update();
			};
			services.ResourceManager.Draw = [this](std::uint32_t argument) {
				Manager.Draw(argument);
			};
			services.ResourceManager.Retire = [this] {
				Manager.Close();
			};
			services.ResourceMeter.Update = [this] {
				Meter.Update();
			};
			services.ResourceMeter.Draw = [this](std::uint32_t argument) {
				Meter.Draw(argument);
			};
			services.ResourceMeter.Retire = [this] {
				Meter.Close();
			};
			services.RootLayers.State = [this](auto handle) -> sgre::scene::LayerFrameProcessState {
				Require(Tree.Contains(handle), "root state requires an actual attached Layer");
				if (DispatchRawRoots && Tree.State(handle).Kind == sgre::scene::LayerKind::RawTexture) {
					return Raw.ProcessState(handle);
				}

				throw std::runtime_error("Priority-only fixture excludes unrecovered populated RawTex root frame dispatch");
			};
			services.RootLayers.Update = [this](auto handle) {
				if (DispatchRawRoots && Tree.State(handle).Kind == sgre::scene::LayerKind::RawTexture) {
					Raw.Update(handle);

					return;
				}

				throw std::runtime_error("Priority-only fixture excludes populated RawTex root frame updates");
			};
			services.RootLayers.Draw = [](auto, auto) {
				throw std::runtime_error("Priority-only fixture excludes GPU root drawing");
			};
			services.RootLayers.PrepareDraw = [] {
				throw std::runtime_error("Priority-only fixture excludes renderer reset without its actual Display");
			};
			Scene = std::make_unique<sgre::scene::DefaultDriverScene>(Controller, Tree, std::move(services), 16);
			++SetupVisits;
		});
		Require(Scene && SetupVisits == 1 && Controller.Parallel().Count() == 7 && Resolutions == resolutions_before && Meter.ProcessingCount() == 1, "actual Common onProcess must publish the original seven-root graph before any RawTex construction");
	}

	SquirrelValue Create(sgre::script::ScriptRawTextureBindings* binding = nullptr) {
		const std::array<SquirrelValue, 3> arguments{Vm.RootTable(), std::int32_t{2}, std::int32_t{1}};

		return Vm.Invoke(binding ? binding->RawTextureClass() : Raw.RawTextureClass(), arguments).Value;
	}

	void AdvanceEmptyRootScene() {
		Require(Tree.ChildCount() == 0, "actual full scene update requires no unsupported populated RawTex root in this component fixture");
		Scene->Update([this] {
			++PhaseVisits;
		});
	}

	sgre::scene::tests::TaskFixtureBackend Backend;
	sgre::scene::LayerTree Tree{16};
	sgre::scene::CommonProcessTasks Controller;
	sgre::scene::LooperHookTasks Hooks;
	sgre::resource::ArchiveMounts Mounts;
	sgre::core::BackgroundCallManager Background;
	sgre::resource::ResourceManagerProcess Manager;
	std::uint32_t Uploads = 0;
	std::uint32_t TextureRetirements = 0;
	sgre::scene::TextureCache Textures;
	sgre::scene::BitmapFontAssetCatalog Catalog;
	std::shared_ptr<const sgre::scene::LoadedBitmapFontAsset> Asset;
	sgre::scene::ResourceMeterProcess Meter;
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptLayerBindings Layers;
	sgre::script::ScriptBinaryBindings Binary;
	sgre::script::ScriptRawImageBindings Images;
	std::unique_ptr<sgre::scene::DefaultDriverScene> Scene;
	std::function<void()> DuringResolution;
	std::size_t Resolutions = 0;
	std::size_t SetupVisits = 0;
	std::size_t PhaseVisits = 0;
	bool DispatchRawRoots = false;
	sgre::script::ScriptRawTextureBindings Raw;
	std::unique_ptr<sgre::script::ScriptRawTextureBindings> Eager;
};

static void TestActualLatePriorityAndDeferredRetirement() {
	ActualPriorityFixture fixture;
	RequireError([&] {
		static_cast<void>(fixture.Create());
	},
	    "actual DefaultDriverScene Priority is not constructed");
	Require(fixture.Resolutions == 1 && fixture.Tree.ChildCount() == 0 && fixture.Backend.Events.empty() && !fixture.Backend.Live && fixture.Controller.Parallel().Count() == 0, "premature request must reject before texture, tree, group or service-owner mutation");
	fixture.PublishScene();
	const auto raw = fixture.Create();
	auto& priority = fixture.Scene->Group(DefaultDriverTaskGroup::Priority);
	std::array<sgre::scene::ParallelTaskHandle, 1> order{};
	Require(priority.WriteOrder(order) == 1 && fixture.Resolutions == 2 && fixture.Tree.ChildCount() == 1 && fixture.Backend.Live && fixture.Raw.Owner(raw).TextureTask().State().LogicalExtent.X == 2 && fixture.Raw.Owner(raw).TextureTask().State().LogicalExtent.Y == 1, "genuine script constructor must append its actual background task only to current Scene Priority");
	for (const auto group : {DefaultDriverTaskGroup::BeforeLayersFirst, DefaultDriverTaskGroup::BeforeLayersSecond, DefaultDriverTaskGroup::AfterResourceMeter}) {
		Require(fixture.Scene->Group(group).Count() == 0, "RawTex must not invent or select a different driver subgroup");
	}
	priority.Update();
	Require(priority.Count() == 1 && priority.State(order[0]).Status == TaskStatus::Active, "actual empty RawTex background remains owned in Priority across its real traversal");
	fixture.Layers.ReleaseInstance(raw);
	Require(priority.Count() == 1 && priority.State(order[0]).Status == TaskStatus::Retire && fixture.Resolutions == 2 && fixture.Tree.ChildCount() == 0 && !fixture.Backend.Live, "wrapper retirement must use the same captured Priority owner and leave its original entry pending Retire");
	fixture.AdvanceEmptyRootScene();
	Require(priority.Count() == 0 && fixture.PhaseVisits == 1 && fixture.Resolutions == 2, "actual enclosing scene traversal must retire the pending entry without another service lookup");
}

static void TestProviderReentryAndUnwind() {
	ActualPriorityFixture fixture;
	fixture.PublishScene();
	fixture.DuringResolution = [&] {
		static_cast<void>(fixture.Create());
	};
	RequireError([&] {
		static_cast<void>(fixture.Create());
	},
	    "Priority service provider cannot mutate or reenter");
	Require(fixture.Resolutions == 1 && fixture.Tree.ChildCount() == 0 && fixture.Scene->Group(DefaultDriverTaskGroup::Priority).Count() == 0 && fixture.Backend.Events.empty() && !fixture.Backend.Live, "reentrant provider failure must precede actual constructor side effects");
	fixture.DuringResolution = {};
	const auto raw = fixture.Create();
	Require(fixture.Resolutions == 2 && fixture.Backend.Live, "failed actual lookup must restore its scope before a later genuine request");
	fixture.Raw.ReleaseInstance(raw);
	fixture.AdvanceEmptyRootScene();
}

static void TestNativeTailAppendInsideActualPriorityTraversal() {
	ActualPriorityFixture fixture;
	fixture.PublishScene();
	auto& priority = fixture.Scene->Group(DefaultDriverTaskGroup::Priority);
	std::optional<SquirrelValue> created;
	std::size_t constructor_releases = 0;
	sgre::scene::ParallelTaskCallbacks constructor;
	constructor.Update = [&](auto handle) {
		created = fixture.Create();
		priority.SetStatus(handle, TaskStatus::Retire);
	};
	constructor.Draw = [](auto, auto) {
		throw std::runtime_error("construction traversal fixture excludes drawing");
	};
	constructor.Release = [&](auto) {
		Require(created.has_value() && fixture.Tree.ChildCount() == 1 && fixture.Backend.Live, "constructor task retires in the same traversal while its actual created RawTex wrapper remains live");
		++constructor_releases;
	};
	static_cast<void>(priority.Append(std::move(constructor)));
	priority.Update();
	Require(created && constructor_releases == 1 && priority.Count() == 1 && fixture.Resolutions == 1 && fixture.Backend.Live, "native tailappend remains live after same-traversal constructor retirement without a fabricated idle requirement");
	fixture.Raw.ReleaseInstance(*created);
	fixture.AdvanceEmptyRootScene();
	Require(priority.Count() == 0, "actual containing traversal must retire the marked RawTex background entry after its wrapper");
}

static void TestRetiredActualOwnersAndEagerCompatibility() {
	ActualPriorityFixture fixture;
	fixture.PublishScene();
	fixture.Eager = std::make_unique<sgre::script::ScriptRawTextureBindings>(fixture.Vm, fixture.Objects, fixture.Layers, fixture.Images, fixture.Binary, fixture.Tree, fixture.Backend, fixture.Scene->Group(DefaultDriverTaskGroup::Priority));
	const auto raw = fixture.Create(fixture.Eager.get());
	Require(fixture.Resolutions == 0 && fixture.Scene->Group(DefaultDriverTaskGroup::Priority).Count() == 1 && fixture.Backend.Live, "existing reference overload must retain its actual direct owner behavior");
	fixture.Eager->ReleaseInstance(raw);
	fixture.AdvanceEmptyRootScene();
	Require(fixture.Scene->Group(DefaultDriverTaskGroup::Priority).Count() == 0 && !fixture.Backend.Live, "eager direct-owner path must complete real delayed retirement");
	fixture.Scene->Close();
	Require(fixture.Manager.IsClosed() && fixture.Meter.IsClosed() && fixture.TextureRetirements == 2, "actual scene retirement must close its real receiving and two-font-texture owners");
	const auto events = fixture.Backend.Events;
	RequireError([&] {
		static_cast<void>(fixture.Create());
	},
	    "scene graph has retired");
	Require(fixture.Resolutions == 1 && fixture.Tree.ChildCount() == 0 && fixture.Backend.Events == events && !fixture.Backend.Live && fixture.Controller.Parallel().Count() == 0, "retired actual scene must reject before any new wrapper/backend/group mutation");
}

static void TestActualRetiredPriorityAndMissingProvider() {
	ActualPriorityFixture fixture;
	RequireError([&] {
		sgre::script::ScriptRawTextureBindings missing(fixture.Vm, fixture.Objects, fixture.Layers, fixture.Images, fixture.Binary, fixture.Tree, fixture.Backend, sgre::script::ScriptRawTextureBindings::PriorityTaskProvider{});
	},
	    "actual Priority provider");
	fixture.PublishScene();
	auto& priority = fixture.Scene->Group(DefaultDriverTaskGroup::Priority);
	priority.SetRetireWhenEmpty(true);
	priority.Update();
	Require(priority.Status() == TaskStatus::Retire, "fixture must reach an actually retired real Priority task group");
	RequireError([&] {
		static_cast<void>(fixture.Create());
	},
	    "retired its actual VM, receiver or task owner");
	Require(fixture.Resolutions == 1 && fixture.Tree.ChildCount() == 0 && priority.Count() == 0 && !fixture.Backend.Live && fixture.Backend.Events.empty(), "actual retired Priority rejection must precede texture/tree/task mutation");
}

static sgre::scene::LayerCameraFrameState ActualCameraState() {
	sgre::scene::LayerCameraFrameState state;
	state.Viewport = {16, 8};
	state.RawScissor = {0, 0, 16, 8};
	state.RoundedScissor = state.RawScissor;
	state.AppliedRoundedScissor = state.RawScissor;

	return state;
}

static void TestActualRawProcessAndMutableDraw() {
	std::vector<sgre::scene::MotionIntegerScissor> scissors;
	const std::array<sgre::scene::MotionScissorMatrix, 1> models{};
	sgre::scene::LayerCameraDrawScope camera(ActualCameraState(), models, [&](const auto& rectangle) {
		scissors.push_back(rectangle);
	});
	ActualPriorityFixture fixture;
	fixture.PublishScene();
	std::vector<std::string> events;
	std::size_t drawn = 0;
	fixture.Raw.ConfigureFrameOwners(camera, [&](const auto& receiver, auto& task, const auto& packet, std::uint32_t argument) {
		Require(fixture.Tree.State(fixture.Layers.Handle(receiver)).Kind == sgre::scene::LayerKind::RawTexture && &task == &fixture.Raw.Owner(receiver).TextureTask(), "actual Raw drawing must consume its genuine receiver and exclusive mutable texture");
		Require(argument == 19 && camera.IsDrawing() && camera.Depth() == 2, "actual Raw packet submission must occur inside its real camera/model scope");
		Require(packet.Vertices[0].Position.X == 0 && packet.Vertices[1].Position.Y == 1 && packet.Vertices[2].Position.X == 2 && packet.Vertices[3].Uv.X == 1 && packet.Vertices[3].Uv.Y == 1 && packet.Color == 0xffffffff && packet.TextureAttributes == 4, "Raw drawing preserves actual native XY/UV strip extent, opaque color and texture attributes");
		RequireError([&] {
			fixture.Raw.ReleaseInstance(receiver);
		}, "own task callback");
		RequireError([&] {
			fixture.Raw.Owner(receiver).Close();
		}, "active task callback");
		task.Texture().Bind(0, sgre::scene::MutableTextureStage::Pixel);
		++drawn;
	});
	SquirrelValue raw;
	const auto derived = fixture.Vm.CreateClass(fixture.Raw.RawTextureClass());
	fixture.Vm.NewSlot(derived, std::string("onProcess"), fixture.Vm.CreateNativeClosure("actual SQRawTex291090", [&](auto& vm, auto arguments) {
		Require(arguments.size() == 1 && vm.BorrowObjectIdentity(arguments[0]) == vm.BorrowObjectIdentity(raw), "native Raw onProcess receives this actual wrapper exactly once");
		events.push_back("onProcess");
		RequireError([&] {
			fixture.Raw.Update(fixture.Layers.Handle(raw));
		}, "non-reentrant operation");

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	raw = fixture.Vm.Invoke(derived, std::array<SquirrelValue, 3>{fixture.Vm.RootTable(), std::int32_t{2}, std::int32_t{1}}).Value;
	const auto handle = fixture.Layers.Handle(raw);
	auto& owner = fixture.Raw.Owner(raw);
	Require(&owner.SerialTasks() == &owner.CommonTasks().Serial() && owner.CommonTasks().Parallel().Count() == 3, "actual Raw common Serial and three ordered parallel roots must share the concrete owner");
	const auto references = fixture.Vm.RetainedReferenceCount(raw);
	for (const auto group : {sgre::scene::RawLayerTaskGroup::CommonFirst, sgre::scene::RawLayerTaskGroup::CommonSecond}) {
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [&, group](auto) {
			Require(fixture.Vm.RetainedReferenceCount(raw) == references, "common groups must observe no extra receiver root after native onProcess invocation returns");
			events.push_back(group == sgre::scene::RawLayerTaskGroup::CommonFirst ? "first" : "second");
		};
		static_cast<void>(owner.Group(group).Append(std::move(callbacks)));
	}
	sgre::scene::ParallelTaskCallbacks inner;
	inner.Update = [&](auto) {
		events.push_back("inner");
	};
	static_cast<void>(owner.TextureTask().ParallelTasks().Append(std::move(inner)));
	fixture.DispatchRawRoots = true;
	fixture.Scene->Update([&] {
		++fixture.PhaseVisits;
	});
	Require(fixture.PhaseVisits == 1 && events == std::vector<std::string>{"onProcess", "first", "second", "inner"}, "actual Driver/root manager must dispatch outer Raw onProcess before native common groups and distinct inner MRawTex update");
	events.clear();
	sgre::scene::SerialTaskCallbacks serial;
	serial.Update = [&](auto& task) {
		events.push_back("serial");
		task.SetStatus(TaskStatus::Retire);
	};
	serial.Release = [&] {
		events.push_back("serial_release");
	};
	static_cast<void>(owner.SerialTasks().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	fixture.Raw.Update(handle);
	Require(events == std::vector<std::string>{"serial", "serial_release", "first", "second", "inner"}, "actual common serial ownership suppresses onProcess before serial then native parallel traversal");
	const std::array<std::uint8_t, 6> rgb{255, 0, 0, 0, 0, 255};
	owner.TextureTask().RestoreRgb(rgb);
	static_cast<void>(fixture.Vm.Invoke(fixture.Vm.GetSlot(raw, std::string("setClip")), std::array<SquirrelValue, 5>{raw, -0.5f, -0.5f, 1.0f, 1.0f}));
	fixture.Raw.Draw(handle, 19);
	Require(drawn == 1 && scissors.size() == 2 && scissors[0].MinimumX == 4 && scissors[0].MinimumY == 2 && scissors[0].MaximumX == 12 && scissors[0].MaximumY == 6 && scissors[1].MinimumX == 0 && scissors[1].MaximumX == 16 && camera.Depth() == 1 && !camera.IsDrawing(), "actual Raw camera clips its mutable strip and restores the parent model/scissor");
	owner.TextureTask().SetDrawPaused(true);
	fixture.Raw.Draw(handle, 19);
	Require(drawn == 1, "actual MRawTex drawing pause must suppress texture submission inside the live common graph");
	fixture.Raw.DispatchFrame(handle, [&](const auto& receiver, auto&) {
		Require(fixture.Vm.RetainedReferenceCount(receiver) == references, "checked handle dispatch must not create a manager self-root");
		RequireError([&] {
			fixture.Vm.Close();
		}, "borrowed receiver");
	});
	fixture.Raw.ReleaseInstance(raw);
	fixture.AdvanceEmptyRootScene();
}

static void TestActualRawErrorStatusAndDirectSerialFence() {
	const std::array<sgre::scene::MotionScissorMatrix, 1> models{};
	sgre::scene::LayerCameraDrawScope camera(ActualCameraState(), models, [](const auto&) {
		throw std::runtime_error("disabled clipping must not require a scissor change");
	});
	ActualPriorityFixture fixture;
	fixture.PublishScene();
	fixture.Raw.ConfigureFrameOwners(camera, [](const auto&, auto&, const auto&, auto) {
		throw std::runtime_error("this process-only fixture excludes actual Raw submission");
	});
	const auto derived = fixture.Vm.CreateClass(fixture.Raw.RawTextureClass());
	fixture.Vm.NewSlot(derived, std::string("onProcess"), fixture.Vm.CreateNativeClosure("genuine failing Raw method", [](auto&, auto) -> sgre::script::SquirrelNativeResult {
		throw std::runtime_error("actual Raw process error");
	}));
	const auto raw = fixture.Vm.Invoke(derived, std::array<SquirrelValue, 3>{fixture.Vm.RootTable(), std::int32_t{2}, std::int32_t{1}}).Value;
	const auto handle = fixture.Layers.Handle(raw);
	std::size_t visits = 0;
	sgre::scene::ParallelTaskCallbacks after_error;
	after_error.Update = [&](auto) {
		++visits;
	};
	static_cast<void>(fixture.Raw.Owner(raw).Group(sgre::scene::RawLayerTaskGroup::CommonFirst).Append(std::move(after_error)));
	fixture.Raw.Update(handle);
	Require(visits == 1 && fixture.Vm.LastError() == SquirrelValue{std::string("actual Raw process error")}, "actual native failure status is ignored after preserving VM LastError and continuing common traversal");
	sgre::scene::SerialTaskCallbacks serial;
	serial.Update = [&](auto&) {
		RequireError([&] {
			fixture.Raw.ReleaseInstance(raw);
		}, "own task callback");
		RequireError([&] {
			fixture.Raw.Owner(raw).Close();
		}, "active task callback");
	};
	static_cast<void>(fixture.Raw.Owner(raw).SerialTasks().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	fixture.Raw.Owner(raw).SerialTasks().Update();
	Require(fixture.Tree.Contains(handle) && fixture.Backend.Live, "direct borrowed common Serial callback must veto retirement before any tree/resource mutation");
	fixture.Raw.Owner(raw).SerialTasks().Clear();
	sgre::scene::LayerFrameProcessState paused;
	paused.Status = TaskStatus::Complete;
	fixture.Raw.SetProcessState(raw, paused);
	fixture.Raw.Update(handle);
	Require(visits == 1, "actual containing nonactive Raw status must suppress update");
	paused.Status = TaskStatus::Active;
	paused.ThreadedUpdate = true;
	fixture.Raw.SetProcessState(raw, paused);
	RequireError([&] {
		fixture.Raw.Update(handle);
	}, "actual worker dispatcher");
	paused.ThreadedUpdate = false;
	fixture.Raw.SetProcessState(raw, paused);
	fixture.Raw.ReleaseInstance(raw);
	fixture.AdvanceEmptyRootScene();

	std::size_t weak_calls = 0;
	auto method = fixture.Vm.CreateNativeClosure("actual weak Raw callback", [&](auto&, auto) {
		++weak_calls;

		return sgre::script::SquirrelNativeResult::NoValues();
	});
	const auto weak_class = fixture.Vm.CreateClass(fixture.Raw.RawTextureClass());
	fixture.Vm.NewSlot(weak_class, std::string("onProcess"), fixture.Vm.CreateWeakReference(method));
	const auto weak_raw = fixture.Vm.Invoke(weak_class, std::array<SquirrelValue, 3>{fixture.Vm.RootTable(), std::int32_t{2}, std::int32_t{1}}).Value;
	fixture.Raw.Update(fixture.Layers.Handle(weak_raw));
	Require(weak_calls == 1, "actual weak method lookup resolves its real native closure");
	method = {};
	fixture.Raw.Update(fixture.Layers.Handle(weak_raw));
	Require(weak_calls == 1, "expired weak method performs no fabricated script callback");
	fixture.Raw.ReleaseInstance(weak_raw);
	fixture.AdvanceEmptyRootScene();

	sgre::script::BytecodeProgram program;
	program.MainFunction.Parameters.push_back(std::string("this"));
	program.MainFunction.StackSize = 2;
	program.MainFunction.Name = std::string("onProcess");
	program.MainFunction.SourceName = std::string("independent Raw actual closure");
	sgre::script::SquirrelInstruction load;
	load.Opcode = 2;
	load.Argument0 = 1;
	load.Argument1 = 77;
	program.MainFunction.Instructions.push_back(load);
	sgre::script::SquirrelInstruction raise;
	raise.Opcode = 58;
	raise.Argument0 = 1;
	program.MainFunction.Instructions.push_back(raise);
	const auto script_class = fixture.Vm.CreateClass(fixture.Raw.RawTextureClass());
	fixture.Vm.NewSlot(script_class, std::string("onProcess"), fixture.Vm.CreateClosure(std::move(program)));
	const auto script_raw = fixture.Vm.Invoke(script_class, std::array<SquirrelValue, 3>{fixture.Vm.RootTable(), std::int32_t{2}, std::int32_t{1}}).Value;
	fixture.Raw.Update(fixture.Layers.Handle(script_raw));
	Require(fixture.Vm.LastError() == SquirrelValue{std::int32_t{77}}, "genuine Raw script closure raises actual VM payload while native wrapper ignores call status");
	fixture.Raw.ReleaseInstance(script_raw);
	fixture.AdvanceEmptyRootScene();
}

static void TestRawFrameRollbackAndOrderedReleaseFailure() {
	const std::array<sgre::scene::MotionScissorMatrix, 1> models{};
	sgre::scene::LayerCameraDrawScope camera(ActualCameraState(), models, [](const auto&) {
		throw std::runtime_error("unused drawing boundary");
	});
	sgre::script::ScriptRawTextureLimits limits;
	limits.MaxBackgroundTasks = 2;
	{
		ActualPriorityFixture small(limits);
		small.PublishScene();
		small.Raw.ConfigureFrameOwners(camera, [](const auto&, auto&, const auto&, auto) {
			throw std::runtime_error("failed construction must never draw");
		});
		RequireError([&] {
			static_cast<void>(small.Create());
		}, "capacity");
		Require(small.Tree.ChildCount() == 0 && !small.Backend.Live && small.Scene->Group(DefaultDriverTaskGroup::Priority).Count() == 1, "full native root capacity must roll back actual texture/tree/common ownership while leaving the background entry pending Retire");
		small.AdvanceEmptyRootScene();
		Require(small.Scene->Group(DefaultDriverTaskGroup::Priority).Count() == 0, "real scene traversal must retire failed constructor background ownership");
	}
	ActualPriorityFixture fixture;
	fixture.PublishScene();
	fixture.Raw.ConfigureFrameOwners(camera, [](const auto&, auto&, const auto&, auto) {
		throw std::runtime_error("retirement-only fixture excludes drawing");
	});
	const auto raw = fixture.Create();
	std::vector<std::string> events;
	for (const auto group : {sgre::scene::RawLayerTaskGroup::CommonFirst, sgre::scene::RawLayerTaskGroup::CommonSecond}) {
		sgre::scene::ParallelTaskCallbacks callbacks;
		callbacks.Release = [&, group](auto) {
			events.push_back(group == sgre::scene::RawLayerTaskGroup::CommonFirst ? "first" : "second");
			if (group == sgre::scene::RawLayerTaskGroup::CommonFirst) {
				throw std::runtime_error("actual first common release error");
			}
		};
		static_cast<void>(fixture.Raw.Owner(raw).Group(group).Append(std::move(callbacks)));
	}
	fixture.Backend.OnTextureRelease = [&] {
		events.push_back("texture");
	};
	sgre::scene::ParallelTaskCallbacks inner;
	inner.Release = [&](auto) {
		events.push_back("inner_parallel");
	};
	static_cast<void>(fixture.Raw.Owner(raw).TextureTask().ParallelTasks().Append(std::move(inner)));
	sgre::scene::SerialTaskCallbacks inner_serial;
	inner_serial.Release = [&] {
		events.push_back("inner_serial");
	};
	static_cast<void>(fixture.Raw.Owner(raw).TextureTask().SerialTasks().Append(std::make_unique<sgre::scene::SerialTask>(std::move(inner_serial))));
	sgre::scene::SerialTaskCallbacks common_serial;
	common_serial.Release = [&] {
		events.push_back("common_serial");
	};
	static_cast<void>(fixture.Raw.Owner(raw).SerialTasks().Append(std::make_unique<sgre::scene::SerialTask>(std::move(common_serial))));
	RequireError([&] {
		fixture.Raw.ReleaseInstance(raw);
	}, "actual first common release error");
	Require(events == std::vector<std::string>{"first", "second", "texture", "inner_parallel", "inner_serial", "common_serial"} && fixture.Tree.ChildCount() == 0 && !fixture.Backend.Live, "actual graph retirement must preserve native common-parallel/texture/serial order despite the first release failure");
	fixture.AdvanceEmptyRootScene();
}

int main() {
	try {
		TestActualLatePriorityAndDeferredRetirement();
		TestProviderReentryAndUnwind();
		TestNativeTailAppendInsideActualPriorityTraversal();
		TestRetiredActualOwnersAndEagerCompatibility();
		TestActualRetiredPriorityAndMissingProvider();
		TestActualRawProcessAndMutableDraw();
		TestActualRawErrorStatusAndDirectSerialFence();
		TestRawFrameRollbackAndOrderedReleaseFailure();
		std::cout << "actual Raw common-frame/VM/mutable-strip component PASS groups=8; no Metal VS4 consumer or application frame claim\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
