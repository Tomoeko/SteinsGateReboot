#include "../src/scene/motion_native_compared_mesh.hpp"
#include "emote_string_zero_native_vectors.hpp"

#include "sgre/scene/emote_string_control.hpp"

#include "sgre/scene/emote_parts_tree.hpp"

#include "sgre/scene/emote_variable_controls.hpp"
#include "sgre/scene/emote_velvety_frame.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_draw.hpp"
#include "sgre/scene/motion_grid_draw.hpp"
#include "sgre/scene/motion_layer_mesh_local_owner.hpp"
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
#include <limits>
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

static sgre::resource::PsbValue ControlMetadata() {
	const auto tress = Atom(sgre::resource::PsbObject{{"topXRate", Atom(0.5)}, {"topYRate", Atom(0.0)}, {"bottomXRate", Atom(0.5)}, {"bottomYRate", Atom(1.0)}});
	return Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("mesh-uid"))}, {"ppm", Atom(2.0)}, {"boneDirection", Atom(std::int64_t{0})}, {"gravity", Atom(1.0)}, {"roughness", Atom(0.0)}, {"rigidMode", Atom(std::int64_t{0})}, {"sizeH", Atom(2.0)}, {"sizeV", Atom(4.0)}, {"originX", Atom(0.25)}, {"originY", Atom(0.75)}, {"splitLeft", Atom(0.5)}, {"splitRight", Atom(0.5)}, {"mass", Atom(1.0)}, {"numPerMeter", Atom(1.0)}, {"frictionTop", Atom(0.0)}, {"frictionBottom", Atom(0.0)}, {"elastic", Atom(0.0)}, {"straightness", Atom(0.0)}, {"coordScaleH", Atom(1.0)}, {"coordScaleV", Atom(1.0)}, {"coordScaleD", Atom(0.0)}, {"compressWiden", Atom(0.0)}, {"controlPointSize", Atom(0.0)}, {"adhocSim", Atom(false)}, {"tresses", Atom(sgre::resource::PsbArray{tress})}, {"rigidRange", Atom(sgre::resource::PsbObject{{"l", Atom(0.0)}, {"t", Atom(0.0)}, {"r", Atom(1.0)}, {"b", Atom(1.0)}})}, {"rigidIter", Atom(std::int64_t{0})}, {"fixes", Atom(sgre::resource::PsbArray{})}, {"coordPred", Atom(std::int64_t{3})}, {"collisionRefs", Atom(sgre::resource::PsbArray{})}, {"emissionRefs", Atom(sgre::resource::PsbArray{})}, {"speedVariables", Atom(sgre::resource::PsbArray{})}});
}

static sgre::scene::EmoteStringControlDefinition ReadDefinition(sgre::resource::PsbValue metadata) {
	sgre::resource::PsbDocument document;
	document.Root = Atom(sgre::resource::PsbObject{{"metadata", Atom(sgre::resource::PsbObject{{"velvetyCommonParams", Atom(sgre::resource::PsbObject{{"ppm", Atom(2.0)}})}, {"velvetyStringControl", Atom(sgre::resource::PsbArray{std::move(metadata)})}})}});
	auto definitions = sgre::scene::ReadEmoteStringControlDefinitions(document);
	Require(definitions.size() == 1, "actual control metadata must preserve one real physical/output/driver record");
	return std::move(definitions.front());
}

static std::vector<sgre::resource::MotionPoint> Positions(const sgre::scene::MotionSceneEvaluation& scene) {
	for (std::size_t index = 0; index != scene.PacketRows().size(); ++index) {
		if (scene.PacketRows()[index]->Node().Raw->Label == "mesh") {
			const auto& command = scene.Packets().Nodes()[index].Command;
			Require(command.Geometry && !command.Hidden && command.GeometryCount != 0, "actual String target must retain completed geometry");
			return {command.Geometry->Positions.begin(), command.Geometry->Positions.end()};
		}
	}
	throw std::runtime_error("actual String target is missing from its scene packets");
}

