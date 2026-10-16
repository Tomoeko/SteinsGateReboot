#include "sgre/render/sprite_pass.hpp"
#include "sgre/scene/motion_selected_mesh_vertices.hpp"

#include "sgre/scene/motion_child_frame_selection.hpp"
#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_layer_parameters.hpp"
#include "sgre/scene/motion_layer_stencil_ancestry.hpp"
#include "sgre/scene/motion_local_frames.hpp"
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
		frames.push_back(Atom(sgre::resource::PsbObject{{"time", Atom(std::array<double, 4>{0, 100, 200, 201}[index])}, {"type", Atom(index == 3 ? std::int64_t{0} : index < 2 ? std::int64_t{3}
		                                                                                                                                                                       : std::int64_t{2})},
		    {"content", Atom(std::move(content))}}));
	}
	const std::array<std::int64_t, 4> kinds = {0, 2, 12, 3};
	const auto inherit = std::int64_t{4} | ((layer == 3 && (case_index & 1) != 0) || (layer == 2 && case_index == 2) ? std::int64_t{0} : std::int64_t{0x02000000});
	const auto order = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})});
	sgre::resource::PsbObject result = {{"label", Atom("n" + std::to_string(layer + 1))}, {"type", Atom(kinds[layer])}, {"inheritMask", Atom(inherit)}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(std::move(frames))}, {"transformOrder", order}, {"children", Atom(std::move(children))}, {"stencilType", Atom(layer == 2 ? std::int64_t{5} : std::int64_t{0})}, {"meshTransform", Atom(layer == 0 ? std::int64_t{1} : std::int64_t{0})}, {"objTriPriority", Atom(std::int64_t{0})}, {"parameterize", Atom(std::int64_t{0})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"motionIndependentLayerInherit", Atom(false)}};
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

