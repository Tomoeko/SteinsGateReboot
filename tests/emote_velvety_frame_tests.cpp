#include "sgre/scene/emote_velvety_frame.hpp"

#include "native_clear_frame_vectors.hpp"

#include "sgre/scene/motion_bezier_lattice.hpp"

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_draw.hpp"
#include "sgre/scene/motion_grid_draw.hpp"
#include "sgre/scene/motion_layer_mesh_local_owner.hpp"
#include "sgre/scene/motion_player_commands.hpp"
#include "sgre/scene/motion_player_task.hpp"
#include "sgre/scene/motion_root_scene.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"
#include "sgre/scene/motion_scene_player.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

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

	throw std::runtime_error("mesh override accepted an invalid actual owner or lattice");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static sgre::resource::PsbValue Layer(bool mesh) {
	sgre::resource::PsbArray frames;
	for (std::size_t index = 0; index != 4; ++index) {
		auto content = sgre::resource::PsbObject{{"mask", Atom(mesh ? std::int64_t{0x02020613} : std::int64_t{0x20603})}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}, {"coord", Atom(sgre::resource::PsbArray{Atom(mesh ? 0.0 : 2.0), Atom(0.0), Atom(0.0)})}, {"ox", Atom(0.0)}, {"oy", Atom(0.0)}, {"opa", Atom(std::int64_t{255})}, {"color", Atom(std::int64_t{0x808080ff})}, {"bm", Atom(std::int64_t{0x10})}};
		if (mesh) {
			content.emplace_back("angle", Atom(90.0));
			const auto identity = sgre::scene::IdentityMotionBezierPatch();
			sgre::resource::PsbArray coordinates;
			for (const auto& point : identity.Controls) {
				coordinates.push_back(Atom(static_cast<double>(point.X + (index == 1 ? 0.25f : 0.0f))));
				coordinates.push_back(Atom(static_cast<double>(point.Y)));
			}
			content.emplace_back("mesh", Atom(sgre::resource::PsbObject{{"cc", Atom(std::monostate{})}, {"bp", Atom(std::move(coordinates))}}));
		}
		frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(static_cast<double>(index * 4))}, {"type", Atom(index == 0 ? std::int64_t{3} : index == 1 ? std::int64_t{2}
		                                                                                                                                                        : std::int64_t{0})},
		    {"content", Atom(std::move(content))}}));
	}
	const auto children = mesh ? sgre::resource::PsbArray{Layer(false)} : sgre::resource::PsbArray{};

	return Atom(sgre::resource::PsbObject{{"label", Atom(std::string(mesh ? "mesh" : "inherited"))}, {"uid", Atom(std::string(mesh ? "mesh-uid" : "ordinary-uid"))}, {"type", Atom(std::int64_t{0})}, {"inheritMask", Atom(std::int64_t{0x02000004})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(children)}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(mesh ? std::int64_t{1} : std::int64_t{0})}, {"meshCombine", Atom(false)}, {"meshSyncChildMask", Atom(mesh ? std::int64_t{8} : std::int64_t{0})}, {"meshDivision", Atom(std::int64_t{4})}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}});
}

