#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include "sgre/scene/motion_child_frame_selection.hpp"
#include "sgre/scene/motion_random.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
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
static void RequireFailure(Operation operation, std::string_view text) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(text) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unsupported selected-node operation was accepted");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue atom;
	atom.Data = std::move(value);

	return atom;
}

struct InputNode {
	std::uint32_t ParentOrdinal = 0;
	std::uint32_t Kind = 3;
	std::uint32_t Plane = 0;
	std::array<std::uint32_t, 4> Order = {0, 3, 2, 1};
	std::uint32_t TrianglePriority = 0;
	bool Independent = false;
	bool JoinTarget = false;
	bool ModifierPresent = false;
	bool Modifier = false;
	std::uint32_t Inherit = 0;
};

struct NativeNode {
	std::uint32_t Identifier = 0;
	std::int32_t RelativeParent = 0;
	std::uint32_t DrawRow = 0xffffffffu;
	std::uint32_t TrianglePriority = 0;
	std::uint32_t SampleBits = 0;
};

static sgre::resource::MotionFrame HiddenFrame(float time) {
	sgre::resource::MotionFrame frame;
	frame.Time = time;

	return frame;
}

static std::vector<sgre::resource::MotionLayer> Layers(std::span<const InputNode> nodes, std::uint32_t parent = 0) {
	std::vector<sgre::resource::MotionLayer> layers;
	for (std::size_t index = 0; index < nodes.size(); ++index) {
		const auto& node = nodes[index];
		if (node.ParentOrdinal != parent) {
			continue;
		}
		sgre::resource::MotionLayer layer;
		layer.Label = "n" + std::to_string(index + 1);
		layer.Type = node.Kind;
		layer.Coordinate = node.Plane;
		layer.InheritMask = node.Inherit;
		layer.TransformOrder.assign(node.Order.begin(), node.Order.end());
		layer.Frames = {HiddenFrame(0), HiddenFrame(8)};
		layer.Properties.emplace_back("parameterize", Atom(std::monostate{}));
		layer.Properties.emplace_back("meshTransform", Atom(std::int64_t{0}));
		layer.Properties.emplace_back("joinTarget", Atom(node.JoinTarget));
		layer.Properties.emplace_back("groundCorrection", Atom(false));
		layer.Properties.emplace_back("objTriPriority", Atom(static_cast<std::int64_t>(std::bit_cast<std::int32_t>(node.TrianglePriority))));
		layer.Properties.emplace_back("motionIndependentLayerInherit", Atom(node.Independent));
		if (node.ModifierPresent) {
			layer.Properties.emplace_back("hasColorModifier", Atom(node.Modifier));
		}
		layer.Children = Layers(nodes, static_cast<std::uint32_t>(index + 1));
		layers.push_back(std::move(layer));
	}

	return layers;
}

static sgre::resource::MotionTimeline Timeline(std::string name, std::span<const InputNode> nodes) {
	sgre::resource::MotionTimeline timeline;
	timeline.Name = std::move(name);
	timeline.LastTime = 8;
	timeline.LayerIndexMap = Atom(sgre::resource::PsbObject{});
	timeline.Layers = Layers(nodes);
	auto first = HiddenFrame(0);
	first.Type = 1;
	sgre::resource::PsbArray priority;
	for (std::size_t index = 0; index < nodes.size(); ++index) {
		priority.push_back(Atom(static_cast<std::int64_t>(index)));
	}
	first.Content = Atom(std::move(priority));
	auto terminal = HiddenFrame(8);
	terminal.Type = 1;
	timeline.Priority = {first, terminal};

	return timeline;
}

static sgre::scene::TextureCache SourceFreeCache() {
	return sgre::scene::TextureCache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("source-free selected-node fixture attempted an upload");
	},
	    [](auto) {
		    throw std::runtime_error("source-free selected-node fixture attempted a texture release");
	    });
}

static std::shared_ptr<sgre::resource::MotionResource> Resource(std::span<const InputNode> main, std::span<const InputNode> nested) {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionObject character;
	character.Name = "a";
	character.Timelines.push_back(Timeline("main", main));
	character.Timelines.push_back(Timeline("nested", nested));
	resource->Objects.push_back(std::move(character));

	return resource;
}

static sgre::scene::MotionProjectId Register(sgre::scene::MotionResourceManager& manager, std::shared_ptr<const sgre::resource::MotionResource> resource) {
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = 1;
	identity.RootOffset = 128;

	return manager.Register(identity, std::move(resource), {});
}

