#include "emote_stretch_native_vectors.hpp"
#include "sgre/scene/emote_parts_tree.hpp"
#include "sgre/scene/emote_stretch_control.hpp"
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
	Require(!positions.empty(), "real Stretch scene retains both mesh owners");
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

static sgre::scene::EmoteStretchDefinition Definition() {
	sgre::resource::PsbDocument document;
	const auto control = Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("other-mesh-uid"))}, {"ppm", Atom(100.0)}, {"sizeH", Atom(2.0)}, {"sizeV", Atom(3.0)}, {"originX", Atom(0.25)}, {"originY", Atom(0.75)}, {"fixes", Atom(sgre::resource::PsbArray{})}, {"inheritShape", Atom(true)}, {"correctScale", Atom(true)}, {"coordPred", Atom(2.999999999)}});
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"velvetyCommonParams", Atom(sgre::resource::PsbObject{})}, {"velvetyStretchControl", Atom(sgre::resource::PsbArray{control})}})}});
	const auto definitions = sgre::scene::ReadEmoteStretchDefinitions(document);
	Require(definitions.size() == 1 && definitions.front().PredictionDegree == 2 && !definitions.front().CorrectScale, "actual Stretch metadata uses direct integer truncation and inheritShape suppression");
	auto& entry = std::get<sgre::resource::PsbArray>(MutableField(MutableField(document.Root, "metadata"), "velvetyStretchControl").Data).front();
	Set(entry, "inheritShape", Atom(false));
	Require(sgre::scene::ReadEmoteStretchDefinitions(document).front().CorrectScale, "actual Stretch correctScale is active only outside inheritShape");
	Set(entry, "coordPred", Atom(4.0));
	RequireFailure([&] {
		static_cast<void>(sgre::scene::ReadEmoteStretchDefinitions(document));
	},
	    "degree0..3");
	Set(entry, "coordPred", Atom(0.0));
	Set(entry, "fixes", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"x", Atom(0.2)}, {"y", Atom(0.3)}})}));
	const auto parsed = sgre::scene::ReadEmoteStretchDefinitions(document);
	Require(parsed.front().Fixes.size() == 1, "actual Stretch owns its required fixes in serialized order");
	return definitions.front();
}

class StretchNativeReader {
public:
	std::uint32_t Word() {
		Require(Position < kEmoteStretchNativeWords.size(), "native Stretch vector ended early");
		return kEmoteStretchNativeWords[Position++];
	}
	float Float() {
		const auto value = std::bit_cast<float>(Word());
		Require(std::isfinite(value), "native Stretch input/output is nonfinite");
		return value;
	}
	sgre::resource::MotionPoint Point() {
		const auto x = Float();
		const auto y = Float();
		return {x, y};
	}
	bool Complete() const {
		return Position == kEmoteStretchNativeWords.size();
	}

private:
	std::size_t Position = 0;
};

static void TestOriginalEntity() {
	StretchNativeReader reader;
	Require(reader.Word() == 0x31525453U && reader.Word() == 64, "actual Stretch native vector header changed");
	float maximum = 0;
	for (std::uint32_t ordinal = 0; ordinal != 64; ++ordinal) {
		const auto index = reader.Word();
		sgre::scene::EmoteStretchDefinition definition;
		definition.CorrectScale = reader.Word() != 0;
		const auto fix_count = reader.Word();
		Require(fix_count <= 3, "native Stretch fix count exceeds actual owner");
		definition.Width = reader.Float();
		definition.Height = reader.Float();
		definition.OriginX = reader.Float();
		definition.OriginY = reader.Float();
		sgre::scene::EmoteVelvetyPosture pose;
		for (auto& value : pose.Physical.Coordinate) {
			value = reader.Float();
		}
		pose.Physical.Rotation = reader.Float();
		pose.Physical.Cosine = reader.Float();
		pose.Physical.Sine = reader.Float();
		pose.CorrectionAngle = reader.Float();
		pose.CorrectionScale = reader.Float();
		for (std::uint32_t fix = 0; fix != fix_count; ++fix) {
			definition.Fixes.push_back(reader.Point());
		}
		std::array<sgre::resource::MotionPoint, 3> targets{};
		for (std::uint32_t fix = 0; fix != fix_count; ++fix) {
			targets[fix] = reader.Point();
		}
		sgre::scene::EmoteStretchMeshOutput output(definition);
		const auto& lattice = output.Prepare(pose, std::span(targets).first(fix_count));
		Require(lattice.EncodedDegree == 0x11 && lattice.Controls.size() == 4, "actual Stretch owns linear/linear four-control output");
		for (std::size_t point = 0; point != 4; ++point) {
			for (std::size_t axis = 0; axis != 2; ++axis) {
				const auto expected = reader.Float();
				const auto actual = axis == 0 ? lattice.Controls[point].X : lattice.Controls[point].Y;
				const auto error = std::abs(actual - expected) / std::max(1.0f, std::abs(expected));
				if (!std::isfinite(actual) || error > 0.000008f) {
					throw std::runtime_error("Stretch case=" + std::to_string(index) + " point=" + std::to_string(point) + " axis=" + std::to_string(axis) + " expected=" + std::to_string(expected) + " actual=" + std::to_string(actual) + " error=" + std::to_string(error));
				}
				maximum = std::max(maximum, error);
			}
		}
	}
	Require(reader.Complete(), "native Stretch vector has trailing bytes");
	std::cout << "Original Stretch64 constructors/full317920/512 output words PASS maximum=" << maximum << '\n';
}