static void RequirePoints(const std::vector<sgre::resource::MotionPoint>& actual, const std::vector<sgre::resource::MotionPoint>& expected, std::string_view operation) {
	Require(actual.size() == expected.size(), operation);
	for (std::size_t index = 0; index != actual.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(actual[index].X) == std::bit_cast<std::uint32_t>(expected[index].X) && std::bit_cast<std::uint32_t>(actual[index].Y) == std::bit_cast<std::uint32_t>(expected[index].Y), operation);
	}
}

static void TestConnectedRealPrimaryControl() {
	Fixture fixture;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	sgre::scene::EmoteStringControl control(task.Commands(), ReadDefinition(ControlMetadata()));
	Require(control.InstanceCount() == 0 && task.Commands().LayerUids().SubscriptionCount() == 1, "actual subscription must not invent a physical instance before a selected UID exists");
	task.SetCoordinate({10, 20});
	task.SetVisible(true);
	task.Play("main");
	control.RequireReady();
	Require(control.InstanceCount() == 1, "real per-node Added must create exactly one physical occurrence");
	task.Progress(0);
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "the actual task must own a complete selected scene");
	sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
	frame_definition.PixelsPerMeter = 2;
	sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
	sgre::scene::EmoteVelvetyGlobalControls global_controls;
	global_controls.Flags = 0x200;
	for (std::size_t frame = 0; frame != 3; ++frame) {
		if (frame != 0) {
			task.SetCoordinate({10 + static_cast<float>(frame), 20});
			task.Progress(1);
		}
		const auto input = global.Prepare(1, global_controls);
		const auto before = Positions(*plain->Scene());
		control.Update(1, input);
		Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() <= 1, "actual active occurrence must run one physical update/output and at most one changed pending write");
		RequirePoints(Positions(*plain->Scene()), before, "tail physics must leave the already completed frame geometry unchanged");
		const auto cache = control.PartPosture();
		Require(std::isfinite(cache.Rotation) && cache.Scale > 0, "actual per-control posture cache must contain the completed UID-derived pose");
		task.Progress(0);
		Require(!Positions(*plain->Scene()).empty(), "the next ordinary plain update consumes actual pending physical output through the real geometry owner");
	}
	task.SetTickCount(8);
	task.Progress(0);
	Require(!task.Commands().LayerUids().Matches("mesh-uid").front().Layer.Active(), "authored hidden interval must make the actual UID inactive");
	control.Update(1, global.Prepare(1, global_controls));
	Require(control.LastUpdatedCount() == 0 && control.LastMeshWriteCount() == 0 && control.InstanceCount() == 1, "inactive owner retains its real physical instance and requests reactivation instead of publishing a pose");
	task.SetTickCount(0);
	task.Progress(0);
	control.Update(1, global.Prepare(1, global_controls));
	Require(control.LastUpdatedCount() == 1, "genuine reactivation must reset and update the existing physical occurrence");
	task.Play("main", sgre::scene::MotionPlayFlags::Force);
	control.RequireReady();
	Require(control.InstanceCount() == 1 && task.Commands().LayerUids().RegisteredCount() == 2, "real replacement must retire the prior physical instance and install only the new selected occurrence");
	control.Close();
	Require(control.InstanceCount() == 0 && task.Commands().LayerUids().SubscriptionCount() == 0, "explicit control close must retire physics before its borrowed actual task owner");
	fixture.Tasks.Clear();
	Require(!task.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "actual selected/task retirement must return UID and layer identifiers");
}

