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
	for (std::size_t index = 0; index != 3; ++index) {
		auto content = sgre::resource::PsbObject{{"mask", Atom(mesh ? std::int64_t{0x02020603} : std::int64_t{0x20603})}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}, {"coord", Atom(sgre::resource::PsbArray{Atom(mesh ? 0.0 : 2.0), Atom(0.0), Atom(0.0)})}, {"ox", Atom(0.0)}, {"oy", Atom(0.0)}, {"opa", Atom(std::int64_t{255})}, {"color", Atom(std::int64_t{0x808080ff})}, {"bm", Atom(std::int64_t{0x10})}};
		if (mesh) {
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

	return Atom(sgre::resource::PsbObject{{"label", Atom(std::string(mesh ? "mesh" : "inherited"))}, {"uid", Atom(std::string(mesh ? "mesh-uid" : "ordinary-uid"))}, {"type", Atom(std::int64_t{0})}, {"inheritMask", Atom(std::int64_t{0x02000004})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(children)}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(mesh ? std::int64_t{1} : std::int64_t{0})}, {"meshSyncChildMask", Atom(mesh ? std::int64_t{8} : std::int64_t{0})}, {"meshDivision", Atom(std::int64_t{4})}, {"meshCombine", Atom(false)}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}});
}

static std::shared_ptr<sgre::resource::PsbDocument> Document() {
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1})})}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	const auto timeline = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(16.0)}, {"bottom", Atom(8.0)}})}, {"lastTime", Atom(8.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(sgre::resource::PsbArray{Layer(true)})}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
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

static void RequirePoints(std::span<const sgre::resource::MotionPoint> actual, std::span<const sgre::resource::MotionPoint> expected, std::string_view message) {
	Require(actual.size() == expected.size(), message);
	for (std::size_t index = 0; index != actual.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(actual[index].X) == std::bit_cast<std::uint32_t>(expected[index].X) && std::bit_cast<std::uint32_t>(actual[index].Y) == std::bit_cast<std::uint32_t>(expected[index].Y), std::string(message) + " at " + std::to_string(index));
	}
}

static std::size_t PacketIndex(const sgre::scene::MotionSceneEvaluation& scene, std::string_view name) {
	const auto rows = scene.PacketRows();
	for (std::size_t index = 0; index != rows.size(); ++index) {
		if (rows[index]->Node().Raw->Label == name) {
			return index;
		}
	}

	throw std::runtime_error("actual mesh row is absent from its packet graph");
}

static std::vector<sgre::resource::MotionPoint> Positions(const sgre::scene::MotionSceneEvaluation& scene, std::string_view name) {
	const auto& command = scene.Packets().Nodes()[PacketIndex(scene, name)].Command;
	Require(command.Geometry && !command.Hidden && command.GeometryCount != 0, "actual mesh scene must publish visible geometry");

	return {command.Geometry->Positions.begin(), command.Geometry->Positions.end()};
}

static void RequirePublishedLattice(const sgre::scene::MotionSceneEvaluation& scene, const sgre::resource::MotionBezierLattice& lattice, float projection) {
	const auto mesh_index = PacketIndex(scene, "mesh");
	const auto inherited_index = PacketIndex(scene, "inherited");
	const auto mesh = scene.DrawState(*scene.PacketRows()[mesh_index]);
	const auto inherited = scene.DrawState(*scene.PacketRows()[inherited_index]);
	Require(mesh.Source && mesh.Source->State().Binding && mesh.Source->State().Binding->Icon && inherited.Source && inherited.Source->State().Binding && inherited.Source->State().Binding->Icon && mesh.Numeric && inherited.Numeric, "mesh oracle requires actual published source/numeric state");
	const auto space = sgre::scene::MotionSpriteDeformationSpace(mesh.Numeric->Hierarchy, *mesh.Source->State().Binding->Icon, projection);
	const auto world = sgre::scene::PrepareMotionDeformation(lattice, space);
	Require(world.WorldLattice.has_value() == (lattice.EncodedDegree != 0x33), "generalized world must use an explicit discriminator and retain the cubic fast path");
	std::vector<sgre::resource::MotionPoint> expected(sgre::scene::MotionMeshGridVertexCount(mesh.Subdivision));
	sgre::scene::EvaluateMotionDeformationGrid(world, mesh.Subdivision, {}, expected);
	RequirePoints(Positions(scene, "mesh"), expected, "actual own mesh grid differs from its current UID lattice");
	const auto corners = sgre::scene::EvaluateMotionSpriteQuad(inherited.Numeric->Hierarchy, *inherited.Source->State().Binding->Icon, projection);
	expected.resize(sgre::scene::MotionMeshGridVertexCount(inherited.Subdivision));
	sgre::scene::EvaluateMotionBilinearGrid(corners, inherited.Subdivision, expected);
	sgre::scene::ApplyMotionDeformation(world, expected);
	RequirePoints(Positions(scene, "inherited"), expected, "actual inherited point path differs from the current parent lattice");
}

