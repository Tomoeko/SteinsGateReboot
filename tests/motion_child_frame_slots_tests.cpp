#include "sgre/scene/motion_child_frame_slots.hpp"

#include "sgre/scene/motion_child_construction.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
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
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unsupported child construction state was accepted");
}

static std::uint32_t Bits(float value) {
	return std::bit_cast<std::uint32_t>(value);
}

static sgre::scene::TextureCache SourceFreeCache() {
	return sgre::scene::TextureCache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("source-free child fixture attempted an upload");
	},
	    [](auto) {
		    throw std::runtime_error("source-free child fixture attempted a texture release");
	    });
}

static sgre::resource::MotionLayer Raw(bool independent) {
	sgre::resource::MotionLayer layer;
	layer.Type = 3;
	layer.Coordinate = 1;
	layer.TransformOrder = {2, 3, 1, 0};
	sgre::resource::PsbValue flag;
	flag.Data = independent;
	layer.Properties.emplace_back("motionIndependentLayerInherit", std::move(flag));

	return layer;
}

static sgre::scene::PreparedMotionLayer Prepared() {
	sgre::scene::PreparedMotionLayer layer;
	layer.Type = 3;
	layer.CoordinatePlane = 1;
	layer.TransformOrder = {sgre::scene::MotionTransformOperation::Scale, sgre::scene::MotionTransformOperation::Rotate, sgre::scene::MotionTransformOperation::Shear, sgre::scene::MotionTransformOperation::Flip};

	return layer;
}

static sgre::scene::MotionProjectId Register(sgre::scene::MotionResourceManager& manager) {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "idle";
	timeline.LastTime = 20;
	sgre::resource::MotionObject character;
	character.Name = "a";
	character.Timelines.push_back(std::move(timeline));
	resource->Objects.push_back(std::move(character));
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = 1;
	identity.RootOffset = 128;

	return manager.Register(identity, resource, {});
}

static void RequireZeroControl(const sgre::scene::MotionChildFrameControl& control) {
	Require(control.Flags == sgre::scene::MotionPlayFlags::None && control.DirectionMode == 0 && Bits(control.DirectionOffset) == 0 && !control.CompleteDirection && control.DirectionTarget.empty() && Bits(control.TimeOffset) == 0 && !control.Specified, "the actual two initial decoded controls must be zeroed independently");
}

