#include "sgre/scene/emote_parts_tree.hpp"
#include "sgre/scene/emote_synchronize_control.hpp"
#include "sgre/scene/emote_velvety_frame.hpp"
#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"
#include "sgre/scene/motion_draw.hpp"
#include "sgre/scene/motion_grid_draw.hpp"
#include "sgre/scene/motion_player_task.hpp"
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

	explicit Fixture(std::shared_ptr<sgre::resource::PsbDocument> document = Document())
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

static void Set(sgre::resource::PsbValue& object, std::string name, sgre::resource::PsbValue value) {
	for (auto& [key, field] : std::get<sgre::resource::PsbObject>(object.Data)) {
		if (key == name) {
			field = std::move(value);
			return;
		}
	}
	std::get<sgre::resource::PsbObject>(object.Data).emplace_back(std::move(name), std::move(value));
}

static std::vector<sgre::resource::MotionPoint> Positions(const sgre::scene::MotionSceneEvaluation& scene) {
	std::vector<sgre::resource::MotionPoint> positions;
	for (std::size_t index = 0; index != scene.PacketRows().size(); ++index) {
		const auto& command = scene.Packets().Nodes()[index].Command;
		Require(command.Geometry && !command.Hidden && command.GeometryCount != 0, "actual source and target retain their completed geometry");
		positions.insert(positions.end(), command.Geometry->Positions.begin(), command.Geometry->Positions.end());
	}
	Require(!positions.empty(), "real Synchronize scene retains both mesh owners");
	return positions;
}

static void RequirePoints(const std::vector<sgre::resource::MotionPoint>& actual, const std::vector<sgre::resource::MotionPoint>& expected, std::string_view operation) {
	Require(actual.size() == expected.size(), operation);
	for (std::size_t index = 0; index != actual.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(actual[index].X) == std::bit_cast<std::uint32_t>(expected[index].X) && std::bit_cast<std::uint32_t>(actual[index].Y) == std::bit_cast<std::uint32_t>(expected[index].Y), operation);
	}
}

static sgre::resource::PsbValue Part(std::string uid, sgre::resource::PsbArray children = {}) {
	return Atom(sgre::resource::PsbObject{{"uid", Atom(std::move(uid))}, {"label", Atom(std::string("actual part"))}, {"children", Atom(std::move(children))}});
}

static sgre::resource::PsbValue& MutableField(sgre::resource::PsbValue& object, std::string_view name) {
	for (auto& [key, value] : std::get<sgre::resource::PsbObject>(object.Data)) {
		if (key == name) {
			return value;
		}
	}
	throw std::runtime_error("independent parts fixture field absent: " + std::string(name));
}

static std::shared_ptr<sgre::resource::PsbDocument> TwoIndependentMeshes() {
	auto document = Document();
	auto& objects = MutableField(document->Root, "object");
	auto& timeline = MutableField(MutableField(MutableField(objects, "character"), "motion"), "main");
	auto first = Layer(true);
	Set(first, "children", Atom(sgre::resource::PsbArray{}));
	Set(first, "meshSyncChildMask", Atom(std::int64_t{0}));
	auto second = Layer(true);
	Set(second, "children", Atom(sgre::resource::PsbArray{}));
	Set(second, "meshSyncChildMask", Atom(std::int64_t{0}));
	Set(second, "uid", Atom(std::string("other-mesh-uid")));
	Set(second, "label", Atom(std::string("other-mesh")));
	for (auto& frame : std::get<sgre::resource::PsbArray>(MutableField(second, "frameList").Data)) {
		Set(MutableField(frame, "content"), "angle", Atom(0.0));
	}
	Set(timeline, "layer", Atom(sgre::resource::PsbArray{std::move(first), std::move(second)}));
	return document;
}

static sgre::scene::EmoteSynchronizeDefinition Definition() {
	sgre::resource::PsbDocument document;
	const auto control = Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("other-mesh-uid"))}, {"sourceUid", Atom(std::string("mesh-uid"))}, {"ppm", Atom(100.0)}, {"sizeH", Atom(2.0)}, {"sizeV", Atom(3.0)}, {"originX", Atom(0.25)}, {"originY", Atom(0.75)}, {"weight", Atom(0.5)}, {"weightHorizontal", Atom(0.8)}, {"weightVertical", Atom(0.6)}, {"flipHorizontal", Atom(true)}, {"flipVertical", Atom(false)}, {"fixes", Atom(sgre::resource::PsbArray{})}, {"inheritShape", Atom(true)}, {"correctFix", Atom(true)}, {"correctScale", Atom(true)}, {"coordPred", Atom(3.999999999)}});
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"velvetyCommonParams", Atom(sgre::resource::PsbObject{})}, {"velvetySynchronizeControl", Atom(sgre::resource::PsbArray{control})}})}});
	auto definitions = sgre::scene::ReadEmoteSynchronizeDefinitions(document);
	Require(definitions.size() == 1 && definitions.front().PredictionDegree == 3 && !definitions.front().CorrectFix && !definitions.front().CorrectScale, "real metadata keeps serialized order, direct double integer conversion and native inheritShape suppression");
	return std::move(definitions.front());
}