static void TestOwnershipAndSlots() {
	std::array<InputNode, 4> main{};
	main[1].ParentOrdinal = 1;
	main[2].ParentOrdinal = 2;
	main[2].Kind = 2;
	main[3].Kind = 0;
	main[3].TrianglePriority = 0xffffffffu;
	std::array<InputNode, 2> nested{};
	nested[0].Kind = 2;
	nested[1].ParentOrdinal = 1;
	const auto resource = Resource(main, nested);
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto registration = Register(manager, resource);
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("a");
	commands.Play("main");
	const auto& timeline = *commands.CachedTimeline();
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
	const auto& nested_timeline = resource->Objects[0].Timelines[1];
	const auto nested_prepared = sgre::scene::PrepareMotionTimeline(nested_timeline, *resource);
	sgre::scene::MotionRootScene root;
	root.SetVisible(true);
	root.SetCoordinate3({12, 13, 14});
	root.SetPriority(4);
	sgre::scene::MotionPlayerRenderControls render;
	render.ColorFilter = 5;
	render.ColorFilterParameter = 39;
	sgre::scene::MotionLayerIdentifierPool identifiers;
	std::size_t samples = 0;
	std::vector<std::pair<std::string_view, std::string_view>> actions;
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [&](const auto& sampled_manager) {
		Require(&sampled_manager == &manager && identifiers.LiveCount() != 0, "ID acquisition must precede actual child manager sampling");
		++samples;

		return 0.5f;
	};
	sgre::scene::MotionChildFrameSlotServices slots;
	slots.EnqueueAction = [&](auto label, auto action) {
		actions.emplace_back(label, action);
	};
	{
		sgre::scene::MotionSelectedLayerNodes tree(commands, root, render, timeline, prepared, identifiers, construction, slots);
		Require(tree.Nodes().size() == 4 && tree.ChildOrdinals().size() == 2 && tree.ChildOrdinals()[0] == 1 && tree.ChildOrdinals()[1] == 2, "selected nodes must retain actual forward child ordinals in DFS preorder");
		Require(tree.SpriteCount() == 1 && tree.KindMask() == 13 && tree.DrawRowCount() == 3, "only actual kind0/3 own rows in this selected domain");
		Require(tree.Nodes()[1].RelativeParent == -1 && tree.Nodes()[2].RelativeParent == -1 && tree.Nodes()[3].RelativeParent == -4, "relative native parent offsets must include the separate root ordinal");
		Require(!tree.Nodes()[2].DrawRow && tree.Nodes()[3].DrawRow == 2 && tree.Nodes()[3].ObjectTrianglePriority == 0xffffffffu, "kind2 has no row and kind0 retains the actual signed metadata bit pattern");
		Require(tree.Nodes()[0].Raw == &timeline.Layers[0] && tree.Nodes()[1].Prepared == &prepared.Layers[1], "selected node metadata borrows genuine raw/prepared owners");
		Require(&tree.Child(0).ImmediateParentCommands() == &commands && &tree.Child(1).ImmediateParentCommands() == &commands && &tree.Child(1).ImmediateParentCommands() != &tree.Child(0).Commands(), "structural nesting must not invent a player parent");
		Require(tree.Child(1).RenderControls().ColorFilter == 5 && tree.Child(1).RenderControls().ColorFilterParameter == 39 && tree.Child(1).Root().Priority() == 4, "selected child construction must use the enclosing actual scalar owner");
		Require(samples == 2 && actions.empty() && identifiers.LiveCount() == 4 && manager.ReferenceCount(registration) == 1, "construction samples real manager once per child without a resource pin or queued actions");
		tree.ChildSlots(0).Select(sgre::scene::MotionFrameTraversal::Seek, 0);
		Require(tree.ChildSlots(0).State().FrameDirty && &tree.ChildSlots(0).CurrentControl() == &tree.Child(0).Control(0), "actual selected child slots must share the exclusive constructor's controls");
		tree.ChildSlots(0).CompleteComposition();
		Require(!tree.ChildSlots(0).State().FrameDirty, "dirty acknowledgement follows the explicit enclosing completion phase");
		RequireFailure([&] {
			static_cast<void>(tree.Child(2));
		},
		    "child owner");
		RequireFailure([&] {
			tree.RebuildChildNodes(0, nested_timeline, nested_prepared, slots);
		},
		    "selected timeline");
		tree.Child(0).Commands().SetCharacter("a");
		tree.Child(0).Commands().Play("nested");
		tree.RebuildChildNodes(0, nested_timeline, nested_prepared, slots);
		const auto* nested_tree = tree.ChildNodes(0);
		Require(nested_tree != nullptr && nested_tree->Nodes().size() == 2 && identifiers.LiveCount() == 6 && samples == 3, "real selected child nodes belong exclusively to their child player");
		Require(&nested_tree->Child(1).ImmediateParentCommands() == &tree.Child(0).Commands() && &nested_tree->Child(1).UltimateParentCommands() == &commands, "selected child trees preserve real immediate and ultimate player ownership");
		tree.ResetChildMotion(0);
		Require(tree.ChildNodes(0) == nullptr && identifiers.LiveCount() == 4 && identifiers.RetiredCount() == 2 && !tree.Child(0).Commands().ClockState().Playing && tree.Child(0).Commands().Names().CurrentMotion.empty(), "child reset must retire its real selected tree and clear actual current motion");
		Require(tree.Child(0).Commands().PreferredProject() == commands.PreferredProject() && tree.Child(0).Commands().CachedTimeline() == &nested_timeline, "native reset preserves preferred and cached selection independently of current names");
		tree.Child(0).Commands().Play("nested");
		tree.RebuildChildNodes(0, nested_timeline, nested_prepared, slots);
		Require(tree.ChildNodes(0)->Nodes()[0].Identifier == 6 && tree.ChildNodes(0)->Nodes()[1].Identifier == 5, "selected-child rebuild uses the actual manager retired stack");
		tree.Retire();
		Require(tree.Nodes().empty() && tree.ChildOrdinals().empty() && tree.KindMask() == 0 && tree.DrawRowCount() == 0 && identifiers.LiveCount() == 0 && identifiers.RetiredCount() == 6, "retirement invalidates selection and returns all exclusively owned descendant IDs");
		tree.Retire();
		RequireFailure([&] {
			static_cast<void>(tree.Child(0));
		},
		    "child owner");
	}

	Require(root.Overrides().Visible && root.Overrides().Coordinate == std::array<float, 3>{12, 13, 14} && root.Priority() == 4 && commands.ClockState().Playing && manager.ReferenceCount(registration) == 1, "selected retirement preserves actual enclosing root/commands/registration");
	const std::array<std::uint32_t, 6> expected = {4, 3, 2, 5, 6, 1};
	for (const auto value : expected) {
		Require(identifiers.Acquire().Value() == value, "parent ID must return before its recursively selected child IDs, then later flattened siblings");
	}
}

static void TestRejectionAndFailure() {
	const std::array<InputNode, 2> main{};
	const std::array<InputNode, 1> nested{};
	auto resource = Resource(main, nested);
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	static_cast<void>(Register(manager, resource));
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("a");
	commands.Play("main");
	auto& timeline = resource->Objects[0].Timelines[0];
	auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
	sgre::scene::MotionRootScene root;
	const sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionLayerIdentifierPool identifiers;
	std::size_t samples = 0;
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [&](const auto&) {
		++samples;

		return samples == 2 ? -1.0f : 0.5f;
	};
	std::vector<std::pair<std::string_view, std::string_view>> actions;
	sgre::scene::MotionChildFrameSlotServices slots;
	slots.EnqueueAction = [&](auto label, auto action) {
		actions.emplace_back(label, action);
	};
	RequireFailure([&] {
		sgre::scene::MotionSelectedLayerNodes rejected(commands, root, render, timeline, prepared, identifiers, construction, slots);
	},
	    "unit sample");
	Require(identifiers.LiveCount() == 0 && identifiers.RetiredCount() == 2 && samples == 2 && actions.empty(), "failed partial construction must return acquired IDs and retire already-created exclusive children");
	RequireFailure([&] {
		sgre::scene::MotionSelectedLayerNodes rejected(commands, root, render, timeline, prepared, identifiers, {}, slots);
	},
	    "services");
	RequireFailure([&] {
		sgre::scene::MotionSelectedLayerNodes rejected(commands, root, render, timeline, prepared, identifiers, construction, {});
	},
	    "services");
	prepared.Parents[1] = 0;
	RequireFailure([&] {
		sgre::scene::MotionSelectedLayerNodes rejected(commands, root, render, timeline, prepared, identifiers, construction, slots);
	},
	    "parent");
	prepared.Parents[1].reset();
	const auto samples_before_uid = samples;
	timeline.Layers[0].Properties.emplace_back("uid", Atom(std::string("actualUid")));
	{
		sgre::scene::MotionSelectedLayerNodes accepted(commands, root, render, timeline, prepared, identifiers, construction, slots);
		Require(commands.LayerUids().RegisteredCount() == 1, "selected UID requires its actual command-owned registry");
		Require(accepted.ChildOrdinals().size() == main.size() && samples == samples_before_uid + main.size() && identifiers.LiveCount() == main.size(), "accepted UID selection must construct both ordinary children and sample once per child");
	}
	Require(commands.LayerUids().RegisteredCount() == 0, "selected UID retirement retained a borrowed node");
	const auto samples_before_unsupported = samples;
	const auto live_before_unsupported = identifiers.LiveCount();
	const auto retired_before_unsupported = identifiers.RetiredCount();
	const auto next_before_unsupported = identifiers.NextIdentifier();
	Require(live_before_unsupported == 0 && samples_before_unsupported == samples_before_uid + main.size(), "accepted UID retirement must preserve its completed child samples and retire all live IDs");
	timeline.Layers[0].Properties.back().second = Atom(std::string());
	prepared.Layers[0].StencilType = 1;
	RequireFailure([&] {
		sgre::scene::MotionSelectedLayerNodes rejected(commands, root, render, timeline, prepared, identifiers, construction, slots);
	},
	    "stencil");
	prepared.Layers[0].StencilType = 0;
	timeline.Layers[0].Type = 6;
	RequireFailure([&] {
		sgre::scene::MotionSelectedLayerNodes rejected(commands, root, render, timeline, prepared, identifiers, construction, slots);
	},
	    "kind");
	timeline.Layers[0].Type = 3;
	timeline.Stream = Atom(sgre::resource::PsbArray{});
	RequireFailure([&] {
		sgre::scene::MotionSelectedLayerNodes rejected(commands, root, render, timeline, prepared, identifiers, construction, slots);
	},
	    "packed");
	std::cout << "selected UID rejection boundaries: rollback_samples=" << samples_before_uid << " after_uid_samples=" << samples_before_unsupported << " after_unsupported_samples=" << samples << " live=" << identifiers.LiveCount() << " retired=" << identifiers.RetiredCount() << " next=" << identifiers.NextIdentifier() << "\n";
	Require(identifiers.LiveCount() == live_before_unsupported && identifiers.RetiredCount() == retired_before_unsupported && identifiers.NextIdentifier() == next_before_unsupported && samples == samples_before_unsupported && actions.empty(), "unsupported resource domains must preserve post-UID sample, ID-pool and action state");
}