static std::shared_ptr<sgre::resource::PsbDocument> Document() {
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1})})}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(12.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	const auto timeline = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(16.0)}, {"bottom", Atom(8.0)}})}, {"layerIndexMap", Atom(sgre::resource::PsbObject{{"move_LR", Atom(std::int64_t{0})}})}, {"lastTime", Atom(12.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(sgre::resource::PsbArray{Layer(true)})}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{4})}, {"height", Atom(std::int64_t{4})}, {"truncated_width", Atom(std::int64_t{4})}, {"truncated_height", Atom(std::int64_t{4})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	const auto icon = Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"width", Atom(2.0)}, {"height", Atom(2.0)}, {"originX", Atom(0.0)}, {"originY", Atom(0.0)}, {"attr", Atom(std::int64_t{2})}});
	const auto source = Atom(sgre::resource::PsbObject{{"texture", texture}, {"icon", Atom(sgre::resource::PsbObject{{"tile", icon}})}});
	const auto object = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"main", timeline}})}});
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 144;
	document->Chunks = {{128, 16}};
	document->Root = Atom(sgre::resource::PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Mesh override owner fixture"))}, {"easing", Atom(sgre::resource::PsbArray{})}, {"source", Atom(sgre::resource::PsbObject{{"atlas", source}})}, {"object", Atom(sgre::resource::PsbObject{{"character", object}})}, {"sourceIconRenameMap", Atom(sgre::resource::PsbObject{})}});
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

	explicit Fixture(std::shared_ptr<sgre::resource::PsbDocument> document = Document()) : Cache([this](auto, const auto&) {
		                                                                                       return sgre::render::SpriteTextureHandle{static_cast<std::uint32_t>(++Uploads), 1, &Uploads};
	                                                                                       },
	                                                                                           [this](auto) {
		                                                                                           ++Releases;
	                                                                                           }),
	                                                                                       Catalog(Cache), Manager(Cache), Clock(0), Tasks(8) {
		for (std::size_t index = 0; index != Seed.size(); ++index) {
			Seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17;
		}
		Asset = Catalog.Prepare(std::move(document), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		Project = Manager.Register(Asset->Identity, Asset->Model, *Asset->Backing);
		sgre::scene::MotionManagerRuntimeServices services;
		services.SceneAssets = &Catalog;
		services.ReadSourceAliases = [this](auto project) {
			Require(project == Project, "mesh aliases require their actual registration");

			return std::span<const sgre::scene::MotionSourceAlias>(Asset->Aliases);
		};
		services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			Require(project == Project, "mesh asset requires its actual registration");

			return *Asset;
		};
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, Clock, Seed, std::move(services), 32);
	}

	~Fixture() {
		Runtime->SceneContext().Clear();
		Tasks.Clear();
		Runtime.reset();
	}
};

