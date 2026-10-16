#include "sgre/scene/motion_stencil_row_policies.hpp"

#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_local_frames.hpp"
#include "sgre/scene/motion_parameter_values.hpp"
#include "sgre/scene/motion_stencil_mask_owner.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
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

	throw std::runtime_error("unsupported stencil ancestry operation was accepted");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue atom;
	atom.Data = std::move(value);

	return atom;
}

class Reader {
public:
	explicit Reader(std::istream& stream)
	    : m_Stream(stream) {
	}
	std::uint8_t Byte() {
		const auto value = m_Stream.get();
		Require(value != std::char_traits<char>::eof(), "short stencil ancestry fixture");

		return static_cast<std::uint8_t>(value);
	}
	std::uint32_t Word() {
		std::uint32_t word = 0;
		for (std::uint32_t shift = 0; shift < 32; shift += 8) {
			word |= static_cast<std::uint32_t>(Byte()) << shift;
		}

		return word;
	}

private:
	std::istream& m_Stream;
};

struct Input {
	std::uint32_t Kind = 0;
	std::uint32_t Parent = 0;
	std::uint32_t Stencil = 0;
	std::uint32_t Inherit = 0;
	bool Visible = true;
	bool Enabled = true;
	bool Hidden = false;
	bool MissingSource = false;
	bool Controlled = false;
	bool Reverse = false;
	float Threshold = 0;
	float Softness = 0;
};

static std::vector<sgre::resource::MotionLayer> Layers(std::span<const Input> inputs, std::uint32_t parent = 0) {
	static constexpr std::array<std::string_view, 6> labels = {"mask1", "hier", "sprite", "child", "mask2", "tail"};
	std::vector<sgre::resource::MotionLayer> output;
	for (std::size_t index = 0; index < inputs.size(); ++index) {
		const auto& input = inputs[index];
		if (input.Parent != parent) {
			continue;
		}
		sgre::resource::MotionLayer layer;
		layer.Label = labels[index];
		layer.Type = input.Kind;
		layer.InheritMask = input.Inherit;
		layer.TransformOrder = {0, 3, 2, 1};
		layer.Properties = {{"parameterize", Atom(std::monostate{})}, {"meshTransform", Atom(std::int64_t{0})}, {"joinTarget", Atom(false)}, {"groundCorrection", Atom(false)}, {"objTriPriority", Atom(std::int64_t{0})}, {"motionIndependentLayerInherit", Atom(false)}, {"stencilType", Atom(static_cast<std::int64_t>(input.Stencil))}};
		if (input.Kind == 12) {
			layer.Properties.emplace_back("stencilCompositeMaskLayerList", Atom(sgre::resource::PsbArray{Atom(std::string("sprite")), Atom(std::string("child")), Atom(std::string("tail")), Atom(std::string("sprite"))}));
		}
		sgre::resource::MotionFrame current;
		current.Type = input.Hidden ? 0u : 2u;
		const sgre::resource::PsbObject stc = {{"wrv", Atom(input.Reverse)}, {"wcc", Atom(std::monostate{})}, {"wrt", Atom(static_cast<double>(input.Threshold))}, {"wsf", Atom(static_cast<double>(input.Softness))}};
		current.Content = Atom(sgre::resource::PsbObject{{"mask", Atom(input.Controlled ? std::int64_t{0x20000000} : std::int64_t{0})}, {"src", Atom(std::string(input.MissingSource ? "missing" : "atlas"))}, {"icon", Atom(std::string("tile"))}, {"stc", Atom(stc)}});
		layer.Frames.push_back(current);
		current.Time = 4;
		const sgre::resource::PsbObject following_stc = {{"wrv", Atom(!input.Reverse)}, {"wcc", Atom(std::monostate{})}, {"wrt", Atom(static_cast<double>(input.Threshold))}, {"wsf", Atom(static_cast<double>(input.Softness))}};
		current.Content = Atom(sgre::resource::PsbObject{{"mask", Atom(input.Controlled ? std::int64_t{0} : std::int64_t{0x20000000})}, {"src", Atom(std::string(input.MissingSource ? "missing" : "atlas"))}, {"icon", Atom(std::string("tile"))}, {"stc", Atom(following_stc)}});
		layer.Frames.push_back(current);
		sgre::resource::MotionFrame terminal;
		terminal.Time = 8;
		layer.Frames.push_back(terminal);
		layer.Children = Layers(inputs, static_cast<std::uint32_t>(index + 1));
		output.push_back(std::move(layer));
	}

	return output;
}

