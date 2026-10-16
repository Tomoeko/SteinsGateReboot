#include "sgre/scene/motion_selected_mesh_vertices.hpp"

#include "sgre/scene/motion_child_frame_selection.hpp"
#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_manager_runtime.hpp"
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

using PatchControls = std::array<float, 32>;

static sgre::resource::PsbValue Layer(std::uint32_t case_index, std::size_t layer, const PatchControls& controls) {
	sgre::resource::PsbArray children;
	if (layer < 3) {
		children.push_back(Layer(case_index, layer + 1, controls));
	}
	sgre::resource::PsbArray frames;
	for (std::size_t index = 0; index < 4; ++index) {
		sgre::resource::PsbObject content;
		content.emplace_back("mask", Atom(layer == 0 ? std::int64_t{0x02000402} : std::int64_t{0x402}));
		content.emplace_back("src", Atom(std::string(layer == 3 ? "child" : "atlas")));
		content.emplace_back("icon", Atom(std::string(layer == 3 ? "main" : "tile")));
		content.emplace_back("coord", Atom(sgre::resource::PsbArray{Atom(static_cast<double>(index * 2 + layer + 3)), Atom(static_cast<double>(layer + 5)), Atom(static_cast<double>(layer) / 2)}));
		content.emplace_back("opa", Atom(std::int64_t{255}));
		if (layer == 0) {
			sgre::resource::PsbArray points;
			for (const auto value : controls) {
				points.push_back(Atom(static_cast<double>(value)));
			}
			content.emplace_back("mesh", Atom(sgre::resource::PsbObject{{"cc", Atom(std::monostate{})}, {"bp", Atom(std::move(points))}}));
		}
		frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(static_cast<double>(index * 2))}, {"type", Atom(index == 3 ? std::int64_t{0} : std::int64_t{2})}, {"content", Atom(std::move(content))}}));
	}
	const std::array<std::int64_t, 4> kinds = {0, 2, 12, 3};
	const auto inherit = std::int64_t{4} | ((layer == 3 && (case_index & 1) != 0) || (layer == 2 && case_index == 2) ? std::int64_t{0} : std::int64_t{0x02000000});
	const auto order = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})});
	sgre::resource::PsbObject result = {{"label", Atom("n" + std::to_string(layer + 1))}, {"type", Atom(kinds[layer])}, {"inheritMask", Atom(inherit)}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", order}, {"children", Atom(std::move(children))}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(layer == 0 ? std::int64_t{1} : std::int64_t{0})}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}};
	if (layer == 0) {
		result.emplace_back("meshSyncChildMask", Atom(std::int64_t{8}));
		result.emplace_back("meshDivision", Atom(std::int64_t{6}));
		result.emplace_back("meshCombine", Atom(false));
	}
	if (layer == 2) {
		result.emplace_back("stencilCompositeMaskLayerList", Atom(sgre::resource::PsbArray{}));
	}

	return Atom(std::move(result));
}