static void TestJoinEligibilityWithoutSnapshots() {
	std::array<InputNode, 2> input{};
	input[0].Kind = 2;
	input[0].JoinTarget = true;
	input[1].Kind = 0;
	input[1].ParentOrdinal = 1;
	const auto resource = Resource(input, {});
	auto& timeline = resource->Objects[0].Timelines[0];
	auto& join = timeline.Layers[0].Properties[2].second;
	Require(timeline.Layers[0].Properties[2].first == "joinTarget", "JOIN fixture must identify the actual property before changing its numeric representation");
	join = Atom(std::int64_t{1});
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto registration = Register(manager, resource);
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("a");
	commands.Play("main");
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
	sgre::scene::MotionRootScene root;
	const sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionLayerIdentifierPool identifiers;
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [](const auto&) -> float {
		throw std::runtime_error("ordinary JOIN eligibility fixture unexpectedly constructed a child player");
	};
	sgre::scene::MotionChildFrameSlotServices slots;
	slots.EnqueueAction = [](auto, auto) {
		throw std::runtime_error("selected constructor unexpectedly emitted an action");
	};
	{
		sgre::scene::MotionSelectedLayerNodes tree(commands, root, render, timeline, prepared, identifiers, construction, slots);
		Require(tree.Nodes().size() == 2 && tree.Nodes()[0].JoinTarget && !tree.Nodes()[1].JoinTarget && tree.SpriteCount() == 1 && tree.DrawRowCount() == 1, "actual numeric JOIN eligibility must be retained separately from ordinary node/row ownership");
		Require(tree.Nodes()[0].Raw == &timeline.Layers[0] && std::get<std::int64_t>(join.Data) == 1 && manager.ReferenceCount(registration) == 1, "retaining eligibility must neither replace raw metadata nor add a resource root");
	}
	Require(identifiers.LiveCount() == 0, "ordinary eligible nodes must preserve actual ID retirement");
	const auto join_serial = commands.SelectionSerial();
	Require(commands.ClockState().TickAssigned, "retired constructor fixture retains the actual assigned-tick JOIN bypass");
	commands.Play("main", sgre::scene::MotionPlayFlags::Force | sgre::scene::MotionPlayFlags::Join);
	Require(commands.SelectionSerial() == join_serial + 1 && commands.CachedTimeline() == &timeline && commands.ClockState().Playing && commands.ClockState().TickAssigned && commands.ClockState().TickCount == 0 && commands.ClockState().EvaluationTime == 0 && identifiers.LiveCount() == 0 && manager.ReferenceCount(registration) == 1, "Force JOIN after assigned ticks must reselect without fabricating a snapshot or selected-node owner");
	for (const auto& rejected : {Atom(std::monostate{}), Atom(std::string("false")), Atom(std::numeric_limits<double>::quiet_NaN()), Atom(std::numeric_limits<double>::infinity()), Atom(-std::numeric_limits<double>::infinity())}) {
		join = rejected;
		RequireFailure([&] {
			sgre::scene::MotionSelectedLayerNodes tree(commands, root, render, timeline, prepared, identifiers, construction, slots);
		},
		    "numeric boolean property joinTarget");
	}
	join = Atom(std::int64_t{1});
	timeline.Layers[0].Type = 3;
	auto child_prepared = prepared;
	child_prepared.Layers[0].Type = 3;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
	for (std::size_t index = 0; index < seed.size(); ++index) {
		seed[index] = static_cast<std::uint32_t>(index + 1);
	}
	sgre::scene::MotionSharedRandom random(seed);
	std::size_t child_samples = 0;
	construction.ReadManagerRandom = [&](const auto& owner) {
		Require(&owner == &manager, "eligible child constructor must sample its actual containing manager generator");
		++child_samples;

		return random.NextUnitFloat();
	};
	{
		sgre::scene::MotionSelectedLayerNodes tree(commands, root, render, timeline, child_prepared, identifiers, construction, slots);
		Require(tree.Nodes()[0].JoinTarget && tree.Nodes()[0].Raw == &timeline.Layers[0] && tree.ChildOrdinals().size() == 1 && tree.Child(0).Control(0).Flags == sgre::scene::MotionPlayFlags::None && tree.Child(0).Control(1).Flags == sgre::scene::MotionPlayFlags::None && child_samples == 1, "eligible kind3 construction must retain actual metadata and default controls without JOIN playback");
	}
	Require(identifiers.LiveCount() == 0 && manager.ReferenceCount(registration) == 1, "eligible child must preserve exact ID and resource retirement");
	timeline.Layers[0].Type = 2;
	timeline.Layers[0].Properties[3].second = Atom(std::int64_t{1});
	RequireFailure([&] {
		sgre::scene::MotionSelectedLayerNodes tree(commands, root, render, timeline, prepared, identifiers, construction, slots);
	},
	    "groundCorrection");
	timeline.Layers[0].Properties[3].second = Atom(false);
	timeline.Layers[0].Properties.erase(timeline.Layers[0].Properties.begin() + 2);
	RequireFailure([&] {
		sgre::scene::MotionSelectedLayerNodes tree(commands, root, render, timeline, prepared, identifiers, construction, slots);
	},
	    "present numeric boolean property joinTarget");
	Require(identifiers.LiveCount() == 0 && manager.ReferenceCount(registration) == 1, "unsupported JOIN/ground/null/missing metadata must reject without resource or ID mutation");
}