static void TestActualOwnedConstruction() {
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto registration = Register(manager);
	sgre::scene::MotionPlayerCommands parent(manager);
	parent.SetCharacter("a");
	parent.Play("idle");
	parent.SetTickCount(11);
	parent.SetSpeed(-2);
	sgre::scene::MotionRootScene root;
	root.SetPriority(-3.5f);
	root.SetVisible(true);
	root.SetCoordinate3({11, 12, 13});
	sgre::scene::MotionPlayerRenderControls render;
	render.ColorFilter = 0xffffffee;
	render.ColorFilterParameter = 43;
	render.EmissiveColor = 0x87654321;
	render.AmbientFactor = 0.25f;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> seed{};
	sgre::scene::MotionSharedRandom shared(seed);
	std::size_t reads = 0;
	sgre::scene::MotionChildConstructionServices services;
	services.ReadManagerRandom = [&](const auto& sampled_manager) {
		Require(&sampled_manager == &manager, "child sampling must borrow its actual parent manager");
		++reads;

		return shared.NextUnitFloat();
	};
	const auto layer = Raw(true);
	const auto prepared = Prepared();
	const auto expected_sample = std::bit_cast<float>(std::uint32_t{0x3f5090fe});
	sgre::scene::MotionPlayerRandom expected_random(expected_sample);
	{
		auto child = std::make_unique<sgre::scene::MotionChildConstructionState>(parent, root, render, layer, prepared, services);
		Require(reads == 1 && &child->Commands().Manager() == &manager && &child->ImmediateParentCommands() == &parent && &child->UltimateParentCommands() == &parent, "child must sample once and borrow its real parent and manager without substituting a task/root owner");
		Require(child->Commands().PreferredProject() == parent.PreferredProject() && !child->Commands().SelectedProject() && child->Commands().CachedTimeline() == nullptr && child->Commands().Names().CurrentCharacter.empty() && child->Commands().Names().CurrentMotion.empty(), "constructor copies preferred project independently of names and cached timeline");
		Require(Bits(child->Commands().ClockState().Speed) == Bits(1) && Bits(child->Commands().ClockState().TickCount) == 0 && child->Commands().ClockState().InitialCompositionPending, "child clock must use real constructor defaults independently of playing parent");
		Require(child->Root().MatrixMode() == sgre::scene::MotionRootMatrixMode::SuppliedByParent && Bits(child->Root().Priority()) == Bits(-3.5f) && child->Root().TransformOrder() == prepared.TransformOrder && child->Root().Frame().Hierarchy.CoordinatePlane == 1 && child->IndependentLayerInherit(), "child root must couple actual plane/order/priority/418 and supplied-matrix configuration");
		Require(!child->Root().Overrides().Visible && child->Root().Overrides().Coordinate == std::array<float, 3>{}, "child constructor must not clone the parent root's visible/coordinate overrides");
		Require(child->RenderControls().ColorFilter == 0xffffffee && child->RenderControls().ColorFilterParameter == 43 && child->RenderControls().EmissiveColor == 0x87654321 && Bits(child->RenderControls().AmbientFactor) == Bits(0.25f), "child copies color-filter mode/parameter, emissive color and ambient factor as separate values");
		Require(child->Random().State() == expected_random.State(), "child owns the actual independent xorshift state initialized from one shared manager sample");
		RequireZeroControl(child->Control(0));
		RequireZeroControl(child->Control(1));
		std::string actual_target = "target";
		sgre::scene::MotionChildFrameControl update;
		update.DirectionMode = 4;
		update.DirectionTarget = actual_target;
		update.CompleteDirection = true;
		update.Specified = true;
		child->ApplyControlUpdate(1, update);
		child->ApplyControlUpdate(1, std::nullopt);
		Require(child->Control(1).DirectionTarget.data() == actual_target.data() && child->Control(1).Specified && child->Control(1).DirectionMode == 4, "reused control slot must borrow its actual immutable target and retain it on a missing update");
		RequireZeroControl(child->Control(0));
		RequireFailure([&] {
			static_cast<void>(child->Control(2));
		},
		    "slot");
		RequireFailure([&] {
			child->ApplyControlUpdate(2, std::nullopt);
		},
		    "slot");
		RequireFailure([&] {
			child->Root().Compose(true);
		},
		    "pose");
		child->Commands().SetCharacter("a");
		child->Commands().Play("idle");
		child->Root().SetPriority(7);
		auto nested = std::make_unique<sgre::scene::MotionChildConstructionState>(*child, layer, prepared, services);
		Require(reads == 2 && &nested->ImmediateParentCommands() == &child->Commands() && &nested->UltimateParentCommands() == &parent && Bits(nested->Root().Priority()) == Bits(7), "nested child borrows its real immediate parent and preserves ultimate parent identity");
		Require(nested->Commands().PreferredProject() == child->Commands().PreferredProject() && nested->Commands().CachedTimeline() == nullptr && nested->Commands().Names().CurrentMotion.empty(), "nested constructor copies preferred project only");
		Require(nested->Random().State() != child->Random().State(), "each child consumes one shared sample and owns an independent player stream");
		const auto nested_random = nested->Random().State();
		static_cast<void>(child->Random().NextUnitFloat());
		Require(nested->Random().State() == nested_random, "player random generation must not mutate sibling/descendant streams");
		Require(manager.ReferenceCount(registration) == 1, "child construction must not invent a resource registration pin");
		nested.reset();
		Require(parent.ClockState().Playing && child->Commands().ClockState().Playing && manager.ReferenceCount(registration) == 1, "exclusive constructor-state retirement must preserve live parent state and manager registration");
	}

	Require(manager.ReferenceCount(registration) == 1 && parent.ClockState().Playing, "constructor-state destruction does not unregister its borrowed parent resources");
	RequireFailure([&] {
		sgre::scene::MotionChildConstructionState rejected(parent, root, render, layer, prepared, {});
	},
	    "sampling");
	services.ReadManagerRandom = [](const auto&) {
		return -1.0f;
	};
	RequireFailure([&] {
		sgre::scene::MotionChildConstructionState rejected(parent, root, render, layer, prepared, services);
	},
	    "unit");
}

