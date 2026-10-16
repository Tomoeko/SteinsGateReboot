#include "sgre/scene/motion_player_task.hpp"

#include "sgre/scene/motion_alternate_textures.hpp"

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_render_queue.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"
#include "sgre/scene/motion_scene_player.hpp"
#include "sgre/script/motion_bindings.hpp"
#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("actual scene owner accepted an unsupported lifetime or service bundle");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);
	return result;
}

static sgre::resource::PsbValue Layer(std::string label, bool child) {
	sgre::resource::PsbArray frames;
	for (std::size_t index = 0; index != 2; ++index) {
		const auto content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x60603})}, {"src", Atom(std::string(child ? "child" : "atlas"))}, {"icon", Atom(std::string(child ? "main" : "tile"))}, {"coord", Atom(sgre::resource::PsbArray{Atom(child ? 2.0 : 0.0), Atom(0.0), Atom(0.0)})}, {"ox", Atom(0.0)}, {"oy", Atom(0.0)}, {"opa", Atom(std::int64_t{255})}, {"color", Atom(std::int64_t{0x808080ff})}, {"bm", Atom(std::int64_t{0x10})}, {"act", Atom(std::string("first"))}});
		frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(index == 0 ? 0.0 : 8.0)}, {"type", Atom(index == 0 ? std::int64_t{2} : std::int64_t{0})}, {"content", content}}));
	}
	return Atom(sgre::resource::PsbObject{{"uid", Atom(std::string(child ? "control" : label == "nested" ? "nested-screen"
	                                                                                                     : "screen"))},
	    {"label", Atom(std::move(label))}, {"type", Atom(child ? std::int64_t{3} : std::int64_t{0})}, {"inheritMask", Atom(std::int64_t{4})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(sgre::resource::PsbArray{})}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(std::int64_t{0})}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}});
}

static sgre::resource::PsbValue Timeline(bool child) {
	const auto order = child ? sgre::resource::PsbArray{Atom(std::int64_t{0})} : sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1})};
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(order)}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	auto layers = sgre::resource::PsbArray{Layer(child ? "nested" : "sprite", false)};
	if (!child) {
		layers.push_back(Layer("selected-child", true));
	}
	return Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(16.0)}, {"bottom", Atom(8.0)}})}, {"lastTime", Atom(8.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(std::move(layers))}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
}

static std::shared_ptr<sgre::resource::PsbDocument> Document() {
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{4})}, {"height", Atom(std::int64_t{4})}, {"truncated_width", Atom(std::int64_t{4})}, {"truncated_height", Atom(std::int64_t{4})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	const auto icon = Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"width", Atom(2.0)}, {"height", Atom(2.0)}, {"originX", Atom(0.0)}, {"originY", Atom(0.0)}, {"attr", Atom(std::int64_t{2})}});
	const auto source = Atom(sgre::resource::PsbObject{{"texture", texture}, {"icon", Atom(sgre::resource::PsbObject{{"tile", icon}})}});
	const auto object = [](bool child) {
		return Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"main", Timeline(child)}})}});
	};
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 144;
	document->Chunks = {{128, 16}};
	document->Root = Atom(sgre::resource::PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Actual task scene fixture"))}, {"easing", Atom(sgre::resource::PsbArray{})}, {"source", Atom(sgre::resource::PsbObject{{"atlas", source}})}, {"object", Atom(sgre::resource::PsbObject{{"character", object(false)}, {"child", object(true)}})}, {"sourceIconRenameMap", Atom(sgre::resource::PsbObject{})}});
	document->Root.SerializedOffset = 44;
	return document;
}

struct Fixture {
	std::size_t Uploads = 0;
	std::size_t Releases = 0;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	std::shared_ptr<const sgre::scene::LoadedMotionAsset> Asset;
	sgre::scene::MotionResourceManager Manager;
	sgre::scene::MotionProjectId Project;
	sgre::core::FrameClock Clock;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	sgre::scene::MotionLayerTasks Tasks;
	std::vector<std::pair<std::string, std::string>> Events;

	Fixture()
	    : Cache([this](auto, const auto&) {
		      return sgre::render::SpriteTextureHandle{static_cast<std::uint32_t>(++Uploads), 1, &Uploads};
	      },
	          [this](auto) {
		          ++Releases;
	          }),
	      Catalog(Cache), Manager(Cache), Clock(0), Tasks(8) {
		for (std::size_t index = 0; index != Seed.size(); ++index) {
			Seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17;
		}
		Asset = Catalog.Prepare(Document(), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		Project = Manager.Register(Asset->Identity, Asset->Model, *Asset->Backing);
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, Clock, Seed, Services(), 32);
	}