static void TestActualQueueMutationGuard() {
	const std::array<InputNode, 1> input{};
	auto resource = Resource(input, input);
	auto& first = resource->Objects[0].Timelines[0].Layers[0].Frames[0];
	first.Type = 2;
	sgre::resource::PsbObject content;
	content.emplace_back("mask", Atom(std::int64_t{0x40000}));
	content.emplace_back("act", Atom(std::string()));
	content.emplace_back("src", Atom(std::string()));
	content.emplace_back("icon", Atom(std::string()));
	first.Content = Atom(std::move(content));
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	static_cast<void>(Register(manager, resource));
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("a");
	commands.Play("main");
	const auto& timeline = *commands.CachedTimeline();
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
	sgre::scene::MotionRootScene root;
	const sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionLayerIdentifierPool identifiers;
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [](const auto&) {
		return 0.5f;
	};
	sgre::scene::MotionSelectedLayerNodes* actual_owner = nullptr;
	std::vector<std::pair<std::string_view, std::string_view>> actions;
	bool fail = false;
	sgre::scene::MotionChildFrameSlotServices slots;
	slots.EnqueueAction = [&](auto label, auto action) {
		actions.emplace_back(label, action);
		RequireFailure([&] {
			actual_owner->Retire();
		},
		    "reenter");
		RequireFailure([&] {
			actual_owner->ResetChildMotion(0);
		},
		    "reenter");
		if (fail) {
			throw std::runtime_error("actual queue producer failure");
		}
	};
	sgre::scene::MotionSelectedLayerNodes tree(commands, root, render, timeline, prepared, identifiers, construction, slots);
	actual_owner = &tree;
	tree.ChildSlots(0).Select(sgre::scene::MotionFrameTraversal::Seek, 0);
	Require(actions.size() == 1 && actions[0].first.data() == timeline.Layers[0].Label.data() && actions[0].second.empty() && identifiers.LiveCount() == 1, "queue phase retains actual borrowed label/empty action and blocks destructive owner reentry");
	fail = true;
	RequireFailure([&] {
		tree.ChildSlots(0).Select(sgre::scene::MotionFrameTraversal::Seek, 0);
	},
	    "queue producer");
	RequireFailure([&] {
		static_cast<void>(tree.ChildSlots(0).State());
	},
	    "selection");
	tree.Retire();
	Require(identifiers.LiveCount() == 0, "failed actual queue service must leave the enclosing selected owner safely retireable");
}

class Reader {
public:
	explicit Reader(std::istream& stream)
	    : m_Stream(stream) {
	}

	std::uint8_t Byte() {
		const auto value = m_Stream.get();
		if (value == std::char_traits<char>::eof()) {
			throw std::runtime_error("short selected-node native fixture");
		}

		return static_cast<std::uint8_t>(value);
	}

	std::uint32_t Word() {
		std::uint32_t value = 0;
		for (std::uint32_t shift = 0; shift < 32; shift += 8) {
			value |= static_cast<std::uint32_t>(Byte()) << shift;
		}

		return value;
	}

private:
	std::istream& m_Stream;
};

static std::pair<InputNode, NativeNode> ReadNode(Reader& reader) {
	InputNode input;
	input.ParentOrdinal = reader.Word();
	input.Kind = reader.Word();
	input.Plane = reader.Word();
	for (auto& operation : input.Order) {
		operation = reader.Word();
	}
	input.TrianglePriority = reader.Word();
	input.Independent = reader.Byte() != 0;
	input.ModifierPresent = reader.Byte() != 0;
	input.Modifier = reader.Byte() != 0;
	input.Inherit = reader.Word();
	NativeNode expected;
	expected.Identifier = reader.Word();
	expected.RelativeParent = std::bit_cast<std::int32_t>(reader.Word());
	expected.DrawRow = reader.Word();
	expected.TrianglePriority = reader.Word();
	expected.SampleBits = reader.Word();

	return {input, expected};
}

static void Compare(const sgre::scene::MotionSelectedLayerNodes& tree, std::span<const InputNode> input, std::span<const NativeNode> expected, const sgre::scene::MotionPlayerCommands& immediate, const sgre::scene::MotionPlayerCommands& ultimate) {
	Require(tree.Nodes().size() == expected.size(), "native selected-node count differs");
	for (std::size_t index = 0; index < expected.size(); ++index) {
		const auto& node = tree.Nodes()[index];
		Require(node.NativeOrdinal == index + 1 && node.Identifier == expected[index].Identifier && node.RelativeParent == expected[index].RelativeParent && node.DrawRow.value_or(0xffffffffu) == expected[index].DrawRow && node.ObjectTrianglePriority.value_or(0) == expected[index].TrianglePriority, "native selected-node identity/relative parent/row/triangle fields differ");
		Require(node.Raw->Label == "n" + std::to_string(index + 1) && node.Raw->Type == input[index].Kind, "native selected-node raw identity differs");
		if (input[index].Kind == 3) {
			const auto& child = tree.Child(static_cast<std::uint32_t>(index));
			Require(&child.ImmediateParentCommands() == &immediate && &child.UltimateParentCommands() == &ultimate, "native selected child player ownership differs");
			Require(child.Random().State() == sgre::scene::MotionPlayerRandom(std::bit_cast<float>(expected[index].SampleBits)).State(), "native full-raw selected child random state differs");
			Require(child.Root().Frame().Hierarchy.CoordinatePlane == input[index].Plane && child.IndependentLayerInherit() == input[index].Independent && child.Commands().PreferredProject() == immediate.PreferredProject(), "native selected-child inherited root configuration differs");
		}
	}
}

static void AppendSamples(std::vector<float>& samples, std::span<const InputNode> input, std::span<const NativeNode> expected) {
	Require(input.size() == expected.size(), "native sample input/output sizes differ");
	for (std::size_t index = 0; index < input.size(); ++index) {
		if (input[index].Kind == 3) {
			samples.push_back(std::bit_cast<float>(expected[index].SampleBits));
		}
	}
}

