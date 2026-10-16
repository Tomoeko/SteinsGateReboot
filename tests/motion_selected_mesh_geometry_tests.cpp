#include "sgre/scene/motion_selected_mesh_geometry.hpp"

#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_manager_runtime.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"
#include "sgre/scene/motion_selected_layer_sources.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

static void Require(bool condition, std::string_view text) {
	if (!condition) {
		throw std::runtime_error(std::string(text));
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

	throw std::runtime_error("foreign or expired selected mesh owner was accepted");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

using PatchControls = std::array<std::array<std::array<float, 32>, 2>, 3>;

static sgre::resource::PsbValue Layer(std::uint32_t case_index, std::size_t layer, const PatchControls& controls) {
	sgre::resource::PsbArray children;
	if (layer < 2) {
		children.push_back(Layer(case_index, layer + 1, controls));
	}
	sgre::resource::PsbArray frames;
	for (std::size_t index = 0; index < 5; ++index) {
		const auto time = static_cast<double>(index * 2);
		const bool hidden = index == 4 || (index == 2 && layer == 1);
		sgre::resource::PsbValue patch;
		if (index != 2 || layer != 0) {
			sgre::resource::PsbArray points;
			for (const auto value : controls[layer][index % 2]) {
				points.push_back(Atom(static_cast<double>(value)));
			}
			patch = Atom(std::move(points));
		}
		const auto mesh = Atom(sgre::resource::PsbObject{{"cc", Atom(std::monostate{})}, {"bp", patch}});
		const auto coordinate = Atom(sgre::resource::PsbArray{Atom(static_cast<double>(layer) + 3 + time), Atom(static_cast<double>(layer) + 5), Atom(static_cast<double>(layer) / 2)});
		const auto opacity = index == 1 && layer == 0 ? std::int64_t{0} : std::int64_t{255};
		const auto content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x02000402})}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}, {"coord", coordinate}, {"opa", Atom(opacity)}, {"mesh", mesh}});
		frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(time)}, {"type", Atom(hidden ? std::int64_t{0} : std::int64_t{2})}, {"content", content}}));
	}
	const auto inherit = std::int64_t{4} | (layer == 2 && (case_index & 2) != 0 ? std::int64_t{0} : std::int64_t{0x02000000});
	const auto order = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})});

	return Atom(sgre::resource::PsbObject{{"label", Atom("n" + std::to_string(layer + 1))}, {"type", Atom(std::int64_t{0})}, {"inheritMask", Atom(inherit)}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", order}, {"children", Atom(std::move(children))}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(std::int64_t{1})}, {"meshSyncChildMask", Atom(layer == 0 ? std::int64_t{8} : std::int64_t{0})}, {"meshDivision", Atom(static_cast<std::int64_t>(6 + layer))}, {"meshCombine", Atom(layer > 0 && (case_index & 1) != 0)}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}});
}

static std::shared_ptr<sgre::resource::PsbDocument> Document(std::uint32_t case_index, const PatchControls& controls) {
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{32})}, {"height", Atom(std::int64_t{32})}, {"truncated_width", Atom(std::int64_t{32})}, {"truncated_height", Atom(std::int64_t{32})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	const auto icon = Atom(sgre::resource::PsbObject{{"left", Atom(8.0)}, {"top", Atom(16.0)}, {"width", Atom(16.0)}, {"height", Atom(8.0)}, {"originX", Atom(3.0)}, {"originY", Atom(5.0)}, {"attr", Atom(std::int64_t{2})}});
	const auto source = Atom(sgre::resource::PsbObject{{"texture", texture}, {"icon", Atom(sgre::resource::PsbObject{{"tile", icon}})}});
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1}), Atom(std::int64_t{2})})}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	const auto bounds = Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(16.0)}, {"bottom", Atom(8.0)}});
	const auto timeline = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", bounds}, {"lastTime", Atom(8.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(sgre::resource::PsbArray{Layer(case_index, 0, controls)})}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
	const auto character = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"main", timeline}})}});
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 1152;
	document->Chunks = {{128, 1024}};
	document->Root = Atom(sgre::resource::PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Selected mesh fixture"))}, {"easing", Atom(sgre::resource::PsbArray{})}, {"source", Atom(sgre::resource::PsbObject{{"atlas", source}})}, {"object", Atom(sgre::resource::PsbObject{{"character", character}})}, {"sourceIconRenameMap", Atom(sgre::resource::PsbObject{})}});
	document->Root.SerializedOffset = 44;

	return document;
}