static std::shared_ptr<sgre::resource::PsbDocument> Document(std::uint32_t case_index, const PatchControls& controls, bool empty_child) {
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(sgre::resource::PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{32})}, {"height", Atom(std::int64_t{32})}, {"truncated_width", Atom(std::int64_t{32})}, {"truncated_height", Atom(std::int64_t{32})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	const auto icon = Atom(sgre::resource::PsbObject{{"left", Atom(8.0)}, {"top", Atom(16.0)}, {"width", Atom(16.0)}, {"height", Atom(8.0)}, {"originX", Atom(3.0)}, {"originY", Atom(5.0)}, {"attr", Atom(std::int64_t{2})}});
	const auto source = Atom(sgre::resource::PsbObject{{"texture", texture}, {"icon", Atom(sgre::resource::PsbObject{{"tile", icon}})}});
	const auto priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1}), Atom(std::int64_t{2}), Atom(std::int64_t{3})})}});
	const auto terminal = Atom(sgre::resource::PsbObject{{"time", Atom(201.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(std::monostate{})}});
	const auto bounds = Atom(sgre::resource::PsbObject{{"left", Atom(0.0)}, {"top", Atom(0.0)}, {"right", Atom(16.0)}, {"bottom", Atom(8.0)}});
	const auto timeline = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", bounds}, {"lastTime", Atom(201.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(sgre::resource::PsbArray{Layer(case_index, 0, controls)})}, {"layerIndexMap", Atom(sgre::resource::PsbObject{{"n1", Atom(std::int64_t{0})}})}, {"priority", Atom(sgre::resource::PsbArray{priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"id", Atom(std::string("param"))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(-100.0)}, {"rangeEnd", Atom(100.0)}, {"division", Atom(200.0)}, {"enabled", Atom(std::int64_t{1})}})})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
	const auto child_priority = Atom(sgre::resource::PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(sgre::resource::PsbArray{Atom(std::int64_t{0})})}});
	auto child_layer = ChildLayer(case_index, controls);
	if (empty_child) {
		for (auto& [name, value] : std::get<sgre::resource::PsbObject>(child_layer.Data)) {
			if (name == "parameterize") {
				value = Atom(std::monostate{});
			}
		}
	}
	auto child_timeline = Atom(sgre::resource::PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", bounds}, {"lastTime", Atom(201.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(sgre::resource::PsbArray{std::move(child_layer)})}, {"priority", Atom(sgre::resource::PsbArray{child_priority, terminal})}, {"tag", Atom(sgre::resource::PsbArray{})}, {"parameter", Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"id", Atom(std::string("param"))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(-100.0)}, {"rangeEnd", Atom(100.0)}, {"division", Atom(200.0)}, {"enabled", Atom(std::int64_t{1})}})})}, {"variable", Atom(std::monostate{})}, {"parameterize", Atom(std::monostate{})}});
	if (empty_child) {
		for (auto& [name, value] : std::get<sgre::resource::PsbObject>(child_timeline.Data)) {
			if (name == "parameter") {
				value = Atom(std::monostate{});
			}
		}
	}
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

static void CompareIndices(const char* path) {
	const std::array<sgre::resource::PsbValue, 18> authored_inputs{Atom(std::monostate{}), Atom(false), Atom(true), Atom(std::int64_t{0}), Atom(std::int64_t{1}), Atom(std::int64_t{-1}), Atom(std::int64_t{256}), Atom(std::int64_t{4294967296}), Atom(std::int64_t{4294967297}), Atom(std::int64_t{-4294967296}), Atom(0.0), Atom(0.75), Atom(-0.75), Atom(1.75), Atom(0.75), Atom(-0.75), Atom(1.75), Atom(2147483648.0)};
	std::ifstream expected(path, std::ios::binary);
	Require(expected.is_open() && Word(expected) == 0x31494c4d && Word(expected) == authored_inputs.size(), "wrong original parameter index fixture");
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("scalar index fixture has no source texture");
	},
	    [](auto) {
		    throw std::runtime_error("scalar index fixture has no source release");
	    });
	sgre::scene::MotionResourceManager manager(cache);
	auto model = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionObject character;
	character.Name = "indices";
	for (std::size_t case_index = 0; case_index <= authored_inputs.size(); ++case_index) {
		sgre::resource::MotionTimeline timeline;
		timeline.Name = "index" + std::to_string(case_index);
		timeline.LastTime = 201;
		sgre::resource::PsbArray ranges;
		for (std::size_t index = 0; index < 257; ++index) {
			ranges.push_back(Atom(sgre::resource::PsbObject{{"id", Atom("param" + std::to_string(index))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(-100.0)}, {"rangeEnd", Atom(100.0)}, {"division", Atom(200.0)}}));
		}
		timeline.Parameters = Atom(std::move(ranges));
		sgre::resource::MotionLayer layer;
		layer.Type = 2;
		if (case_index < authored_inputs.size()) {
			layer.Properties.emplace_back("parameterize", authored_inputs[case_index]);
		}
		timeline.Layers.push_back(std::move(layer));
		character.Timelines.push_back(std::move(timeline));
	}
	model->Objects.push_back(std::move(character));
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = cache.AllocateDocumentIdentity();
	identity.RootOffset = 72;
	const auto registration = manager.Register(identity, model, {});
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("indices");
	for (std::size_t case_index = 0; case_index <= authored_inputs.size(); ++case_index) {
		commands.Play("index" + std::to_string(case_index));
		sgre::scene::MotionTimelineParameters parameters(commands.ParameterValues(), *commands.CachedTimeline());
		if (case_index == authored_inputs.size()) {
			RequireFailure([&] {
				sgre::scene::MotionLayerParameterBindings missing(parameters);
			},
			    "missing its required parameterize");
			continue;
		}
		const auto category = Word(expected);
		const auto has_index = Word(expected);
		const auto index_word = Word(expected);
		if (index_word >= parameters.Entries().size()) {
			RequireFailure([&] {
				sgre::scene::MotionLayerParameterBindings invalid(parameters);
			},
			    "index");
			continue;
		}
		sgre::scene::MotionLayerParameterBindings bindings(parameters);
		Require((category == 0) == (has_index == 0), "original null index getter scope changed");
		Require(bindings.Parameter(0) == (has_index != 0 ? &parameters.Entries()[index_word] : nullptr), "actual raw index did not borrow its native-coerced ordered record");
	}
	Require(expected.get() == std::char_traits<char>::eof() && cache.Count() == 0 && commands.ParameterValues().RegisteredParameterCount() == 0, "index expected scope or actual retirement differs");
	Require(manager.Unregister(registration), "scalar index registration failed retirement");
}