static void Replay(std::string_view path) {
	std::ifstream stream{std::string(path), std::ios::binary};
	Require(static_cast<bool>(stream), "cannot open selected-node native fixture");
	Reader reader(stream);
	Require(reader.Byte() == 'M' && reader.Byte() == 'S' && reader.Byte() == 'N' && reader.Byte() == '1', "unexpected selected-node native fixture magic");
	const auto cases = reader.Word();
	Require(cases == 32, "unexpected selected-node native fixture count");
	std::size_t actual_nodes = 0;
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		const auto count = reader.Word();
		const auto nested_count = reader.Word();
		Require(count >= 2 && count <= 16 && nested_count >= 2 && nested_count <= 8, "unbounded selected-node native fixture count");
		std::vector<InputNode> input;
		std::vector<NativeNode> expected;
		std::vector<InputNode> nested_input;
		std::vector<NativeNode> nested_expected;
		for (std::uint32_t index = 0; index < count + nested_count; ++index) {
			const auto row = ReadNode(reader);
			if (index < count) {
				input.push_back(row.first);
				expected.push_back(row.second);
			} else {
				nested_input.push_back(row.first);
				nested_expected.push_back(row.second);
			}
		}
		const auto retired_count = reader.Word();
		Require(retired_count == count + nested_count, "native selected retirement count differs");
		std::vector<std::uint32_t> retired;
		for (std::uint32_t index = 0; index < retired_count; ++index) {
			retired.push_back(reader.Word());
		}
		std::vector<NativeNode> rebuilt = expected;
		for (auto& row : rebuilt) {
			row.Identifier = reader.Word();
		}
		for (auto& row : rebuilt) {
			row.SampleBits = reader.Word();
		}

		const auto resource = Resource(input, nested_input);
		auto cache = SourceFreeCache();
		sgre::scene::MotionResourceManager manager(cache);
		const auto registration = Register(manager, resource);
		sgre::scene::MotionPlayerCommands commands(manager);
		commands.SetCharacter("a");
		commands.Play("main");
		const auto& timeline = *commands.CachedTimeline();
		const auto& nested_timeline = resource->Objects[0].Timelines[1];
		const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
		const auto nested_prepared = sgre::scene::PrepareMotionTimeline(nested_timeline, *resource);
		sgre::scene::MotionRootScene root;
		const sgre::scene::MotionPlayerRenderControls render;
		sgre::scene::MotionLayerIdentifierPool identifiers;
		std::vector<float> samples;
		AppendSamples(samples, input, expected);
		AppendSamples(samples, nested_input, nested_expected);
		AppendSamples(samples, input, rebuilt);
		std::size_t sample_index = 0;
		sgre::scene::MotionChildConstructionServices construction;
		construction.ReadManagerRandom = [&](const auto& sampled_manager) {
			Require(&sampled_manager == &manager && sample_index < samples.size(), "selected native fixture manager sampler ownership/order differs");

			return samples[sample_index++];
		};
		std::vector<std::pair<std::string_view, std::string_view>> actions;
		sgre::scene::MotionChildFrameSlotServices slots;
		slots.EnqueueAction = [&](auto label, auto action) {
			actions.emplace_back(label, action);
		};
		{
			sgre::scene::MotionSelectedLayerNodes tree(commands, root, render, timeline, prepared, identifiers, construction, slots);
			Compare(tree, input, expected, commands, commands);
			tree.Child(0).Commands().SetCharacter("a");
			tree.Child(0).Commands().Play("nested");
			tree.RebuildChildNodes(0, nested_timeline, nested_prepared, slots);
			Compare(*tree.ChildNodes(0), nested_input, nested_expected, tree.Child(0).Commands(), commands);
			tree.Retire();
			Require(identifiers.LiveCount() == 0 && identifiers.RetiredCount() == retired_count && identifiers.NextIdentifier() == retired_count + 1, "native selected-tree retirement and manager counter differ");
			std::vector<sgre::scene::MotionLayerIdentifier> observed_ids;
			for (auto iterator = retired.rbegin(); iterator != retired.rend(); ++iterator) {
				const auto identifier = identifiers.Acquire();
				Require(identifier.Value() == *iterator, "complete native recursively retired ID order differs");
				observed_ids.push_back(identifier);
			}
			for (auto iterator = observed_ids.rbegin(); iterator != observed_ids.rend(); ++iterator) {
				identifiers.Retire(*iterator);
			}
		}
		{
			sgre::scene::MotionSelectedLayerNodes tree(commands, root, render, timeline, prepared, identifiers, construction, slots);
			Compare(tree, input, rebuilt, commands, commands);
			Require(identifiers.RetiredCount() == nested_count && identifiers.LiveCount() == count, "native rebuild must reuse manager IDs from the real recursive retirement stack");
		}
		Require(sample_index == samples.size() && actions.empty() && manager.ReferenceCount(registration) == 1, "native selected construction must neither queue actions nor acquire a resource pin");
		actual_nodes += 2 * count + nested_count;
	}

	Require(stream.peek() == std::char_traits<char>::eof(), "extra selected-node native fixture bytes");
	Require(actual_nodes == 576, "unexpected actual selected-node comparison count");
	std::cout << "native selected-node replay: 576 nodes, 32 recursively selected child trees PASS\n";
}

