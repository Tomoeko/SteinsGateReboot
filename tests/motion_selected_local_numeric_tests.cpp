#include "sgre/scene/motion_stencil_row_policies.hpp"

#include "sgre/scene/motion_child_frame_selection.hpp"
#include "sgre/scene/motion_child_root.hpp"
#include "sgre/scene/motion_local_frames.hpp"
#include "sgre/scene/motion_selected_mask_packets.hpp"
#include "sgre/scene/motion_selected_quad_geometry.hpp"

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
	explicit Reader(std::istream& stream) : m_Stream(stream) {
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
	bool JoinTarget = false;
	bool ChildNumeric = false;
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
		layer.Properties = {{"parameterize", Atom(std::monostate{})}, {"meshTransform", Atom(std::int64_t{0})}, {"joinTarget", Atom(input.JoinTarget)}, {"groundCorrection", Atom(false)}, {"objTriPriority", Atom(std::int64_t{0})}, {"motionIndependentLayerInherit", Atom(false)}, {"stencilType", Atom(static_cast<std::int64_t>(input.Stencil))}};
		if (input.Kind == 12) {
			layer.Properties.emplace_back("stencilCompositeMaskLayerList", Atom(sgre::resource::PsbArray{Atom(std::string("sprite")), Atom(std::string("child")), Atom(std::string("tail")), Atom(std::string("sprite"))}));
		}
		sgre::resource::MotionFrame current;
		current.Type = input.Hidden ? 0u : 2u;
		const sgre::resource::PsbObject stc = {{"wrv", Atom(input.Reverse)}, {"wcc", Atom(std::monostate{})}, {"wrt", Atom(static_cast<double>(input.Threshold))}, {"wsf", Atom(static_cast<double>(input.Softness))}};
		current.Content = Atom(sgre::resource::PsbObject{{"mask", Atom(input.Controlled ? std::int64_t{0x20000000} : std::int64_t{0})}, {"src", Atom(std::string(input.MissingSource ? "missing" : "atlas"))}, {"icon", Atom(std::string("tile"))}, {"stc", Atom(stc)}});
		if (input.Kind == 3 && input.ChildNumeric) {
			std::get<sgre::resource::PsbObject>(current.Content.Data).emplace_back("motion", Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0})}, {"timeOffset", Atom(0.0)}}));
			std::get<sgre::resource::PsbObject>(current.Content.Data).emplace_back("coord", Atom(sgre::resource::PsbArray{Atom(5.0), Atom(7.0), Atom(1.0)}));
			std::get<sgre::resource::PsbObject>(current.Content.Data).emplace_back("opa", Atom(std::int64_t{127}));
			std::get<sgre::resource::PsbObject>(current.Content.Data)[0].second = Atom(std::int64_t{0x80402});
		}
		layer.Frames.push_back(current);
		current.Time = 4;
		const sgre::resource::PsbObject following_stc = {{"wrv", Atom(!input.Reverse)}, {"wcc", Atom(std::monostate{})}, {"wrt", Atom(static_cast<double>(input.Threshold))}, {"wsf", Atom(static_cast<double>(input.Softness))}};
		current.Content = Atom(sgre::resource::PsbObject{{"mask", Atom(input.Controlled ? std::int64_t{0} : std::int64_t{0x20000000})}, {"src", Atom(std::string(input.MissingSource ? "missing" : "atlas"))}, {"icon", Atom(std::string("tile"))}, {"stc", Atom(following_stc)}});
		if (input.Kind == 3 && input.ChildNumeric) {
			std::get<sgre::resource::PsbObject>(current.Content.Data).emplace_back("motion", Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0})}, {"timeOffset", Atom(0.0)}}));
			std::get<sgre::resource::PsbObject>(current.Content.Data).emplace_back("coord", Atom(sgre::resource::PsbArray{Atom(8.0), Atom(11.0), Atom(2.0)}));
			std::get<sgre::resource::PsbObject>(current.Content.Data).emplace_back("opa", Atom(std::int64_t{96}));
			std::get<sgre::resource::PsbObject>(current.Content.Data)[0].second = Atom(std::int64_t{0x80402});
		}
		layer.Frames.push_back(current);
		sgre::resource::MotionFrame terminal;
		terminal.Time = 8;
		layer.Frames.push_back(terminal);
		layer.Children = Layers(inputs, static_cast<std::uint32_t>(index + 1));
		output.push_back(std::move(layer));
	}

	return output;
}

static std::shared_ptr<sgre::resource::MotionResource> Resource(std::span<const Input> inputs, std::span<const std::uint32_t> priority_order) {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "main";
	timeline.LastTime = 8;
	timeline.Layers = Layers(inputs);
	timeline.LayerIndexMap = Atom(sgre::resource::PsbObject{{"sprite", Atom(std::int64_t{2})}, {"child", Atom(std::int64_t{3})}, {"tail", Atom(std::int64_t{5})}});
	sgre::resource::MotionFrame priority;
	priority.Type = 1;
	sgre::resource::PsbArray order;
	for (const auto index : priority_order) {
		order.push_back(Atom(static_cast<std::int64_t>(index)));
	}
	priority.Content = Atom(order);
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
	source.Texture.Width = 64;
	source.Texture.Height = 128;
	source.Texture.TruncatedWidth = 64;
	source.Texture.TruncatedHeight = 128;
	source.Texture.PixelRange.Size = 64 * 128;
	sgre::resource::MotionIcon icon;
	icon.Name = "tile";
	icon.Left = 8;
	icon.Top = 16;
	icon.AttributeFlags = 2;
	icon.Width = 32;
	icon.Height = 64;
	icon.OriginX = 3;
	icon.OriginY = 5;
	source.Icons.push_back(icon);
	resource->Sources.push_back(source);

	return resource;
}