static std::shared_ptr<sgre::resource::MotionResource> Resource(std::span<const Input> inputs) {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "main";
	timeline.LastTime = 8;
	timeline.Layers = Layers(inputs);
	timeline.LayerIndexMap = Atom(sgre::resource::PsbObject{{"sprite", Atom(std::int64_t{2})}, {"child", Atom(std::int64_t{3})}, {"tail", Atom(std::int64_t{5})}});
	sgre::resource::MotionFrame priority;
	priority.Type = 1;
	priority.Content = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1}), Atom(std::int64_t{2}), Atom(std::int64_t{3}), Atom(std::int64_t{4}), Atom(std::int64_t{5})});
	timeline.Priority.push_back(priority);
	priority.Time = 8;
	priority.Content = Atom(std::monostate{});
	timeline.Priority.push_back(priority);
	sgre::resource::MotionObject object;
	object.Name = "a";
	object.Timelines.push_back(std::move(timeline));
	resource->Objects.push_back(std::move(object));
	sgre::resource::MotionSource source;
	source.Name = "atlas";
	source.Texture.SerializedValueOffset = 128;
	source.Texture.Width = 32;
	source.Texture.Height = 64;
	source.Texture.TruncatedWidth = 32;
	source.Texture.TruncatedHeight = 64;
	source.Texture.PixelRange.Size = 32 * 64;
	sgre::resource::MotionIcon icon;
	icon.Name = "tile";
	icon.Width = 32;
	icon.Height = 64;
	icon.OriginX = 3;
	icon.OriginY = 5;
	source.Icons.push_back(icon);
	resource->Sources.push_back(source);

	return resource;
}

static sgre::scene::MotionChildConstructionServices Construction() {
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [](const auto&) {
		return 0.5f;
	};

	return construction;
}

static sgre::scene::MotionChildFrameSlotServices Slots() {
	sgre::scene::MotionChildFrameSlotServices slots;
	slots.EnqueueAction = [](auto, auto) {
		throw std::runtime_error("action-free ancestry fixture queued an action");
	};

	return slots;
}