static void TestActualDescriptorVariableTargets() {
	std::array<InputNode, 4> nodes{};
	nodes[0].Kind = 2;
	nodes[1].ParentOrdinal = 1;
	nodes[2].Kind = 2;
	nodes[3].ParentOrdinal = 3;
	auto resource = Resource(nodes, {});
	auto& outer = resource->Objects[0].Timelines[0];
	outer.Layers[0].Label = "wrapper";
	outer.Layers[0].Children[0].Label = "text";
	outer.Layers[1].Label = "another";
	outer.Layers[1].Children[0].Label = "other";
	// Exported lookup aliases intentionally differ from the actual descriptor node labels.
	outer.LayerIndexMap = Atom(sgre::resource::PsbObject{{"text", Atom(std::int64_t{3})}});
	auto& inner = resource->Objects[0].Timelines[1];
	inner.Parameters = Atom(sgre::resource::PsbArray{Atom(sgre::resource::PsbObject{{"id", Atom(std::string("param"))}, {"discretization", Atom(false)}, {"rangeBegin", Atom(-10.0)}, {"rangeEnd", Atom(10.0)}, {"division", Atom(20.0)}})});
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	static_cast<void>(Register(manager, resource));
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("a");
	commands.Play("main");
	commands.ParameterValues().WriteLocal("param", -8, 0);
	commands.ParameterValues().WriteLocal("text::param", 3, 0);
	Require(commands.ParameterValues().ReadLocal("text/param") == 3 && commands.ParameterValues().ReadLocal("missing::param") == 0, "descriptor getter must read only its stored canonical controller map");
	const auto prepared = sgre::scene::PrepareMotionTimeline(outer, *resource);
	const auto inner_prepared = sgre::scene::PrepareMotionTimeline(inner, *resource);
	sgre::scene::MotionRootScene root;
	sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionLayerIdentifierPool identifiers;
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [](const auto&) {
		return 0.5f;
	};
	sgre::scene::MotionChildFrameSlotServices slots;
	slots.EnqueueAction = [](auto, auto) {
	};
	{
		sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, outer, prepared, identifiers, construction, slots);
		auto& text = selected.Child(1).Commands();
		auto& other = selected.Child(3).Commands();
		Require(text.CachedTimeline() == nullptr && text.ParameterValues().Read("param") == 3 && other.ParameterValues().Read("param") == -8, "actual empty kind3 players already receive descriptor membership by node labels, independently of the exported index map");
		commands.ParameterValues().WriteLocal("other/wrapper::param", 5, 0);
		Require(other.ParameterValues().Read("param") == 5 && text.ParameterValues().Read("param") == 3, "native label accumulator persists across candidate nodes instead of substituting a clean hierarchical path search");
		text.SetCharacter("a");
		text.Play("nested");
		selected.RebuildChildNodes(1, inner, inner_prepared, slots);
		const auto* parameters = text.ParameterValues().CurrentTimelineParameters(inner);
		Require(parameters != nullptr && parameters->Entries()[0].Position == 13 && parameters->Entries()[0].ControlFlags == 0 && text.ParameterValues().ReadLocal("param") == 0, "future real selected parameter cells initialize from the retained parent descriptor without a fabricated child local value");
		text.Stop();
		commands.ParameterValues().WriteLocal("text/param", 7, 0x80000001u);
		Require(!text.ClockState().Playing && parameters->Entries()[0].Position == 17 && parameters->Entries()[0].ControlFlags == 0x80000001u && text.ParameterValues().ReadLocal("param") == 0, "hidden/stopped real child parameter fanout preserves the complete control word and does not change playback/local map");
		text.ParameterValues().WriteLocal("param", -2, 0);
		commands.ParameterValues().WriteLocal("wrapper/text::param", 8, 5);
		Require(parameters->Entries()[0].Position == 18 && parameters->Entries()[0].ControlFlags == 5 && text.ParameterValues().Read("param") == -2 && commands.ParameterValues().ReadLocal("wrapper/text/param") == 8, "ancestor-window route updates actual records while the child's own local initializer retains precedence");
		commands.ParameterValues().WriteLocal("text::param::extra", 9, 0);
		Require(commands.ParameterValues().ReadLocal("text::param/extra") == 0 && commands.ParameterValues().ReadLocal("text::param::extra") == 9, "last double-colon precedence must not reinterpret a later slash inside its parameter suffix");
		commands.ParameterValues().WriteLocal("::", 2, 0);
		Require(commands.ParameterValues().ReadLocal("/") == 2, "native pure descriptor split admits empty ends");
		const auto selection = text.SelectionSerial();
		selected.ResetChildMotion(1);
		Require(text.SelectionSerial() == selection && text.ParameterValues().Read("param") == -2 && commands.ParameterValues().ReadLocal("text::param") == 7, "actual child subtree retirement preserves the containing controller and the live child command map");
		text.Play("nested");
		selected.RebuildChildNodes(1, inner, inner_prepared, slots);
		parameters = text.ParameterValues().CurrentTimelineParameters(inner);
		Require(parameters->Entries()[0].Position == 8, "a new actual child selection reads its own retained local value before the containing descriptor");
		commands.ParameterValues().WriteLocal("text::param", 4, 11);
		Require(parameters->Entries()[0].Position == 14 && parameters->Entries()[0].ControlFlags == 11, "controller target association remains valid across actual child selected-node replacement");
		selected.Retire();
		commands.ParameterValues().WriteLocal("text/param", 6, 0);
		Require(commands.ParameterValues().ReadLocal("text::param") == 6 && commands.ParameterValues().RegisteredParameterCount() == 0, "retiring actual child owners clears all controller borrows before a later descriptor write");
	}
	{
		sgre::scene::MotionSelectedLayerNodes selected(commands, root, render, outer, prepared, identifiers, construction, slots);
		Require(selected.Child(1).Commands().ParameterValues().Read("param") == 6, "a real replacement containing tree rebinds retained descriptor values to new actual child owners");
	}
	Require(identifiers.LiveCount() == 0, "descriptor ownership must not retain selected IDs after both real trees retire");
	std::cout << "actual descriptor maps, child routing, hidden/stopped fanout, initialization and retirement PASS\n";
}