static void TestActualSourceTargetAndPendingOutput() {
	Fixture fixture(TwoIndependentMeshes());
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "real Synchronize consumes the genuine selected plain scene");
	const auto& registry = task.Commands().LayerUids();
	Require(registry.Matches("mesh-uid").size() == 1 && registry.Matches("other-mesh-uid").size() == 1, "actual source and target UIDs are independent selected occurrences");
	const auto source = registry.Matches("mesh-uid").front().Layer;
	const auto target = registry.Matches("other-mesh-uid").front().Layer;
	auto lattice = sgre::scene::IdentityMotionBezierLattice(0x33);
	for (std::size_t index = 0; index != lattice.Controls.size(); ++index) {
		lattice.Controls[index].X += static_cast<float>(index + 1) * 0.0125f;
		lattice.Controls[index].Y -= static_cast<float>(index + 1) * 0.0075f;
	}
	Require(source.SetMesh(1, &lattice, true), "source installs its genuine pending lattice before the Synchronize consumer");
	source.InitializeMeshOrigin({17, 19}, {0.125f, 0.375f});
	const auto before = Positions(*plain->Scene());
	auto definition = Definition();
	sgre::scene::EmoteSynchronizeControl control(task.Commands(), definition);
	Require(control.InstanceCount() == 1 && registry.SubscriptionCount() == 2, "actual Synchronize separately subscribes target occurrences and source presence");
	sgre::scene::EmotePartsTree tree(task.Commands(), Atom(sgre::resource::PsbArray{Part("other-mesh-uid")}));
	Require(tree.AttachSynchronize(control) && tree.SynchronizeCount() == 1, "typed PartsTree attaches the real Synchronize callback to its exact UID");
	RequireFailure([&] {
		static_cast<void>(tree.AttachSynchronize(control));
	},
	    "duplicate");
	sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
	frame_definition.PixelsPerMeter = 100;
	sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls globals;
	globals.Flags = 0x200;
	const auto frame = global.Prepare(1, globals);
	RequireFailure([&] {
		tree.UpdateStrings(1, frame);
	},
	    "String-only dispatch");
	tree.UpdateControls(1, frame);
	Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() == 1, "one genuine active target consumes one real source mesh without another plain update");
	RequirePoints(Positions(*plain->Scene()), before, "source and target pending mesh writes preserve already completed borrowed geometry");
	const auto& actual = target.CurrentMesh();
	Require(actual.EncodedDegree == 0x33 && actual.Controls.size() == 16, "Synchronize copies the actual current source operator/control extent");
	for (std::size_t y = 0; y != 4; ++y) {
		for (std::size_t x = 0; x != 4; ++x) {
			const auto& incoming = lattice.Controls[y * 4 + 3 - x];
			const auto& point = actual.Controls[y * 4 + x];
			const auto u = static_cast<float>(x) / 3.0f;
			const auto v = static_cast<float>(y) / 3.0f;
			RequireNear(point.X, (1.0f - 0.4f) * u + 0.4f * (1.0f - incoming.X), "native horizontal reverse plus 1-x precedes actual weighted identity blend");
			RequireNear(point.Y, (1.0f - 0.3f) * v + 0.3f * incoming.Y, "native vertical weight consumes the real source y values");
		}
	}
	RequireNear(target.MeshPhysicalOrigin().X, 0.125f, "native source origin x uses vertical flip flag");
	RequireNear(target.MeshPhysicalOrigin().Y, -0.375f, "native source origin y uses horizontal flip flag");
	const auto previous_occurrence = registry.Matches("other-mesh-uid").front().Occurrence;
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	Require(control.InstanceCount() == 1 && registry.Matches("other-mesh-uid").front().Occurrence != previous_occurrence, "true Force retirement replaces actual source and target identities without retaining dead numeric cells");
	task.Progress(0);
	control.Update(1, global.Prepare(1, globals));
	const auto new_target = registry.Matches("other-mesh-uid").front().Layer;
	RequireNear(new_target.MeshPhysicalOrigin().X, 0, "new actual source occurrence cannot reuse retired source origins");
	RequireNear(new_target.MeshPhysicalOrigin().Y, 0, "new target receives actual fresh source origin words");
	tree.Close();
	control.Close();
	Require(registry.SubscriptionCount() == 0, "source and target observers retire before the actual selected commands");
	auto absent = Definition();
	absent.SourceUid = "genuinely-absent-source";
	sgre::scene::EmoteSynchronizeControl no_source(task.Commands(), absent);
	no_source.Update(1, global.Prepare(1, globals));
	Require(no_source.InstanceCount() == 1 && no_source.LastUpdatedCount() == 0 && no_source.LastMeshWriteCount() == 0, "native absent source is real inactive presence, not fabricated empty mesh production");
	no_source.Close();

	const auto current_source = registry.Matches("mesh-uid").front().Layer;
	const auto retained_target = new_target.CurrentMesh();
	sgre::resource::MotionBezierLattice zero_axis;
	zero_axis.EncodedDegree = 0x30;
	zero_axis.Controls = {{0.25f, 0}, {0.25f, 0.25f}, {0.25f, 0.75f}, {0.25f, 1}};
	RequireFailure([&] {
		static_cast<void>(current_source.SetMesh(1, &zero_axis, false));
	},
	    "degree1..6 geometry consumer");
	Require(new_target.CurrentMesh().EncodedDegree == retained_target.EncodedDegree, "unsupported weighted identity does not publish a different target operator");
	RequirePoints(new_target.CurrentMesh().Controls, retained_target.Controls, "unsupported weighted identity rejects before publishing target controls");
	Require(registry.SubscriptionCount() == 0, "all actual Synchronize subscriptions remain retired after a rejected source producer");
	fixture.Tasks.Clear();
}

int main() {
	try {
		TestActualSourceTargetAndPendingOutput();
		std::cout << "Actual Synchronize source/target UID ownership, native flip/weight order and pending mesh lifetime passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