	~Fixture() {
		if (Runtime) {
			Runtime->SceneContext().Clear();
		}
		Tasks.Clear();
	}

	sgre::scene::MotionManagerRuntimeServices Services() {
		sgre::scene::MotionManagerRuntimeServices services;
		services.ReadSourceAliases = [this](auto project) {
			Require(project == Project, "scene aliases borrowed a different actual registration");
			return std::span<const sgre::scene::MotionSourceAlias>(Asset->Aliases);
		};
		services.SceneAssets = &Catalog;
		services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			Require(project == Project, "scene asset borrowed a different actual registration");
			return *Asset;
		};
		return services;
	}

	sgre::scene::MotionPlayerTask Attach() {
		return sgre::scene::MotionPlayerTask::Attach(*Runtime, Tasks, false, "character", [this](const auto& event) {
			Require(event.Kind == sgre::scene::MotionPlayerEventKind::Action, "fixture has only authored real layer actions");
			Events.emplace_back(event.Layer, event.Action);
		});
	}
};

static sgre::scene::MotionPreparedFrame Globals(std::span<const sgre::resource::MotionPoint> sizes) {
	sgre::scene::MotionPreparedFrame globals;
	globals.TextureSizes = sizes;
	globals.InitialScissor = {0, 0, 16, 8};
	globals.AppliedRoundedScissor = globals.InitialScissor;
	globals.TargetBounds = globals.InitialScissor;
	globals.Viewport = {16, 8};
	globals.AxisScale = {1, 1};
	return globals;
}

static sgre::scene::MotionFrameScratch Scratch(std::array<sgre::resource::MotionPoint, 32>& positions, std::array<sgre::resource::MotionPoint, 32>& uvs, std::array<std::uint32_t, 32>& colors, std::array<std::uint32_t, 64>& indices, std::array<sgre::scene::MotionMaskOperation, 64>& masks, std::array<sgre::scene::MotionMaskedBlendPass, 64>& blends) {
	sgre::scene::MotionFrameScratch scratch;
	scratch.Batch.Positions = positions;
	scratch.Batch.Uvs = uvs;
	scratch.Batch.Colors = colors;
	scratch.Batch.Indices = indices;
	scratch.MaskOperations = masks;
	scratch.BlendPasses = blends;

	return scratch;
}