class Reader {
public:
	explicit Reader(std::istream& stream) : m_Stream(stream) {
	}

	std::uint8_t Byte() {
		const auto value = m_Stream.get();
		if (value == std::char_traits<char>::eof()) {
			throw std::runtime_error("short child constructor fixture");
		}

		return static_cast<std::uint8_t>(value);
	}

	std::uint32_t Word() {
		std::uint32_t value = 0;
		for (std::uint32_t shift = 0; shift != 32; shift += 8) {
			value |= static_cast<std::uint32_t>(Byte()) << shift;
		}

		return value;
	}

private:
	std::istream& m_Stream;
};

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue atom;
	atom.Data = std::move(value);

	return atom;
}

static sgre::resource::MotionFrame MakeFrame(float time, std::uint32_t type, bool present, std::uint32_t motion_mask, std::uint32_t flags, std::uint32_t direction, float offset, bool complete, std::string target, float time_offset, std::uint8_t action) {
	sgre::resource::MotionFrame frame;
	frame.Time = time;
	frame.Type = type;
	sgre::resource::PsbObject motion;
	motion.emplace_back("mask", Atom(static_cast<std::int64_t>(motion_mask)));
	motion.emplace_back("flags", Atom(static_cast<std::int64_t>(flags)));
	motion.emplace_back("dt", Atom(static_cast<std::int64_t>(direction)));
	motion.emplace_back("dofst", Atom(static_cast<double>(offset)));
	motion.emplace_back("docmpl", Atom(complete));
	motion.emplace_back("dtgt", Atom(std::move(target)));
	motion.emplace_back("timeOffset", Atom(static_cast<double>(time_offset)));
	sgre::resource::PsbObject content;
	content.emplace_back("mask", Atom(static_cast<std::int64_t>((present ? 0x80000 : 0) | (action != 0 ? 0x40000 : 0))));
	content.emplace_back("motion", Atom(std::move(motion)));
	content.emplace_back("src", Atom(std::string("source")));
	content.emplace_back("icon", Atom(std::string("icon")));
	content.emplace_back("act", Atom(std::string(action == 2 ? "" : "act")));
	frame.Content = Atom(std::move(content));

	return frame;
}

static void AddFrame(sgre::resource::MotionLayer& raw, sgre::scene::PreparedMotionLayer& prepared, sgre::resource::MotionFrame frame, std::uint32_t mask) {
	sgre::scene::PreparedMotionFrame value;
	value.Time = frame.Time;
	value.Hidden = frame.Type == 0;
	value.Tween = frame.Type == 3;
	value.Attributes.Mask = mask;
	value.Attributes.Source = "source";
	value.Attributes.Icon = "icon";
	raw.Frames.push_back(std::move(frame));
	prepared.Frames.push_back(std::move(value));
}

using ControlWords = std::array<std::uint32_t, 6>;

static ControlWords Words(const sgre::scene::MotionChildFrameControl& control) {
	ControlWords words{};
	words[0] = static_cast<std::uint32_t>(control.Flags);
	words[1] = control.DirectionMode;
	words[2] = Bits(control.DirectionOffset);
	words[3] = control.CompleteDirection ? 1u : 0u;
	words[4] = Bits(control.TimeOffset);
	Require(control.DirectionTarget == "" || control.DirectionTarget == "target" || control.DirectionTarget == "alt", "unrecognized native target text");
	words[5] = control.DirectionTarget == "target" ? 1u : control.DirectionTarget == "alt" ? 2u
	                                                                                       : 0u;

	return words;
}

