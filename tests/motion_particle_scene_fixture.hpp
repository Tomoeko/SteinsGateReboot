#pragma once

#include "sgre/scene/motion_player_task.hpp"

#include "sgre/scene/motion_particle_layer_state.hpp"
#include "sgre/scene/motion_particle_player_state.hpp"
#include "sgre/scene/motion_timeline_variables.hpp"

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_render_queue.hpp"
#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"
#include "sgre/scene/motion_scene_player.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

static sgre::resource::PsbValue Layer(std::string label, bool child, bool missing_child = false) {
	sgre::resource::PsbArray frames;
	for (std::size_t index = 0; index != 2; ++index) {
		const auto content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x60603})}, {"src", Atom(std::string(child ? "child" : "atlas"))}, {"icon", Atom(std::string(child ? missing_child ? "absent" : "main" : "tile"))}, {"coord", Atom(sgre::resource::PsbArray{Atom(child ? 2.0 : 0.0), Atom(0.0), Atom(0.0)})}, {"ox", Atom(0.0)}, {"oy", Atom(0.0)}, {"opa", Atom(std::int64_t{255})}, {"color", Atom(std::int64_t{0x808080ff})}, {"bm", Atom(std::int64_t{0x10})}, {"act", Atom(std::string("first"))}});
		frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(index == 0 ? 0.0 : 8.0)}, {"type", Atom(index == 0 ? std::int64_t{2} : std::int64_t{0})}, {"content", content}}));
	}
	return Atom(sgre::resource::PsbObject{{"label", Atom(std::move(label))}, {"type", Atom(child ? std::int64_t{3} : std::int64_t{0})}, {"inheritMask", Atom(std::int64_t{4})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(sgre::resource::PsbArray{})}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(std::int64_t{0})}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}});
}

static sgre::resource::PsbValue Timeline(bool child, bool retained_child = false, bool sync_child = false, bool timeline_scalar = false, bool missing_child = false) {
	const auto order = child ? sgre::resource::PsbArray{Atom(std::int64_t{0})} : sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1})};
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(order)}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	auto layers = sgre::resource::PsbArray{Layer(child ? "nested" : "sprite", false)};
	if (!child) {
		layers.push_back(Layer("selected-child", true, missing_child));
	}
	auto result = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(16.0)}, {"bottom", Atom(8.0)}})}, {"lastTime", Atom(8.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(std::move(layers))}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
	if (missing_child && !child) {
		// Native FindLayer uses the serialized zero-based layerIndexMap, independently of labels and UIDs.
		std::get<sgre::resource::PsbObject>(result.Data).emplace_back("layerIndexMap", Atom(sgre::resource::PsbObject{{"selected-child", Atom(std::int64_t{1})}}));
	}

	if (retained_child && !child) {
		for (auto& [name, value] : std::get<sgre::resource::PsbObject>(result.Data)) {
			if (name == "parameter") {
				value = Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"id", Atom(std::string("param"))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(0.0)}, {"rangeEnd", Atom(8.0)}, {"division", Atom(8.0)}, {"enabled", Atom(std::int64_t{1})}})});
			}
		}
	}
	if (timeline_scalar && !child) {
		for (auto& [name, value] : std::get<sgre::resource::PsbObject>(result.Data)) {
			if (name == "parameterize") {
				value = Atom(std::int64_t{0});
			}
		}
	}
	if (sync_child && child) {
		for (auto& [name, value] : std::get<sgre::resource::PsbObject>(result.Data)) {
			if (name == "tag") {
				const auto sync = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbObject{{"align", Atom(false)}, {"sync", Atom(true)}, {"action", Atom(std::string{})}})}});
				const auto end = Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{0})}, {"content", Atom(std::monostate{})}});
				value = Atom(sgre::resource::PsbArray{sync, end});
			}
		}
	}
	return result;
}

static std::shared_ptr<sgre::resource::PsbDocument> Document(bool retained_child = false, bool sync_child = false, bool retained_mesh = false, bool timeline_scalar = false, bool missing_child = false, std::size_t extra_timelines = 0) {
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{4})}, {"height", Atom(std::int64_t{4})}, {"truncated_width", Atom(std::int64_t{4})}, {"truncated_height", Atom(std::int64_t{4})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	auto icon = Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"width", Atom(2.0)}, {"height", Atom(2.0)}, {"originX", Atom(0.0)}, {"originY", Atom(0.0)}, {"attr", Atom(std::int64_t{2})}});
	if (retained_mesh) {
		// The native rectangle branch retains this atom without entering its lazy polygon decoder.
		std::get<sgre::resource::PsbObject>(icon.Data).emplace_back("mesh", Atom(sgre::resource::PsbObject{{"vertices", Atom(chunk)}}));
	}
	const auto source = Atom(sgre::resource::PsbObject{{"texture", texture}, {"icon", Atom(sgre::resource::PsbObject{{"tile", icon}})}});
	const auto object = [retained_child, sync_child, timeline_scalar, missing_child, extra_timelines](bool child) {
		auto motions = sgre::resource::PsbObject{{"main", Timeline(child, retained_child, sync_child, timeline_scalar, missing_child)}};
		if (!child) {
			for (std::size_t index = 0; index != extra_timelines; ++index) {
				motions.emplace_back("variant" + std::to_string(index), Timeline(false, retained_child, sync_child, timeline_scalar, missing_child));
			}
		}
		return Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(std::move(motions))}});
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