static void Replay(const char* path) {
	std::ifstream stream(path, std::ios::binary);
	Require(stream.is_open(), "cannot read stencil ancestry fixture");
	Reader reader(stream);
	Require(reader.Byte() == 'M' && reader.Byte() == 'S' && reader.Byte() == 'R' && reader.Byte() == '1', "wrong stencil ancestry fixture magic");
	const auto cases = reader.Word();
	const auto phases = reader.Word();
	Require(cases > 0 && cases <= 64 && phases == 3 && reader.Word() == 6, "native ancestry fixture exceeds its bounded domain");
	std::size_t compared = 0;
	std::size_t transferred = 0;
	std::size_t row_comparisons = 0;
	std::size_t member_comparisons = 0;
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		std::array<Input, 6> inputs{};
		for (auto& input : inputs) {
			input.Kind = reader.Word();
			input.Parent = reader.Word();
			input.Stencil = reader.Word();
			input.Inherit = reader.Word();
			input.Visible = reader.Word() != 0;
			input.Enabled = reader.Word() != 0;
			input.Hidden = reader.Word() != 0;
			input.MissingSource = reader.Word() != 0;
			input.Controlled = reader.Word() != 0;
			input.Reverse = reader.Word() != 0;
			input.Threshold = std::bit_cast<float>(reader.Word());
			input.Softness = std::bit_cast<float>(reader.Word());
		}
		const auto resource = Resource(inputs);
		std::array<std::uint8_t, 32 * 64> pixels{};
		std::size_t backend_owner = 0;
		sgre::scene::TextureCache cache([&](auto, const auto& image) {
			Require(image.Width == 32 && image.Height == 64, "ancestry source import must use the actual first-party decoded texture");
			sgre::render::SpriteTextureHandle handle;
			handle.Generation = 1;
			handle.Owner = &backend_owner;

			return handle;
		},
		    [](auto) {
		    });
		sgre::scene::MotionResourceManager manager(cache);
		sgre::scene::MotionProjectIdentity identity;
		identity.DocumentInstance = 1;
		identity.RootOffset = 128;
		const auto registration = manager.Register(identity, resource, pixels);
		sgre::scene::MotionPlayerCommands commands(manager);
		commands.SetCharacter("a");
		commands.Play("main");
		const auto& timeline = *commands.CachedTimeline();
		const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
		sgre::scene::MotionRootScene root;
		root.SetVisible(true);
		const sgre::scene::MotionPlayerRenderControls render;
		sgre::scene::MotionLayerIdentifierPool identifiers;
		sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, identifiers, Construction(), Slots());
		sgre::scene::MotionLayerStencilAncestry ancestry(selected);
		sgre::scene::MotionStencilRowPolicies policies(selected, ancestry);
		sgre::scene::MotionPriorityCursor priority(prepared);
		std::array<sgre::scene::MotionSourceRegistration, 1> registrations{};
		registrations[0].Project = identity;
		registrations[0].Sources = resource->Sources;
		sgre::scene::PreparedMotionSourceRegistry source_registry(registrations);
		sgre::scene::MotionStaticSourceServices service;
		service.FindTexture = [&](const auto& icon) {
			Require(icon.Project == identity && icon.Source == &resource->Sources[0], "source readiness must use the real selected registration");

			return manager.FindSource(registration, icon.Source->Name);
		};
		std::array<std::unique_ptr<sgre::scene::MotionStaticSourceBinding>, 6> bindings;
		std::array<const sgre::scene::MotionStaticSourceBinding*, 6> sources{};
		for (std::size_t index = 0; index < sources.size(); ++index) {
			if (inputs[index].Kind == 0 || inputs[index].Kind == 12) {
				bindings[index] = std::make_unique<sgre::scene::MotionStaticSourceBinding>(source_registry, identity, service);
				sources[index] = bindings[index].get();
			}
		}
		RequireFailure([&] {
			static_cast<void>(ancestry.State(0));
		},
		    "live composed");
		const auto row_count = reader.Word();
		Require(row_count == 5 && policies.Size() == row_count, "native actual persistent row allocation differs");
		for (std::uint32_t phase = 0; phase < phases; ++phase) {
			const auto position = std::bit_cast<float>(reader.Word());
			priority.Seek(position);
			const auto preparations = reader.Word();
			Require(preparations <= 4, "ancestry native mask-slot decoder trace exceeds bound");
			for (std::uint32_t index = 0; index < preparations; ++index) {
				const auto mask_index = reader.Word();
				const auto slot = reader.Word();
				const auto frame = reader.Word();
				Require(mask_index < 2, "native current mask index exceeds actual owner count");
				selected.StencilMask(mask_index == 0 ? 0u : 4u).PrepareFrame(slot, frame);
			}
			for (const auto layer : {0u, 4u}) {
				const auto slot = reader.Word();
				const auto current = reader.Word();
				const bool force = reader.Word() != 0;
				const bool dirty = reader.Word() != 0;
				const bool enabled = reader.Word() != 0;
				const bool reverse = reader.Word() != 0;
				const auto scale = reader.Word();
				const auto offset = reader.Word();
				auto& mask = selected.StencilMask(layer);
				static_cast<void>(mask.Evaluate(*resource, position, slot, force, dirty));
				const auto& summary = mask.Summary();
				Require(mask.EvaluatedFrameIndex() == current && mask.EvaluatedPosition() == position && summary.Enabled == enabled && summary.ReverseWrite == reverse && std::bit_cast<std::uint32_t>(summary.Scale) == scale && std::bit_cast<std::uint32_t>(summary.Offset) == offset, "actual current mask owner summary/provenance differs");
			}
			std::array<sgre::scene::MotionComposedLayer, 6> numeric{};
			std::array<std::size_t, 6> current{};
			std::array<bool, 6> active{};
			std::array<std::int32_t, 6> inherited{};
			for (std::size_t index = 0; index < numeric.size(); ++index) {
				current[index] = reader.Word();
				numeric[index].FrameHidden = reader.Word() != 0;
				numeric[index].Active = reader.Word() != 0;
				numeric[index].Visible = reader.Word() != 0;
				const bool source_ready = reader.Word() != 0;
				active[index] = reader.Word() != 0;
				Require((reader.Word() != 0) == (inputs[index].Kind == 12), "native summary owner marker differs from exact selected kind");
				inherited[index] = std::bit_cast<std::int32_t>(reader.Word());
				if (bindings[index]) {
					static_cast<void>(bindings[index]->Bind(prepared.Layers[index].Frames[current[index]]));
					Require(bindings[index]->State().Ready == source_ready, "native actual registered source readiness differs");
				}
			}
			const auto transfer_count = reader.Word();
			const auto child_ancestor = std::bit_cast<std::int32_t>(reader.Word());
			ancestry.Compose(numeric, current, sources, position, nullptr);
			for (std::size_t index = 0; index < active.size(); ++index) {
				const auto& state = ancestry.State(index);
				const auto* expected = inherited[index] < 0 ? nullptr : &ancestry.State(static_cast<std::size_t>(inherited[index]));
				if (&state.Node() != &selected.Nodes()[index] || state.Active() != active[index] || state.Ancestor() != expected) {
					const auto actual_ancestor = state.Ancestor() == nullptr ? -1 : static_cast<std::int32_t>(state.Ancestor()->Node().NativeOrdinal - 1);
					throw std::runtime_error("native ancestry case " + std::to_string(case_index) + " phase " + std::to_string(phase) + " node " + std::to_string(index) + " active " + std::to_string(state.Active()) + "/" + std::to_string(active[index]) + " ancestor " + std::to_string(actual_ancestor) + "/" + std::to_string(inherited[index]));
				}
				const auto* mask = inputs[index].Kind == 12 ? &selected.StencilMask(static_cast<std::uint32_t>(index)) : nullptr;
				Require(state.MaskOwner() == mask, "stencil link must retain its exact exclusive current mask owner identity");
				++compared;
			}
			if (transfer_count != 0) {
				Require(transfer_count == 1, "native ancestry phase updated the selected child more than once");
				auto& child = selected.Child(3);
				const auto dirty_before = child.Root().Overrides().Dirty;
				ancestry.BindChildStencil(3, child);
				Require(child.Root().Overrides().Dirty == dirty_before, "stencil pointer transfer must not write numeric root dirty state");
				++transferred;
			}
			const auto* expected_child = child_ancestor < 0 ? nullptr : &ancestry.State(static_cast<std::size_t>(child_ancestor));
			Require(selected.Child(3).InheritedStencil() == expected_child, "native actual child root+3f0 transfer identity differs");
			policies.Publish(numeric, current, sources, position, priority);
			for (const auto& node : selected.Nodes()) {
				if (!node.DrawRow) {
					continue;
				}
				const auto row_index = reader.Word();
				const bool published = reader.Word() != 0;
				const auto stencil_type = reader.Word();
				const bool control_enabled = reader.Word() != 0;
				const auto scale = reader.Word();
				const auto offset = reader.Word();
				const auto ancestor_index = std::bit_cast<std::int32_t>(reader.Word());
				const auto member_count = reader.Word();
				Require(row_index == *node.DrawRow && member_count <= 16, "native row/member storage identity exceeds its actual selected domain");
				const auto& row = policies.Row(row_index);
				const auto* ancestor = ancestor_index < 0 ? nullptr : &policies.Row(static_cast<std::size_t>(ancestor_index));
				if (&row.Node() != &node || row.Published() != published || row.StencilType() != stencil_type || row.ControlEnabled() != control_enabled || std::bit_cast<std::uint32_t>(row.Scale()) != scale || std::bit_cast<std::uint32_t>(row.Offset()) != offset || row.Ancestor() != ancestor || row.Members().size() != member_count) {
					throw std::runtime_error("native stencil row policy differs at case " + std::to_string(case_index) + " phase " + std::to_string(phase) + " row " + std::to_string(row_index));
				}
				for (std::size_t member = 0; member < member_count; ++member) {
					const auto index = reader.Word();
					Require(index < policies.Size() && row.Members()[member] == &policies.Row(index), "native resolved mask row member identity/order/duplicate differs");
					++member_comparisons;
				}
				++row_comparisons;
			}
			if (phase == phases - 1) {
				auto missing = sources;
				missing[4] = nullptr;
				RequireFailure([&] {
					policies.Publish(numeric, current, missing, position, priority);
				},
				    "actual drawing source");
				RequireFailure([&] {
					static_cast<void>(policies.Row(0));
				},
				    "live published");
				policies.Publish(numeric, current, sources, position, priority);
				auto active_child = numeric;
				active_child[3].Active = true;
				RequireFailure([&] {
					policies.Publish(active_child, current, sources, position, priority);
				},
				    "exact completed recursive selected owner");
				policies.Publish(numeric, current, sources, position, priority);
				RequireFailure([&] {
					policies.Publish(numeric, current, sources, position + 1, priority);
				},
				    "exact current retained mask owner");
				policies.Publish(numeric, current, sources, position, priority);
				{
					sgre::scene::MotionPlayerCommands other_commands(manager);
					other_commands.SetCharacter("a");
					other_commands.Play("main");
					sgre::scene::MotionRootScene other_root;
					const sgre::scene::MotionPlayerRenderControls other_render;
					sgre::scene::MotionLayerIdentifierPool other_identifiers;
					sgre::scene::MotionSelectedLayerNodes other(other_commands, other_root, other_render, timeline, prepared, other_identifiers, Construction(), Slots());
					sgre::scene::MotionStencilRowPolicies unrelated(other, ancestry);
					RequireFailure([&] {
						unrelated.Publish(numeric, current, sources, position, priority);
					},
					    "exact selected ancestry/current frame");
				}
				const auto& borrowed = policies.Row(0);
				selected.Retire();
				RequireFailure([&] {
					static_cast<void>(borrowed.Published());
				},
				    "live published");
			}
		}
		Require(identifiers.LiveCount() == 0 && manager.ReferenceCount(registration) == 1, "ancestry must not acquire a resource pin or omit actual selected retirement");
	}
	Require(stream.peek() == std::char_traits<char>::eof(), "unexpected trailing native ancestry fixture bytes");
	std::cout << row_comparisons << " original persistent row policies and " << member_comparisons << " resolved mask members matched; " << compared << " ancestry links and " << transferred << " actual child transfers checked\n";
}