static void TestSlotReuseAndOrder() {
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands parent(manager);
	sgre::scene::MotionRootScene root;
	auto raw = Raw(false);
	auto prepared = Prepared();
	raw.Label = "label";
	prepared.Label = raw.Label;
	AddFrame(raw, prepared, MakeFrame(0, 2, true, 3, 1, 1, 0, false, "", 0, 1), 0xc0000);
	AddFrame(raw, prepared, MakeFrame(1, 2, true, 3, 2, 2, 0, false, "", 0, 1), 0xc0000);
	AddFrame(raw, prepared, MakeFrame(1, 2, true, 3, 4, 3, 0, false, "", 0, 2), 0xc0000);
	AddFrame(raw, prepared, MakeFrame(2, 2, false, 0, 0, 0, 0, false, "", 0, 1), 0x40000);
	AddFrame(raw, prepared, MakeFrame(3, 0, false, 0, 0, 0, 0, false, "", 0, 0), 0);
	const sgre::resource::MotionResource actual_resource;
	prepared = sgre::scene::PrepareMotionLayer(raw, actual_resource);
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [](const auto&) {
		return 0.0f;
	};
	sgre::scene::MotionChildConstructionState child(parent, root, {}, raw, prepared, construction);
	std::vector<std::string> actions;
	std::vector<std::array<ControlWords, 2>> action_controls;
	sgre::scene::MotionChildFrameSlotServices services;
	services.EnqueueAction = [&](auto label, auto action) {
		Require(label == "label", "selected actions must use the actual raw node label");
		actions.emplace_back(action);
		action_controls.push_back({Words(child.Control(0)), Words(child.Control(1))});
	};
	sgre::scene::MotionChildFrameSlots slots(raw, prepared, 3, child, services);
	RequireFailure([&] {
		static_cast<void>(slots.State());
	},
	    "published");
	RequireFailure([&] {
		slots.Select(sgre::scene::MotionFrameTraversal::Forward, 0);
	},
	    "initial seek");
	slots.Select(sgre::scene::MotionFrameTraversal::Seek, 0);
	Require(slots.State().CurrentSlot == 0 && slots.State().FrameIndices == std::array<std::size_t, 2>{0, 1} && slots.State().FrameDirty && actions == std::vector<std::string>{"act"}, "seek must load and prepare both slots before exact-time action");
	Require(action_controls[0][0][0] == 1 && action_controls[0][1][0] == 2, "seek action must observe freshly prepared controls");
	slots.CompleteComposition();
	actions.clear();
	action_controls.clear();
	slots.Select(sgre::scene::MotionFrameTraversal::Forward, 2);
	Require(actions == std::vector<std::string>{"act", "", "act"} && slots.State().CurrentSlot == 1 && slots.State().FrameIndices == std::array<std::size_t, 2>{4, 3}, "forward must cross equality/duplicate times with real slot toggles and all entered actions");
	Require(std::all_of(action_controls.begin(), action_controls.end(), [](const auto& pair) {
		return pair[0][0] == 1 && pair[1][0] == 2;
	}),
	    "all crossing actions queue before preparing final decoded slots");
	Require(Words(child.Control(0))[0] == 1 && Words(child.Control(1))[0] == 2, "intermediate specified motion metadata must not overwrite retained controls when both final decoded frames omit metadata");
	Require(&slots.CurrentFrame() == &prepared.Frames[3] && &slots.FollowingFrame() == &prepared.Frames[4] && &slots.CurrentControl() == &child.Control(1) && &slots.FollowingControl() == &child.Control(0), "published frames/controls must couple actual current/following owners by slot identity");
	slots.CompleteComposition();
	slots.Select(sgre::scene::MotionFrameTraversal::Forward, 2);
	Require(!slots.State().FrameDirty, "no crossing must preserve clean composition state and existing controls");
	actions.clear();
	action_controls.clear();
	slots.Select(sgre::scene::MotionFrameTraversal::Reverse, 1);
	Require(actions == std::vector<std::string>{""} && slots.State().CurrentSlot == 0 && slots.State().FrameIndices == std::array<std::size_t, 2>{2, 3} && Words(child.Control(0))[0] == 4 && Words(child.Control(1))[0] == 2, "reverse crosses only below current time, prepares final reused slot and retains following absent control");
	Require(action_controls[0][0][0] == 1, "reverse action precedes final control preparation");
	RequireFailure([&] {
		slots.Select(sgre::scene::MotionFrameTraversal::Seek, 4);
	},
	    "domain");
	RequireFailure([&] {
		static_cast<void>(slots.CurrentControl());
	},
	    "published");
	slots.Select(sgre::scene::MotionFrameTraversal::Seek, 1);
	Require(slots.State().FrameIndices == std::array<std::size_t, 2>{1, 2} && slots.State().CurrentSlot == 0 && Words(child.Control(0))[0] == 2 && Words(child.Control(1))[0] == 4, "duplicate-time seek chooses the first exact frame and resets slot identity without clearing retained data");
	RequireFailure([&] {
		sgre::scene::MotionChildFrameSlots rejected(raw, prepared, 3, child, {});
	},
	    "action queue");
	prepared.Frames[1].Time = 1.25f;
	RequireFailure([&] {
		sgre::scene::MotionChildFrameSlots rejected(raw, prepared, 3, child, services);
	},
	    "matching finite");
	prepared.Frames[1].Time = 1;
	services.EnqueueAction = [&](auto, auto) {
		throw std::runtime_error("observed queue failure");
	};
	sgre::scene::MotionChildFrameSlots failed(raw, prepared, 3, child, services);
	RequireFailure([&] {
		failed.Select(sgre::scene::MotionFrameTraversal::Seek, 0);
	},
	    "queue failure");
	RequireFailure([&] {
		static_cast<void>(failed.State());
	},
	    "published");
}