static void TestZeroTimeRequiresActualActiveDispatch() {
	for (const bool physical_zero : {false, true}) {
		Fixture fixture;
		sgre::scene::MotionScenePlayer* installed = nullptr;
		sgre::scene::MotionPlayerCommands commands(fixture.Manager, [&](const auto& selection, auto) {
			installed->Rebuild(selection);
		});
		sgre::scene::MotionRootScene root;
		root.SetVisible(true);
		sgre::scene::MotionPlayerRenderControls render;
		sgre::scene::MotionScenePlayer plain(*fixture.Runtime, commands, root, render, [](const auto&) {
		});
		installed = &plain;
		commands.SetCharacter("character");
		commands.Play("main");
		commands.SetTickCount(8);
		plain.Progress(0);
		Require(plain.Scene(), "String zero-time fixture uses its actual selected primary scene");
		sgre::scene::EmoteVariableProgram program;
		program.Transitions = {"zero-speed"};
		sgre::scene::EmoteVariableControls variables(
		    commands, [] {
		    },
		    std::move(program));
		auto definition = ReadDefinition(ControlMetadata());
		definition.Physics.Speed = physical_zero ? 0 : 1;
		definition.Physics.RigidSide = sgre::scene::EmoteTressRigidSide::First;
		definition.Physics.Roughness = 1;
		definition.Output.Fixes = {{0.5f, 0.5f}};
		definition.Output.CorrectFix = true;
		definition.Output.CorrectScale = true;
		definition.InheritShape = false;
		if (!physical_zero) {
			definition.SpeedVariables = {"zero-speed"};
		}
		sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
		frame_definition.PixelsPerMeter = 2;
		sgre::scene::EmoteVelvetyFrameOwner global(root, commands, plain, frame_definition, 720);
		sgre::scene::EmoteVelvetyGlobalControls globals;
		globals.Flags = 0x200;
		{
			auto missing = definition;
			missing.Physics.Uid = "absent-uid";
			sgre::scene::EmoteStringControl unmatched(commands, missing, &variables);
			unmatched.Update(1, global.Prepare(1, globals));
			Require(unmatched.InstanceCount() == 0 && unmatched.LastUpdatedCount() == 0 && unmatched.LastMeshWriteCount() == 0, "native empty UID map performs no physical time validation or invented update");
			unmatched.Close();
		}
		sgre::scene::EmoteStringControl control(commands, definition, &variables);
		control.Update(1, global.Prepare(1, globals));
		const auto retained = control.PartPosture();
		Require(control.InstanceCount() == 1 && control.LastUpdatedCount() == 0 && control.LastMeshWriteCount() == 0, "native inactive UID keeps its real zero-time instance while only setting reactivation reset");
		control.Update(3, global.Prepare(3, globals));
		Require(control.PartPosture().Rotation == retained.Rotation && control.PartPosture().Scale == retained.Scale, "inactive String retains actual part cache without fake posture/output");
		commands.SetTickCount(0);
		plain.Progress(0);
		const auto target = commands.LayerUids().Matches("mesh-uid").front().Layer;
		target.SetMesh(1, nullptr, false);
		const auto before = Positions(*plain.Scene());
		for (std::size_t update = 0; update != 3; ++update) {
			control.Update(1, global.Prepare(1, globals));
			Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() == 0 && target.CurrentMesh().EncodedDegree == 0, "actual String zero-time substep/output reaches native unordered identity comparison without inventing a mesh write");
			RequirePoints(Positions(*plain.Scene()), before, "zero String physics leaves the completed finite geometry unchanged until ordinary plain progress");
		}
		auto deformed = sgre::scene::IdentityMotionBezierLattice(0x43);
		deformed.Controls[5].X += 0.25f;
		Require(target.SetMesh(1, &deformed, true), "String comparison fixture begins with a real finite nonidentity override");
		control.Update(1, global.Prepare(1, globals));
		Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() == 1 && target.CurrentMesh().EncodedDegree == 0, "native unordered String output clears the finite pending override without copying nonfinite coordinates");
		RequirePoints(Positions(*plain.Scene()), before, "pending native comparison does not mutate the already-published geometry");
		commands.SetTickCount(8);
		plain.Progress(0);
		control.Update(1, global.Prepare(1, globals));
		Require(control.LastUpdatedCount() == 0, "actual hidden zero String retains its occurrence and requests a real reset");
		commands.SetTickCount(0);
		plain.Progress(0);
		control.Update(1, global.Prepare(1, globals));
		Require(control.LastUpdatedCount() == 1 && target.CurrentMesh().EncodedDegree == 0, "reactivation performs genuine finite initialization before the next native zero-time transaction");
		commands.Play("main", sgre::scene::MotionPlayFlags::Force);
		plain.Progress(0);
		control.RequireReady();
		control.Update(1, global.Prepare(1, globals));
		Require(control.InstanceCount() == 1 && control.LastUpdatedCount() == 1, "real FORCE retires the old zero String model and constructs exactly one fresh occurrence before dispatch");
		control.Close();
	}
}