static void CompareSeek(const char* path) {
	std::ifstream expected(path, std::ios::binary);
	Require(expected.is_open() && Word(expected) == 0x31534c4d && Word(expected) == 4, "wrong original above-terminal seek fixture");
	std::array<std::uint32_t, 5> structural{};
	for (std::size_t case_index = 0; case_index < 4; ++case_index) {
		std::array<std::uint32_t, 5> words{};
		for (auto& word : words) {
			word = Word(expected);
		}
		if (words[0] == 2) {
			structural = words;
		}
	}
	Require(expected.get() == std::char_traits<char>::eof() && structural[0] == 2, "original seek corpus omitted its actual structural cursor");
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("structural cursor has no texture source");
	},
	    [](auto) {
		    throw std::runtime_error("structural cursor has no texture release");
	    });
	sgre::scene::MotionResourceManager manager(cache);
	auto model = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "seek";
	timeline.LastTime = 201;
	sgre::resource::MotionFrame priority;
	priority.Time = 0;
	priority.Type = 1;
	priority.Content = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0})});
	timeline.Priority.push_back(std::move(priority));
	sgre::resource::MotionFrame terminal;
	terminal.Time = 201;
	terminal.Type = 1;
	terminal.Content = Atom(std::monostate{});
	timeline.Priority.push_back(std::move(terminal));
	timeline.Parameters = Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"id", Atom(std::string("param"))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(-100.0)}, {"rangeEnd", Atom(100.0)}, {"division", Atom(400.0)}})});
	sgre::resource::MotionLayer layer;
	layer.Type = 2;
	layer.Label = "cursor";
	layer.TransformOrder = {0, 3, 2, 1};
	layer.Properties = {{"parameterize", Atom(std::int64_t{0})}, {"meshTransform", Atom(std::int64_t{0})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}};
	for (const auto time : std::array<float, 4>{0, 100, 200, 201}) {
		sgre::resource::MotionFrame frame;
		frame.Time = time;
		frame.Type = 2;
		frame.Content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0})}});
		layer.Frames.push_back(std::move(frame));
	}
	timeline.Layers.push_back(std::move(layer));
	sgre::resource::MotionObject character;
	character.Name = "cursor";
	character.Timelines.push_back(std::move(timeline));
	model->Objects.push_back(std::move(character));
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = cache.AllocateDocumentIdentity();
	identity.RootOffset = 72;
	const auto registration = manager.Register(identity, model, {});
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("cursor");
	commands.Play("seek");
	const auto& raw = *commands.CachedTimeline();
	const auto prepared = sgre::scene::PrepareMotionTimeline(raw, *model);
	sgre::scene::MotionRootScene root;
	const sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionLayerIdentifierPool identifiers;
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [](const auto&) -> float {
		throw std::runtime_error("structural cursor has no kind3 constructor");
	};
	sgre::scene::MotionChildFrameSlotServices slots;
	slots.EnqueueAction = [](auto, auto) {
		throw std::runtime_error("structural cursor has no action");
	};
	sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, raw, prepared, identifiers, construction, slots);
	sgre::scene::MotionLocalFrameServices services;
	services.BindSource = [](auto, const auto&, const auto&) {
		throw std::runtime_error("structural cursor has no drawing binding");
	};
	services.EnqueueAction = slots.EnqueueAction;
	sgre::scene::MotionLocalFrames local(selected, *model, services);
	local.Select(sgre::scene::MotionFrameTraversal::Seek, commands.ClockState().TickCount);
	Require(local.State(0).CurrentIndex == structural[1], "actual initial range constructor did not reach the native nonzero prior cursor");
	commands.ParameterValues().WriteLocal("param", 50, 0);
	local.Select(sgre::scene::MotionFrameTraversal::Seek, commands.ClockState().TickCount);
	Require(local.State(0).CurrentSlot == structural[2] && local.State(0).CurrentIndex == structural[3] && std::bit_cast<std::uint32_t>(local.Position(0)) == structural[4], "above-terminal seek did not reset its actual bound cursor to native zero");
	selected.Retire();
	Require(manager.Unregister(registration), "structural cursor registration did not retire");
}

struct NativeState {
	float Input = 0;
	std::uint32_t Flags = 0;
	std::array<std::array<std::uint32_t, 10>, 5> Samples{};
};