static void TestNativeDeepCloneOwnership() {
	std::array<InputNode, 3> main{};
	main[1].Kind = 2;
	main[2].Kind = 0;
	std::array<InputNode, 2> nested{};
	nested[0].Kind = 2;
	nested[1].Kind = 0;
	const auto resource = Resource(main, nested);
	resource->Objects[0].Timelines[0].Layers[0].Properties.emplace_back("uid", Atom(std::string("parent")));
	resource->Objects[0].Timelines[1].Layers[1].Properties.emplace_back("uid", Atom(std::string("child")));
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto registration = Register(manager, resource);
	sgre::scene::MotionResourceManager other_manager(cache);
	sgre::scene::MotionProjectIdentity unrelated;
	unrelated.DocumentInstance = 2;
	unrelated.RootOffset = 128;
	static_cast<void>(other_manager.Register(unrelated, resource, {}));
	const auto other_registration = Register(other_manager, resource);
	sgre::scene::MotionPlayerCommands other_commands(other_manager);
	sgre::scene::MotionRootScene other_root;
	sgre::scene::MotionLayerIdentifierPool other_identifiers;
	std::unique_ptr<sgre::scene::MotionSelectedLayerNodes> other_copy;
	const auto& timeline = resource->Objects[0].Timelines[0];
	const auto& inner = resource->Objects[0].Timelines[1];
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *resource);
	const auto inner_prepared = sgre::scene::PrepareMotionTimeline(inner, *resource);
	sgre::scene::MotionLayerIdentifierPool identifiers;
	sgre::scene::MotionPlayerRenderControls render;
	render.ColorFilter = 5;
	render.ColorFilterParameter = 9;
	sgre::scene::MotionPlayerCommands destination(manager);
	sgre::scene::MotionRootScene copied_root;
	std::unique_ptr<sgre::scene::MotionSelectedLayerNodes> copied;
	std::size_t samples = 0;
	std::size_t source_actions = 0;
	std::size_t copied_actions = 0;
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [&](const auto&) {
		++samples;
		return 0.25f;
	};
	sgre::scene::MotionChildFrameSlotServices queue;
	queue.EnqueueAction = [&](auto, auto) {
		++source_actions;
	};
	sgre::scene::MotionChildFrameSlotServices destination_queue;
	destination_queue.EnqueueAction = [&](auto, auto) {
		++copied_actions;
	};
	std::optional<sgre::scene::MotionChildBorrow> old_borrow;
	std::optional<sgre::scene::MotionChildBorrow> new_borrow;
	{
		sgre::scene::MotionPlayerCommands source(manager);
		source.SetCharacter("a");
		source.Play("main");
		source.SetTickCount(2.75f);
		source.ParameterValues().WriteLocal("plain", 3.5f, 0x41);
		sgre::scene::MotionRootScene root;
		root.SetVisible(true);
		root.SetCoordinate3({3, 4, 5});
		root.Compose(true);
		sgre::scene::MotionSelectedLayerNodes selected(source, root, render, timeline, prepared, identifiers, construction, queue);
		auto& child = selected.Child(0);
		child.Commands().SetCharacter("a");
		child.Commands().Play("nested");
		child.Commands().SetTickCount(1.5f);
		child.Root().SetZoom(2, 3);
		static_cast<void>(child.Random().NextUnitFloat());
		selected.RebuildChildNodes(0, inner, inner_prepared, queue);
		selected.ChildSlots(0).Select(sgre::scene::MotionFrameTraversal::Seek, 2.75f);
		selected.ChildSlots(0).CompleteComposition();
		old_borrow = child.Borrow();
		const auto child_random = child.Random().State();
		const auto child_slots = selected.ChildSlots(0).State();
		Require(identifiers.LiveCount() == 5 && samples == 1, "clone fixture requires the genuine selected kind3 subtree before copy");

		destination.CopyForClone(source);
		copied_root = root;
		copied = std::make_unique<sgre::scene::MotionSelectedLayerNodes>(destination, copied_root, render, selected, identifiers, construction, destination_queue);
		new_borrow = copied->Child(0).Borrow();
		other_commands.CopyForClone(source);
		other_root = root;
		other_copy = std::make_unique<sgre::scene::MotionSelectedLayerNodes>(other_commands, other_root, render, selected, other_identifiers, construction, destination_queue);
		Require(other_commands.SelectedProject()->Project == other_registration && other_registration != source.SelectedProject()->Project && other_copy->Nodes()[0].Identifier == 1 && other_copy->ChildNodes(0)->Nodes()[0].Identifier == 2 && other_copy->Nodes()[1].Identifier == 4 && other_identifiers.LiveCount() == 5, "cross-manager copy must resolve the actual shared document but acquire independent destination IDs, including valid numeric collisions");
		Require(&other_copy->Child(0).Commands().Manager() == &other_manager && &other_copy->Child(0).UltimateParentCommands() == &other_commands && other_copy->Child(0).Commands().CachedTimeline() == child.Commands().CachedTimeline(), "cross-manager nested capsules must rebind exact containing manager/parents while retaining immutable selected atoms");
		sgre::scene::MotionPlayerCommands failed_commands(manager);
		failed_commands.CopyForClone(source);
		sgre::scene::MotionLayerIdentifierPool exhausted(4);
		RequireFailure([&] {
			sgre::scene::MotionSelectedLayerNodes failed(failed_commands, copied_root, render, selected, exhausted, construction, destination_queue);
		},
		    "identifier");
		Require(exhausted.LiveCount() == 0 && failed_commands.LayerUids().RegisteredCount() == 0 && selected.Nodes().size() == 3 && old_borrow->IsLive(), "partial recursive clone failure must retire its new IDs/UID registrations without touching source ownership");
		Require(copied->Nodes()[0].Identifier == 6 && copied->ChildNodes(0)->Nodes()[0].Identifier == 7 && copied->ChildNodes(0)->Nodes()[1].Identifier == 8 && copied->Nodes()[1].Identifier == 9 && copied->Nodes()[2].Identifier == 10, "native clone IDs must descend into the child before acquiring the next parent-layer ID");
		Require(destination.Names().CurrentMotion.empty() && destination.Names().RequestedMotion == "main" && destination.ClockState().TickCount == 2.75f && source.Names().CurrentMotion == "main", "selected ownership copy must preserve the native retained selection/current-name distinction without Play");
		Require(&copied->Child(0).ImmediateParentCommands() == &destination && &copied->Child(0).UltimateParentCommands() == &destination && copied->Child(0).Commands().Names().CurrentMotion.empty() && copied->Child(0).Commands().ClockState().TickCount == 1.5f, "copied nested commands must link to new actual parents and retain live child clock");
		Require(copied->Child(0).Random().State() == child_random && copied->Child(0).Root().Overrides().ZoomX == 2 && copied->Child(0).Root().Overrides().ZoomY == 3 && copied->Child(0).RenderControls().ColorFilterParameter == 9 && samples == 1, "clone must copy actual child pose/render/RNG without manager sampling or evaluation");
		const auto& slots = copied->ChildSlots(0).State();
		Require(slots.FrameIndices == child_slots.FrameIndices && slots.CurrentSlot == child_slots.CurrentSlot && slots.Initialized == child_slots.Initialized && slots.FrameDirty == child_slots.FrameDirty && &copied->ChildSlots(0).Child() == &copied->Child(0), "clone must retain both decoded physical slots and acknowledge state with an independent child binding");
		Require(old_borrow->IsLive() && new_borrow->IsLive() && !old_borrow->SameTarget(*new_borrow), "cloned actual child must have a fresh weak lifetime identity");
		Require(destination.LayerUids().RegisteredCount() == 2 && destination.LayerUids().SubscriptionCount() == 0 && destination.LayerUids().Matches("parent").front().Layer.Node().Identifier == 6 && destination.LayerUids().Matches("child").front().Layer.Node().Identifier == 8, "UID occurrences must register destination player/node identities without copying subscriptions");
		source.ParameterValues().WriteLocal("plain", 12, 0);
		child.Root().SetZoom(7, 8);
		static_cast<void>(child.Random().NextUnitFloat());
		Require(destination.ParameterValues().ReadLocal("plain") == 3.5f && copied->Child(0).Root().Overrides().ZoomX == 2 && copied->Child(0).Random().State() == child_random && source_actions == 0 && copied_actions == 0, "cloned state must not alias source values or execute source/destination actions during copy");
	}
	Require(!old_borrow->IsLive() && new_borrow->IsLive() && identifiers.LiveCount() == 5 && copied->ChildNodes(0)->Nodes().size() == 2, "source-first retirement must leave the cloned selected graph and child weak borrow alive");
	other_copy.reset();
	Require(other_identifiers.LiveCount() == 0 && other_commands.LayerUids().RegisteredCount() == 0, "cross-manager clone retires destination IDs/UIDs independently after source retirement");
	copied.reset();
	Require(!new_borrow->IsLive() && identifiers.LiveCount() == 0 && destination.LayerUids().RegisteredCount() == 0 && manager.ReferenceCount(registration) == 1, "clone retirement must release only its fresh IDs/registrations without retaining resource ownership");
}