static void TestOrdinaryZeroTimePhysicsRemainsRejected() {
	const auto definition = ReadDefinition(ControlMetadata());
	sgre::scene::EmoteStringPhysics physics(definition.Physics);
	sgre::scene::EmotePhysicalPosture posture;
	physics.SetPosture(posture);
	physics.Initialize();
	RequireFailure([&] {
		physics.Update(0);
	},
	    "positive actual scaled ticks");
	for (const auto& point : physics.PreparePhysicalSamples()) {
		Require(std::isfinite(point[0]) && std::isfinite(point[1]), "rejected ordinary zero update preserves the initialized finite physical owner");
	}
}

static void TestCapturedNativeZeroStringComparison() {
	for (const auto& capture : kNativeZeroStringOutputs) {
		sgre::resource::MotionBezierLattice output;
		output.EncodedDegree = capture.Degree;
		for (const auto& point : capture.Controls) {
			output.Controls.push_back({std::bit_cast<float>(point[0]), std::bit_cast<float>(point[1])});
		}
		sgre::resource::MotionBezierLattice current;
		Require(!sgre::scene::AssignNativeComparedMotionBezierLattice(current, output) && current.EncodedDegree == 0, "original zero String output preserves the empty pending override under native identity comparison");
		current = sgre::scene::IdentityMotionBezierLattice(output.EncodedDegree);
		current.Controls.front().X = 0.25f;
		Require(sgre::scene::AssignNativeComparedMotionBezierLattice(current, output), "original String comparison changes a genuinely different finite pending override");
		if (capture.Unordered) {
			Require(current.EncodedDegree == 0 && current.Controls.empty(), "original all-unordered String output clears the real finite override");
			output.Controls.front().X = 0.125f;
			RequireFailure([&] {
				static_cast<void>(sgre::scene::AssignNativeComparedMotionBezierLattice(current, output));
			},
			    "finite coordinates");
			Require(current.EncodedDegree == 0 && current.Controls.empty(), "native mixed would-copy branch cannot publish nonfinite controls");
		} else {
			Require(current.EncodedDegree == output.EncodedDegree && std::equal(current.Controls.begin(), current.Controls.end(), output.Controls.begin(), output.Controls.end(), [](const auto& a, const auto& b) {
				return std::bit_cast<std::uint32_t>(a.X) == std::bit_cast<std::uint32_t>(b.X) && std::bit_cast<std::uint32_t>(a.Y) == std::bit_cast<std::uint32_t>(b.Y);
			}),
			    "original finite custom-knot first output is copied rather than replaced with manufactured unordered values");
		}
	}
}