static std::uint32_t Word(std::istream& stream) {
	std::uint32_t word = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto value = stream.get();
		Require(value != std::char_traits<char>::eof(), "short selected mesh fixture");
		word |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(value)) << shift;
	}

	return word;
}

static void Replay(const char* path) {
	std::ifstream stream(path, std::ios::binary);
	Require(stream.is_open() && Word(stream) == 0x3147534d, "wrong selected mesh fixture");
	const auto cases = Word(stream);
	const auto steps = Word(stream);
	Require(cases == 4 && steps == 8, "selected mesh fixture exceeds its bounded scope");
	std::size_t position_words = 0;
	for (std::size_t case_index = 0; case_index < cases; ++case_index) {
		Require(Word(stream) == case_index, "wrong selected mesh case order");
		PatchControls controls;
		for (auto& layer : controls) {
			for (auto& patch : layer) {
				for (auto& point : patch) {
					point = std::bit_cast<float>(Word(stream));
				}
			}
		}
		std::size_t backend_owner = 0;
		sgre::scene::TextureCache cache([&](auto, const auto&) {
			sgre::render::SpriteTextureHandle handle;
			handle.Owner = &backend_owner;
			handle.Generation = 1;

			return handle;
		},
		    [](auto) {
		    });
		sgre::scene::MotionAssetCatalog catalog(cache);
		const auto asset = catalog.Prepare(Document(static_cast<std::uint32_t>(case_index), controls), std::make_shared<const std::vector<std::uint8_t>>(1152, 128));
		sgre::scene::MotionResourceManager manager(cache);
		const auto project = manager.Register(asset->Identity, asset->Model, *asset->Backing);
		sgre::core::FrameClock clock;
		const std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
		sgre::scene::MotionManagerRuntimeServices manager_services;
		manager_services.ReadSourceAliases = [&](auto requested) {
			Require(requested == project, "wrong actual registered alias owner");

			return std::span<const sgre::scene::MotionSourceAlias>(asset->Aliases);
		};
		sgre::scene::MotionManagerRuntime runtime(manager, clock, seed, manager_services);
		runtime.SetDepthProjection(0.25f);
		sgre::scene::MotionPlayerCommands commands(manager);
		Require(commands.MeshDivisionRatio() == 1, "actual ordinary player mesh ratio must initialize to native one");
		commands.SetCharacter("character");
		commands.Play("main");
		const auto& timeline = *commands.CachedTimeline();
		const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *asset->Model);
		sgre::scene::MotionRootScene root;
		root.SetVisible(true);
		const sgre::scene::MotionPlayerRenderControls render;
		sgre::scene::MotionLayerIdentifierPool identifiers;
		sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, identifiers, {}, {});
		sgre::scene::MotionSelectedLayerSources sources(selected, catalog, [&](auto requested) -> const sgre::scene::LoadedMotionAsset& {
			Require(requested == project, "wrong requesting selected mesh asset");

			return *asset;
		});
		auto local_owner = std::make_unique<sgre::scene::MotionLocalFrames>(selected, *asset->Model, sources.FrameServices([](auto, auto) {
			throw std::runtime_error("action-free selected mesh fixture queued an action");
		}));
		auto& local = *local_owner;
		sgre::scene::MotionLayerComposition composition(selected, root, false);
		sgre::scene::MotionSelectedMeshGeometry geometry(selected, composition, sources, runtime);
		RequireFailure([&] {
			sgre::scene::MotionSelectedMeshGeometry duplicate(selected, composition, sources, runtime);
		},
		    "exact live selected numeric/source/manager");
		RequireFailure([&] {
			geometry.Compose();
		},
		    "current completed actual local/numeric");
		for (std::size_t step = 0; step < steps; ++step) {
			const auto position = std::bit_cast<float>(Word(stream));
			const auto traversal = Word(stream);
			const auto ratio = std::bit_cast<float>(Word(stream));
			const bool visible = Word(stream) != 0;
			commands.SetMeshDivisionRatio(ratio);
			root.SetVisible(visible);
			local.Select(static_cast<sgre::scene::MotionFrameTraversal>(traversal), position);
			if (step > 0) {
				RequireFailure([&] {
					static_cast<void>(geometry.Link(0));
				},
				    "current actual completed geometry");
				RequireFailure([&] {
					geometry.Compose();
				},
				    "current completed actual local/numeric");
			}
			composition.Compose(local, false);
			if (step > 0) {
				RequireFailure([&] {
					static_cast<void>(geometry.Link(0));
				},
				    "current actual completed geometry");
			}
			geometry.Compose();
			for (std::uint32_t layer = 0; layer < 3; ++layer) {
				const auto current = Word(stream);
				const bool active = Word(stream) != 0;
				const bool exported = Word(stream) != 0;
				const bool stopped = Word(stream) != 0;
				const auto ancestor = Word(stream);
				const bool changed = Word(stream) != 0;
				const bool dirty = Word(stream) != 0;
				const bool actual = Word(stream) != 0;
				const bool allocated = Word(stream) != 0;
				const auto& link = geometry.Link(layer);
				Require(composition.Layers()[layer].Hierarchy.UnresolvedInheritMask == (prepared.Layers[layer].InheritMask & 0x02000000u), "geometry ancestry must not erase or waive its actual inherited mesh mask");
				const auto prefix = "case " + std::to_string(case_index) + " step " + std::to_string(step) + " layer " + std::to_string(layer);
				Require(local.State(layer).CurrentIndex == current && link.Active == active && link.ExportToChildren == exported && link.StopsInheritance == stopped && link.Ancestor.value_or(0xffffffffu) == ancestor, prefix + " actual ancestry differs from native");
				Require(composition.Changed(layer) == changed && geometry.Changed(layer) == dirty && composition.Layers()[layer].Active == actual && geometry.Allocated(layer) == allocated, prefix + " actual numeric/geometry dirty or allocation differs from native");
				if (allocated) {
					const auto x = Word(stream);
					const auto y = Word(stream);
					Require(geometry.Subdivision(layer).X == x && geometry.Subdivision(layer).Y == y, prefix + " actual mesh subdivision differs from native");
					const auto positions = geometry.Geometry(layer).Positions;
					Require(positions.size() == static_cast<std::size_t>(x + 1) * (y + 1), "actual mesh grid size differs from native");
					for (const auto& point : positions) {
						for (const auto value : {point.X, point.Y}) {
							const auto expected = Word(stream);
							Require(std::bit_cast<std::uint32_t>(value) == expected, prefix + " actual mesh position " + std::to_string(position_words) + " differs from native: " + std::to_string(std::bit_cast<std::uint32_t>(value)) + " / " + std::to_string(expected));
							++position_words;
						}
					}
				}
			}
			local.CompleteComposition();
		}
		local_owner.reset();
		Require(composition.Layers().empty(), "retired actual local owner must invalidate numeric output without dereferencing it");
		RequireFailure([&] {
			static_cast<void>(geometry.Link(0));
		},
		    "current actual completed geometry");
		selected.Retire();
		RequireFailure([&] {
			static_cast<void>(geometry.Link(0));
		},
		    "exact live selected");
	}
	Require(stream.peek() == std::char_traits<char>::eof(), "selected mesh fixture has trailing bytes");
	std::cout << cases * steps * 3 << " actual selected mesh records and " << position_words << " position words matched\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: motion_selected_mesh_geometry_tests native_fixture");
		Replay(argv[1]);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