static std::array<NativeState, 8> ReadExpected(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open() && Word(input) == 0x31504c4d && Word(input) == 8 && Word(input) == 5, "wrong original parameter/local fixture");
	std::array<NativeState, 8> states{};
	for (auto& state : states) {
		state.Input = std::bit_cast<float>(Word(input));
		state.Flags = Word(input);
		for (auto& sample : state.Samples) {
			for (auto& word : sample) {
				word = Word(input);
			}
		}
	}
	Require(input.get() == std::char_traits<char>::eof(), "original parameter/local fixture has trailing bytes");

	return states;
}

static void Compare(const sgre::scene::MotionLocalFrames& local, const sgre::scene::MotionLayerComposition& composition, const NativeState& state, bool child, std::size_t state_index) {
	const auto count = child ? 1u : 4u;
	for (std::uint32_t layer = 0; layer < count; ++layer) {
		const auto& expected = state.Samples[child ? 4 : layer];
		const auto& actual = local.State(layer);
		const auto& pose = *local.Evaluation(layer).Sample;
		// Native50bc40 leaves the previous composed fields in shared node storage when its cached return is zero.
		const auto& coordinate = expected[6] != 0 ? pose.Coordinate : composition.Layers()[layer].Hierarchy.Attributes.Coordinate;
		const auto prefix = "state " + std::to_string(state_index) + (child ? " child " : " parent ") + std::to_string(layer);
		Require(expected[0] == (child ? 1u : 0u) && expected[1] == layer && expected[2] == 1, prefix + " native scope differs");
		Require(std::bit_cast<std::uint32_t>(local.Position(layer)) == expected[3], prefix + " record position differs");
		Require(actual.CurrentSlot == expected[4] && actual.CurrentIndex == expected[5], prefix + " physical slot/current frame differs");
		Require(local.EvaluationChanged(layer) == (expected[6] != 0), prefix + " original local dirty return differs");
		for (std::size_t component = 0; component < 3; ++component) {
			Require(std::bit_cast<std::uint32_t>(coordinate[component]) == expected[7 + component], prefix + " original local coordinate differs");
		}
	}
}

class ChildPhases {
public:
	ChildPhases(sgre::scene::MotionSelectedLayerNodes& selected, sgre::scene::MotionChildConstructionState& child, const sgre::scene::MotionSelectedMeshAncestry& parent_ancestry, sgre::scene::MotionAssetCatalog& catalog, const sgre::scene::LoadedMotionAsset& asset, sgre::scene::MotionManagerRuntime& runtime, sgre::scene::MotionChildFrameSlotServices slots, const NativeState*& expected, std::size_t& state_index, bool empty_child) : m_EmptyChild(empty_child), m_Child(child), m_Expected(expected), m_StateIndex(state_index), m_Sources(selected, catalog, [&](auto project) -> const sgre::scene::LoadedMotionAsset& {
		                                                                                                                                                                                                                                                                                                                                                                                                                                               Require(runtime.Manager().Identity(project) == asset.Identity, "child source requesting registration differs");
		                                                                                                                                                                                                                                                                                                                                                                                                                                               return asset;
	                                                                                                                                                                                                                                                                                                                                                                                                                                               }),
	                                                                                                                                                                                                                                                                                                                                                                                                                                               m_Local(selected, *asset.Model, m_Sources.FrameServices(slots.EnqueueAction)), m_Ancestry(selected, m_Sources, runtime, &parent_ancestry), m_Composition(selected, child.Root(), true), m_Vertices(m_Ancestry), m_Stencil(selected) {
		sgre::scene::MotionPlayerUpdateServices services;
		services.ReadFrameDelta = []() -> float {
			throw std::runtime_error("bounded child proof has no task delta reader");
		};
		services.PrepareFrames = [&](auto traversal, const auto& context) {
			m_Local.Select(traversal, context.Clock.TickCount);
		};
		services.ComposeLayers = [&](const auto& context) {
			m_Composition.Compose(m_Local, context.Clock.TickAssigned);
			if (!m_EmptyChild) {
				Compare(m_Local, m_Composition, *m_Expected, true, m_StateIndex);
			} else {
				Require(m_Local.ParameterBindings()->Parameter(0) == nullptr && m_Local.Position(0) == m_Child.Commands().ClockState().TickCount && m_Local.State(0).CurrentIndex == 0, "zero-record child did not use its actual ordinary clock/cursor");
				Require(m_Local.Evaluation(0).Sample->Coordinate == std::array<float, 3>{4, 6, 0.5f}, "zero-record child lost the retained actual frame-zero local fields");
			}
			m_Ancestry.Compose(m_Composition);
			const std::array<std::size_t, 1> indices{m_Local.State(0).CurrentIndex};
			const std::array<const sgre::scene::MotionStaticSourceBinding*, 1> sources{&m_Sources.Source(0)};
			m_Stencil.Compose(m_Composition.Layers(), indices, sources, context.Clock.TickCount, m_Child.InheritedStencil());
			RequireFailure([&] {
				m_Local.CompleteComposition();
			},
			    "phase");
			m_Vertices.Compose();
			m_Local.CompleteComposition();
			Require(m_Local.ParameterBindings()->ControlFlags(0) == 0 && !m_Local.State(0).FrameDirty, "child actual finish did not clear its own control/dirty flags");
		};
		services.PublishDrawPackets = [](const auto&) {
			throw std::runtime_error("bounded child proof does not publish substitute draw packets");
		};
		services.Draw = [] {
			throw std::runtime_error("bounded child proof does not submit GPU work");
		};
		services.DeliverEvent = [](const auto&) {
			throw std::runtime_error("action-free child proof delivered an action");
		};
		m_Update = std::make_unique<sgre::scene::MotionPlayerUpdate>(child.Commands(), std::move(services));
		m_Update->PrepareTimeline(*child.Commands().CachedTimeline());
	}