static void TestActualUidAndPartsTree() {
	Fixture fixture(TwoIndependentMeshes());
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "actual Stretch fixture has a genuine selected scene");
	const auto& registry = task.Commands().LayerUids();
	const auto target = registry.Matches("other-mesh-uid").front().Layer;
	const auto before = Positions(*plain->Scene());
	const auto degree = target.CurrentMesh().EncodedDegree;
	auto definition = Definition();
	definition.Fixes = {{0.2f, 0.3f}};
	definition.CorrectScale = true;
	sgre::scene::EmoteStretchControl control(task.Commands(), definition);
	Require(control.InstanceCount() == 1 && registry.SubscriptionCount() == 1, "actual Stretch owns one UID occurrence and one subscription");
	RequireNear(target.MeshPhysicalOrigin().X, -0.5f, "constructor actual physical origin X");
	RequireNear(target.MeshPhysicalOrigin().Y, 0.75f, "constructor actual physical origin Y");
	sgre::scene::EmotePartsTree tree(task.Commands(), Atom(sgre::resource::PsbArray{Part("other-mesh-uid")}));
	Require(tree.AttachStretch(control) && tree.StretchCount() == 1, "real PartsTree indexes actual Stretch UID");
	RequireFailure([&] {
		static_cast<void>(tree.AttachStretch(control));
	},
	    "duplicate");
	sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
	frame_definition.PixelsPerMeter = 100;
	sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls globals;
	globals.Flags = 0x200;
	const auto frame = global.Prepare(1, globals);
	const sgre::scene::EmoteVelvetyPartPosture parent{0.7f, 1.4f};
	sgre::scene::EmoteVelvetyPostureReader reader(definition.Width, definition.Height, definition.OriginX, definition.OriginY, definition.PixelsPerMeter, definition.Fixes);
	auto raw_pose = reader.Read(target, frame, &parent);
	const auto fitted_pose = raw_pose;
	raw_pose.Physical.Coordinate[2] = 0;
	raw_pose.Physical.Rotation = raw_pose.CorrectionAngle;
	raw_pose.Physical.Cosine = std::cos(raw_pose.CorrectionAngle);
	raw_pose.Physical.Sine = std::sin(raw_pose.CorrectionAngle);
	sgre::scene::EmoteStretchMeshOutput raw_output(definition);
	sgre::scene::EmoteStretchMeshOutput fitted_output(definition);
	const auto expected = raw_output.Prepare(raw_pose, reader.WorldFixes());
	const auto wrong = fitted_output.Prepare(fitted_pose, reader.WorldFixes());
	Require(std::abs(expected.Controls.front().X - wrong.Controls.front().X) > 0.001f || std::abs(expected.Controls.front().Y - wrong.Controls.front().Y) > 0.001f, "fixture distinguishes actual raw Stretch angle from fitted parent rotation");
	control.Update(1, frame, &parent);
	Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() == 1, "actual Stretch updates and latches one pending mesh");
	Require(degree == 0x33 && target.CurrentMesh().EncodedDegree == 0x11, "actual UID mesh latch exposes pending degree11 before completed geometry changes");
	RequirePoints(Positions(*plain->Scene()), before, "actual Stretch physical tail leaves current draw geometry unchanged");
	RequireFailure([&] {
		tree.UpdateStrings(1, frame);
	},
	    "non-String");
	task.Progress(0);
	Require(target.CurrentMesh().EncodedDegree == 0x11, "next genuine plain Progress consumes actual pending degree11");
	RequirePoints(target.CurrentMesh().Controls, expected.Controls, "actual Stretch driver uses raw selected rotation and zero Z rather than fitted parent rotation");
	RequireNear(control.PartPosture().Rotation, parent.Rotation, "actual Stretch PartsTree cache still retains fitted parent rotation");
	tree.UpdateControls(1, global.Prepare(1, globals));
	Require(control.PartPosture().Scale > 0 && tree.Posture("other-mesh-uid")->Scale > 0, "PartsTree retains actual fitted posture after successful active callback");
	task.Stop();
	task.Progress(0);
	const auto retained = Positions(*plain->Scene());
	tree.UpdateControls(1, global.Prepare(1, globals));
	RequirePoints(Positions(*plain->Scene()), retained, "stopped timeline physical update still does not run an extra plain update");
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	Require(control.InstanceCount() == 1, "Force retires old Stretch occurrence then binds the real replacement");
	task.Progress(0);
	tree.UpdateControls(1, global.Prepare(1, globals));
	tree.Close();
	control.Close();
	Require(registry.SubscriptionCount() == 0, "real Stretch retires UID subscription before Commands");
	RequireFailure([&] {
		control.Update(1, frame);
	},
	    "live idle");
	task.RequestRetire();
	fixture.Tasks.Clear();
}

int main() {
	try {
		TestOriginalEntity();
		TestActualUidAndPartsTree();
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
