#include "sgre/scene/motion_selected_mesh_mask_packets.hpp"

#include "sgre/scene/motion_child_frame_selection.hpp"
#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_manager_runtime.hpp"
#include "sgre/scene/motion_player_update.hpp"
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
		const bool blank = layer == 0 && (case_index < 4 || index > 0);
		content.emplace_back("src", Atom(std::string(layer == 3 ? "child" : blank ? "blank"
		                                                                          : "atlas")));
		content.emplace_back("icon", Atom(std::string(layer == 3 ? "main" : blank ? "16:8:3:5"
		                                                                          : "tile")));
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
	const auto inherit = std::int64_t{4} | ((layer == 3 && (case_index & 1) != 0) || (layer == 2 && case_index % 4 == 2) ? std::int64_t{0} : std::int64_t{0x02000000});
	const auto order = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})});
	sgre::resource::PsbObject result = {{"label", Atom("n" + std::to_string(layer + 1))}, {"type", Atom(kinds[layer])}, {"inheritMask", Atom(inherit)}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", order}, {"children", Atom(std::move(children))}, {"stencilType", Atom(layer == 2 ? std::int64_t{5} : std::int64_t{0})}, {"meshTransform", Atom(layer == 0 ? std::int64_t{1} : std::int64_t{0})}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::monostate{})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}};
	if (layer == 0) {
		result.emplace_back("meshSyncChildMask", Atom(std::int64_t{8}));
		result.emplace_back("meshDivision", Atom(std::int64_t{6}));
		result.emplace_back("meshCombine", Atom(false));
	}
	if (layer == 2) {
		result.emplace_back("stencilCompositeMaskLayerList", Atom(sgre::resource::PsbArray{Atom(std::string("n1"))}));
	}

	return Atom(std::move(result));
}