static void TestNativeScalarSpeedBinding() {
	Fixture fixture;
	sgre::scene::MotionPlayerCommands commands(fixture.Manager);
	commands.SetCharacter("character");
	sgre::scene::EmoteVariableProgram program;
	program.Transitions = {"existing-speed"};
	program.Immediate = {"existing-immediate"};
	sgre::scene::EmoteVariableControls variables(
	    commands, [] {
	    },
	    std::move(program));
	Require(variables.BindSpeedTransition("existing-speed") && variables.SpeedTransitionValue("existing-speed") == 0, "actual existing scalar cell keeps its native current value, rather than resetting to1");
	Require(!variables.BindSpeedTransition("existing-immediate") && !variables.SpeedTransitionValue("existing-immediate"), "native speed binder must skip an existing nontransition route");
	Require(variables.BindSpeedTransition("new-speed") && variables.SpeedTransitionValue("new-speed") == 1, "absent speed name creates a real scalar transition initialized to native1");
	variables.Set("new-speed", 0.5f);
	Require(variables.BindSpeedTransition("new-speed") && variables.SpeedTransitionValue("new-speed") == 0.5f, "repeated binding shares the actual scalar cell without default overwrite");
	variables.Update(0);
	RequireNear(commands.ParameterValues().ReadLocal("new-speed"), 0.5f, "new native scalar transition participates in genuine Motion variable publication");
}

static void TestPreciseOpenProducerDomains() {
	Fixture fixture;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	auto collision = ControlMetadata();
	Set(collision, "collisionRefs", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("actual-collider-uid"))}, {"ignoreMask", Atom(std::int64_t{0})}})}));
	Set(collision, "collisionHardness", Atom(std::int64_t{0}));
	Set(collision, "collisionRepulsion", Atom(100.0));
	const auto definition = ReadDefinition(collision);
	Require(definition.RequiresColliders, "nonempty actual collider declarations must survive parser ownership");
	RequireFailure([&] {
		sgre::scene::EmoteStringControl unsupported(task.Commands(), definition);
	},
	    "actual primary collision owner");
	Require(task.Commands().LayerUids().SubscriptionCount() == 0, "missing mandatory producer must fail before actual UID subscription");
	auto emission = ControlMetadata();
	Set(emission, "emissionRefs", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"uid", Atom(std::string("actual-emitter-uid"))}})}));
	RequireFailure([&] {
		sgre::scene::EmoteStringControl unsupported(task.Commands(), ReadDefinition(emission));
	},
	    "resolved emission owner join");
	Require(task.Commands().LayerUids().SubscriptionCount() == 0, "unrecovered actual emission owner must remain a pre-subscription gate");
	auto speed = ControlMetadata();
	Set(speed, "speedVariables", Atom(sgre::resource::PsbArray{Atom(std::string("actual-speed"))}));
	RequireFailure([&] {
		sgre::scene::EmoteStringControl unsupported(task.Commands(), ReadDefinition(speed));
	},
	    "actual transition publisher");
	auto wrong = ReadDefinition(ControlMetadata());
	wrong.Physics.Uid = "ordinary-uid";
	sgre::scene::EmoteStringControl control(task.Commands(), wrong);
	task.Play("main");
	RequireFailure([&] {
		control.RequireReady();
	},
	    "mode1 mesh owner");
	Require(control.InstanceCount() == 0, "a real nonmesh UID must not acquire a disconnected physical owner");
	control.Close();
	fixture.Tasks.Clear();
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