static void TestRealUidSourceAndPublication() {
	Fixture fixture;
	auto player = fixture.Attach();
	player.SetVisible(true);
	std::uint32_t filtering = 0;
	sgre::scene::NativeTextureSnapshot source;
	source.Texture = {37, 19, &filtering};
	source.Size = {8, 4};
	source.ActiveBounds = {0, 0, 6, 4};
	const auto read = [&] {
		auto current = source;
		current.Minification = filtering;
		current.Magnification = filtering;
		return current;
	};
	const auto filter = [&](auto minimum, auto maximum) {
		Require(minimum == maximum, "fixture accepts the actual common filtering pair only");
		filtering = minimum;
	};
	sgre::scene::NativeTextureReference first(read, filter);
	sgre::scene::NativeTextureReference second(read, filter);
	// Reverse subscription order must not preassign native slots ahead of actual selected-node registration order.
	struct PublicationCleanup {
		Fixture& Owner;
		sgre::scene::MotionPlayerTask& Player;
		~PublicationCleanup() noexcept {
			try {
				if (Owner.Runtime) {
					Owner.Runtime->SceneContext().Clear();
				}
				if (Player.IsLive()) {
					Player.ClearAlternateTextures();
				}
			} catch (...) {
				std::terminate();
			}
		}
	} publication_cleanup{fixture, player};
	player.RegisterAlternateTexture("nested-screen", second, {2.75f, 1.25f, 6.75f, 3.25f}, {1, -1});
	player.RegisterAlternateTexture("screen", first, {2.75f, 1.25f, 6.75f, 3.25f}, {1, -1});
	Require(fixture.Manager.ExternalTextures().RegisteredCount() == 0, "a UID listener without selected matches must not fabricate a live external slot");
	player.Play("main");
	auto& group = fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second);
	group.Update();
	Require(fixture.Manager.ExternalTextures().RegisteredCount() == 2 && fixture.Manager.ExternalTextures().At(0)->External == &first && fixture.Manager.ExternalTextures().At(1)->External == &second, "descriptor identity and actual first-match source slot order changed");
	RequireFailure([&] {
		first.RequireCanRelease();
	},
	    "listeners");
	first.SetSmoothing(true);
	Require(second.Read().Minification == 1, "two distinct NativeTextureRef wrappers must still mutate the same actual source sampler");
	std::array<sgre::resource::MotionPoint, 32> positions{};
	std::array<sgre::resource::MotionPoint, 32> uvs{};
	std::array<std::uint32_t, 32> colors{};
	std::array<std::uint32_t, 64> indices{};
	std::array<sgre::scene::MotionMaskOperation, 64> masks{};
	std::array<sgre::scene::MotionMaskedBlendPass, 64> blends{};
	const auto scratch = Scratch(positions, uvs, colors, indices, masks, blends);
	const std::array<sgre::resource::MotionPoint, 1> local_sizes{{{4, 4}}};
	for (const float tick : std::array<float, 2>{0, 4}) {
		player.SetTickCount(tick);
		group.Update();
		group.Draw(0);
		const auto frame = fixture.Runtime->SceneContext().Prepare(Globals(local_sizes));
		Require(frame.Queue.size() == 2 && frame.ExternalTextures.size() == 2, "actual nested scene lost its two installed external source descriptors");
		std::array<bool, 2> seen{};
		for (const auto& command : frame.Queue) {
			Require(command.Texture == -1 || command.Texture == -2, "external publication must retain the genuine complement-encoded native texture index");
			seen[static_cast<std::size_t>(-command.Texture - 1)] = true;
			Require(command.PixelUvs.X == 2 && command.PixelUvs.Y == 1 && command.PixelUvs.Width == 4 && command.PixelUvs.Height == 2 && command.Key == 0 && command.Colors[0] == 0x808080ff, "native external signed rectangle truncation/resolution/attribute clearing or retained material changed");
			const auto points = command.Clip->Geometry->Positions;
			Require(points.size() == 4 && points[1].X - points[0].X == 4 && points[2].Y - points[0].Y == 2, "actual selected geometry kept atlas dimensions after external source replacement");
		}
		Require(seen[0] && seen[1], "two wrappers naming one physical texture were incorrectly deduplicated");
		std::size_t batches = 0;
		sgre::scene::SubmitPreparedMotionFrame(frame, scratch, [&](const auto& event) {
			if (const auto* batch = std::get_if<sgre::scene::MotionBatchSubmission>(&event)) {
				++batches;
				Require(batch->Uvs[0].X == .25f && batch->Uvs[0].Y == .25f, "external frame UVs used a PSB atlas or logical active extent instead of shared physical dimensions");
			}
		});
		Require(batches == 2, "actual distinct external texture selections must retain native batch boundaries");
		RequireFailure([&] {
			static_cast<void>(player.UnregisterAlternateTexture("screen"));
		},
		    "queued");
		fixture.Runtime->SceneContext().Clear();
	}
	Require(player.UnregisterAlternateTexture("screen"), "actual root source listener did not unregister");
	first.RequireCanRelease();
	player.SetTickCount(0);
	group.Update();
	group.Draw(0);
	const auto restored = fixture.Runtime->SceneContext().Prepare(Globals(local_sizes));
	Require(std::count_if(restored.Queue.begin(), restored.Queue.end(), [](const auto& command) {
		return command.Texture == 0 && command.Key == 2;
	}) == 1 &&
	        std::count_if(restored.Queue.begin(), restored.Queue.end(), [](const auto& command) {
		        return command.Texture == -2;
	        }) == 1,
	    "explicit unregister must restore the real current authored source without altering another actual descriptor");
	fixture.Runtime->SceneContext().Clear();
	player.ClearAlternateTextures();
	second.RequireCanRelease();
	fixture.Tasks.Clear();
	Require(fixture.Manager.ExternalTextures().RegisteredCount() == 0 && fixture.Manager.ExternalTextures().CanClose(), "selected retirement or listener clear leaked actual descriptor references");
	fixture.Runtime->Close();
	fixture.Runtime.reset();
	Require(fixture.Manager.Unregister(fixture.Project), "actual source resource could not retire after listener cleanup");
}