static void TestPendingWriteAndCompletedFrame() {
	for (const auto degree : {std::uint8_t{0x11}, std::uint8_t{0x12}, std::uint8_t{0x21}, std::uint8_t{0x22}, std::uint8_t{0x13}, std::uint8_t{0x31}, std::uint8_t{0x23}, std::uint8_t{0x32}, std::uint8_t{0x33}, std::uint8_t{0x34}, std::uint8_t{0x43}, std::uint8_t{0x44}, std::uint8_t{0x35}, std::uint8_t{0x53}, std::uint8_t{0x36}, std::uint8_t{0x63}}) {
		Fixture fixture;
		auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
		});
		task.SetVisible(true);
		task.Play("main");
		auto& group = fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second);
		group.Update();
		const auto* scene = task.GenericPlayer()->Scene();
		Require(scene && scene->Packets().Nodes().size() == 2 && scene->GlobalRows().size() == 2, "actual task must complete both real mesh and inherited source/geometry/row owners");
		const auto& matches = task.Commands().LayerUids().Matches("mesh-uid");
		Require(matches.size() == 1, "mesh write requires one genuine registered UID match");
		const auto reference = matches.front().Layer;
		const auto old_mesh = Positions(*scene, "mesh");
		const auto old_inherited = Positions(*scene, "inherited");
		group.Draw(0);
		Require(fixture.Runtime->SceneContext().Size() == 1, "preceding completed scene must remain genuinely queued");
		auto lattice = sgre::scene::IdentityMotionBezierLattice(degree);
		for (std::size_t index = 0; index != lattice.Controls.size(); ++index) {
			lattice.Controls[index].X += static_cast<float>(index % 3) * 0.125f + 0.5f;
			lattice.Controls[index].Y -= static_cast<float>(index % 4) * 0.0625f;
		}
		Require(reference.SetMesh(1, &lattice, false), "actual UID mode1 must install its pending controls");
		RequirePoints(Positions(*scene, "mesh"), old_mesh, "pending physics write invalidated or changed the preceding completed own geometry");
		RequirePoints(Positions(*scene, "inherited"), old_inherited, "pending physics write invalidated or changed the preceding completed descendant geometry");
		Require(!reference.SetMesh(1, &lattice, true), "native equivalent comparison must avoid another write");
		fixture.Runtime->SceneContext().Clear();
		// A real held numeric evaluation still consumes the pending controls, rather than decoding over the override latch.
		task.SetTickCount(task.Commands().ClockState().TickCount);
		task.Progress(0);
		RequirePublishedLattice(*scene, lattice, fixture.Runtime->DepthProjection());
		const auto held = Positions(*scene, "mesh");
		task.Progress(1);
		RequirePublishedLattice(*scene, lattice, fixture.Runtime->DepthProjection());
		RequirePoints(Positions(*scene, "mesh"), held, "numeric tween changes must not replace latched physical controls");
		Require(reference.SetMesh(1, nullptr, false), "null must perform a genuine latched clear");
		RequirePoints(Positions(*scene, "mesh"), held, "pending null write must leave preceding geometry intact");
		task.Progress(0);
		const auto plain = Positions(*scene, "mesh");
		Require(plain.size() == 4 && !reference.SetMesh(1, nullptr, true), "completed null clear must use ordinary rectangle geometry and native no-change comparison");
		task.Progress(1);
		RequirePoints(Positions(*scene, "mesh"), plain, "persistent clear latch must not be replaced by later serialized mesh frames");
		fixture.Tasks.Clear();
		Require(fixture.Runtime->Identifiers().LiveCount() == 0 && !task.IsLive(), "actual retirement must return mesh and UID registrations");
	}
}

struct AddedMeshWriter final : sgre::scene::MotionLayerUidObserver {
	sgre::resource::MotionBezierLattice Lattice = sgre::scene::IdentityMotionBezierLattice(0x34);
	std::exception_ptr Failure;
	std::size_t Added = 0;
	std::size_t RejectedRetirementWrites = 0;
	bool Retiring = false;

	AddedMeshWriter() {
		Lattice.Controls[6].X += 0.5f;
	}

	void LayerAdded(std::string_view, const sgre::scene::MotionLayerUidMatch& match) noexcept override {
		try {
			Require(match.Layer.Node().NativeOrdinal == 1 && !match.Layer.Node().DrawRow && !match.Layer.Node(2).DrawRow, "actual Added callback must precede current and later native row assignment");
			Require(match.Layer.SetMesh(1, &Lattice, false), "genuine per-node Added callback must install its physical grid before publication");
			++Added;
		} catch (...) {
			Failure = std::current_exception();
		}
	}