static void TestActualPartsTreeStringOrder() {
	Fixture fixture(TwoIndependentMeshes());
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	auto parent_definition = ReadDefinition(ControlMetadata());
	auto child_definition = parent_definition;
	child_definition.Physics.Uid = "other-mesh-uid";
	sgre::scene::EmoteStringControl parent(task.Commands(), parent_definition);
	sgre::scene::EmoteStringControl child(task.Commands(), child_definition);
	sgre::scene::EmotePartsTree tree(task.Commands(), Atom(sgre::resource::PsbArray{Part("mesh-uid", {Part("other-mesh-uid")}), Part("unused")}));
	Require(tree.PartCount() == 3 && tree.StringCount() == 0, "actual tree owns all serialized parts without creating physical callbacks");
	Require(tree.Posture("unused")->Rotation == 0 && tree.Posture("unused")->Scale == 0 && !tree.Posture("absent"), "native tree caches start zero, including untouched scale, and absent UID stays absent");
	Require(tree.AttachString(child) && tree.AttachString(parent), "actual control UID attachments must resolve independent of construction attachment order");
	RequireFailure([&] {
		static_cast<void>(tree.AttachString(parent));
	},
	    "duplicate one actual control");
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	const auto* plain = task.GenericPlayer();
	Require(plain && plain->Scene(), "parts dispatch borrows the genuine selected primary scene");
	sgre::scene::EmoteVelvetyInputFrame frame;
	frame.ResetRotationReference = true;
	const auto before = Positions(*plain->Scene());
	tree.UpdateStrings(1, frame);
	Require(parent.LastUpdatedCount() == 1 && child.LastUpdatedCount() == 1, "serialized tree must dispatch both active callbacks exactly once");
	const auto parent_pose = *tree.Posture("mesh-uid");
	const auto child_pose = *tree.Posture("other-mesh-uid");
	RequireNear(parent_pose.Rotation, 1.57079637050628662109375f, "real first root UID carries authored quarter turn");
	RequireNear(child_pose.Rotation, parent_pose.Rotation, "independent scene root must inherit actual PartsTree parent rather than its own zero angle");
	RequireNear(child_pose.Scale, parent_pose.Scale, "child uses the actual preceding parent scale cache");
	Require(tree.Posture("unused")->Scale == 0, "unbound part cache must remain native zero after dispatch");
	RequirePoints(Positions(*plain->Scene()), before, "PartsTree tail must not run a second plain update or change completed scene geometry");
	RequireFailure([&] {
		static_cast<void>(tree.AttachString(parent));
	},
	    "before dispatch");
	task.SetTickCount(9);
	task.Progress(0);
	frame.ResetRotationReference = false;
	tree.UpdateStrings(1, frame);
	Require(parent.LastUpdatedCount() == 0 && child.LastUpdatedCount() == 0, "inactive actual UID callbacks retain physical reset state without fabricated work");
	RequireNear(tree.Posture("mesh-uid")->Rotation, parent_pose.Rotation, "inactive parent retains preceding tree posture");
	tree.Close();
	child.Close();
	parent.Close();
	fixture.Tasks.Clear();
}

static void TestPartsTreeExactLookupAndOwner() {
	Fixture fixture;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	sgre::scene::EmoteStringControl control(task.Commands(), ReadDefinition(ControlMetadata()));
	sgre::scene::EmotePartsTree absent(task.Commands(), Atom(sgre::resource::PsbArray{Part("unrelated")}));
	Require(!absent.AttachString(control) && absent.StringCount() == 0, "missing exact part UID discards its callback without global fallback");
	sgre::scene::MotionPlayerCommands foreign(fixture.Manager);
	sgre::scene::EmotePartsTree other(foreign, Atom(sgre::resource::PsbArray{Part("mesh-uid")}));
	RequireFailure([&] {
		static_cast<void>(other.AttachString(control));
	},
	    "exact live primary");
	// Descendants enter the native index before their parent; an equal UID keeps the first indexed descendant.
	sgre::scene::EmotePartsTree duplicates(task.Commands(), Atom(sgre::resource::PsbArray{Part("mesh-uid", {Part("mesh-uid")})}));
	Require(duplicates.AttachString(control), "actual duplicate UID index still resolves one real callback target");
	task.SetVisible(true);
	task.Play("main");
	task.Progress(0);
	sgre::scene::EmoteVelvetyInputFrame frame;
	RequireFailure([&] {
		duplicates.UpdateStrings(1, frame);
	},
	    "finite positive actual inherited deformation scale");
	duplicates.Close();
	other.Close();
	absent.Close();
	control.Close();
	fixture.Tasks.Clear();
}