static sgre::script::SquirrelVmLimits WrapperLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	return limits;
}

class WrapperFixture {
public:
	WrapperFixture()
	    : Vm(WrapperLimits()), Objects(Vm), Psb(Vm, Objects), Cache([this](auto, const auto&) {
		      return sgre::render::SpriteTextureHandle{static_cast<std::uint32_t>(++Uploads), 1, &Uploads};
	      },
	                                                              [](auto) {
	                                                              }),
	      Assets(Cache), Tree(8), Layers(Vm, Objects, Tree), Clock(0) {
		Layers.ConfigureMotionOwners(Psb, Assets, Cache);
		Leaf = Vm.Invoke(Layers.LayerClass(), std::array<sgre::script::SquirrelValue, 1>{Vm.RootTable()}).Value;
		LeafRoot = Vm.Retain(Leaf);
		for (std::size_t index = 0; index < Seed.size(); ++index) {
			Seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17;
		}
		sgre::scene::MotionManagerRuntimeServices runtime;
		runtime.ReadSourceAliases = [this](auto project) {
			return Layers.MotionResources(Leaf).SourceAliases(project);
		};
		runtime.SceneAssets = &Assets;
		runtime.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			return Layers.MotionResources(Leaf).BorrowAsset(project);
		};
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Layers.MotionResources(Leaf).Secondary(), Clock, Seed, std::move(runtime), 32);
		Layers.AttachMotionRuntime(Leaf, *Runtime);
		sgre::script::ScriptMotionBindingServices bindings;
		bindings.ResolveRuntime = [this](const auto& leaf, auto source) -> sgre::scene::MotionManagerRuntime& {
			Require(leaf == Leaf && source == sgre::script::MotionLayerSource::Secondary, "wrapper fixture must select its actual Layer Secondary runtime");
			return *Runtime;
		};
		bindings.ReadAsynchronousMode = [this] {
			return Asynchronous;
		};
		Bindings = std::make_unique<sgre::script::ScriptMotionBindings>(Vm, Objects, Layers, std::move(bindings));
	}

	~WrapperFixture() noexcept {
		if (m_Closed) {
			return;
		}
		try {
			Close();
		} catch (const std::exception& error) {
			std::cerr << "alternate wrapper fixture cleanup at " << m_ClosePhase << ": " << error.what() << '\n';
			std::terminate();
		} catch (...) {
			std::cerr << "alternate wrapper fixture cleanup at " << m_ClosePhase << ": nonstandard failure\n";
			std::terminate();
		}
	}

	void Close() {
		if (m_Closed) {
			return;
		}
		m_ClosePhase = "VM invocation result";
		Vm.ResetExecution();
		m_ClosePhase = "actual scene publication";
		Runtime->SceneContext().Clear();
		m_ClosePhase = "Motion wrapper/listeners";
		if (std::holds_alternative<sgre::script::SquirrelObjectHandle>(Motion) && Bindings->NativeOwners().Contains(Motion)) {
			Bindings->NativeOwners().ReleaseInstance(Motion);
		}
		MotionRoot = {};
		Motion = {};
		m_ClosePhase = "actual Second task retirement";
		Layers.MotionTasks(Leaf).Group(sgre::scene::MotionTaskGroup::Second).Update();
		m_ClosePhase = "actual Layer/runtime source-manager retirement";
		Layers.ReleaseInstance(Leaf);
		LeafRoot = {};
		Leaf = {};
		Bindings->NativeOwners().Close();
		m_ClosePhase = "owned runtime";
		Runtime->Close();
		Runtime.reset();
		m_ClosePhase = "VM finalization";
		Vm.Close();
		m_ClosePhase = "actual texture cache";
		Cache.Close();
		m_Closed = true;
	}

	sgre::script::SquirrelValue Call(const sgre::script::SquirrelValue& instance, std::string_view name, std::initializer_list<sgre::script::SquirrelValue> values = {}) {
		std::vector<sgre::script::SquirrelValue> arguments{instance};
		arguments.insert(arguments.end(), values.begin(), values.end());
		return Vm.Invoke(Vm.GetSlot(instance, std::string(name)), arguments).Value;
	}

	std::size_t Uploads = 0;
	sgre::script::SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects;
	sgre::script::ScriptPsbBindings Psb;
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Assets;
	sgre::scene::LayerTree Tree;
	sgre::script::ScriptLayerBindings Layers;
	sgre::core::FrameClock Clock;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	std::unique_ptr<sgre::script::ScriptMotionBindings> Bindings;
	sgre::script::SquirrelValue Leaf;
	sgre::script::SquirrelValue Motion;
	// The collector marks explicit host/VM roots; ordinary C++ handles alone do not model script reachability.
	sgre::script::SquirrelHostRoot LeafRoot;
	sgre::script::SquirrelHostRoot MotionRoot;
	bool Asynchronous = false;