static void RequireNear(float actual, float expected, std::string_view message) {
	if (!std::isfinite(actual) || std::abs(actual - expected) > 0.00002f) {
		throw std::runtime_error(std::string(message) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
	}
}

static void TestActualGlobalFrameAndReferenceSpeedLimit() {
	Fixture fixture;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	task.SetCoordinate({10, 20});
	task.SetVisible(true);
	task.Play("main");
	fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second).Update();
	const auto* plain = task.GenericPlayer();
	Require(plain != nullptr, "global frame fixture requires the actual generic plain owner");
	sgre::scene::EmoteVelvetyFrameDefinition definition;
	definition.PixelsPerMeter = 2;
	sgre::scene::EmoteVelvetyFrameOwner frame_owner(task.Root(), task.Commands(), *plain, definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls controls;
	const auto first = frame_owner.Prepare(60, controls);
	RequireNear(first.WorldToPhysics.XX, 0.5f, "native default0x201 flags convert actual plain root zoom and ppm");
	RequireNear(first.WorldToPhysics.YY, 0.5f, "physical units apply to both actual coordinate axes");
	RequireNear(first.WorldToPhysics.TranslationX, 0, "first actual reference initializes previous and accumulated positions together");
	Require(first.ResetRotationReference, "native metadata initialization starts the real control rotation caches");
	RequireNear(first.DepthScale, -(720.0f / 100.0f) / (std::tan(0.52359879016876220703125f) * 2.0f), "native physical depth uses actual containing view height and tangent producer");
	const auto& inverse = frame_owner.PhysicsToWorld();
	RequireNear(inverse.XX, 2, "actual frame owner retains the inverse coordinate conversion");
	task.SetCoordinate({12, 20});
	task.Progress(0);
	const auto next = frame_owner.Prepare(60, controls);
	const auto expected_step = 1.0f - std::exp(-1.60000002384185791015625f);
	RequireNear(next.WorldToPhysics.TranslationX, expected_step - 1, "actual reference speed cap retains displacement deficit in the global conversion");
	Require(!next.ResetRotationReference, "later actual frame preserves control rotation-reference continuity");
	definition.ReferenceSpeedLimit = 0;
	sgre::scene::EmoteVelvetyFrameOwner unlimited(task.Root(), task.Commands(), *plain, definition, 720);
	static_cast<void>(unlimited.Prepare(60, controls));
	task.SetCoordinate({14, 20});
	task.Progress(0);
	RequireNear(unlimited.Prepare(60, controls).WorldToPhysics.TranslationX, 0, "native zero reference limit forwards exact actual displacement");
	sgre::scene::EmoteVelvetyFrameOwner rotated(task.Root(), task.Commands(), *plain, definition, 720);
	controls.Rotation = 1.57079637050628662109375f;
	const auto transformed = rotated.Prepare(60, controls);
	RequireNear(transformed.WorldToPhysics.XX, 0, "native global conversion uses the actual Emote rotation");
	RequireNear(transformed.WorldToPhysics.XY, -0.5f, "native global rotation is composed before unit conversion");
	RequireNear(transformed.WorldToPhysics.TranslationX, 17, "actual global conversion rotates about genuine plain root coordinate");
	RequireNear(transformed.WorldToPhysics.TranslationY, 3, "root-centered rotation retains the original translation convention");
	controls.Flags = 0;
	sgre::scene::EmoteVelvetyFrameOwner unscaled(task.Root(), task.Commands(), *plain, definition, 720);
	const auto unscaled_frame = unscaled.Prepare(60, controls);
	RequireNear(unscaled_frame.WorldToPhysics.XY, -1, "native flag9 clear keeps the actual unscaled affine");
	controls.Rotation = 0;
	controls.Flags = 0x100;
	sgre::scene::EmoteVelvetyFrameOwner centered(task.Root(), task.Commands(), *plain, definition, 720);
	const auto centered_frame = centered.Prepare(60, controls);
	RequireNear(centered_frame.WorldToPhysics.XX, 0.5f, "native flag8 separately scales around actual root");
	RequireNear(centered_frame.WorldToPhysics.TranslationX, 7, "flag8 root pivot remains distinct from flag9 global-unit conversion");
	controls.Flags = 0x300;
	sgre::scene::EmoteVelvetyFrameOwner combined(task.Root(), task.Commands(), *plain, definition, 720);
	RequireNear(combined.Prepare(60, controls).WorldToPhysics.XX, 0.25f, "both real native flag branches execute in their actual order");
	definition.ReferenceLayer = "absent actual map entry";
	sgre::scene::EmoteVelvetyFrameOwner missing(task.Root(), task.Commands(), *plain, definition, 720);
	RequireFailure([&] {
		static_cast<void>(missing.Prepare(60, controls));
	},
	    "reference layer absent");
}

static void TestActualCommonMetadataPresenceAndDefaults() {
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{})}});
	Require(!sgre::scene::ReadEmoteVelvetyFrameDefinition(document), "absent Common leaves the actual separate driver disabled");
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"velvetyCommonParams", Atom(sgre::resource::PsbObject{{"ppm", Atom(2625.0)}})}})}});
	const auto definition = sgre::scene::ReadEmoteVelvetyFrameDefinition(document);
	Require(definition && definition->PixelsPerMeter == 2625 && definition->ReferenceLayer == "move_LR" && definition->ReferenceSpeedLimit == 1, "native Common initialization preserves real ppm, default reference name and constructor speed limit");
}

static sgre::resource::PsbValue& MutableField(sgre::resource::PsbValue& value, std::string_view name) {
	for (auto& field : std::get<sgre::resource::PsbObject>(value.Data)) {
		if (field.first == name) {
			return field.second;
		}
	}
	throw std::runtime_error("clear fixture field is absent");
}

static std::shared_ptr<sgre::resource::PsbDocument> ReferenceDocument(const std::array<float, 2>& initial, bool child = false) {
	auto document = Document();
	auto& objects = MutableField(document->Root, "object");
	auto& object = MutableField(objects, "character");
	auto& timeline = MutableField(MutableField(object, "motion"), "main");
	auto& layers = std::get<sgre::resource::PsbArray>(MutableField(timeline, "layer").Data);
	auto& selected = child ? std::get<sgre::resource::PsbArray>(MutableField(layers[0], "children").Data)[0] : layers[0];
	auto& frames = std::get<sgre::resource::PsbArray>(MutableField(selected, "frameList").Data);
	for (std::size_t index = 0; index != frames.size(); ++index) {
		auto& content = MutableField(frames[index], "content");
		MutableField(content, "coord") = Atom(sgre::resource::PsbArray{Atom(static_cast<double>(index == 0 ? initial[0] : 10.0f)), Atom(static_cast<double>(initial[1])), Atom(0.0)});
	}
	if (child) {
		MutableField(timeline, "layerIndexMap") = Atom(sgre::resource::PsbObject{{"move_LR", Atom(std::int64_t{1})}});
	}
	return document;
}