static void TestSerializedCorrectionDispatchAndConstructorContract() {
	for (const bool inherit : {false, true}) {
		for (const bool correct_fix : {false, true}) {
			for (const bool correct_scale : {false, true}) {
				Fixture fixture;
				auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
				});
				auto metadata = ControlMetadata();
				Set(metadata, "inheritShape", Atom(inherit));
				Set(metadata, "correctFix", Atom(correct_fix));
				Set(metadata, "correctScale", Atom(correct_scale));
				Set(metadata, "fixes", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"x", Atom(0.25)}, {"y", Atom(0.25)}}), Atom(sgre::resource::PsbObject{{"x", Atom(0.75)}, {"y", Atom(0.75)}})}));
				const auto definition = ReadDefinition(std::move(metadata));
				Require(definition.InheritShape == inherit && definition.Output.CorrectFix == (!inherit && correct_fix) && definition.Output.CorrectScale == (!inherit && correct_scale), "actual driver metadata preserves the native independent correction predicates");
				sgre::scene::EmoteStringControl control(task.Commands(), definition);
				task.SetVisible(true);
				task.Play("main");
				task.Progress(0);
				const auto* plain = task.GenericPlayer();
				Require(plain && plain->Scene() && control.InstanceCount() == 1, "serialized correction cases use a genuine active completed UID occurrence");
				sgre::scene::EmoteVelvetyFrameDefinition frame_definition;
				frame_definition.PixelsPerMeter = 2;
				sgre::scene::EmoteVelvetyFrameOwner global(task.Root(), task.Commands(), *plain, frame_definition, 720);
				sgre::scene::EmoteVelvetyGlobalControls globals;
				globals.Flags = 0x200;
				for (std::size_t phase = 0; phase != 4; ++phase) {
					task.SetCoordinate({10 + static_cast<float>(phase), 20 - static_cast<float>(phase)});
					task.SetAngleRadians(static_cast<float>(phase) * 0.1f);
					task.SetZoom(1 + static_cast<float>(phase) * 0.125f, 1 + static_cast<float>(phase) * 0.125f);
					task.Progress(0);
					const auto before = Positions(*plain->Scene());
					control.Update(1, global.Prepare(1, globals));
					Require(control.LastUpdatedCount() == 1 && control.LastMeshWriteCount() <= 1, "effective native correction flags dispatch one real positive String step without missing transformed fix targets");
					RequirePoints(Positions(*plain->Scene()), before, "native correction dispatch leaves the completed frame unchanged until the next genuine plain phase");
					const auto target = task.Commands().LayerUids().Matches("mesh-uid").front().Layer;
					for (const auto& point : target.CurrentMesh().Controls) {
						Require(std::isfinite(point.X) && std::isfinite(point.Y), "the actual pending correction output must stay finite through moving/rotating/scaling poses");
					}
				}
				control.Close();
				Require(task.Commands().LayerUids().SubscriptionCount() == 0, "each actual serialized correction control retires before its selected owner");
				if (inherit) {
					for (const bool scale_only : {false, true}) {
						auto invalid = definition;
						invalid.Output.CorrectFix = !scale_only;
						invalid.Output.CorrectScale = scale_only;
						RequireFailure([&] {
							sgre::scene::EmoteStringControl rejected(task.Commands(), invalid);
						},
						    "inherited-shape metadata suppresses correction flags");
						Require(task.Commands().LayerUids().SubscriptionCount() == 0, "invalid manually contradictory inherited definitions reject before subscribing an actual UID");
					}
				}
				fixture.Tasks.Clear();
				Require(fixture.Runtime->Identifiers().LiveCount() == 0, "actual correction cases return all layer identifiers before resource retirement");
			}
		}
	}
}

int main() {
	try {
		TestConnectedRealPrimaryControl();
		TestPreciseOpenProducerDomains();
		TestNativeScalarSpeedBinding();
		TestZeroTimeRequiresActualActiveDispatch();
		TestOrdinaryZeroTimePhysicsRemainsRejected();
		TestCapturedNativeZeroStringComparison();
		TestActualPartsTreeStringOrder();
		TestPartsTreeExactLookupAndOwner();
		TestSerializedCorrectionDispatchAndConstructorContract();
		std::cout << "Actual primary String control: real UID lifetime/origin, global frame, pose/physical/output phases, held geometry, inactive/reactivation and ordered retirement passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