static sgre::resource::PsbValue ChildLayer(std::uint32_t case_index, const PatchControls& controls) {
	auto layer = Layer(case_index, 1, controls);
	auto& fields = std::get<sgre::resource::PsbObject>(layer.Data);
	for (auto& field : fields) {
		if (field.first == "type") {
			field.second = Atom(std::int64_t{0});
		} else if (field.first == "label") {
			field.second = Atom(std::string("n1"));
		} else if (field.first == "children") {
			field.second = Atom(sgre::resource::PsbArray{});
		}
	}
	return layer;
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
	const auto timeline = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", bounds}, {"lastTime", Atom(6.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(sgre::resource::PsbArray{Layer(case_index, 0, controls)})}, {"layerIndexMap", Atom(sgre::resource::PsbObject{{"n1", Atom(std::int64_t{0})}})}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
	const auto child_priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0})})}});
	const auto child_timeline = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", bounds}, {"lastTime", Atom(6.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(sgre::resource::PsbArray{ChildLayer(case_index, controls)})}, {"priority", Atom(sgre::resource::PsbArray{child_priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(std::monostate{})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
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
	Require(stream.is_open() && Word(stream) == 0x3144424d && Word(stream) == 8 && Word(stream) == 4, "wrong bounded actual inherited mask fixture");
	std::size_t vertex_words = 0;
	std::size_t world_words = 0;
	std::size_t rows_compared = 0;
	std::size_t members_compared = 0;
	for (std::uint32_t case_index = 0; case_index < 8; ++case_index) {
		Require(Word(stream) == case_index, "wrong actual mask case order");
		PatchControls controls{};
		for (auto& point : controls) {
			point = std::bit_cast<float>(Word(stream));
		}
		const auto ratio = std::bit_cast<float>(Word(stream));
		const bool retained = case_index >= 4;
		Require(Word(stream) == (retained ? 2u : 1u), "actual child UpdateChild phase count differs from native");
		std::array<std::uint32_t, 32> expected_world{};
		for (auto& word : expected_world) {
			word = Word(stream);
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

		std::array<std::size_t, 4> indices{};
		std::array<const sgre::scene::MotionStaticSourceBinding*, 4> bindings{};
		sgre::scene::MotionLayerStencilAncestry stencil(selected);
		auto& child = selected.Child(3);
		sgre::scene::MotionPlayerUpdateServices parent_services;
		parent_services.ReadFrameDelta = []() -> float {
			throw std::runtime_error("bounded shared clock fixture entered its unobserved ordinary task delta service");
		};
		parent_services.PrepareFrames = [&](auto traversal, const auto& context) {
			Require(context.Timeline == &timeline, "actual parent clock selected a foreign timeline");
			local.Select(traversal, context.Clock.TickCount);
		};
		parent_services.ComposeLayers = [&](const auto& context) {
			composition.Compose(local, context.Clock.TickAssigned);
			ancestry.Compose(composition);
			for (std::uint32_t layer = 0; layer < indices.size(); ++layer) {
				indices[layer] = local.State(layer).CurrentIndex;
			}
			bindings[0] = &sources.Source(0);
			bindings[2] = &sources.Source(2);
			stencil.Compose(composition.Layers(), indices, bindings, context.Clock.TickCount, nullptr);
			Require(sgre::scene::SelectMotionChildFrameCommands(selected.ChildSlots(3), context.Clock, composition.Changed(3), 0, [&] {
				Require(selected.ChildNodes(3) == nullptr, "new actual child unexpectedly retired descendants");
			}) == sgre::scene::MotionChildFrameSelection::BindRoot,
			    "actual child command target was not selected");
			ancestry.BindChildDeformation(3, child);
			stencil.BindChildStencil(3, child);
		};
		parent_services.PublishDrawPackets = [](const auto&) {
			throw std::runtime_error("bounded shared clock fixture entered its separate row publication task");
		};
		parent_services.Draw = [] {
			throw std::runtime_error("bounded geometry fixture has no submitted draw service");
		};
		parent_services.DeliverEvent = [](const auto&) {
			throw std::runtime_error("action-free clock fixture delivered an unobserved event");
		};
		sgre::scene::MotionPlayerUpdate parent_update(commands, parent_services);
		parent_update.PrepareTimeline(timeline);
		parent_update.UpdateChild(0);
		Require(commands.ClockState().Playing && commands.ClockState().DeltaTicks == 0 && commands.ClockState().TickCount == 0 && !commands.ClockState().TickAssigned, "actual parent clock predecessor differs from the original prepared zero-delta transition");
		const auto* child_timeline = child.Commands().CachedTimeline();
		Require(child_timeline != nullptr, "actual child selected timeline disappeared");
		const auto child_prepared = sgre::scene::PrepareMotionTimeline(*child_timeline, *asset->Model);
		selected.RebuildChildNodes(3, *child_timeline, child_prepared, slots);
		auto* child_nodes = selected.ChildNodes(3);
		Require(child_nodes != nullptr && child_nodes->Nodes().size() == 1, "actual child selection differs from the original nonempty timeline");
		sgre::scene::MotionSelectedLayerSources child_sources(*child_nodes, catalog, [&](auto requested) -> const sgre::scene::LoadedMotionAsset& {
			Require(requested == project, "wrong actual child source project");
			return *asset;
		});
		sgre::scene::MotionLocalFrames child_local(*child_nodes, *asset->Model, child_sources.FrameServices(slots.EnqueueAction));
		sgre::scene::MotionSelectedMeshAncestry child_ancestry(*child_nodes, child_sources, runtime, &ancestry);
		sgre::scene::MotionLayerComposition child_composition(*child_nodes, child.Root(), true);
		sgre::scene::MotionSelectedMeshVertices child_vertices(child_ancestry);
		sgre::scene::MotionLayerStencilAncestry child_stencil(*child_nodes);
		std::array<std::size_t, 1> child_indices{};
		std::array<const sgre::scene::MotionStaticSourceBinding*, 1> child_bindings{};
		auto child_services = parent_services;
		child_services.PrepareFrames = [&](auto traversal, const auto& context) {
			Require(context.Timeline == child_timeline, "actual child clock selected a foreign timeline");
			child_local.Select(traversal, context.Clock.TickCount);
		};
		child_services.ComposeLayers = [&](const auto& context) {
			child_composition.Compose(child_local, context.Clock.TickAssigned);
			child_ancestry.Compose(child_composition);
			child_indices[0] = child_local.State(0).CurrentIndex;
			child_bindings[0] = &child_sources.Source(0);
			child_stencil.Compose(child_composition.Layers(), child_indices, child_bindings, context.Clock.TickCount, child.InheritedStencil());
		};
		sgre::scene::MotionPlayerUpdate child_update(child.Commands(), child_services);
		child_update.PrepareTimeline(*child_timeline);
		child_update.UpdateChild(commands.ClockState().DeltaTicks);
		parent_update.PrependChildEvents(child_update);
		Require(child.Commands().ClockState().DeltaTicks == 0 && child.Commands().ClockState().TickCount == 0 && child_indices[0] == 0 && parent_update.PendingEventCount() == 0 && child_update.PendingEventCount() == 0, "actual child clock/cursor/event transfer differs from the original prepared transition");
		sgre::scene::MotionStencilRowPolicies rows(selected, stencil);
		sgre::scene::MotionStencilRowPolicies child_rows(*child_nodes, child_stencil, &rows);
		rows.BindChildRows(3, child_rows);
		const std::array<sgre::scene::MotionMeshMaskOwner, 2> contexts{sgre::scene::MotionMeshMaskOwner(rows, vertices), sgre::scene::MotionMeshMaskOwner(child_rows, child_vertices)};
		RequireFailure([&] {
			sgre::scene::MotionSelectedMeshMaskPackets omitted(std::span(contexts).first(1));
		},
		    "cannot omit");
		const std::array<sgre::scene::MotionMeshMaskOwner, 3> duplicated_child{contexts[0], contexts[1], contexts[1]};
		RequireFailure([&] {
			sgre::scene::MotionSelectedMeshMaskPackets duplicate_child(duplicated_child);
		},
		    "duplicates an actual selected context");
		sgre::scene::MotionSelectedMeshMaskPackets packets(contexts);
		RequireFailure([&] {
			sgre::scene::MotionSelectedMeshMaskPackets duplicate(contexts);
		},
		    "exclusive selected row/vertex");
		sgre::scene::MotionPriorityCursor priority(prepared);
		sgre::scene::MotionPriorityCursor child_priority(child_prepared);
		priority.Seek(0);
		child_priority.Seek(child.Commands().ClockState().TickCount);
		rows.PrepareRecursiveFrame(composition.Layers());
		child_rows.PrepareRecursiveFrame(child_composition.Layers());
		vertices.Compose();
		child_vertices.Compose();
		child_rows.Publish(child_composition.Layers(), child_indices, child_bindings, child.Commands().ClockState().TickCount, child_priority);
		rows.Publish(composition.Layers(), indices, bindings, commands.ClockState().TickCount, priority);
		packets.Build();
		if (retained) {
			Require(vertices.Allocated(0) && !sources.State(0).Blank, "ordinary warm phase did not produce actual retained geometry");
			const auto* previous_geometry = &vertices.Geometry(0);
			const auto previous_points = std::vector<sgre::resource::MotionPoint>(previous_geometry->Positions.begin(), previous_geometry->Positions.end());
			parent_update.UpdateChild(2);
			child_update.UpdateChild(commands.ClockState().DeltaTicks);
			parent_update.PrependChildEvents(child_update);
			Require(commands.ClockState().TickCount == 2 && commands.ClockState().DeltaTicks == 2 && child.Commands().ClockState().TickCount == 2 && indices[0] == 1 && child_indices[0] == 1, "genuine forward parent/child blank transition differs from native clock/slot ownership");
			priority.Seek(commands.ClockState().TickCount);
			child_priority.Seek(child.Commands().ClockState().TickCount);
			rows.PrepareRecursiveFrame(composition.Layers());
			child_rows.PrepareRecursiveFrame(child_composition.Layers());
			vertices.Compose();
			child_vertices.Compose();
			Require(vertices.Allocated(0) && &vertices.Geometry(0) == previous_geometry && vertices.Geometry(0).Positions.size() == previous_points.size(), "blank transition replaced its actual retained geometry object or extent");
			for (std::size_t index = 0; index < previous_points.size(); ++index) {
				Require(std::bit_cast<std::uint32_t>(vertices.Geometry(0).Positions[index].X) == std::bit_cast<std::uint32_t>(previous_points[index].X) && std::bit_cast<std::uint32_t>(vertices.Geometry(0).Positions[index].Y) == std::bit_cast<std::uint32_t>(previous_points[index].Y), "blank transition mutated previously produced draw coordinates");
			}
			child_rows.Publish(child_composition.Layers(), child_indices, child_bindings, child.Commands().ClockState().TickCount, child_priority);
			rows.Publish(composition.Layers(), indices, bindings, commands.ClockState().TickCount, priority);
			packets.Build();
		}
		const auto& direct = ancestry.State(0);
		const auto* geometry = direct.SourceGeometry();
		Require(sources.State(0).Ready && sources.State(0).Blank && !sources.State(0).Binding && !sources.State(0).Texture && geometry != nullptr && geometry->Blank && geometry->BlankSource == &sources.Source(0) && geometry->Source == nullptr && geometry->Icon == nullptr && direct.Active() && direct.ExportsToChildren(), "blank world geometry lost its actual descriptor/cache/numeric owner or fabricated an atlas identity");
		Require(vertices.Allocated(0) == retained, "initial blank allocated substitute vertices or lost real retained geometry");
		const auto* world = direct.World();
		Require(world != nullptr, "blank source failed to produce its genuine active world patch");
		std::size_t world_index = 0;
		for (const auto& control : world->WorldPatch.Controls) {
			for (const auto coordinate : {control.X, control.Y}) {
				Require(std::bit_cast<std::uint32_t>(coordinate) == expected_world[world_index++], "blank world patch differs from original descriptor/numeric producer");
				++world_words;
			}
		}
		const auto nodes = packets.Nodes();
		const auto settings = packets.Settings();
		Require(nodes.size() == 4 && settings.size() == 4 && rows.CollectedRows().size() == 3, "actual selected mask row count differs or a drawing cell was omitted");
		for (std::size_t index = 0; index < 4; ++index) {
			const auto context = Word(stream);
			const auto layer = Word(stream);
			const bool published = Word(stream) != 0;
			const auto mode = Word(stream);
			const auto enabled = Word(stream);
			const auto scale = Word(stream);
			const auto offset = Word(stream);
			const auto ancestor_row = Word(stream);
			const auto opacity = Word(stream);
			const auto point_count = Word(stream);
			const auto member_count = Word(stream);
			const bool suppressed = Word(stream) != 0;
			Require(context == (index < 3 ? 0u : 1u), "actual mask context order differs from native");
			const auto& row = context == 0 ? rows.Row(index) : child_rows.Row(0);
			const auto& packet = nodes[index];
			Require(row.Node().NativeOrdinal - 1 == layer && row.Published() == published && packet.ModeFlags == mode, "actual persistent mask row/publication differs from original");
			Require(packet.Parent.value_or(0xffffffffu) == ancestor_row && settings[index].Operation == enabled && std::bit_cast<std::uint32_t>(settings[index].ThresholdMinimum) == scale && std::bit_cast<std::uint32_t>(settings[index].ThresholdExtent) == offset, "actual mask ancestry/retained settings differ from original");
			Require(!published || packet.Command.Hidden == suppressed, "blank retained persistent row suppression differs from native");
			Require(packet.Command.GeometryCount == opacity && (packet.Command.Geometry == nullptr ? point_count == 0 : packet.Command.Geometry->Positions.size() == point_count), "actual mask geometry binding/count differs from original");
			if (point_count > 0) {
				Require(packet.Command.Geometry == &(context == 0 ? vertices.Geometry(layer) : child_vertices.Geometry(layer)), "mask geometry did not borrow its exact actual source-derived owner");
				for (const auto& point : packet.Command.Geometry->Positions) {
					for (const auto coordinate : {point.X, point.Y}) {
						const auto expected_word = Word(stream);
						Require(std::bit_cast<std::uint32_t>(coordinate) == expected_word, "case " + std::to_string(case_index) + " context " + std::to_string(context) + " layer " + std::to_string(layer) + " vertex word " + std::to_string(vertex_words) + " actual " + std::to_string(coordinate) + " / native " + std::to_string(std::bit_cast<float>(expected_word)));
						++vertex_words;
					}
				}
			}
			Require(packet.Members.size() == member_count, "actual mask member count differs from original");
			for (const auto& member : packet.Members) {
				const auto expected_row = Word(stream);
				Require(expected_row < nodes.size() && member.Geometry == nodes[expected_row].Command.Geometry && member.GeometryCount == nodes[expected_row].Command.GeometryCount && member.Hidden == nodes[expected_row].Command.Hidden, "actual composite mask lost its exact current row geometry borrow");
				++members_compared;
			}
			++rows_compared;
		}
		rows.PrepareRecursiveFrame(composition.Layers());
		RequireFailure([&] {
			static_cast<void>(packets.Nodes());
		},
		    "same genuine completed");
		child_rows.PrepareRecursiveFrame(child_composition.Layers());
		child_rows.Publish(child_composition.Layers(), child_indices, child_bindings, child.Commands().ClockState().TickCount, child_priority);
		rows.Publish(composition.Layers(), indices, bindings, commands.ClockState().TickCount, priority);
		RequireFailure([&] {
			packets.Build();
		},
		    "same genuine completed");
		vertices.Compose();
		RequireFailure([&] {
			packets.Build();
		},
		    "same genuine completed");
		child_vertices.Compose();
		packets.Build();
		Require(packets.Nodes()[1].Members.size() == 2, "genuine repeated phases changed actual composite members");
		vertices.Compose();
		RequireFailure([&] {
			static_cast<void>(packets.Settings());
		},
		    "borrows expired");
		packets.Build();
		const auto* same_numeric_storage = composition.Layers().data();
		composition.Compose(local, false);
		ancestry.Compose(composition);
		Require(composition.Layers().data() == same_numeric_storage, "discriminating numeric fixture unexpectedly replaced its backing storage");
		RequireFailure([&] {
			vertices.Compose();
		},
		    "actual prepared row/numeric phase");
		RequireFailure([&] {
			static_cast<void>(packets.Settings());
		},
		    "same genuine completed");
		RequireFailure([&] {
			stencil.Compose(composition.Layers(), {}, bindings, commands.ClockState().TickCount, nullptr);
		},
		    "every actual live numeric/current/source input");
		RequireFailure([&] {
			rows.PrepareRecursiveFrame(composition.Layers());
		},
		    "actual live composed selected owner");
		stencil.Compose(composition.Layers(), indices, bindings, commands.ClockState().TickCount, nullptr);
		RequireFailure([&] {
			vertices.Compose();
		},
		    "actual prepared row/numeric phase");
		RequireFailure([&] {
			rows.Publish(composition.Layers(), indices, bindings, commands.ClockState().TickCount, priority);
		},
		    "current actual ancestry preparation");
		ancestry.BindChildDeformation(3, child);
		child_composition.Compose(child_local, child.Commands().ClockState().TickAssigned);
		child_ancestry.Compose(child_composition);
		rows.PrepareRecursiveFrame(composition.Layers());
		child_rows.PrepareRecursiveFrame(child_composition.Layers());
		vertices.Compose();
		child_vertices.Compose();
		child_rows.Publish(child_composition.Layers(), child_indices, child_bindings, child.Commands().ClockState().TickCount, child_priority);
		rows.Publish(composition.Layers(), indices, bindings, commands.ClockState().TickCount, priority);
		packets.Build();
		Require(packets.Nodes()[1].Members.size() == 2, "genuine refreshed row/numeric/geometry generation changed actual mask members");
		child_nodes->Retire();
		RequireFailure([&] {
			static_cast<void>(packets.Nodes());
		},
		    "live selected registered owners");
	}
	Require(stream.peek() == std::char_traits<char>::eof() && rows_compared == 32 && members_compared == 16 && vertex_words == 568 && world_words == 256, "actual mask vector scope or exact fixture end changed");
	std::cout << "32 actual blank/retained source mask rows, 16 members, 256 world and 568 original vertex words matched\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: motion_blank_deformation_tests native_fixture");
		Replay(argv[1]);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