struct PlainFixture {
	sgre::scene::MotionRootScene Root;
	sgre::scene::MotionPlayerRenderControls Render;
	sgre::scene::MotionPlayerCommands Commands;
	std::unique_ptr<sgre::scene::MotionScenePlayer> Plain;

	explicit PlainFixture(Fixture& fixture)
	    : Commands(fixture.Manager, [this](const auto& selected, auto) {
		      Plain->Rebuild(selected);
	      },
	          [this] {
		          Plain->CaptureJoin();
	          }) {
		Plain = std::make_unique<sgre::scene::MotionScenePlayer>(*fixture.Runtime, Commands, Root, Render, [](const auto&) {
		});
		Root.SetVisible(true);
		Commands.SetCharacter("character");
		Commands.Play("main");
		Plain->Progress(0);
	}
};

static std::array<float, 12> Matrices(const sgre::scene::EmoteVelvetyInputFrame& frame, const sgre::resource::MotionMeshMatrix& inverse) {
	const auto& matrix = frame.WorldToPhysics;
	return {matrix.XX, matrix.XY, matrix.TranslationX, matrix.YX, matrix.YY, matrix.TranslationY, inverse.XX, inverse.XY, inverse.TranslationX, inverse.YX, inverse.YY, inverse.TranslationY};
}

static void RequireNativeMatrices(const sgre::scene::EmoteVelvetyInputFrame& frame, const sgre::scene::EmoteVelvetyFrameOwner& owner, const std::array<std::uint32_t, 12>& expected) {
	const auto actual = Matrices(frame, owner.PhysicsToWorld());
	for (std::size_t index = 0; index != actual.size(); ++index) {
		RequireNear(actual[index], std::bit_cast<float>(expected[index]), "original warm Clear frame matrix differs beyond2e-5 host-math tolerance");
	}
}

static void TestWarmClearOriginalFrames() {
	for (const auto& item : emote_clear_vectors::kCases) {
		const std::array<float, 2> initial{std::bit_cast<float>(item.InitialReference[0]), std::bit_cast<float>(item.InitialReference[1])};
		Fixture fixture(ReferenceDocument(initial));
		PlainFixture actual(fixture);
		sgre::scene::EmoteVelvetyFrameDefinition definition;
		definition.PixelsPerMeter = 2;
		sgre::scene::EmoteVelvetyFrameOwner frame(actual.Root, actual.Commands, *actual.Plain, definition, 100);
		sgre::scene::EmoteVelvetyGlobalControls controls;
		const auto reference = actual.Plain->Scene()->FindLayer("move_LR");
		Require(reference.has_value(), "warm clear projection uses a real resolved scene reference");
		const auto raw = actual.Plain->Scene()->LayerCoordinate(*reference);
		RequireNear(raw[0], initial[0], "actual first scene coordinate matches prepared original numeric input");
		RequireNear(raw[1], initial[1], "actual second scene coordinate matches prepared original numeric input");
		RequireNativeMatrices(frame.Prepare(1, controls), frame, item.Matrices[0]);
		actual.Commands.SetTickCount(4);
		actual.Plain->Progress(0);
		const auto moving_frame = frame.Prepare(0.5f, controls);
		RequireNativeMatrices(moving_frame, frame, item.Matrices[1]);
		frame.ClearPlain(*actual.Plain);
		Require(actual.Plain->Scene() == nullptr && actual.Plain->CanMutate(), "genuine clear retires the scene tree while actual root/plain remain");
		RequireNativeMatrices(moving_frame, frame, item.Matrices[1]);
		RequireNativeMatrices(frame.Prepare(1, controls), frame, item.Matrices[2]);
		actual.Root.SetCoordinate({5, -2});
		actual.Root.SetZoom(2, 2);
		actual.Plain->Progress(0);
		controls.Rotation = 0.25f;
		controls.Flags = 0x301;
		controls.Mirror = true;
		RequireNativeMatrices(frame.Prepare(1, controls), frame, item.Matrices[3]);
		frame.ClearPlain(*actual.Plain);
		RequireNativeMatrices(frame.Prepare(0, controls), frame, item.Matrices[4]);
		Require(actual.Root.Overrides().Coordinate[0] == 5 && actual.Root.Overrides().ZoomX == 2 && fixture.Manager.ReferenceCount(fixture.Project) == 1, "repeated clear preserves real outer root and independent registration count");
	}
}

