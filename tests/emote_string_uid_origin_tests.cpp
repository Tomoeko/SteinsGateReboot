#include "sgre/scene/motion_bezier_lattice.hpp"

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_draw.hpp"
#include "sgre/scene/motion_grid_draw.hpp"
#include "sgre/scene/motion_layer_composition.hpp"
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

static sgre::resource::PsbValue Layer(std::size_t depth) {
	const bool mesh = depth < 2;
	sgre::resource::PsbArray frames;
	for (std::size_t index = 0; index != 4; ++index) {
		auto content = sgre::resource::PsbObject{{"mask", Atom(mesh ? std::int64_t{0x02020613} : std::int64_t{0x20603})}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}, {"coord", Atom(sgre::resource::PsbArray{Atom(mesh ? 0.0 : 2.0), Atom(0.0), Atom(0.0)})}, {"ox", Atom(0.0)}, {"oy", Atom(0.0)}, {"opa", Atom(std::int64_t{255})}, {"color", Atom(std::int64_t{0x808080ff})}, {"bm", Atom(std::int64_t{0x10})}};
		if (mesh) {
			content.emplace_back("angle", Atom(depth == 0 ? 90.0 : 0.0));
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
	const auto children = depth < 2 ? sgre::resource::PsbArray{Layer(depth + 1)} : sgre::resource::PsbArray{};

	return Atom(sgre::resource::PsbObject{{"label", Atom(std::string(depth == 0 ? "mesh" : depth == 1 ? "boundary"
	                                                                                                  : "inherited"))},
	    {"uid", Atom(std::string(depth == 0 ? "mesh-uid" : depth == 1 ? "boundary-uid"
	                                                                  : "ordinary-uid"))},
	    {"type", Atom(std::int64_t{0})}, {"inheritMask", Atom(depth == 1 ? std::int64_t{4} : std::int64_t{0x02000004})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(children)}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(mesh ? std::int64_t{1} : std::int64_t{0})}, {"meshCombine", Atom(false)}, {"meshSyncChildMask", Atom(depth == 0 ? std::int64_t{8} : std::int64_t{0})}, {"meshDivision", Atom(std::int64_t{4})}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}});
}