static sgre::scene::MotionChildFrameSlotServices Slots() {
	sgre::scene::MotionChildFrameSlotServices slots;
	slots.EnqueueAction = [](auto, auto) {
		throw std::runtime_error("action-free ancestry fixture queued an action");
	};

	return slots;
}

struct RowObservation {
	std::uint32_t Index = 0;
	bool Published = false;
	std::uint32_t StencilType = 0;
	bool ControlEnabled = false;
	std::uint32_t Scale = 0;
	std::uint32_t Offset = 0;
	std::int32_t Ancestor = -1;
	std::vector<std::uint32_t> Members;
};

static RowObservation ReadRow(Reader& reader) {
	RowObservation row;
	row.Index = reader.Word();
	row.Published = reader.Word() != 0;
	row.StencilType = reader.Word();
	row.ControlEnabled = reader.Word() != 0;
	row.Scale = reader.Word();
	row.Offset = reader.Word();
	row.Ancestor = std::bit_cast<std::int32_t>(reader.Word());
	const auto count = reader.Word();
	Require(count <= 16, "native recursive row members exceed bound");
	for (std::size_t index = 0; index < count; ++index) {
		row.Members.push_back(reader.Word());
	}

	return row;
}

struct LayerObservation {
	sgre::scene::MotionComposedLayer Numeric;
	std::size_t Current = 0;
	bool SourceReady = false;
	std::uint8_t Changed = 0;
	bool Geometry = false;
	std::array<std::uint32_t, 8> Positions{};
};

static LayerObservation ReadLayer(Reader& reader, const sgre::scene::PreparedMotionLayer& prepared) {
	LayerObservation layer;
	layer.Current = reader.Word();
	Require(layer.Current < prepared.Frames.size(), "native recursive current frame exceeds preparation");
	layer.Numeric.FrameHidden = reader.Word() != 0;
	layer.Numeric.Active = reader.Word() != 0;
	layer.Numeric.Visible = reader.Word() != 0;
	layer.SourceReady = reader.Word() != 0;
	layer.Changed = static_cast<std::uint8_t>(reader.Word());
	auto& hierarchy = layer.Numeric.Hierarchy;
	hierarchy.Attributes = sgre::scene::SamplePreparedMotionFrame(prepared.Frames[layer.Current]);
	for (auto& coordinate : hierarchy.Attributes.Coordinate) {
		coordinate = std::bit_cast<float>(reader.Word());
	}
	hierarchy.Attributes.Angle = std::bit_cast<float>(reader.Word());
	hierarchy.Attributes.ZoomX = std::bit_cast<float>(reader.Word());
	hierarchy.Attributes.ZoomY = std::bit_cast<float>(reader.Word());
	hierarchy.Attributes.ShearX = std::bit_cast<float>(reader.Word());
	hierarchy.Attributes.ShearY = std::bit_cast<float>(reader.Word());
	hierarchy.Attributes.Opacity = reader.Word();
	hierarchy.Transform.XX = std::bit_cast<float>(reader.Word());
	hierarchy.Transform.XY = std::bit_cast<float>(reader.Word());
	hierarchy.Transform.YX = std::bit_cast<float>(reader.Word());
	hierarchy.Transform.YY = std::bit_cast<float>(reader.Word());
	hierarchy.ModulationColor = reader.Word();
	for (auto& color : hierarchy.Attributes.Colors) {
		color = reader.Word();
	}
	layer.Geometry = reader.Word() != 0;
	for (auto& position : layer.Positions) {
		position = reader.Word();
	}

	return layer;
}

static void RequireNumeric(const sgre::scene::MotionComposedLayer& actual, const sgre::scene::MotionComposedLayer& expected, std::uint32_t case_index, std::uint32_t phase, std::size_t index) {
	const auto context = "produced numeric differs native case " + std::to_string(case_index) + " phase " + std::to_string(phase) + " index " + std::to_string(index);
	Require(actual.FrameHidden == expected.FrameHidden && actual.Active == expected.Active && actual.Visible == expected.Visible, context + " gates");
	const auto& attributes = actual.Hierarchy.Attributes;
	const auto& wanted = expected.Hierarchy.Attributes;
	Require(attributes.Opacity == wanted.Opacity && attributes.Colors == wanted.Colors, context + " opacity/corner colors");
	const auto& transform = actual.Hierarchy.Transform;
	const auto& target = expected.Hierarchy.Transform;
	const std::array<float, 12> actual_fields = {attributes.Coordinate[0], attributes.Coordinate[1], attributes.Coordinate[2], attributes.Angle, attributes.ZoomX, attributes.ZoomY, attributes.ShearX, attributes.ShearY, transform.XX, transform.XY, transform.YX, transform.YY};
	const std::array<float, 12> expected_fields = {wanted.Coordinate[0], wanted.Coordinate[1], wanted.Coordinate[2], wanted.Angle, wanted.ZoomX, wanted.ZoomY, wanted.ShearX, wanted.ShearY, target.XX, target.XY, target.YX, target.YY};
	for (std::size_t field = 0; field < actual_fields.size(); ++field) {
		Require(std::bit_cast<std::uint32_t>(actual_fields[field]) == std::bit_cast<std::uint32_t>(expected_fields[field]), context + " field " + std::to_string(field) + " word " + std::to_string(std::bit_cast<std::uint32_t>(actual_fields[field])) + "/" + std::to_string(std::bit_cast<std::uint32_t>(expected_fields[field])));
	}
	Require(actual.Hierarchy.ModulationColor == expected.Hierarchy.ModulationColor, context + " modulation");
}