static void TestWarmChildClearAndExactOwnerAdmission() {
	Fixture fixture(ReferenceDocument({2, 0}, true));
	PlainFixture actual(fixture);
	PlainFixture foreign(fixture);
	sgre::scene::EmoteVelvetyFrameOwner frame(actual.Root, actual.Commands, *actual.Plain, {}, 100);
	sgre::scene::EmoteVelvetyGlobalControls controls;
	static_cast<void>(frame.Prepare(1, controls));
	RequireFailure([&] {
		frame.ClearPlain(*foreign.Plain);
	},
	    "exact live idle");
	Require(foreign.Plain->Scene() != nullptr && actual.Plain->Scene() != nullptr, "foreign clear rejection preserves both actual trees");
	bool rejected = false;
	std::thread other([&] {
		try {
			frame.ClearPlain(*actual.Plain);
		} catch (const std::runtime_error&) {
			rejected = true;
		}
	});
	other.join();
	Require(rejected && actual.Plain->Scene() != nullptr, "foreign thread rejects before reading or retiring the genuine plain");
	actual.Plain->Draw();
	RequireFailure([&] {
		frame.ClearPlain(*actual.Plain);
	},
	    "idle unqueued");
	fixture.Runtime->SceneContext().Clear();
	actual.Commands.Play("main", sgre::scene::MotionPlayFlags::Force);
	actual.Plain->Progress(0);
	RequireFailure([&] {
		frame.ClearPlain(*actual.Plain);
	},
	    "unchanged actual cached reference");
	Require(actual.Plain->Scene() != nullptr, "stale physical-cell reuse rejects before actual tree mutation");
	static_cast<void>(frame.Prepare(1, controls));
	frame.ClearPlain(*actual.Plain);
	const auto next = frame.Prepare(1, controls);
	Require(std::isfinite(next.WorldToPhysics.TranslationX) && !next.ResetRotationReference, "real inherited child reference is retired without resetting warm frame history");
	actual.Commands.Play("main", sgre::scene::MotionPlayFlags::Force);
	actual.Plain->Progress(0);
	RequireFailure([&] {
		frame.ClearPlain(*actual.Plain);
	},
	    "post-clear physical-cell reuse");
	Require(actual.Plain->Scene() != nullptr, "new physical cells cannot borrow the previous cleared tree's raw coordinates before rebinding");
	static_cast<void>(frame.Prepare(1, controls));
	frame.ClearPlain(*actual.Plain);
	static_cast<void>(frame.Prepare(1, controls));
}

static void TestColdClearAndColdNextUpdateGate() {
	Fixture fixture;
	PlainFixture actual(fixture);
	sgre::scene::EmoteVelvetyFrameOwner frame(actual.Root, actual.Commands, *actual.Plain, {}, 100);
	frame.ClearPlain(*actual.Plain);
	frame.ClearPlain(*actual.Plain);
	Require(actual.Plain->Scene() == nullptr && actual.Plain->CanMutate(), "cold native Clear is real and idempotent before first physical update");
	RequireFailure([&] {
		static_cast<void>(frame.Prepare(1, {}));
	},
	    "cold missing-anchor");
	Require(actual.Plain->CanMutate(), "unsupported cold history does not invent samples or block actual plain destruction");
}

int main() {
	try {
		TestActualCommonMetadataPresenceAndDefaults();
		TestActualGlobalFrameAndReferenceSpeedLimit();
		TestWarmClearOriginalFrames();
		TestWarmChildClearAndExactOwnerAdmission();
		TestColdClearAndColdNextUpdateGate();
		std::cout << "Actual Velvety frame: Common metadata, real plain root/scene map, unit/rotation flags, reference history and bounded-speed deficit passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