struct ParticleSceneFixture {
	std::size_t Uploads = 0;
	std::size_t Releases = 0;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	sgre::scene::TextureCache Cache;
	sgre::scene::MotionAssetCatalog Catalog;
	std::shared_ptr<const sgre::scene::LoadedMotionAsset> Asset;
	std::vector<std::shared_ptr<const sgre::scene::LoadedMotionAsset>> AdditionalAssets;
	sgre::scene::MotionResourceManager Manager;
	sgre::scene::MotionProjectId Project;
	sgre::core::FrameClock Clock;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	sgre::scene::MotionLayerTasks Tasks;
	std::vector<std::pair<std::string, std::string>> Events;

	explicit ParticleSceneFixture(bool retained_child = false, bool sync_child = false, bool retained_mesh = false, bool timeline_scalar = false, std::size_t draw_capacity = 32, bool missing_child = false, std::size_t extra_timelines = 0, std::shared_ptr<const sgre::resource::PsbDocument> document = {}, std::uint32_t seed_bias = 0)
	    : Cache([this](auto, const auto&) {
		      return sgre::render::SpriteTextureHandle{static_cast<std::uint32_t>(++Uploads), 1, &Uploads};
	      },
	          [this](auto) {
		          ++Releases;
	          }),
	      Catalog(Cache), Manager(Cache), Clock(0), Tasks(8) {
		for (std::size_t index = 0; index != Seed.size(); ++index) {
			Seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17 + seed_bias;
		}
		Asset = Catalog.Prepare(document ? std::move(document) : Document(retained_child, sync_child, retained_mesh, timeline_scalar, missing_child, extra_timelines), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		Project = Manager.Register(Asset->Identity, Asset->Model, *Asset->Backing);
		Runtime = std::make_unique<sgre::scene::MotionManagerRuntime>(Manager, Clock, Seed, Services(), draw_capacity);
	}

	~ParticleSceneFixture() {
		if (Runtime) {
			Runtime->SceneContext().Clear();
		}
		Tasks.Clear();
	}

	sgre::scene::MotionManagerRuntimeServices Services() {
		sgre::scene::MotionManagerRuntimeServices services;
		services.ReadSourceAliases = [this](auto project) {
			if (project == Project) {
				return std::span<const sgre::scene::MotionSourceAlias>(Asset->Aliases);
			}
			for (const auto& asset : AdditionalAssets) {
				if (asset->Identity == Manager.Identity(project)) {
					return std::span<const sgre::scene::MotionSourceAlias>(asset->Aliases);
				}
			}
			throw std::runtime_error("scene aliases borrowed a different actual registration");
		};
		services.SceneAssets = &Catalog;
		services.ReadRegisteredAsset = [this](auto project) -> const sgre::scene::LoadedMotionAsset& {
			if (project == Project) {
				return *Asset;
			}
			for (const auto& asset : AdditionalAssets) {
				if (asset->Identity == Manager.Identity(project)) {
					return *asset;
				}
			}
			throw std::runtime_error("scene asset borrowed a different actual registration");
		};
		return services;
	}

	sgre::scene::MotionProjectId RegisterDocument(std::shared_ptr<const sgre::resource::PsbDocument> document) {
		auto asset = Catalog.Prepare(std::move(document), std::make_shared<const std::vector<std::uint8_t>>(144, 255));
		const auto project = Manager.Register(asset->Identity, asset->Model, *asset->Backing);
		AdditionalAssets.push_back(std::move(asset));
		return project;
	}

	sgre::scene::MotionPlayerTask Attach() {
		return sgre::scene::MotionPlayerTask::Attach(*Runtime, Tasks, false, "character", [this](const auto& event) {
			Require(event.Kind == sgre::scene::MotionPlayerEventKind::Action, "fixture has only authored real layer actions");
			Events.emplace_back(event.Layer, event.Action);
		});
	}
};

static sgre::scene::MotionPreparedFrame ParticleSceneGlobals(std::span<const sgre::resource::MotionPoint> sizes) {
	sgre::scene::MotionPreparedFrame globals;
	globals.TextureSizes = sizes;
	globals.InitialScissor = {0, 0, 16, 8};
	globals.AppliedRoundedScissor = globals.InitialScissor;
	globals.TargetBounds = globals.InitialScissor;
	globals.Viewport = {16, 8};
	globals.AxisScale = {1, 1};
	return globals;
}