static std::shared_ptr<sgre::resource::PsbDocument> Document() {
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1}), Atom(std::int64_t{2})})}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(12.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	const auto timeline = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(16.0)}, {"bottom", Atom(8.0)}})}, {"lastTime", Atom(12.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(sgre::resource::PsbArray{Layer(0)})}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
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

	Fixture() : Cache([this](auto, const auto&) {
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

static void RequireNear(float actual, float expected, std::string_view message) {
	if (!std::isfinite(actual) || std::abs(actual - expected) > 0.00002f) {
		throw std::runtime_error(std::string(message) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
	}
}

struct OriginInstaller final : sgre::scene::MotionLayerUidObserver {
	sgre::resource::MotionPoint Pixel{3, -2};
	sgre::resource::MotionPoint Physical{0.3f, -0.2f};
	sgre::resource::MotionBezierLattice Lattice = sgre::scene::IdentityMotionBezierLattice(0x33);
	std::exception_ptr Failure;
	std::size_t MeshAdded = 0;
	std::size_t BoundaryAdded = 0;

	OriginInstaller() {
		for (std::size_t index = 0; index < Lattice.Controls.size(); ++index) {
			const auto column = index % 4;
			Lattice.Controls[index].X = column < 2 ? 0.0f : column == 2 ? 1.0f / 3.0f
			                                                            : 1.0f;
		}
	}

	void LayerAdded(std::string_view uid, const sgre::scene::MotionLayerUidMatch& match) noexcept override {
		try {
			if (uid == "mesh-uid") {
				Require(match.Layer.SetMesh(1, &Lattice, false), "actual ancestor Added must install its nonaffine mesh before composition");
				++MeshAdded;
			} else {
				Require(uid == "boundary-uid", "origin listener must receive its exact registered UID");
				match.Layer.InitializeMeshOrigin(Pixel, Physical);
				++BoundaryAdded;
			}
		} catch (...) {
			Failure = std::current_exception();
		}
	}

	void LayerRemoved(std::string_view, const sgre::scene::MotionLayerUidMatch&) noexcept override {
	}

	void RequireSuccess() const {
		if (Failure) {
			std::rethrow_exception(Failure);
		}
	}
};

static sgre::scene::MotionSceneDrawState DrawState(const sgre::scene::MotionSceneEvaluation& scene, std::string_view label) {
	for (const auto* row : scene.PacketRows()) {
		if (row->Node().Raw->Label == label) {
			return scene.DrawState(*row);
		}
	}

	throw std::runtime_error("origin fixture requires its genuine current selected row");
}

static sgre::resource::MotionPoint Anchor(const sgre::scene::MotionComposedLayer& numeric, const sgre::resource::MotionPoint& pixel_origin) {
	const auto& transform = numeric.Hierarchy.Transform;
	return {(pixel_origin.Y * transform.XY + pixel_origin.X * transform.XX) + numeric.Hierarchy.Attributes.Coordinate[0], (pixel_origin.Y * transform.YY + pixel_origin.X * transform.YX) + numeric.Hierarchy.Attributes.Coordinate[1]};
}

static void TestActualAddedOriginAndBoundaryDisplacement() {
	Fixture fixture;
	OriginInstaller observer;
	auto task = sgre::scene::MotionPlayerTask::Attach(*fixture.Runtime, fixture.Tasks, false, "character", [](const auto&) {
	});
	auto& uids = task.Commands().LayerUids();
	uids.Subscribe("mesh-uid", observer);
	uids.Subscribe("boundary-uid", observer);
	try {
		task.SetCoordinate({10, 20});
		task.SetVisible(true);
		task.Play("main");
		observer.RequireSuccess();
		Require(observer.MeshAdded == 1 && observer.BoundaryAdded == 1, "real selected construction must install both actual UID callback owners");
		task.Progress(0);
		const auto* scene = task.GenericPlayer()->Scene();
		Require(scene != nullptr && scene->PacketRows().size() == 3, "normal task Progress must publish all three real layer owners");
		const auto root = DrawState(*scene, "mesh");
		const auto boundary = DrawState(*scene, "boundary");
		const auto leaf = DrawState(*scene, "inherited");
		Require(root.Numeric && root.Source && root.Source->State().Binding && root.Source->State().Binding->Icon && boundary.Numeric && leaf.Numeric, "boundary oracle requires exact completed numeric and source owners");
		const auto world = sgre::scene::PrepareMotionDeformation(observer.Lattice, sgre::scene::MotionSpriteDeformationSpace(root.Numeric->Hierarchy, *root.Source->State().Binding->Icon, fixture.Runtime->DepthProjection()));
		const auto original = Anchor(*boundary.Numeric, observer.Pixel);
		auto transformed = original;
		sgre::scene::ApplyMotionDeformation(world, std::span(&transformed, 1));
		const auto unshifted = Anchor(*boundary.Numeric, {});
		auto wrong = unshifted;
		sgre::scene::ApplyMotionDeformation(world, std::span(&wrong, 1));
		Require(std::abs((transformed.X - original.X) - (wrong.X - unshifted.X)) + std::abs((transformed.Y - original.Y) - (wrong.Y - unshifted.Y)) > 0.25f, "nonaffine ancestor and rotated noncentral origin must discriminate the native boundary anchor");
		std::array<sgre::resource::MotionPoint, 1> actual{};
		const auto reference = uids.Matches("ordinary-uid").front().Layer;
		reference.TransformPoints(actual);
		RequireNear(actual[0].X, leaf.Numeric->Hierarchy.Attributes.Coordinate[0] + (transformed.X - original.X), "stopped ancestor adds actual shifted-anchor displacement to leaf worldX");
		RequireNear(actual[0].Y, leaf.Numeric->Hierarchy.Attributes.Coordinate[1] + (transformed.Y - original.Y), "stopped ancestor adds actual shifted-anchor displacement to leaf worldY");
		const auto completed = actual[0];
		// Native51f0f0 does not mark node40; an unchanged completed geometry retains its cached boundary.
		uids.Matches("boundary-uid").front().Layer.InitializeMeshOrigin({9, 11}, {0.9f, 1.1f});
		task.Progress(0);
		actual[0] = {};
		reference.TransformPoints(actual);
		RequireNear(actual[0].X, completed.X, "origin initialization alone must not force an ordinary numeric or boundary rebuild");
		RequireNear(actual[0].Y, completed.Y, "unchanged boundary preserves the native cached displacement");
		uids.Unsubscribe("boundary-uid", observer);
		uids.Unsubscribe("mesh-uid", observer);
	} catch (...) {
		uids.Unsubscribe("boundary-uid", observer);
		uids.Unsubscribe("mesh-uid", observer);
		throw;
	}
	fixture.Tasks.Clear();
	Require(!task.IsLive() && fixture.Runtime->Identifiers().LiveCount() == 0, "actual UID origin borrows retire with the real task");
}

static void TestRealOriginOwnerStateAndGuards() {
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
	auto& mesh = selected.MeshLocal(0);
	Require(!mesh.OverrideLatched() && !mesh.PendingWrite() && mesh.CurrentLattice().EncodedDegree == 0, "actual selected constructor starts with native zero origin/lattice owner state");
	reference.InitializeMeshOrigin({5, -7}, {0.5f, -0.7f});
	Require(mesh.OverrideLatched() && !mesh.PendingWrite() && mesh.CurrentLattice().EncodedDegree == 0, "origin initialization sets only the override latch, without publishing a fabricated patch or dirty mesh");
	RequireNear(mesh.PixelOrigin().X, 5, "actual owner retains first pixel origin lane");
	RequireNear(mesh.PixelOrigin().Y, -7, "actual owner retains second pixel origin lane");
	RequireNear(mesh.PhysicalOrigin().X, 0.5f, "actual owner separately retains the physical origin pair");
	RequireNear(mesh.PhysicalOrigin().Y, -0.7f, "physical origin does not acquire another ppm conversion");
	RequireFailure([&] {
		reference.InitializeMeshOrigin({std::numeric_limits<float>::infinity(), 0}, {});
	},
	    "finite actual input");
	RequireNear(mesh.PixelOrigin().X, 5, "failed validation must precede every actual owner mutation");
	RequireFailure([&] {
		commands.LayerUids().Matches("ordinary-uid").front().Layer.InitializeMeshOrigin({}, {});
	},
	    "mode1 mesh owner");
}

int main() {
	try {
		TestActualAddedOriginAndBoundaryDisplacement();
		TestRealOriginOwnerStateAndGuards();
		std::cout << "Actual UID mesh origins: per-node installation, retained pairs/latch, shifted native boundary, held geometry and retirement passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