private:
	bool m_Closed = false;
	const char* m_ClosePhase = "unstarted";
};

static void TestActualWrapperRootsAndFailedMutation() {
	sgre::scene::NativeTextureSnapshot source;
	source.Texture = {19, 7, &source};
	source.Size = {8, 4};
	source.ActiveBounds = {0, 0, 8, 4};
	sgre::scene::NativeTextureReference descriptor([&] {
		return source;
	},
	    [](auto, auto) {
	    });
	WrapperFixture fixture;
	// This CPU boundary fixture supplies the checked class/resolver; physical NativeTextureRef source construction is separate.
	const auto texture_class = fixture.Vm.CreateClass(fixture.Objects.ObjectClass());
	std::size_t resolved = 0;
	sgre::script::SquirrelObjectIdentity expected_texture;
	fixture.Bindings->ConfigureAlternateTextures(texture_class, [&](const auto& value) -> const sgre::scene::NativeTextureReference& {
		Require(fixture.Vm.BorrowObjectIdentity(value) == expected_texture, "reentrant properties changed the actual NativeTextureRef operand");
		++resolved;
		return descriptor;
	});
	fixture.Motion = fixture.Vm.Invoke(fixture.Bindings->MotionClass(), std::array<sgre::script::SquirrelValue, 2>{fixture.Vm.RootTable(), fixture.Leaf}).Value;
	fixture.MotionRoot = fixture.Vm.Retain(fixture.Motion);
	const auto backing = std::make_shared<const std::vector<std::uint8_t>>(144, 255);
	const auto resource = fixture.Psb.WrapDocument(Document(), backing);
	static_cast<void>(fixture.Call(fixture.Leaf, "registerMotionResource", {resource}));
	std::vector<std::string> coordinate_reads;
	bool grew_operands = false;
	const auto grow = fixture.Vm.CreateNativeClosure("fixture.actualOperandGrowth", [&](auto& vm, auto arguments) {
		Require(arguments.size() == 32768 && vm.NativeStackSize() == arguments.size(), "adversarial getter must enter a real large native operand frame");
		grew_operands = true;
		return sgre::script::SquirrelNativeResult::NoValues();
	});
	const auto rectangle_class = fixture.Vm.CreateClass();
	fixture.Vm.NewSlot(rectangle_class, std::string("_get"), fixture.Vm.CreateNativeClosure("fixture.reentrantRectangle", [&](auto& vm, auto arguments) {
		// This getter is itself an adversarial native callback, so its own borrowed arguments cannot survive the nested call.
		const auto key = std::string(sgre::script::StringText(arguments[1]));
		coordinate_reads.push_back(key);
		if (!grew_operands) {
			Require(key == "bottom", "native rectangle member order changed before its first reentrant getter");
			std::vector<sgre::script::SquirrelValue> growth(32768, std::int32_t{17});
			growth[0] = vm.RootTable();
			const auto result = vm.InvokeInContext(vm.CurrentContext(), grow, growth);
			Require(result.State == sgre::script::SquirrelRunState::Completed && grew_operands, "the getter did not complete actual VM reentry");
		}
		for (const auto& [member, value] : std::array<std::pair<std::string_view, float>, 4>{{{"left", 2.75f}, {"top", 1.25f}, {"right", 6.75f}, {"bottom", 3.25f}}}) {
			if (key == member) {
				return sgre::script::SquirrelNativeResult::ReturnValue(value);
			}
		}
		throw std::runtime_error("unexpected rectangle property in the adversarial native getter");
	}));
	const auto rectangle = fixture.Vm.CreateInstance(rectangle_class);
	const auto rectangle_root = fixture.Vm.Retain(rectangle);
	const auto origin = fixture.Vm.CreateTable();
	fixture.Vm.NewSlot(origin, std::string("x"), 1.0f);
	fixture.Vm.NewSlot(origin, std::string("y"), -1.0f);
	const auto origin_root = fixture.Vm.Retain(origin);
	auto texture = fixture.Vm.CreateInstance(texture_class);
	expected_texture = fixture.Vm.BorrowObjectIdentity(texture);
	const auto weak = fixture.Vm.CreateWeakReference(texture);
	static_cast<void>(fixture.Call(fixture.Motion, "registerAlternateTexture", {std::string("screen"), texture, rectangle, origin}));
	Require(resolved == 1 && grew_operands && coordinate_reads == std::vector<std::string>{"bottom", "right", "top", "left"}, "actual typed wrapper registration must preserve the native member order after real operand-stack growth");
	texture = {};
	fixture.Vm.ResetExecution();
	static_cast<void>(fixture.Vm.CollectGarbage());
	Require(fixture.Vm.Type(fixture.Vm.ResolveWeakReference(weak)) == sgre::script::SquirrelValueType::Instance, "the real Motion wrapper map must root its NativeTextureRef through future UID matches");
	RequireFailure([&] {
		descriptor.RequireCanRelease();
	},
	    "listeners");
	static_cast<void>(fixture.Call(fixture.Motion, "setChara", {std::string("character")}));
	static_cast<void>(fixture.Call(fixture.Motion, "play", {std::string("main")}));
	static_cast<void>(fixture.Call(fixture.Motion, "setVisible", {true}));
	auto& group = fixture.Layers.MotionTasks(fixture.Leaf).Group(sgre::scene::MotionTaskGroup::Second);
	group.Update();
	group.Draw(0);
	const std::array<sgre::resource::MotionPoint, 1> actual_sizes{{{4, 4}}};
	const auto frame = fixture.Runtime->SceneContext().Prepare(Globals(actual_sizes));
	Require(frame.ExternalTextures.size() == 1 && frame.ExternalTextures[0].Reference == &descriptor, "reentry lost the exact registered descriptor/actual receiver");
	const auto published = std::find_if(frame.Queue.begin(), frame.Queue.end(), [](const auto& command) {
		return command.Texture < 0;
	});
	Require(published != frame.Queue.end() && published->Texture == -1 && published->PixelUvs.X == 2 && published->PixelUvs.Y == 1 && published->PixelUvs.Width == 4 && published->PixelUvs.Height == 2, "reentry changed the original UID/rectangle source operands");
	Require(published->Clip->Geometry->Positions[0].X == -1 && published->Clip->Geometry->Positions[0].Y == 1, "reentry changed the original origin operand before real geometry construction");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(fixture.Motion, "unregisterAlternateTexture", {std::string("screen")}));
	},
	    "queued");
	RequireFailure([&] {
		static_cast<void>(fixture.Call(fixture.Motion, "releaseAllAlternateTextures"));
	},
	    "queued");
	Require(fixture.Vm.Type(fixture.Vm.ResolveWeakReference(weak)) == sgre::script::SquirrelValueType::Instance, "rejected queued mutation must not release a descriptor's actual script owner");
	fixture.Runtime->SceneContext().Clear();
	fixture.Asynchronous = true;
	static_cast<void>(fixture.Call(fixture.Motion, "unregisterAlternateTexture", {std::string("screen")}));
	RequireFailure([&] {
		descriptor.RequireCanRelease();
	},
	    "listeners");
	fixture.Asynchronous = false;
	static_cast<void>(fixture.Call(fixture.Motion, "releaseAllAlternateTextures"));
	fixture.Vm.ResetExecution();
	static_cast<void>(fixture.Vm.CollectGarbage());
	descriptor.RequireCanRelease();
	Require(fixture.Vm.Type(fixture.Vm.ResolveWeakReference(weak)) == sgre::script::SquirrelValueType::Null, "successful listener removal must release its exact NativeTextureRef VM root");
	static_cast<void>(fixture.Call(fixture.Motion, "registerAlternateTexture", {std::string("screen"), fixture.Vm.CreateTable(), rectangle, origin}));
	Require(resolved == 1, "native wrong-class registration must skip descriptor/property work");
	fixture.Close();
}

int main() {
	try {
		TestRealUidSourceAndPublication();
		TestActualWrapperRootsAndFailedMutation();
		std::cout << "NATIVE_TEXTURE_REF_ACTUAL_UID_SOURCE_GEOMETRY_NEGATIVE_FRAME_AND_RESTORE=PASS\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}

	return 0;
}