static void AddChildResource(sgre::resource::MotionResource& resource, bool join_target) {
	sgre::resource::MotionObject object;
	object.Name = "atlas";
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "tile";
	timeline.LastTime = 8;
	std::array<Input, 2> inputs{};
	for (auto& input : inputs) {
		input.Kind = 0;
		input.JoinTarget = join_target;
	}
	timeline.Layers = Layers(inputs);
	for (std::size_t index = 0; index < timeline.Layers.size(); ++index) {
		auto& layer = timeline.Layers[index];
		layer.Frames.resize(3);
		layer.Frames[1].Time = 4;
		layer.Frames[2].Time = 8;
		layer.Frames[2].Type = 0;
		sgre::resource::PsbArray coordinate{Atom(static_cast<double>(index * 10 + 3)), Atom(static_cast<double>(index * 5 + 7)), Atom(static_cast<double>(index))};
		layer.Frames[0].Content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x402})}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}, {"coord", Atom(coordinate)}, {"opa", Atom(std::int64_t{255})}});
		layer.Frames[1].Content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x402})}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}, {"coord", Atom(coordinate)}, {"opa", Atom(static_cast<std::int64_t>(96 + index * 64))}});
	}
	sgre::resource::MotionFrame priority;
	priority.Type = 1;
	priority.Content = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{1})});
	timeline.Priority.push_back(priority);
	priority.Time = 8;
	priority.Content = Atom(std::monostate{});
	timeline.Priority.push_back(priority);
	object.Timelines.push_back(std::move(timeline));
	resource.Objects.push_back(std::move(object));
}

static const sgre::scene::MotionStencilRowState& ResolveRow(std::size_t index, const sgre::scene::MotionStencilRowPolicies& parent, const sgre::scene::MotionStencilRowPolicies& child) {
	if (index < parent.Size()) {
		return parent.Row(index);
	}

	return child.Row(index - parent.Size());
}