static void TestActualParameterMaskPositions() {
	std::array<Input, 6> inputs{};
	inputs[0].Kind = 12;
	inputs[0].Stencil = 1;
	inputs[0].Controlled = true;
	inputs[0].Threshold = 0.25f;
	inputs[0].Softness = 0.5f;
	inputs[1].Kind = 2;
	inputs[2].Kind = 0;
	inputs[3].Kind = 2;
	inputs[4] = inputs[0];
	inputs[5] = inputs[0];
	const auto resource = Resource(inputs);
	auto& raw_timeline = resource->Objects[0].Timelines[0];
	sgre::resource::PsbArray parameters;
	for (const auto name : {"first", "second"}) {
		parameters.push_back(Atom(sgre::resource::PsbObject{{"id", Atom(std::string(name))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(0.0)}, {"rangeEnd", Atom(8.0)}, {"division", Atom(8.0)}}));
	}
	raw_timeline.Parameters = Atom(std::move(parameters));
	for (const auto layer : {0u, 4u}) {
		for (auto& [name, value] : raw_timeline.Layers[layer].Properties) {
			if (name == "parameterize") {
				value = Atom(layer == 0 ? std::int64_t{0} : std::int64_t{1});
			}
		}
	}

	std::array<std::uint8_t, 32 * 64> pixels{};
	std::size_t backend_owner = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		sgre::render::SpriteTextureHandle handle;
		handle.Generation = 1;
		handle.Owner = &backend_owner;

		return handle;
	},
	    [](auto) {
	    });
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = cache.AllocateDocumentIdentity();
	identity.RootOffset = 128;
	const auto project = manager.Register(identity, resource, pixels);
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("a");
	commands.Play("main");
	const auto& timeline = *commands.CachedTimeline();
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
	sgre::scene::MotionRootScene root;
	root.SetVisible(true);
	const sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionLayerIdentifierPool identifiers;
	sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, identifiers, Construction(), Slots());
	std::array<sgre::scene::MotionSourceRegistration, 1> registrations{};
	registrations[0].Project = identity;
	registrations[0].Sources = resource->Sources;
	sgre::scene::PreparedMotionSourceRegistry registry(registrations);
	sgre::scene::MotionStaticSourceServices source_services;
	source_services.FindTexture = [&](const auto& icon) {
		Require(icon.Project == identity && icon.Source == &resource->Sources[0], "parameter mask source must use its actual registered owner");

		return manager.FindSource(project, icon.Source->Name);
	};
	std::array<std::unique_ptr<sgre::scene::MotionStaticSourceBinding>, 6> bindings;
	std::array<const sgre::scene::MotionStaticSourceBinding*, 6> sources{};
	for (std::size_t index = 0; index < inputs.size(); ++index) {
		if (inputs[index].Kind == 0 || inputs[index].Kind == 12) {
			bindings[index] = std::make_unique<sgre::scene::MotionStaticSourceBinding>(registry, identity, source_services);
			sources[index] = bindings[index].get();
		}
	}
	sgre::scene::MotionLocalFrameServices local_services;
	local_services.BindSource = [&](auto layer, const auto& current, const auto&) {
		Require(layer < bindings.size() && bindings[layer] != nullptr, "parameter mask binding requires the actual drawing layer");
		static_cast<void>(bindings[layer]->Bind(current));
	};
	local_services.EnqueueAction = [](auto, auto) {
		throw std::runtime_error("action-free parameter mask fixture queued an action");
	};
	sgre::scene::MotionLocalFrames local(selected, *resource, local_services);
	sgre::scene::MotionLayerComposition composition(selected, root, false);
	sgre::scene::MotionLayerStencilAncestry ancestry(selected);
	sgre::scene::MotionStencilRowPolicies policies(selected, ancestry);
	sgre::scene::MotionPriorityCursor priority(prepared);
	static constexpr float position = 1;
	const std::array<std::array<float, 2>, 2> parameter_positions{{{2, 6}, {6, 2}}};
	for (const auto& positions : parameter_positions) {
		commands.ParameterValues().WriteLocal("first", positions[0], 0);
		commands.ParameterValues().WriteLocal("second", positions[1], 0);
		local.Select(sgre::scene::MotionFrameTraversal::Seek, position);
		composition.Compose(local, true);
		const auto numeric = composition.Layers();
		std::array<std::size_t, 6> current{};
		for (std::uint32_t layer = 0; layer < current.size(); ++layer) {
			current[layer] = local.State(layer).CurrentIndex;
		}
		Require(local.Position(0) == positions[0] && local.Position(4) == positions[1] && local.Position(5) == position, "actual selected records must differ from each other and ordinary time");
		Require(current[0] != current[4], "distinct parameter positions must select distinct physical mask frames");
		ancestry.Compose(numeric, current, sources, position, nullptr);
		policies.PrepareRecursiveFrame(numeric);
		priority.Seek(position);
		policies.Publish(numeric, current, sources, position, priority);
		for (const auto layer : {0u, 4u, 5u}) {
			const auto& mask = selected.StencilMask(layer);
			const auto& summary = mask.Summary();
			const auto& row = policies.Row(*selected.Nodes()[layer].DrawRow);
			Require(row.Published() && row.ControlEnabled() == summary.Enabled && mask.EvaluatedPosition() == local.Position(layer), "published mask must consume its own actual parameter position and retained summary");
			if (summary.Enabled) {
				Require(row.Scale() == summary.Scale && row.Offset() == summary.Offset, "actual retained mask coefficients must reach the same persistent row");
			}
		}

		// The owner and physical-slot guards still reject a genuine mask evaluated at the wrong local position.
		auto& mask = selected.StencilMask(0);
		static_cast<void>(mask.Evaluate(*resource, local.Position(0) + 0.25f, local.State(0).CurrentSlot, true, true));
		RequireFailure([&] {
			policies.Publish(numeric, current, sources, position, priority);
		},
		    "exact current retained mask owner");
		static_cast<void>(mask.Evaluate(*resource, local.Position(0), local.State(0).CurrentSlot, true, true));
		policies.Publish(numeric, current, sources, position, priority);
		auto wrong_frame = current;
		wrong_frame[0] = 1 - current[0];
		RequireFailure([&] {
			policies.Publish(numeric, wrong_frame, sources, position, priority);
		},
		    "exact current retained mask owner");
		policies.Publish(numeric, current, sources, position, priority);
	}
	selected.Retire();
	Require(identifiers.LiveCount() == 0 && manager.ReferenceCount(project) == 1, "parameter row retirement must preserve only its actual registration");
	std::cout << "actual parameterized mask rows preserve independent local positions and physical-slot guards PASS\n";
}