static void TestClonedChildTargetReceipt() {
	std::array<InputNode, 1> main{};
	std::array<InputNode, 1> nested{};
	nested[0].Kind = 2;
	const auto model = Resource(main, nested);
	auto& raw = model->Objects[0].Timelines[0].Layers[0];
	const auto content = Atom(sgre::resource::PsbObject{{"mask", Atom(std::int64_t{0x20603})}, {"src", Atom(std::string("a"))}, {"icon", Atom(std::string("nested"))}, {"coord", Atom(sgre::resource::PsbArray{Atom(0.0), Atom(0.0), Atom(0.0)})}, {"ox", Atom(0.0)}, {"oy", Atom(0.0)}, {"opa", Atom(std::int64_t{255})}, {"color", Atom(std::int64_t{0x808080ff})}, {"bm", Atom(std::int64_t{0x10})}});
	auto present = HiddenFrame(0);
	present.Type = 2;
	present.Content = content;
	auto same_target = present;
	same_target.Time = 4;
	raw.Frames = {present, same_target, HiddenFrame(8)};
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	static_cast<void>(Register(manager, model));
	sgre::scene::MotionPlayerCommands source(manager);
	source.SetCharacter("a");
	source.Play("main");
	sgre::scene::MotionPlayerCommands copied_commands(manager);
	sgre::scene::MotionPlayerCommands repeated_commands(manager);
	sgre::scene::MotionRootScene root;
	sgre::scene::MotionPlayerRenderControls render;
	sgre::scene::MotionLayerIdentifierPool identifiers;
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [](const auto&) {
		return 0.5f;
	};
	sgre::scene::MotionChildFrameSlotServices queue;
	queue.EnqueueAction = [](auto, auto) {
	};
	const auto& timeline = *source.CachedTimeline();
	const auto& inner = model->Objects[0].Timelines[1];
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *model);
	const auto nested_prepared = sgre::scene::PrepareMotionTimeline(inner, *model);
	sgre::scene::MotionSelectedLayerNodes selected(source, root, render, timeline, prepared, identifiers, construction, queue);
	auto& child = selected.Child(0);
	child.Commands().SetCharacter("a");
	child.Commands().Play("nested");
	selected.RebuildChildNodes(0, inner, nested_prepared, queue);
	selected.ChildSlots(0).Select(sgre::scene::MotionFrameTraversal::Seek, 0);
	selected.ChildSlots(0).CompleteComposition();
	Require(!selected.ChildSlots(0).HasClonedSelectedTarget(), "ordinary selected controls must not acquire a cloned-child receipt");
	copied_commands.CopyForClone(source);
	sgre::scene::MotionRootScene copied_root = root;
	sgre::scene::MotionSelectedLayerNodes copied(copied_commands, copied_root, render, selected, identifiers, construction, queue);
	Require(copied.Child(0).Commands().Names().CurrentMotion.empty() && copied.ChildSlots(0).HasClonedSelectedTarget(), "literal native empty currentMotion must retain only the actual copied selected frame/atom receipt");
	copied.Child(0).Commands().SetTickCount(2);
	Require(copied.ChildSlots(0).HasClonedSelectedTarget(), "an assigned child tick does not replace its copied selected timeline or physical parent frame");
	repeated_commands.CopyForClone(copied_commands);
	sgre::scene::MotionRootScene repeated_root = copied_root;
	sgre::scene::MotionSelectedLayerNodes repeated(repeated_commands, repeated_root, render, copied, identifiers, construction, queue);
	Require(repeated.ChildSlots(0).HasClonedSelectedTarget(), "copying a genuine unchanged clone must preserve its proven target provenance independently");
	selected.Retire();
	Require(copied.ChildSlots(0).HasClonedSelectedTarget() && repeated.ChildSlots(0).HasClonedSelectedTarget() && copied.ChildNodes(0) != nullptr, "retiring the source selected child must not retire either independent clone's retained target provenance");
	copied.ChildSlots(0).Select(sgre::scene::MotionFrameTraversal::Forward, 4);
	Require(!copied.ChildSlots(0).HasClonedSelectedTarget(), "a physical crossing alone must not authorize the retained cloned target");
	copied.ChildSlots(0).Select(sgre::scene::MotionFrameTraversal::Reverse, 0);
	Require(!copied.ChildSlots(0).HasClonedSelectedTarget(), "returning to the same frame without an actual command must not revive dormant clone provenance");
	const auto retained_serial = copied.Child(0).Commands().SelectionSerial();
	const auto* retained_timeline = copied.Child(0).Commands().CachedTimeline();
	const auto* retained_nodes = copied.ChildNodes(0);
	copied.ChildSlots(0).Select(sgre::scene::MotionFrameTraversal::Forward, 4);
	const auto action = sgre::scene::SelectMotionChildFrameCommands(copied.ChildSlots(0), copied_commands.ClockState(), true, 0, [] {
		throw std::runtime_error("same-target copied child must not retire its actual selected tree");
	});
	Require(action == sgre::scene::MotionChildFrameSelection::BindRoot && copied.ChildSlots(0).HasClonedSelectedTarget(), "a genuine same-target ordinary Play must renew copied selection provenance for its new physical frame");
	Require(copied.Child(0).Commands().SelectionSerial() == retained_serial && copied.Child(0).Commands().CachedTimeline() == retained_timeline && copied.ChildNodes(0) == retained_nodes && copied.Child(0).Commands().Names().CurrentMotion.empty(), "native ordinary no-op Play must retain the actual selected owner and empty copied CurrentMotion without reconstruction");
	copied.ChildSlots(0).CompleteComposition();
	copied.ChildSlots(0).Select(sgre::scene::MotionFrameTraversal::Reverse, 0);
	Require(!copied.ChildSlots(0).HasClonedSelectedTarget(), "reverse physical crossing must also require the real repeated command result");
	static_cast<void>(sgre::scene::SelectMotionChildFrameCommands(copied.ChildSlots(0), copied_commands.ClockState(), true, 0, [] {
		throw std::runtime_error("same-target reverse command must not retire its actual selected tree");
	}));
	Require(copied.ChildSlots(0).HasClonedSelectedTarget() && copied.Child(0).Commands().SelectionSerial() == retained_serial && copied.ChildNodes(0) == retained_nodes, "a reverse same-target no-op must renew the same exact copied selected owner");
	copied.Child(0).Commands().Play("nested", sgre::scene::MotionPlayFlags::Force);
	Require(!copied.ChildSlots(0).HasClonedSelectedTarget(), "a Force selection after genuine no-op renewal must invalidate the actual copied selection serial");
	repeated.Child(0).Commands().Play("nested", sgre::scene::MotionPlayFlags::Force);
	Require(!repeated.ChildSlots(0).HasClonedSelectedTarget(), "a real Force selection must invalidate the copied selected timeline serial receipt");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: selected_node_tests [native fixture]");
		TestOwnershipAndSlots();
		TestNativeDeepCloneOwnership();
		TestClonedChildTargetReceipt();
		TestActualDescriptorVariableTargets();
		TestRejectionAndFailure();
		TestActualQueueMutationGuard();
		TestJoinEligibilityWithoutSnapshots();
		if (argc == 2) {
			Replay(argv[1]);
		}
		std::cout << "selected-node independent ownership, slot, rejection and failure fixtures PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