static void ReplaySlots(std::string_view filename) {
	std::ifstream stream(std::string(filename), std::ios::binary);
	Reader reader(stream);
	Require(reader.Word() == 0x3153434d, "wrong child frame-slot fixture magic");
	const auto cases = reader.Word();
	const auto steps = reader.Word();
	Require(cases == 64 && steps == 16, "native child slots fixture requires64 by16 transitions");
	auto cache = SourceFreeCache();
	sgre::scene::MotionResourceManager manager(cache);
	sgre::scene::MotionPlayerCommands parent(manager);
	sgre::scene::MotionRootScene root;
	sgre::scene::MotionChildConstructionServices construction;
	construction.ReadManagerRandom = [](const auto&) {
		return 0.0f;
	};
	for (std::uint32_t instance = 0; instance < cases; ++instance) {
		auto raw = Raw(false);
		auto prepared = Prepared();
		raw.Label = "label";
		prepared.Label = raw.Label;
		const auto frame_count = reader.Word();
		Require(frame_count >= 2 && frame_count <= 8, "invalid child slot frame count");
		for (std::uint32_t index = 0; index < frame_count; ++index) {
			const auto time = std::bit_cast<float>(reader.Word());
			const auto type = reader.Byte();
			const auto action = reader.Byte();
			const bool present = reader.Byte() != 0;
			const auto mask = reader.Byte();
			const auto flags = reader.Word();
			const auto direction = reader.Word();
			const auto offset = std::bit_cast<float>(reader.Word());
			const bool complete = reader.Byte() != 0;
			const auto target = reader.Byte();
			const auto time_offset = std::bit_cast<float>(reader.Word());
			Require(target < 3, "invalid fixture target identity");
			AddFrame(raw, prepared, MakeFrame(time, type, present, mask, flags, direction, offset, complete, target == 1 ? "target" : target == 2 ? "alt"
			                                                                                                                                      : "",
			                            time_offset, action),
			    (present ? 0x80000 : 0) | (action != 0 ? 0x40000 : 0));
		}

		const sgre::resource::MotionResource actual_resource;
		prepared = sgre::scene::PrepareMotionLayer(raw, actual_resource);
		sgre::scene::MotionChildConstructionState child(parent, root, {}, raw, prepared, construction);
		std::vector<std::uint8_t> actions;
		std::vector<std::array<ControlWords, 2>> action_controls;
		sgre::scene::MotionChildFrameSlotServices services;
		services.EnqueueAction = [&](auto label, auto action) {
			Require(label == "label" && (action == "" || action == "act"), "native action owner differs");
			actions.push_back(action.empty() ? 2 : 1);
			action_controls.push_back({Words(child.Control(0)), Words(child.Control(1))});
		};
		sgre::scene::MotionChildFrameSlots slots(raw, prepared, raw.Frames.back().Time, child, services);
		for (std::uint32_t step = 0; step < steps; ++step) {
			actions.clear();
			action_controls.clear();
			const std::array<ControlWords, 2> prior_controls{Words(child.Control(0)), Words(child.Control(1))};
			const auto mode = reader.Byte();
			const auto position = std::bit_cast<float>(reader.Word());
			slots.Select(static_cast<sgre::scene::MotionFrameTraversal>(mode), position);
			Require(slots.State().CurrentSlot == reader.Byte() && slots.State().FrameIndices[0] == reader.Word() && slots.State().FrameIndices[1] == reader.Word(), "native selected indices/slot identity differ");
			std::array<ControlWords, 2> expected{};
			for (auto& control : expected) {
				control[0] = reader.Word();
				control[1] = reader.Word();
				control[2] = reader.Word();
				control[3] = reader.Byte();
				control[4] = reader.Word();
				control[5] = reader.Byte();
			}

			Require(Words(child.Control(0)) == expected[0] && Words(child.Control(1)) == expected[1], "native retained/reused decoded controls differ");
			const auto preparations = reader.Byte();
			const auto event_count = reader.Byte();
			Require(preparations <= 2 && event_count == actions.size(), "native preparation/action count differs");
			std::size_t previous_slot = 0;
			for (std::uint32_t index = 0; index < preparations; ++index) {
				const auto slot = reader.Byte();
				const auto frame = reader.Byte();
				const auto queued = reader.Byte();
				Require(slot < 2 && (index == 0 || slot > previous_slot) && frame == slots.State().FrameIndices[slot] && queued == (mode == 0 ? 0 : event_count), "native final-slot preparation/order differs");
				previous_slot = slot;
			}

			for (const auto action : actions) {
				Require(action == reader.Byte(), "native selected layer action sequence differs");
			}

			for (const auto& controls : action_controls) {
				Require(controls == (mode == 0 ? expected : prior_controls), "native action versus selected control preparation order differs");
			}

			Require(&slots.CurrentFrame() == &prepared.Frames[slots.State().FrameIndices[slots.State().CurrentSlot]] && &slots.CurrentControl() == &child.Control(slots.State().CurrentSlot), "current-slot references must use their actual frame/control owners");
		}
	}

	Require(stream.get() == std::char_traits<char>::eof(), "trailing child slots fixture bytes");
	std::cout << "native child frame slots1024 PASS\n";
}

int main(int argc, char** argv) {
	try {
		TestActualOwnedConstruction();
		TestSlotReuseAndOrder();
		if (argc == 2) {
			ReplaySlots(argv[1]);
		} else if (argc != 1) {
			throw std::runtime_error("usage: motion_child_frame_slots_tests [native-fixture]");
		}

		std::cout << "motion child frame slots PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