static void Replay(const char* path) {
	std::ifstream stream(path, std::ios::binary);
	Require(stream.is_open(), "cannot read native recursive row fixture");
	Reader reader(stream);
	Require(reader.Byte() == 'M', "wrong native numeric fixture magic");
	const auto family = reader.Byte();
	const bool child_numeric = family == 'N';
	Require((family == 'J' || child_numeric) && reader.Byte() == 'C' && reader.Byte() == '1', "wrong native numeric fixture family");
	const auto cases = reader.Word();
	Require(cases > 0 && cases <= 64 && reader.Word() == 3 && reader.Word() == 6, "native recursive fixture exceeds bounded scope");
	std::size_t row_comparisons = 0;
	std::size_t member_comparisons = 0;
	std::size_t geometry_comparisons = 0;
	std::size_t queue_comparisons = 0;
	std::size_t packet_comparisons = 0;
	std::size_t numeric_comparisons = 0;
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
		for (auto& word : seed) {
			word = reader.Word();
		}
		sgre::scene::MotionSharedRandom random(seed);
		const auto constructor_samples = reader.Word();
		Require(constructor_samples > 0 && constructor_samples <= 8, "native constructor random sample count exceeds bound");
		for (std::size_t index = 1; index < constructor_samples; ++index) {
			static_cast<void>(random.NextUnitFloat());
		}
		sgre::scene::MotionChildConstructionServices construction;
		construction.ReadManagerRandom = [&](const auto&) {
			return random.NextUnitFloat();
		};
		std::array<std::uint32_t, 6> priority_order{};
		for (auto& index : priority_order) {
			index = reader.Word();
			Require(index < 6, "native recursive priority exceeds selected count");
		}
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
			input.JoinTarget = reader.Word() != 0;
			input.ChildNumeric = child_numeric;
		}
		Require(reader.Word() == 5, "native recursive parent row count differs");
		const auto resource = Resource(inputs, priority_order);
		AddChildResource(*resource, inputs[3].JoinTarget);
		std::array<std::uint8_t, 64 * 128> pixels{};
		pixels.fill(255);
		std::size_t backend_owner = 0;
		sgre::scene::TextureCache cache([&](auto, const auto& image) {
			Require(image.Width == 64 && image.Height == 128, "recursive source must use the actual first-party decoded atlas");
			sgre::render::SpriteTextureHandle texture;
			texture.Owner = &backend_owner;
			texture.Generation = 1;

			return texture;
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
		sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, timeline, prepared, identifiers, construction, Slots());
		for (std::size_t index = 0; index < inputs.size(); ++index) {
			Require(selected.Nodes()[index].JoinTarget == inputs[index].JoinTarget, "actual child JOIN eligibility differs from original construction metadata");
		}
		sgre::scene::MotionLayerStencilAncestry ancestry(selected);
		sgre::scene::MotionStencilRowPolicies policies(selected, ancestry);
		sgre::scene::MotionSelectedQuadGeometry geometries(selected, policies);
		sgre::scene::MotionPriorityCursor priority(prepared);
		auto& child = selected.Child(3);
		child.Commands().SetCharacter("atlas");
		child.Commands().Play("tile");
		Require(child.Commands().CachedTimeline() != nullptr, "actual child command selector failed its registered timeline");
		const auto& child_timeline = *child.Commands().CachedTimeline();
		const auto child_prepared = sgre::scene::PrepareMotionTimeline(child_timeline, *resource);
		selected.RebuildChildNodes(3, child_timeline, child_prepared, Slots());
		auto* child_nodes = selected.ChildNodes(3);
		Require(child_nodes != nullptr, "actual selected child tree was not rebuilt");
		{
			sgre::scene::MotionLayerStencilAncestry child_ancestry(*child_nodes);
			sgre::scene::MotionStencilRowPolicies child_policies(*child_nodes, child_ancestry, &policies);
			sgre::scene::MotionSelectedQuadGeometry child_geometries(*child_nodes, child_policies, &geometries);
			sgre::scene::MotionPriorityCursor child_priority(child_prepared);
			RequireFailure([&] {
				static_cast<void>(child_geometries.Allocated(0));
			},
			    "actual live completed selected owner");
			policies.BindChildRows(3, child_policies);
			std::array<sgre::scene::MotionSourceRegistration, 1> registrations{};
			registrations[0].Project = identity;
			registrations[0].Sources = resource->Sources;
			sgre::scene::PreparedMotionSourceRegistry registry(registrations);
			sgre::scene::MotionStaticSourceServices source_services;
			source_services.FindTexture = [&](const auto& icon) {
				return manager.FindSource(registration, icon.Source->Name);
			};
			std::array<std::uint8_t, 8> source_changed{};
			std::array<std::unique_ptr<sgre::scene::MotionStaticSourceBinding>, 8> bindings;
			std::array<const sgre::scene::MotionStaticSourceBinding*, 8> sources{};
			for (std::size_t index = 0; index < sources.size(); ++index) {
				if (index >= 6 || inputs[index].Kind == 0 || inputs[index].Kind == 12) {
					bindings[index] = std::make_unique<sgre::scene::MotionStaticSourceBinding>(registry, identity, source_services);
					sources[index] = bindings[index].get();
				}
			}
			sgre::scene::MotionLocalFrameServices parent_services;
			parent_services.EnqueueAction = Slots().EnqueueAction;
			parent_services.BindSource = [&](std::uint32_t layer, const auto& frame, const auto&) {
				source_changed[layer] = static_cast<std::uint8_t>(bindings[layer]->Bind(frame));
			};
			sgre::scene::MotionLocalFrameServices child_services;
			child_services.EnqueueAction = Slots().EnqueueAction;
			child_services.BindSource = [&](std::uint32_t layer, const auto& frame, const auto&) {
				source_changed[layer + 6] = static_cast<std::uint8_t>(bindings[layer + 6]->Bind(frame));
			};
			RequireFailure([&] {
				const auto unrelated_resource = *resource;
				sgre::scene::MotionLocalFrames foreign_resource(selected, unrelated_resource, parent_services);
			},
			    "actual registered selected resource");
			sgre::scene::MotionLocalFrames parent_local(selected, *resource, parent_services);
			sgre::scene::MotionLocalFrames child_local(*child_nodes, *resource, child_services);
			RequireFailure([&] {
				sgre::scene::MotionLocalFrames duplicate_local(selected, *resource, parent_services);
			},
			    "exact exclusive owner");
			RequireFailure([&] {
				sgre::scene::MotionRootScene foreign_root;
				sgre::scene::MotionLayerComposition foreign_composition(selected, foreign_root, false);
			},
			    "actual live selected preparation and containing root");
			RequireFailure([&] {
				sgre::scene::MotionLayerComposition foreign_child_policy(*child_nodes, child.Root(), child.IndependentLayerInherit());
			},
			    "actual constructor inheritance policy");
			sgre::scene::MotionLayerComposition parent_composition(selected, root, false);
			sgre::scene::MotionLayerComposition child_composition(*child_nodes, child.Root(), !child.IndependentLayerInherit());
			for (std::uint32_t layer = 0; layer < inputs.size(); ++layer) {
				sgre::scene::MotionLayerOverrides overrides;
				overrides.Visible = inputs[layer].Visible;
				overrides.Enabled = inputs[layer].Enabled;
				parent_composition.SetOverrides(layer, overrides);
			}
			std::unique_ptr<sgre::scene::MotionSelectedMaskPackets> packets;
			for (std::uint32_t phase = 0; phase < 3; ++phase) {
				const auto position = std::bit_cast<float>(reader.Word());
				const auto preparations = reader.Word();
				Require(preparations <= 4, "native recursive mask preparation count exceeds bound");
				for (std::size_t index = 0; index < preparations; ++index) {
					for (std::size_t field = 0; field < 3; ++field) {
						static_cast<void>(reader.Word());
					}
				}
				std::array<std::array<std::uint32_t, 8>, 2> expected_summaries{};
				for (auto& summary : expected_summaries) {
					for (auto& field : summary) {
						field = reader.Word();
					}
				}
				for (std::size_t index = 0; index < 6; ++index) {
					for (std::size_t word = 0; word < 8; ++word) {
						static_cast<void>(reader.Word());
					}
				}
				Require(reader.Word() == 1, "native recursive phase must execute its actual child transfer");
				const auto child_ancestor = std::bit_cast<std::int32_t>(reader.Word());
				std::array<RowObservation, 7> expected_rows{};
				for (std::size_t index = 0; index < 5; ++index) {
					expected_rows[index] = ReadRow(reader);
				}
				Require(reader.Word() == 2 && reader.Word() == 2, "native actual child selected/row counts differ");
				const auto child_position = std::bit_cast<float>(reader.Word());
				Require(reader.Word() != 0, "native registered child must remain playing");
				std::array<std::uint32_t, 8> expected_root{};
				for (auto& word : expected_root) {
					word = reader.Word();
				}
				std::array<sgre::scene::MotionComposedLayer, 8> numeric{};
				std::array<std::size_t, 8> current{};
				std::array<std::uint8_t, 8> changed{};
				std::array<LayerObservation, 8> observations{};
				for (std::size_t index = 0; index < 8; ++index) {
					const auto& layer = index < 6 ? prepared.Layers[index] : child_prepared.Layers[index - 6];
					observations[index] = ReadLayer(reader, layer);
				}
				for (std::size_t index = 5; index < 7; ++index) {
					expected_rows[index] = ReadRow(reader);
				}
				const auto global_count = reader.Word();
				Require(global_count <= 16, "native global queue exceeds bound");
				std::vector<std::uint32_t> global_rows;
				for (std::size_t index = 0; index < global_count; ++index) {
					global_rows.push_back(reader.Word());
				}

				std::array<std::array<std::uint32_t, 4>, 7> expected_commands{};
				for (auto& command : expected_commands) {
					for (auto& field : command) {
						field = reader.Word();
					}
				}

				source_changed.fill(0);
				commands.SetTickCount(position);
				parent_local.Select(sgre::scene::MotionFrameTraversal::Seek, commands.ClockState().TickCount);
				parent_composition.Compose(parent_local, commands.ClockState().TickAssigned);
				const auto parent_numeric = parent_composition.Layers();
				const auto command_selection = sgre::scene::SelectMotionChildFrameCommands(selected.ChildSlots(3), commands.ClockState(), parent_composition.Changed(3), 0, [&] {
					throw std::runtime_error("actual recursive witness unexpectedly reset its selected child");
				});
				Require(command_selection == sgre::scene::MotionChildFrameSelection::BindRoot && child.Commands().ClockState().TickCount == child_position, "produced actual child timing differs from native selected frame commands");
				sgre::scene::BindMotionChildRoot(child.Root(), prepared.Layers[3], selected.ChildSlots(3).CurrentFrame(), parent_numeric[3], selected.ChildSlots(3).CurrentControl(), 0);
				child_local.Select(sgre::scene::MotionFrameTraversal::Seek, child.Commands().ClockState().TickCount);
				child_composition.Compose(child_local, child.Commands().ClockState().TickAssigned);
				const auto child_numeric = child_composition.Layers();
				for (std::size_t index = 0; index < numeric.size(); ++index) {
					const auto local_index = static_cast<std::uint32_t>(index < 6 ? index : index - 6);
					const auto& local = index < 6 ? parent_local : child_local;
					const auto& composition = index < 6 ? parent_composition : child_composition;
					numeric[index] = composition.Layers()[local_index];
					current[index] = local.State(local_index).CurrentIndex;
					changed[index] = static_cast<std::uint8_t>(composition.Changed(local_index) || source_changed[index]);
					RequireNumeric(numeric[index], observations[index].Numeric, case_index, phase, index);
					++numeric_comparisons;
					Require(current[index] == observations[index].Current && changed[index] == observations[index].Changed && (!bindings[index] || bindings[index]->State().Ready == observations[index].SourceReady), "produced selected cursor/source/changed differs from native case " + std::to_string(case_index) + " phase " + std::to_string(phase) + " index " + std::to_string(index));
				}
				for (std::size_t index = 0; index < expected_summaries.size(); ++index) {
					const auto layer = index == 0 ? 0u : 4u;
					const auto& expected = expected_summaries[index];
					const auto& mask = selected.StencilMask(layer);
					const auto& summary = mask.Summary();
					Require(mask.EvaluatedFrameIndex() == expected[1] && parent_local.State(layer).CurrentSlot == expected[0] && summary.Enabled == (expected[4] != 0) && summary.ReverseWrite == (expected[5] != 0) && std::bit_cast<std::uint32_t>(summary.Scale) == expected[6] && std::bit_cast<std::uint32_t>(summary.Offset) == expected[7], "produced current selected stencil summary differs from native");
				}
				const auto parent_current = std::span(current).first(6);
				const auto child_current = std::span(current).subspan(6);
				const auto parent_sources = std::span(sources).first(6);
				const auto child_sources = std::span(sources).subspan(6);
				ancestry.Compose(parent_numeric, parent_current, parent_sources, position, nullptr);
				ancestry.BindChildStencil(3, child);
				const auto* expected_stencil = child_ancestor < 0 ? nullptr : &ancestry.State(static_cast<std::size_t>(child_ancestor));
				Require(child.InheritedStencil() == expected_stencil, "actual recursive containing-child stencil transfer differs");

				const auto& child_root = child.Root().Frame().Hierarchy;
				const std::array<float, 7> root_values = {child_root.Attributes.Coordinate[0], child_root.Attributes.Coordinate[1], child_root.Attributes.Coordinate[2], child_root.Transform.XX, child_root.Transform.XY, child_root.Transform.YX, child_root.Transform.YY};
				for (std::size_t index = 0; index < root_values.size(); ++index) {
					Require(std::bit_cast<std::uint32_t>(root_values[index]) == expected_root[index], "actual child root numeric pose differs from original binding");
				}
				Require(child_root.ModulationColor == expected_root[7], "actual child root modulation differs");
				child_ancestry.Compose(child_numeric, child_current, child_sources, child_position, child.InheritedStencil());
				policies.PrepareRecursiveFrame(parent_numeric);
				child_policies.PrepareRecursiveFrame(child_numeric);
				geometries.Compose(parent_numeric, parent_current, parent_sources, std::span(changed).first(6), 0.25f);
				child_geometries.Compose(child_numeric, child_current, child_sources, std::span(changed).subspan(6), 0.25f);
				priority.Seek(position);
				child_priority.Seek(child_position);
				child_policies.Publish(child_numeric, child_current, child_sources, child_position, child_priority);
				policies.Publish(parent_numeric, parent_current, parent_sources, position, priority);

				for (std::size_t index = 0; index < 7; ++index) {
					const auto& expected = expected_rows[index];
					const auto& row = ResolveRow(index, policies, child_policies);
					const auto* ancestor = expected.Ancestor < 0 ? nullptr : &ResolveRow(static_cast<std::size_t>(expected.Ancestor), policies, child_policies);
					Require(expected.Index == index && row.Published() == expected.Published && row.StencilType() == expected.StencilType && row.ControlEnabled() == expected.ControlEnabled && std::bit_cast<std::uint32_t>(row.Scale()) == expected.Scale && std::bit_cast<std::uint32_t>(row.Offset()) == expected.Offset && row.Ancestor() == ancestor && row.Members().size() == expected.Members.size(), "native recursive row differs case " + std::to_string(case_index) + " phase " + std::to_string(phase) + " row " + std::to_string(index) + " published " + std::to_string(row.Published()) + "/" + std::to_string(expected.Published) + " type " + std::to_string(row.StencilType()) + "/" + std::to_string(expected.StencilType) + " enabled " + std::to_string(row.ControlEnabled()) + "/" + std::to_string(expected.ControlEnabled) + " scale " + std::to_string(std::bit_cast<std::uint32_t>(row.Scale())) + "/" + std::to_string(expected.Scale) + " offset " + std::to_string(std::bit_cast<std::uint32_t>(row.Offset())) + "/" + std::to_string(expected.Offset) + " members " + std::to_string(row.Members().size()) + "/" + std::to_string(expected.Members.size()));
					for (std::size_t member = 0; member < expected.Members.size(); ++member) {
						Require(row.Members()[member] == &ResolveRow(expected.Members[member], policies, child_policies), "native recursive member row identity/order/duplicate differs");
						++member_comparisons;
					}
					++row_comparisons;
				}
				for (std::size_t index = 0; index < 8; ++index) {
					auto& geometry_owner = index < 6 ? geometries : child_geometries;
					const auto local_index = index < 6 ? index : index - 6;
					Require(geometry_owner.Allocated(local_index) == observations[index].Geometry, "native recursive geometry allocation differs at case " + std::to_string(case_index) + " phase " + std::to_string(phase) + " node " + std::to_string(index) + " actual/expected " + std::to_string(geometry_owner.Allocated(local_index)) + "/" + std::to_string(observations[index].Geometry) + " changed " + std::to_string(changed[index]) + " active " + std::to_string(numeric[index].Active));
					if (observations[index].Geometry) {
						const auto positions = geometry_owner.Geometry(local_index).Positions;
						Require(positions.size() == 4, "actual recursive geometry must retain four source-derived corners");
						for (std::size_t corner = 0; corner < 4; ++corner) {
							Require(std::bit_cast<std::uint32_t>(positions[corner].X) == observations[index].Positions[corner * 2] && std::bit_cast<std::uint32_t>(positions[corner].Y) == observations[index].Positions[corner * 2 + 1], "native recursive geometry vertex differs");
						}
						++geometry_comparisons;
					}
				}
				std::array<sgre::scene::MotionMaskOwnerFrame, 2> packet_owners = {sgre::scene::MotionMaskOwnerFrame(policies, geometries, parent_numeric), sgre::scene::MotionMaskOwnerFrame(child_policies, child_geometries, child_numeric)};
				if (!packets) {
					sgre::scene::MotionStencilRowPolicies unrelated_rows(selected, ancestry);
					std::array<sgre::scene::MotionMaskOwnerFrame, 1> unrelated_owner = {sgre::scene::MotionMaskOwnerFrame(unrelated_rows, geometries, parent_numeric)};
					RequireFailure([&] {
						sgre::scene::MotionSelectedMaskPackets unrelated_geometry_owner(unrelated_owner);
					},
					    "exact selected row and geometry context identities");
					RequireFailure([&] {
						sgre::scene::MotionSelectedMaskPackets missing_child(std::span(packet_owners).first(1));
					},
					    "omit an actual bound selected child owner");
					packets = std::make_unique<sgre::scene::MotionSelectedMaskPackets>(packet_owners);
					RequireFailure([&] {
						static_cast<void>(packets->Nodes());
					},
					    "completed actual geometry binding phase");
				}
				packets->Build(packet_owners);
				const auto packet_nodes = packets->Nodes();
				const auto packet_settings = packets->Settings();
				Require(packet_nodes.size() == 7 && packet_settings.size() == 7, "actual recursive mask packet count differs");
				for (std::size_t index = 0; index < 7; ++index) {
					const auto& expected = expected_rows[index];
					const auto& packet = packet_nodes[index];
					const auto& settings = packet_settings[index];
					const auto parent = expected.Ancestor < 0 ? std::nullopt : std::optional<std::uint32_t>(static_cast<std::uint32_t>(expected.Ancestor));
					Require(packet.ModeFlags == expected.StencilType && packet.Parent == parent && settings.Operation == expected.ControlEnabled && std::bit_cast<std::uint32_t>(settings.ThresholdMinimum) == expected.Scale && std::bit_cast<std::uint32_t>(settings.ThresholdExtent) == expected.Offset && packet.Members.size() == expected.Members.size(), "native recursive mask packet control/ancestry differs");
					Require((packet.Command.Geometry != nullptr) == (expected_commands[index][0] != 0) && packet.Command.Hidden == (expected_commands[index][1] != 0) && packet.Command.GeometryCount == expected_commands[index][2] && packet.Command.ClipGroup == expected_commands[index][3], "original row geometry/hidden/count/clip packet fields differ");
					for (std::size_t member = 0; member < expected.Members.size(); ++member) {
						const auto& actual = packet.Members[member];
						const auto& target = packet_nodes[expected.Members[member]].Command;
						Require(actual.Geometry == target.Geometry && actual.GeometryCount == target.GeometryCount && actual.Hidden == target.Hidden, "actual mask member must bind its exact owned source geometry and row opacity");
					}
					++packet_comparisons;
				}
				const auto queued = policies.GlobalRows();
				Require(queued.size() == global_rows.size(), "native ultimate containing-player queue count differs");
				for (std::size_t index = 0; index < queued.size(); ++index) {
					Require(queued[index] == &ResolveRow(global_rows[index], policies, child_policies), "native ultimate containing-player queue order/row identity differs");
					if (const auto* ancestor = queued[index]->Ancestor()) {
						std::array<sgre::scene::MotionMaskOperation, 128> operations{};
						const auto count = sgre::scene::PrepareMotionMaskPlan(packet_nodes, packet_settings, static_cast<std::uint32_t>(packets->Index(*ancestor)), operations);
						for (std::size_t operation = 0; operation < count; ++operation) {
							if (operations[operation].Kind == sgre::scene::MotionMaskOperationKind::VisitCommand) {
								Require(operations[operation].Command != nullptr && operations[operation].Command->Geometry != nullptr, "actual generated mask plans require genuine bound geometry for every visited member");
							}
						}
					}
					++queue_comparisons;
				}
				parent_local.CompleteComposition();
				child_local.CompleteComposition();
				if (phase == 2) {
					policies.PrepareRecursiveFrame(parent_numeric);
					child_policies.PrepareRecursiveFrame(child_numeric);
					RequireFailure([&] {
						static_cast<void>(packets->Nodes());
					},
					    "completed actual prepared packet phase");
					child_policies.Publish(child_numeric, child_current, child_sources, child_position, child_priority);
					policies.Publish(parent_numeric, parent_current, parent_sources, position, priority);
					RequireFailure([&] {
						static_cast<void>(packets->Nodes());
					},
					    "completed actual prepared packet phase");
					RequireFailure([&] {
						packets->Build(packet_owners);
					},
					    "actual live completed selected owner");
					RequireFailure([&] {
						child_geometries.Compose(child_numeric, child_current, child_sources, std::span(changed).subspan(6), 0.25f);
					},
					    "completed actual containing geometry phase");
					geometries.Compose(parent_numeric, parent_current, parent_sources, std::span(changed).first(6), 0.25f);
					RequireFailure([&] {
						packets->Build(packet_owners);
					},
					    "actual live completed selected owner");
					child_geometries.Compose(child_numeric, child_current, child_sources, std::span(changed).subspan(6), 0.25f);
					packets->Build(packet_owners);

					const auto actual_source = sources[4];
					sources[4] = nullptr;
					RequireFailure([&] {
						geometries.Compose(parent_numeric, parent_current, parent_sources, std::span(changed).first(6), 0.25f);
					},
					    "actual drawing source binding");
					RequireFailure([&] {
						static_cast<void>(packets->Nodes());
					},
					    "actual live completed selected owner");
					sources[4] = actual_source;
					geometries.Compose(parent_numeric, parent_current, parent_sources, std::span(changed).first(6), 0.25f);
					packets->Build(packet_owners);

					const auto foreign_numeric = numeric;
					std::array<sgre::scene::MotionMaskOwnerFrame, 2> foreign_phase = {sgre::scene::MotionMaskOwnerFrame(policies, geometries, std::span(foreign_numeric).first(6)), sgre::scene::MotionMaskOwnerFrame(child_policies, child_geometries, child_numeric)};
					RequireFailure([&] {
						packets->Build(foreign_phase);
					},
					    "owner identity differs");
					RequireFailure([&] {
						static_cast<void>(packets->Nodes());
					},
					    "completed actual geometry binding phase");
					packets->Build(packet_owners);

					if (numeric[3].Active) {
						policies.PrepareRecursiveFrame(parent_numeric);
						RequireFailure([&] {
							policies.Publish(parent_numeric, parent_current, parent_sources, position, priority);
						},
						    "exact completed recursive selected owner");
						child_policies.PrepareRecursiveFrame(child_numeric);
						geometries.Compose(parent_numeric, parent_current, parent_sources, std::span(changed).first(6), 0.25f);
						child_geometries.Compose(child_numeric, child_current, child_sources, std::span(changed).subspan(6), 0.25f);
						child_policies.Publish(child_numeric, child_current, child_sources, child_position, child_priority);
						policies.Publish(parent_numeric, parent_current, parent_sources, position, priority);
						packets->Build(packet_owners);
					}

					const auto foreign_preparation = prepared;
					sgre::scene::MotionPriorityCursor foreign_priority(foreign_preparation);
					foreign_priority.Seek(position);
					RequireFailure([&] {
						policies.Publish(parent_numeric, parent_current, parent_sources, position, foreign_priority);
					},
					    "complete actual selected priority order");
					RequireFailure([&] {
						static_cast<void>(packets->Nodes());
					},
					    "exact live published selected owner");
					policies.Publish(parent_numeric, parent_current, parent_sources, position, priority);
					packets->Build(packet_owners);

					parent_local.Select(sgre::scene::MotionFrameTraversal::Reverse, 2);
					for (const auto layer : {0u, 4u}) {
						const auto& state = parent_local.State(layer);
						Require(state.CurrentSlot == 1 && state.CurrentIndex == 0 && state.FrameDirty, "selected mask reverse crossing must decode the physical current slot and retain dirty");
						Require(selected.StencilMask(layer).Control(1).Enabled == (inputs[layer].Controlled && !inputs[layer].Hidden), "selected mask reverse crossing must use its actual frame control");
					}
					parent_composition.Compose(parent_local, false);
					parent_local.Select(sgre::scene::MotionFrameTraversal::Forward, 4);
					for (const auto layer : {0u, 4u}) {
						Require(parent_local.State(layer).CurrentSlot == 0 && parent_local.State(layer).CurrentIndex == 1 && parent_local.State(layer).FrameDirty, "selected mask forward crossing must flip its actual current slot and retain dirty");
						Require(selected.StencilMask(layer).Control(1).Enabled == (inputs[layer].Controlled && !inputs[layer].Hidden), "selected mask decoded hidden terminal must preserve the physical slot control");
					}
					parent_composition.Compose(parent_local, false);
					parent_local.Select(sgre::scene::MotionFrameTraversal::Reverse, 2);
					const auto* same_current = &selected.ChildSlots(3).CurrentFrame();
					selected.ChildSlots(3).Select(sgre::scene::MotionFrameTraversal::Seek, 2);
					Require(same_current == &selected.ChildSlots(3).CurrentFrame() && parent_local.State(3).CurrentSlot != selected.ChildSlots(3).State().CurrentSlot, "physical child cursor discriminator must preserve the same actual current frame");
					RequireFailure([&] {
						parent_composition.Compose(parent_local, false);
					},
					    "actual selected physical child cursor tuple");
					parent_local.Select(sgre::scene::MotionFrameTraversal::Seek, 2);
					parent_composition.Compose(parent_local, false);
					parent_local.Select(sgre::scene::MotionFrameTraversal::Seek, 4);
					parent_composition.Compose(parent_local, false);
					selected.StencilMask(4).PrepareFrame(0, 0);
					selected.StencilMask(4).PrepareFrame(1, 1);
					RequireFailure([&] {
						parent_composition.Compose(parent_local, false, true);
					},
					    "actual selected physical mask cursor tuple");
					parent_local.Select(sgre::scene::MotionFrameTraversal::Seek, 2);
					parent_composition.Compose(parent_local, false);
					child_nodes->Retire();
					Require(child_composition.Layers().empty() && child_local.Samples().empty(), "retired actual selected owner must invalidate numeric views");
					RequireFailure([&] {
						child_local.Select(sgre::scene::MotionFrameTraversal::Seek, child_position);
					},
					    "actual live selected raw/prepared owner");
					RequireFailure([&] {
						static_cast<void>(packets->Nodes());
					},
					    "exact live published selected owner");
				}
			}
		}
		selected.Retire();
		Require(identifiers.LiveCount() == 0 && manager.ReferenceCount(registration) == 1, "recursive row/geometry owner must preserve exact selected retirement and registration lifetime");
	}
	Require(stream.peek() == std::char_traits<char>::eof(), "unexpected trailing recursive fixture bytes");
	std::cout << numeric_comparisons << " produced native node poses; " << row_comparisons << " recursive native rows, " << member_comparisons << " mask members, " << geometry_comparisons << " owned quads and " << queue_comparisons << " queue rows and " << packet_comparisons << " actual mask packets matched\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: recursive_stencil_rows_tests native_fixture");
		Replay(argv[1]);

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