	void AttemptStaleFinish() {
		if (!m_EmptyChild || m_Local.Samples().empty()) {
			return;
		}
		m_Local.Select(sgre::scene::MotionFrameTraversal::Forward, m_Child.Commands().ClockState().TickCount);
		RequireFailure([&] {
			m_Local.CompleteComposition();
		},
		    "numeric composition");
	}

	void Update(float delta) {
		m_Update->UpdateChild(delta);
	}

	[[nodiscard]] sgre::scene::MotionPlayerUpdate& UpdateOwner() {
		return *m_Update;
	}

private:
	bool m_EmptyChild = false;
	sgre::scene::MotionChildConstructionState& m_Child;
	const NativeState*& m_Expected;
	std::size_t& m_StateIndex;
	sgre::scene::MotionSelectedLayerSources m_Sources;
	sgre::scene::MotionLocalFrames m_Local;
	sgre::scene::MotionSelectedMeshAncestry m_Ancestry;
	sgre::scene::MotionLayerComposition m_Composition;
	sgre::scene::MotionSelectedMeshVertices m_Vertices;
	sgre::scene::MotionLayerStencilAncestry m_Stencil;
	std::unique_ptr<sgre::scene::MotionPlayerUpdate> m_Update;
};

static void Replay(const char* path, const char* shader_path, bool empty_child) {
	const std::array<std::pair<float, std::uint32_t>, 8> authored_setters{{{0, 0}, {-50, 0}, {50, 2}, {100, 1}, {-100, 3}, {10, 0}, {-25, 4}, {-25, 0}}};
	const auto states = ReadExpected(path);
	const std::array<std::uint32_t, 32> raw_control_words{0x3f61c6c3u, 0x3f13ac0cu, 0x3f12e1b9u, 0x3f288d31u, 0x3f814c61u, 0x3f1b8846u, 0xbe2e8378u, 0x3cd11e04u, 0x3f6a0838u, 0x3f175e7eu, 0xbdf1082au, 0xbe013601u, 0x3d84af8au, 0xbe47d464u, 0x3b55b4a0u, 0x3ed75689u, 0x3f3dbc8eu, 0x3e8029e1u, 0x3da6322au, 0xbe2f728au, 0x3f85311eu, 0x3f16cbefu, 0x3f8ced2au, 0x3f03218fu, 0x3f1ac131u, 0x3f3c3465u, 0x3e47a938u, 0x3f7095d4u, 0x3f9087cau, 0x3f17f9dbu, 0x3f81ce81u, 0x3ea8bdd9u};
	PatchControls controls{};
	for (std::size_t index = 0; index < controls.size(); ++index) {
		controls[index] = std::bit_cast<float>(raw_control_words[index]);
	}
	sgre::render::SpritePassDescription description;
	description.Width = 4;
	description.Height = 4;
	description.MaxTextures = 2;
	description.MaxTextureBytes = 4096;
	sgre::render::SpritePassRenderer renderer(description, shader_path);
	sgre::scene::TextureCache cache([&](auto format, const auto& image) {
		return renderer.UploadTexture(format, image);
	},
	    [&](auto texture) {
		    renderer.ReleaseTexture(texture);
	    });
	sgre::scene::MotionAssetCatalog catalog(cache);
	const auto asset = catalog.Prepare(Document(0, controls, empty_child), std::make_shared<const std::vector<std::uint8_t>>(1152, 128));
	sgre::scene::MotionResourceManager manager(cache);
	const auto project = manager.Register(asset->Identity, asset->Model, *asset->Backing);
	sgre::core::FrameClock clock;
	const std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
	sgre::scene::MotionManagerRuntimeServices runtime_services;
	runtime_services.ReadSourceAliases = [&](auto requested) {
		Require(requested == project, "parent requesting source registration differs");
		return std::span<const sgre::scene::MotionSourceAlias>(asset->Aliases);
	};
	sgre::scene::MotionManagerRuntime runtime(manager, clock, seed, runtime_services);
	runtime.SetDepthProjection(0.25f);
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("character");
	commands.Play("main");
	const auto& timeline = *commands.CachedTimeline();
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *asset->Model);
	sgre::scene::MotionRootScene root;
	root.SetVisible(true);
	const sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionLayerIdentifierPool identifiers;
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [&](const auto& requested) {
		Require(&requested == &manager, "wrong actual parameter constructor manager");
		return runtime.NextUnitFloat();
	};
	sgre::scene::MotionChildFrameSlotServices slots;
	slots.EnqueueAction = [](auto, auto) {
		throw std::runtime_error("action-free parameter graph queued an action");
	};
	sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, identifiers, construction, slots);
	sgre::scene::MotionSelectedLayerSources sources(selected, catalog, [&](auto requested) -> const sgre::scene::LoadedMotionAsset& {
		Require(requested == project, "parent source requesting project differs");
		return *asset;
	});
	sgre::scene::MotionLocalFrames local(selected, *asset->Model, sources.FrameServices(slots.EnqueueAction));
	sgre::scene::MotionSelectedMeshAncestry ancestry(selected, sources, runtime);
	sgre::scene::MotionLayerComposition composition(selected, root, false);
	sgre::scene::MotionSelectedMeshVertices vertices(ancestry);
	sgre::scene::MotionLayerStencilAncestry stencil(selected);
	auto& child = selected.Child(3);
	std::unique_ptr<sgre::scene::PreparedMotionTimeline> child_prepared;
	std::unique_ptr<ChildPhases> child_phases;
	std::size_t state_index = 0;
	const NativeState* expected = &states[0];
	sgre::scene::MotionPlayerUpdate* parent_update_owner = nullptr;
	sgre::scene::MotionPlayerUpdateServices update_services;
	update_services.ReadFrameDelta = []() -> float {
		throw std::runtime_error("bounded parent proof has no task delta reader");
	};
	update_services.PrepareFrames = [&](auto traversal, const auto& context) {
		local.Select(traversal, context.Clock.TickCount);
	};
	update_services.ComposeLayers = [&](const auto& context) {
		composition.Compose(local, context.Clock.TickAssigned);
		Compare(local, composition, *expected, false, state_index);
		RequireFailure([&] {
			local.CompleteComposition();
		},
		    "phase");
		ancestry.Compose(composition);
		const std::array<std::size_t, 4> indices{local.State(0).CurrentIndex, local.State(1).CurrentIndex, local.State(2).CurrentIndex, local.State(3).CurrentIndex};
		const std::array<const sgre::scene::MotionStaticSourceBinding*, 4> bindings{&sources.Source(0), nullptr, &sources.Source(2), nullptr};
		stencil.Compose(composition.Layers(), indices, bindings, context.Clock.TickCount, nullptr);
		RequireFailure([&] {
			static_cast<void>(sgre::scene::SelectMotionChildFrameCommands(selected.ChildSlots(3), context.Clock, composition.Changed(3), 0, [] {
			}));
		},
		    "genuine parameter");
		const auto action = sgre::scene::SelectMotionChildFrameCommands(selected.ChildSlots(3), composition, [&] {
			child_phases.reset();
		});
		Require(action != sgre::scene::MotionChildFrameSelection::Reset, "present parameter child selected an empty substitute");
		ancestry.BindChildDeformation(3, child);
		stencil.BindChildStencil(3, child);
		if (!child_phases) {
			auto next_prepared = std::make_unique<sgre::scene::PreparedMotionTimeline>(sgre::scene::PrepareMotionTimeline(*child.Commands().CachedTimeline(), *asset->Model));
			selected.RebuildChildNodes(3, *child.Commands().CachedTimeline(), *next_prepared, slots);
			child_prepared = std::move(next_prepared);
			child_phases = std::make_unique<ChildPhases>(*selected.ChildNodes(3), child, ancestry, catalog, *asset, runtime, slots, expected, state_index, empty_child);
		}
		vertices.Compose();
		RequireFailure([&] {
			local.CompleteComposition();
		},
		    "child");
		Require(local.ParameterBindings()->ControlFlags(3) == expected->Flags, "failed parent finish cleared actual pending parameter flags");
		child_phases->AttemptStaleFinish();
		child_phases->Update(context.Clock.DeltaTicks);
		parent_update_owner->PrependChildEvents(child_phases->UpdateOwner());
		local.CompleteComposition();
		Require(local.ParameterBindings()->ControlFlags(0) == 0 && !local.State(0).FrameDirty, "actual parent finish did not clear its own controls/dirty flags");
	};
	update_services.PublishDrawPackets = [](const auto&) {
		throw std::runtime_error("bounded parent proof does not publish substitute packets");
	};
	update_services.Draw = [] {
		throw std::runtime_error("bounded parent proof does not submit GPU work");
	};
	update_services.DeliverEvent = [](const auto&) {
		throw std::runtime_error("action-free parent proof delivered an action");
	};
	sgre::scene::MotionPlayerUpdate update(commands, update_services);
	parent_update_owner = &update;
	update.PrepareTimeline(timeline);
	for (state_index = 0; state_index < states.size(); ++state_index) {
		expected = &states[state_index];
		const auto [input, flags] = authored_setters[state_index];
		Require(std::bit_cast<std::uint32_t>(input) == std::bit_cast<std::uint32_t>(expected->Input) && flags == expected->Flags, "native fixture raw setter arguments differ from independently authored driver inputs");
		if (state_index != 0) {
			commands.ParameterValues().WriteLocal("param", input, flags);
			RequireFailure([&] {
				static_cast<void>(local.Evaluation(0));
			},
			    "changed after");
		}
		try {
			update.UpdateChild(0);
		} catch (const std::exception& error) {
			throw std::runtime_error("original parameter state " + std::to_string(state_index) + ": " + error.what());
		}
		Require(commands.ClockState().TickCount == 0 && child.Commands().ClockState().TickCount == 0 && commands.ParameterValues().RegisteredParameterCount() == (empty_child ? 1u : 2u) && update.PendingEventCount() == 0 && child_phases->UpdateOwner().PendingEventCount() == 0, "actual parameter graph changed ordinary clocks, exact registrations or action scope");
	}
	child_phases.reset();
	selected.Retire();
	child_prepared.reset();
	Require(commands.ParameterValues().RegisteredParameterCount() == 0, "selected retirement retained actual child parameter registrations");
	std::cout << (empty_child ? "zero-record child phase regression PASS; " : "parameterized child original differential PASS; ") << "18 original null/scalar index cases and required-field rejection match; 40 original parameterized local slot/dirty/coordinate samples match; 8 true parent/child finish transitions retain controls until current geometry and child completion; real atlas upload, no GPU submit or full character claim\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 5, "motion_parameter_local_finish_tests NATIVE_FIXTURE INDEX_FIXTURE SEEK_FIXTURE SHADER_SOURCE");
		CompareIndices(argv[2]);
		CompareSeek(argv[3]);
		Replay(argv[1], argv[4], false);
		Replay(argv[1], argv[4], true);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