static void TestRequiredProducerGuards() {
	std::array<Input, 6> inputs{};
	inputs[0].Kind = 12;
	inputs[0].Stencil = 5;
	inputs[0].Controlled = true;
	inputs[0].Threshold = 0.25f;
	inputs[0].Softness = 0.5f;
	inputs[1].Kind = 2;
	inputs[2].Kind = 0;
	inputs[3].Kind = 3;
	inputs[3].Hidden = true;
	inputs[4] = inputs[0];
	inputs[5].Kind = 0;
	const auto resource = Resource(inputs);
	std::array<std::uint8_t, 32 * 64> pixels{};
	std::size_t backend_owner = 0;
	sgre::scene::TextureCache cache([&](auto, const auto&) {
		sgre::render::SpriteTextureHandle handle;
		handle.Generation = 1;
		handle.Owner = &backend_owner;

		return handle;
	},
	    [](auto) {
	    });
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = 1;
	identity.RootOffset = 128;
	static_cast<void>(manager.Register(identity, resource, pixels));
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("a");
	commands.Play("main");
	const auto& timeline = *commands.CachedTimeline();
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
	sgre::scene::MotionRootScene root;
	const sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionLayerIdentifierPool identifiers;
	sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, identifiers, Construction(), Slots());
	sgre::scene::MotionLayerStencilAncestry ancestry(selected);
	sgre::scene::MotionStencilRowPolicies policies(selected, ancestry);
	sgre::scene::MotionPriorityCursor priority(prepared);
	sgre::scene::MotionStencilRowState unbound_row;
	RequireFailure([&] {
		static_cast<void>(unbound_row.Node());
	},
	    "no actual selected owner");
	RequireFailure([&] {
		policies.Publish({}, {}, {}, 0, priority);
	},
	    "every actual");
	sgre::scene::MotionStencilLayerState unbound;
	RequireFailure([&] {
		static_cast<void>(unbound.Node());
	},
	    "no actual");
	RequireFailure([&] {
		ancestry.Compose({}, {}, {}, 0, nullptr);
	},
	    "every actual");
	std::array<sgre::scene::MotionComposedLayer, 6> numeric{};
	std::array<std::size_t, 6> current{};
	std::array<const sgre::scene::MotionStaticSourceBinding*, 6> missing_sources{};
	for (std::size_t index = 0; index < numeric.size(); ++index) {
		numeric[index].FrameHidden = prepared.Layers[index].Frames[0].Hidden;
	}
	RequireFailure([&] {
		ancestry.Compose(numeric, current, missing_sources, 0, nullptr);
	},
	    "initial numeric");
	RequireFailure([&] {
		static_cast<void>(ancestry.State(0));
	},
	    "live composed");
	selected.Retire();
	Require(identifiers.LiveCount() == 0, "failed ancestry phases must preserve exact selected retirement");
	std::cout << "stencil row policies require actual selected/source/mask owners PASS\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: stencil_ancestry_tests [native fixture]");
		TestRequiredProducerGuards();
		TestActualParameterMaskPositions();
		if (argc == 2) {
			Replay(argv[1]);
		}

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