	void LayerRemoved(std::string_view, const sgre::scene::MotionLayerUidMatch& match) noexcept override {
		if (!Retiring) {
			return;
		}
		try {
			RequireFailure([&] {
				static_cast<void>(match.Layer.SetMesh(1, &Lattice, false));
			},
			    "constructed exclusive mesh owner");
			++RejectedRetirementWrites;
		} catch (...) {
			Failure = std::current_exception();
		}
	}

	void RequireSuccess() const {
		if (Failure) {
			std::rethrow_exception(Failure);
		}
	}
};

static void TestGenuineAddedCallbackWrite() {
	Fixture fixture;
	AddedMeshWriter observer;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	auto& uids = task.Commands().LayerUids();
	uids.Subscribe("mesh-uid", observer);
	try {
		task.SetVisible(true);
		task.Play("main");
		observer.RequireSuccess();
		Require(observer.Added == 1, "actual initial selection must notify its registered mesh once");
		auto& group = fixture.Tasks.Group(sgre::scene::MotionTaskGroup::Second);
		group.Update();
		RequirePublishedLattice(*task.GenericPlayer()->Scene(), observer.Lattice, fixture.Runtime->DepthProjection());
		observer.Retiring = true;
		task.Play("main", sgre::scene::MotionPlayFlags::Force);
		observer.RequireSuccess();
		Require(observer.Added == 2 && observer.RejectedRetirementWrites == 1, "real forced replacement must reject old retirement writes and admit only the new Added phase");
		group.Update();
		RequirePublishedLattice(*task.GenericPlayer()->Scene(), observer.Lattice, fixture.Runtime->DepthProjection());
		observer.Retiring = false;
		uids.Unsubscribe("mesh-uid", observer);
	} catch (...) {
		observer.Retiring = false;
		uids.Unsubscribe("mesh-uid", observer);
		throw;
	}
	fixture.Tasks.Clear();
	Require(!task.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "Added callback fixture must retire the actual task after removing its borrowed observer");
}

static void TestUidModeAndInputGuards() {
	Fixture fixture;
	sgre::scene::MotionPlayerCommands commands(fixture.Manager);
	commands.SetCharacter("character");
	commands.Play("main");
	const auto& timeline = *commands.CachedTimeline();
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *fixture.Asset->Model);
	sgre::scene::MotionRootScene root;
	const sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, fixture.Runtime->Identifiers(), {}, {});
	const auto reference = commands.LayerUids().Matches("mesh-uid").front().Layer;
	const auto ordinary = commands.LayerUids().Matches("ordinary-uid").front().Layer;
	auto malformed = sgre::scene::IdentityMotionBezierLattice(0x34);
	malformed.Controls.pop_back();
	Require(!reference.SetMesh(0, &malformed, false) && !reference.SetMesh(2, &malformed, false) && !ordinary.SetMesh(1, &malformed, false), "unsupported modes/nonmesh nodes must no-op before touching lattice storage");
	RequireFailure([&] {
		static_cast<void>(reference.SetMesh(1, &malformed, false));
	},
	    "encoded axis");
	auto lattice = sgre::scene::IdentityMotionBezierLattice(0x34);
	Require(!reference.SetMesh(1, &lattice, true) && !selected.MeshLocal(0).OverrideLatched(), "identity comparison against an empty owner must leave the latch untouched");
	lattice.Controls[7].X += 0.5f;
	Require(reference.SetMesh(1, &lattice, false) && selected.MeshLocal(0).PendingWrite() && selected.MeshLocal(0).OverrideLatched(), "real selected constructor grid accepts a pending write before its first numeric phase");
	RequirePoints(selected.MeshLocal(0).CurrentLattice().Controls, lattice.Controls, "actual pending owner must retain all twenty controls");
	const auto previous = lattice;
	lattice.Controls[0].X = std::numeric_limits<float>::infinity();
	RequireFailure([&] {
		static_cast<void>(reference.SetMesh(1, &lattice, false));
	},
	    "finite");
	RequirePoints(selected.MeshLocal(0).CurrentLattice().Controls, previous.Controls, "rejected nonfinite input must leave the actual owner unchanged");
	const auto unsupported = sgre::scene::IdentityMotionBezierLattice(0x37);
	RequireFailure([&] {
		static_cast<void>(reference.SetMesh(1, &unsupported, false));
	},
	    "degree1..6 geometry");
	selected.Retire();
	RequireFailure([&] {
		static_cast<void>(reference.SetMesh(1, nullptr, false));
	},
	    "actual selected tree");
}

int main() {
	try {
		TestPendingWriteAndCompletedFrame();
		TestUidModeAndInputGuards();
		TestGenuineAddedCallbackWrite();
		std::cout << "Mesh UID degree1..6 pending writes preserve completed frames, latch across real updates, publish own/inherited grids and reject malformed owners.\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