static std::shared_ptr<sgre::resource::PsbDocument> Document(std::uint32_t case_index, const PatchControls& controls) {
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{32})}, {"height", Atom(std::int64_t{32})}, {"truncated_width", Atom(std::int64_t{32})}, {"truncated_height", Atom(std::int64_t{32})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	const auto icon = Atom(sgre::resource::PsbObject{{"left", Atom(8.0)}, {"top", Atom(16.0)}, {"width", Atom(16.0)}, {"height", Atom(8.0)}, {"originX", Atom(3.0)}, {"originY", Atom(5.0)}, {"attr", Atom(std::int64_t{2})}});
	const auto source = Atom(sgre::resource::PsbObject{{"texture", texture}, {"icon", Atom(sgre::resource::PsbObject{{"tile", icon}})}});
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1}), Atom(std::int64_t{2}), Atom(std::int64_t{3})})}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(6.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	const auto bounds = Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(16.0)}, {"bottom", Atom(8.0)}});
	const auto timeline = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", bounds}, {"lastTime", Atom(6.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(sgre::resource::PsbArray{Layer(case_index, 0, controls)})}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
	const auto child_priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbArray{})}});
	const auto child_timeline = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", bounds}, {"lastTime", Atom(6.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(sgre::resource::PsbArray{})}, {"priority", Atom(sgre::resource::PsbArray{child_priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
	const auto character = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"main", timeline}})}});
	const auto child = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(sgre::resource::PsbObject{{"main", child_timeline}})}});
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 1152;
	document->Chunks = {{128, 1024}};
	document->Root = Atom(sgre::resource::PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Mixed ancestry fixture"))}, {"easing", Atom(sgre::resource::PsbArray{})}, {"source", Atom(sgre::resource::PsbObject{{"atlas", source}})}, {"object", Atom(sgre::resource::PsbObject{{"character", character}, {"child", child}})}, {"sourceIconRenameMap", Atom(sgre::resource::PsbObject{})}});
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
	Require(stream.is_open() && Word(stream) == 0x3156494d && Word(stream) == 4 && Word(stream) == 1, "wrong bounded inherited vertex fixture");
	std::size_t position_words = 0;
	for (std::uint32_t case_index = 0; case_index < 4; ++case_index) {
		Require(Word(stream) == case_index, "wrong actual inherited vertex case order");
		PatchControls controls{};
		for (auto& point : controls) {
			point = std::bit_cast<float>(Word(stream));
		}
		const auto ratio = std::bit_cast<float>(Word(stream));
		const auto transfer = Word(stream);
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
		const auto asset = catalog.Prepare(Document(case_index, controls), std::make_shared<const std::vector<std::uint8_t>>(1152, 128));
		sgre::scene::MotionResourceManager manager(cache);
		const auto project = manager.Register(asset->Identity, asset->Model, *asset->Backing);
		sgre::core::FrameClock clock;
		const std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
		sgre::scene::MotionManagerRuntimeServices services;
		services.ReadSourceAliases = [&](auto requested) {
			Require(requested == project, "wrong actual inherited alias registration");

			return std::span<const sgre::scene::MotionSourceAlias>(asset->Aliases);
		};
		sgre::scene::MotionManagerRuntime runtime(manager, clock, seed, services);
		runtime.SetDepthProjection(0.25f);
		sgre::scene::MotionPlayerCommands commands(manager);
		commands.SetCharacter("character");
		commands.Play("main");
		commands.SetMeshDivisionRatio(ratio);
		const auto& timeline = *commands.CachedTimeline();
		const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *asset->Model);
		sgre::scene::MotionRootScene root;
		root.SetVisible(true);
		const sgre::scene::MotionPlayerRenderControls render;
		sgre::scene::MotionLayerIdentifierPool identifiers;
		sgre::scene::MotionChildConstructionServices construction;
		construction.ReadManagerRandom = [&](const auto& requested) {
			Require(&requested == &manager, "wrong actual inherited constructor manager");

			return runtime.NextUnitFloat();
		};
		sgre::scene::MotionChildFrameSlotServices slots;
		slots.EnqueueAction = [](auto, auto) {
			throw std::runtime_error("action-free vertex fixture queued an action");
		};
		sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, identifiers, construction, slots);
		sgre::scene::MotionSelectedLayerSources sources(selected, catalog, [&](auto requested) -> const sgre::scene::LoadedMotionAsset& {
			Require(requested == project, "wrong requesting inherited asset");

			return *asset;
		});
		auto local_owner = std::make_unique<sgre::scene::MotionLocalFrames>(selected, *asset->Model, sources.FrameServices(slots.EnqueueAction));
		auto& local = *local_owner;
		sgre::scene::MotionSelectedMeshAncestry ancestry(selected, sources, runtime);
		sgre::scene::MotionLayerComposition composition(selected, root, false);
		sgre::scene::MotionSelectedMeshVertices vertices(ancestry);
		RequireFailure([&] {
			sgre::scene::MotionSelectedMeshVertices duplicate(ancestry);
		},
		    "exact actual ancestry/selected owner");
		RequireFailure([&] {
			vertices.Compose();
		},
		    "completed ancestry phase");
		local.Select(sgre::scene::MotionFrameTraversal::Seek, 0);
		composition.Compose(local, false);
		ancestry.Compose(composition);
		RequireFailure([&] {
			static_cast<void>(vertices.Allocated(0));
		},
		    "completed owned geometry phase");
		vertices.Compose();
		for (std::uint32_t layer = 0; layer < 4; ++layer) {
			const auto current = Word(stream);
			const bool active = Word(stream) != 0;
			const bool exported = Word(stream) != 0;
			const bool stopped = Word(stream) != 0;
			const auto ancestor = Word(stream);
			const bool numeric_changed = Word(stream) != 0;
			const bool geometry_changed = Word(stream) != 0;
			const bool numeric_active = Word(stream) != 0;
			const bool allocated = Word(stream) != 0;
			const auto& state = ancestry.State(layer);
			const auto ancestor_index = state.Ancestor() == nullptr ? 0xffffffffu : state.Ancestor()->Node().NativeOrdinal - 1;
			Require(local.State(layer).CurrentIndex == current && state.Active() == active && state.ExportsToChildren() == exported && state.StopsInheritance() == stopped && ancestor_index == ancestor, "actual inherited ancestry differs from native");
			Require(composition.Changed(layer) == numeric_changed && state.Changed() == geometry_changed && composition.Layers()[layer].Active == numeric_active, "actual inherited numeric/geometry gates differ from native");
			Require(&vertices.Node(layer) == &selected.Nodes()[layer] && vertices.Allocated(layer) == allocated, "actual vertex allocation differs or a selected cell was omitted");
			if (allocated) {
				const auto x = Word(stream);
				const auto y = Word(stream);
				Require(vertices.Subdivision(layer).X == x && vertices.Subdivision(layer).Y == y, "actual inherited division differs from native");
				const auto positions = vertices.Geometry(layer).Positions;
				Require(positions.size() == static_cast<std::size_t>(x + 1) * (y + 1), "actual vertex allocation differs from native grid count");
				for (const auto& point : positions) {
					for (const auto value : {point.X, point.Y}) {
						const auto expected = Word(stream);
						Require(std::bit_cast<std::uint32_t>(value) == expected, "case " + std::to_string(case_index) + " layer " + std::to_string(layer) + " actual vertex word " + std::to_string(position_words) + " differs: " + std::to_string(std::bit_cast<std::uint32_t>(value)) + " / " + std::to_string(expected));
						++position_words;
					}
				}
			} else {
				Require(state.SourceGeometry() == nullptr && state.World() == nullptr, "intrinsic structural/kind3 absence acquired substitute geometry");
				RequireFailure([&] {
					static_cast<void>(vertices.Geometry(layer));
				},
				    "no actual vertex allocation");
			}
		}
		auto& child = selected.Child(3);
		Require(sgre::scene::SelectMotionChildFrameCommands(selected.ChildSlots(3), commands.ClockState(), composition.Changed(3), 0, [&] {
			Require(selected.ChildNodes(3) == nullptr, "new actual empty child unexpectedly retired descendants");
		}) == sgre::scene::MotionChildFrameSelection::BindRoot,
		    "actual inherited child command target was not selected");
		ancestry.BindChildDeformation(3, child);
		Require(child.InheritedDeformation() == &ancestry.State(transfer), "actual stopping/exporting child transfer differs from native");
		{
			const auto* child_timeline = child.Commands().CachedTimeline();
			Require(child_timeline != nullptr, "actual child timeline disappeared before its typed vertex context");
			const auto child_prepared = sgre::scene::PrepareMotionTimeline(*child_timeline, *asset->Model);
			selected.RebuildChildNodes(3, *child_timeline, child_prepared, slots);
			auto* child_nodes = selected.ChildNodes(3);
			Require(child_nodes != nullptr && child_nodes->Nodes().empty(), "actual empty child target differs from original fixture metadata");
			sgre::scene::MotionSelectedLayerSources child_sources(*child_nodes, catalog, [&](auto requested) -> const sgre::scene::LoadedMotionAsset& {
				Require(requested == project, "wrong actual child source project");

				return *asset;
			});
			sgre::scene::MotionLocalFrames child_local(*child_nodes, *asset->Model, child_sources.FrameServices(slots.EnqueueAction));
			sgre::scene::MotionSelectedMeshAncestry child_ancestry(*child_nodes, child_sources, runtime, &ancestry);
			sgre::scene::MotionLayerComposition child_composition(*child_nodes, child.Root(), true);
			sgre::scene::MotionSelectedMeshVertices child_vertices(child_ancestry);
			RequireFailure([&] {
				child_vertices.Compose();
			},
			    "completed ancestry phase");
			child_local.Select(sgre::scene::MotionFrameTraversal::Seek, child.Commands().ClockState().TickCount);
			child_composition.Compose(child_local, child.Commands().ClockState().TickAssigned);
			child_ancestry.Compose(child_composition);
			child_vertices.Compose();
			child_local.Select(sgre::scene::MotionFrameTraversal::Forward, 1);
			RequireFailure([&] {
				child_vertices.Compose();
			},
			    "completed ancestry phase");
		}

		auto& geometry = vertices.Geometry(0);
		const auto owned_positions = geometry.Positions;
		const std::array<sgre::resource::MotionPoint, 1> foreign_positions{};
		geometry.Positions = foreign_positions;
		RequireFailure([&] {
			static_cast<void>(vertices.Allocated(0));
		},
		    "completed owned geometry phase");
		geometry.Positions = owned_positions;
		local.Select(sgre::scene::MotionFrameTraversal::Forward, 1);
		RequireFailure([&] {
			static_cast<void>(vertices.Allocated(0));
		},
		    "completed ancestry phase");
		composition.Compose(local, false);
		RequireFailure([&] {
			vertices.Compose();
		},
		    "completed ancestry phase");
		ancestry.Compose(composition);
		RequireFailure([&] {
			static_cast<void>(vertices.Allocated(0));
		},
		    "completed owned geometry phase");
		vertices.Compose();
		Require(vertices.Allocated(0) && vertices.Allocated(2), "genuine current inherited geometry did not compose after its required phase");
		local_owner.reset();
		RequireFailure([&] {
			static_cast<void>(vertices.Allocated(0));
		},
		    "completed ancestry phase");
		selected.Retire();
		RequireFailure([&] {
			static_cast<void>(vertices.Allocated(0));
		},
		    "live selected registered owners");
	}
	Require(stream.peek() == std::char_traits<char>::eof(), "inherited vertex fixture has trailing bytes");
	Require(position_words == 296, "inherited native vector scope changed");
	std::cout << "16 actual mixed vertex records and 296 original inherited vertex words matched\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: motion_selected_mesh_vertices_tests native_fixture");
		Replay(argv[1]);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
