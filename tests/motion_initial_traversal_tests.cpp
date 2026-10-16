#include "sgre/scene/motion_child_initial_timing.hpp"

#include "sgre/scene/motion_priority_cursor.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
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
static void RequireFailure(Operation operation, std::string_view text) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(text) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("unsupported initial-traversal operation was accepted");
}

template <typename Value>
static sgre::resource::PsbValue Atom(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static const std::array<std::string_view, 11> kStrings = {"", "n1", "a", "main", "l0", "l1", "l2", "l3", "a0", "a1", "a2"};

struct FrameInput {
	float Time = 0;
	bool ControlPresent = true;
	std::uint32_t Flags = 0;
	std::uint32_t Direction = 0;
	float Offset = 0;
	std::uint8_t Action = 0;
};

struct CaseInput {
	float LoopTime = -1;
	std::array<std::uint8_t, 3> TagFlags = {9, 9, 9};
	std::uint32_t ManagerSample = 0x3f000000;
	std::array<FrameInput, 5> Frames;
};

static sgre::resource::MotionFrame LayerFrame(const FrameInput& input, bool terminal) {
	sgre::resource::MotionFrame frame;
	frame.Time = input.Time;
	frame.Type = terminal ? 0 : 2;
	sgre::resource::PsbObject motion;
	motion.emplace_back("mask", Atom(std::int64_t{3}));
	motion.emplace_back("flags", Atom(static_cast<std::int64_t>(input.Flags)));
	motion.emplace_back("dt", Atom(static_cast<std::int64_t>(input.Direction)));
	motion.emplace_back("timeOffset", Atom(static_cast<double>(input.Offset)));
	sgre::resource::PsbObject content;
	content.emplace_back("mask", Atom(static_cast<std::int64_t>(0x40000 | (input.ControlPresent ? 0x80000 : 0))));
	content.emplace_back("act", Atom(std::string(kStrings.at(input.Action))));
	content.emplace_back("src", Atom(std::string("a")));
	content.emplace_back("icon", Atom(std::string("main")));
	content.emplace_back("motion", Atom(std::move(motion)));
	frame.Content = Atom(std::move(content));

	return frame;
}

static std::shared_ptr<sgre::resource::MotionResource> MakeResource(const CaseInput& input) {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionLayer layer;
	layer.Label = "n1";
	layer.Type = 3;
	layer.TransformOrder = {0, 3, 2, 1};
	layer.Properties.emplace_back("meshTransform", Atom(std::int64_t{0}));
	layer.Properties.emplace_back("parameterize", Atom(std::monostate{}));
	layer.Properties.emplace_back("joinTarget", Atom(false));
	layer.Properties.emplace_back("groundCorrection", Atom(false));
	layer.Properties.emplace_back("motionIndependentLayerInherit", Atom(false));
	for (std::size_t index = 0; index < input.Frames.size(); ++index) {
		layer.Frames.push_back(LayerFrame(input.Frames[index], index + 1 == input.Frames.size()));
	}
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "main";
	timeline.LastTime = 8;
	timeline.LoopTime = input.LoopTime;
	timeline.LayerIndexMap = Atom(sgre::resource::PsbObject{});
	timeline.Layers.push_back(std::move(layer));
	sgre::resource::MotionFrame first_priority;
	first_priority.Type = 1;
	first_priority.Content = Atom(sgre::resource::PsbArray{Atom(std::int64_t{0})});
	sgre::resource::MotionFrame terminal_priority;
	terminal_priority.Time = 8;
	terminal_priority.Type = 1;
	timeline.Priority = {first_priority, terminal_priority};
	const std::array<float, 4> tag_times = {0, 2, 4, 8};
	for (std::size_t index = 0; index < tag_times.size(); ++index) {
		const auto flags = index < input.TagFlags.size() ? input.TagFlags[index] : 0;
		sgre::resource::MotionFrame tag;
		tag.Time = tag_times[index];
		tag.Type = (flags & 1) != 0 ? 1 : 0;
		sgre::resource::PsbObject content;
		content.emplace_back("align", Atom((flags & 2) != 0));
		content.emplace_back("sync", Atom((flags & 4) != 0));
		content.emplace_back("action", Atom(std::string((flags & 8) != 0 ? kStrings[index + 8] : kStrings[0])));
		tag.Content = Atom(std::move(content));
		timeline.Tags.push_back(std::move(tag));
	}
	sgre::resource::MotionObject character;
	character.Name = "a";
	character.Timelines.push_back(std::move(timeline));
	resource->Objects.push_back(std::move(character));

	return resource;
}

static sgre::scene::TextureCache SourceFreeCache() {
	return sgre::scene::TextureCache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("initial traversal fixture attempted texture upload");
	},
	    [](auto) {
		    throw std::runtime_error("initial traversal fixture attempted texture release");
	    });
}

static sgre::scene::MotionProjectId Register(sgre::scene::MotionResourceManager& manager, std::shared_ptr<const sgre::resource::MotionResource> resource) {
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = 1;
	identity.RootOffset = 128;

	return manager.Register(identity, std::move(resource), {});
}

struct Phase {
	std::uint8_t Kind = 0;
	std::uint32_t EvaluationBits = 0;
	bool operator==(const Phase&) const = default;
};

struct Event {
	std::uint8_t Kind = 0;
	std::uint8_t Label = 0;
	std::uint8_t Action = 0;
	bool operator==(const Event&) const = default;
};

static std::uint8_t StringIndex(std::string_view text) {
	for (std::size_t index = 0; index < kStrings.size(); ++index) {
		if (kStrings[index] == text) {
			return static_cast<std::uint8_t>(index);
		}
	}

	throw std::runtime_error("unexpected initial-traversal synthetic event text");
}

class Scene {
public:
	explicit Scene(const CaseInput& input) : m_Resource(MakeResource(input)), m_Cache(SourceFreeCache()), m_Manager(m_Cache), m_Commands(m_Manager), m_Registered(Register(m_Manager, m_Resource)) {
		m_Commands.SetCharacter("a");
		m_Commands.Play("main");
		m_Prepared = sgre::scene::PrepareMotionTimeline(*m_Commands.CachedTimeline(), *m_Resource);
		m_Priority = std::make_unique<sgre::scene::MotionPriorityCursor>(m_Prepared);
		sgre::scene::MotionChildConstructionServices construction;
		construction.ReadManagerRandom = [&, sample = input.ManagerSample](const auto& manager) {
			Require(&manager == &m_Manager, "initial-traversal selected child must sample its actual manager");

			return std::bit_cast<float>(sample);
		};
		sgre::scene::MotionChildFrameSlotServices slots;
		slots.EnqueueAction = [this](auto label, auto action) {
			m_Update->EnqueueAction(label, action);
		};
		m_Nodes = std::make_unique<sgre::scene::MotionSelectedLayerNodes>(m_Commands, m_Root, m_Render, *m_Commands.CachedTimeline(), m_Prepared, m_Identifiers, std::move(construction), std::move(slots));
		sgre::scene::MotionPlayerUpdateServices services;
		services.ReadFrameDelta = []() -> float {
			throw std::runtime_error("child phase must not read manager delta");
		};
		services.PrepareFrames = [this](auto traversal, const auto& context) {
			Phase phase;
			phase.Kind = traversal == sgre::scene::MotionFrameTraversal::Seek ? 6 : traversal == sgre::scene::MotionFrameTraversal::Forward ? 7
			                                                                                                                                : 8;
			phase.EvaluationBits = std::bit_cast<std::uint32_t>(context.Clock.EvaluationTime);
			Phases.push_back(phase);
			if (traversal == sgre::scene::MotionFrameTraversal::Seek) {
				m_Priority->Seek(context.Clock.EvaluationTime);
			} else if (traversal == sgre::scene::MotionFrameTraversal::Forward) {
				m_Priority->Forward(context.Clock.EvaluationTime);
			} else {
				m_Priority->Reverse(context.Clock.EvaluationTime);
			}

			m_Nodes->ChildSlots(0).Select(traversal, context.Clock.EvaluationTime);
		};
		services.ComposeLayers = [this](const auto& context) {
			Phase phase;
			phase.Kind = 4;
			phase.EvaluationBits = std::bit_cast<std::uint32_t>(context.Clock.EvaluationTime);
			Phases.push_back(phase);
			m_Root.Compose(context.Clock.TickAssigned);
			// This proves real root numeric composition only. Selected child geometry/publication is still gated;
			// its dirty slot state is deliberately not acknowledged as a completed whole frame.
		};
		services.PublishDrawPackets = [](const auto&) {
			throw std::runtime_error("initial traversal has no selected geometry/publication producer");
		};
		services.Draw = [] {
			throw std::runtime_error("initial traversal has no draw producer");
		};
		services.DeliverEvent = [this](const auto& event) {
			Event record;
			record.Kind = event.Kind == sgre::scene::MotionPlayerEventKind::Sync ? 1 : 0;
			record.Label = StringIndex(event.Layer);
			record.Action = StringIndex(event.Action);
			Events.push_back(record);
		};
		m_Update = std::make_unique<sgre::scene::MotionPlayerUpdate>(m_Commands, std::move(services));
		m_Update->PrepareTimeline(*m_Commands.CachedTimeline());
	}

	void Step(bool assigned, bool boundary, float requested, float delta, float speed) {
		Phases.clear();
		Events.clear();
		if (assigned) {
			m_Commands.SetTickCount(requested);
		}
		if (boundary) {
			m_Commands.RequestInitialBoundaryTraversal();
		}
		m_Commands.SetSpeed(speed);
		m_Update->UpdateChild(delta);
		m_Update->PostUpdate();
		Require(m_Manager.ReferenceCount(m_Registered) == 1, "initial traversal must not add a resource registration pin");
	}

	[[nodiscard]] const sgre::scene::MotionTimelineClockState& Clock() const {
		return m_Commands.ClockState();
	}

	[[nodiscard]] std::size_t TagIndex() const {
		return m_Update->TagIndex();
	}

	[[nodiscard]] const sgre::scene::MotionChildFrameSlots& Slots() const {
		return m_Nodes->ChildSlots(0);
	}

	[[nodiscard]] const sgre::scene::MotionChildConstructionState& Child() const {
		return m_Nodes->Child(0);
	}

	[[nodiscard]] sgre::scene::MotionPlayerCommands& Commands() {
		return m_Commands;
	}

	void SelectHelperFrame(float time) {
		m_Nodes->ChildSlots(0).Select(sgre::scene::MotionFrameTraversal::Seek, time);
	}

	std::vector<Phase> Phases;
	std::vector<Event> Events;

private:
	std::shared_ptr<const sgre::resource::MotionResource> m_Resource;
	sgre::scene::TextureCache m_Cache;
	sgre::scene::MotionResourceManager m_Manager;
	sgre::scene::MotionPlayerCommands m_Commands;
	sgre::scene::MotionProjectId m_Registered;
	sgre::scene::PreparedMotionTimeline m_Prepared;
	sgre::scene::MotionRootScene m_Root;
	sgre::scene::MotionPlayerRenderControls m_Render;
	sgre::scene::MotionLayerIdentifierPool m_Identifiers;
	std::unique_ptr<sgre::scene::MotionPriorityCursor> m_Priority;
	std::unique_ptr<sgre::scene::MotionSelectedLayerNodes> m_Nodes;
	std::unique_ptr<sgre::scene::MotionPlayerUpdate> m_Update;
};

static CaseInput DefaultInput() {
	CaseInput input;
	const std::array<float, 5> times = {0, 1, 2, 4, 8};
	for (std::size_t index = 0; index < times.size(); ++index) {
		input.Frames[index].Time = times[index];
		input.Frames[index].ControlPresent = index != 2;
		input.Frames[index].Flags = static_cast<std::uint32_t>(index);
		input.Frames[index].Direction = static_cast<std::uint32_t>(index % 3);
		input.Frames[index].Action = static_cast<std::uint8_t>(index == 1 ? 0 : index + 4);
	}
	input.Frames[4].Action = 0;

	return input;
}

static void TestEndpointAndBlocking() {
	const auto input = DefaultInput();
	Scene ordinary(input);
	Scene boundary(input);
	ordinary.Step(true, false, 6, 1, 1);
	boundary.Step(true, true, 6, 1, 1);
	Require(ordinary.Phases.size() == 2 && ordinary.Phases[0].Kind == 6 && boundary.Phases.size() == 3 && boundary.Phases[0].EvaluationBits == 0 && boundary.Phases[1].Kind == 7, "boundary flag must seek origin and traverse assigned position rather than seek only current position");
	const std::vector<Event> expected = {{0, 0, 8}, {0, 1, 4}, {0, 0, 9}, {0, 0, 10}, {0, 1, 0}, {0, 1, 6}, {0, 1, 7}};
	Require(boundary.Events == expected && !boundary.Clock().InitialBoundaryTraversalPending, "initial forward traversal must preserve tag/layer action order including explicit empty layer action");
	Require(boundary.Slots().State().CurrentSlot == 1 && boundary.Slots().CurrentControl().Flags == (sgre::scene::MotionPlayFlags::Force | sgre::scene::MotionPlayFlags::Chain), "actual decoded slot cursor must follow the final crossing");
	Require(boundary.Slots().FollowingControl().Flags == sgre::scene::MotionPlayFlags::None && boundary.Slots().State().FrameDirty, "intermediate missing metadata and final hidden reuse must retain controls without false full-frame acknowledgement");
	Scene reverse(input);
	reverse.Step(true, true, 3, -1, 1);
	Require(reverse.Phases.size() == 3 && reverse.Phases[0].EvaluationBits == std::bit_cast<std::uint32_t>(8.0f) && reverse.Phases[1].Kind == 8 && reverse.Clock().EvaluationTime == 3, "negative initial boundary traversal must seek terminal then restore and reverse to assigned position");
	Scene reverse_zero(input);
	reverse_zero.Step(true, true, 0, -1, 1);
	Require(reverse_zero.Phases.size() == 2 && reverse_zero.Phases[0].EvaluationBits == std::bit_cast<std::uint32_t>(8.0f), "negative zero assignment first maps tick to lastTime and uses one terminal seek");
	auto blocked_input = input;
	blocked_input.TagFlags[0] = 13;
	Scene blocked(blocked_input);
	blocked.Step(true, true, 6, 1, 1);
	Require(blocked.Phases.size() == 2 && blocked.Clock().SyncWaiting && blocked.Clock().EvaluationTime == 0 && !blocked.Clock().InitialBoundaryTraversalPending, "sync at endpoint must stop before restore/second traversal while consuming boundary flag");
	blocked.Step(true, true, 3, 1, 1);
	Require(blocked.Phases.size() == 1 && blocked.Clock().InitialTagDispatchPending && blocked.Clock().InitialBoundaryTraversalPending, "preexisting sync wait must retain unconsumed initial flags");
	blocked_input.TagFlags[0] = 9;
	blocked_input.TagFlags[1] = 13;
	Scene middle(blocked_input);
	middle.Step(true, true, 6, 1, 1);
	Require(middle.Clock().SyncWaiting && middle.Clock().TickCount == 2 && middle.Slots().CurrentFrame().Time == 2, "sync during second traversal must own final timing and selected-slot preparation");
}

class Reader {
public:
	explicit Reader(std::istream& stream) : m_Stream(stream) {
	}

	std::uint8_t Byte() {
		const auto value = m_Stream.get();
		if (value == std::char_traits<char>::eof()) {
			throw std::runtime_error("short initial-traversal native fixture");
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

	float Real() {
		return std::bit_cast<float>(Word());
	}

private:
	std::istream& m_Stream;
};

static void Replay(std::string_view path) {
	std::ifstream stream{std::string(path), std::ios::binary};
	Require(static_cast<bool>(stream), "cannot open initial-traversal native fixture");
	Reader reader(stream);
	Require(reader.Byte() == 'M' && reader.Byte() == 'I' && reader.Byte() == 'T' && reader.Byte() == '1', "unexpected initial-traversal native fixture magic");
	const auto cases = reader.Word();
	const auto steps = reader.Word();
	Require(cases == 64 && steps == 4, "unexpected initial-traversal fixture counts");
	for (std::uint32_t case_index = 0; case_index < cases; ++case_index) {
		CaseInput input;
		input.LoopTime = reader.Real();
		for (auto& tag : input.TagFlags) {
			tag = reader.Byte();
		}
		input.ManagerSample = reader.Word();
		for (auto& frame : input.Frames) {
			frame.Time = reader.Real();
			frame.ControlPresent = reader.Byte() != 0;
			frame.Flags = reader.Word();
			frame.Direction = reader.Word();
			frame.Offset = reader.Real();
			frame.Action = reader.Byte();
		}
		Scene scene(input);
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto assigned = reader.Byte() != 0;
			const auto boundary = reader.Byte() != 0;
			const auto requested = reader.Real();
			const auto delta = reader.Real();
			const auto speed = reader.Real();
			std::array<std::uint32_t, 7> clock_words{};
			for (auto& word : clock_words) {
				word = reader.Word();
			}
			std::array<bool, 6> clock_flags{};
			for (auto&& flag : clock_flags) {
				flag = reader.Byte() != 0;
			}
			const auto phase_count = reader.Byte();
			const auto event_count = reader.Byte();
			const auto preparation_count = reader.Byte();
			std::vector<Phase> phases;
			for (std::uint8_t index = 0; index < phase_count; ++index) {
				Phase phase;
				phase.Kind = reader.Byte();
				phase.EvaluationBits = reader.Word();
				phases.push_back(phase);
			}
			std::vector<Event> events;
			for (std::uint8_t index = 0; index < event_count; ++index) {
				Event event;
				event.Kind = reader.Byte();
				event.Label = reader.Byte();
				event.Action = reader.Byte();
				events.push_back(event);
			}
			const auto slot = reader.Byte();
			const std::array<std::uint32_t, 2> indices = {reader.Word(), reader.Word()};
			std::array<std::array<std::uint32_t, 5>, 2> controls{};
			for (auto& control : controls) {
				control[0] = reader.Word();
				control[1] = reader.Word();
				control[2] = reader.Word();
				control[3] = reader.Byte();
				control[4] = reader.Word();
			}
			for (std::uint8_t index = 0; index < preparation_count; ++index) {
				Require(reader.Byte() < 2 && reader.Byte() < 5 && reader.Byte() <= event_count, "invalid independently proved native final-slot preparation record");
			}
			scene.Step(assigned, boundary, requested, delta, speed);
			const auto& state = scene.Clock();
			const std::array<std::uint32_t, 7> actual_words = {std::bit_cast<std::uint32_t>(state.TickCount), std::bit_cast<std::uint32_t>(state.EvaluationTime), std::bit_cast<std::uint32_t>(state.LastTime), std::bit_cast<std::uint32_t>(state.LoopTime), std::bit_cast<std::uint32_t>(state.Speed), std::bit_cast<std::uint32_t>(state.DeltaTicks), static_cast<std::uint32_t>(scene.TagIndex())};
			const std::array<bool, 6> actual_flags = {state.Playing, state.TickAssigned, state.InitialTagDispatchPending, state.SyncWaiting, state.AlignedThisUpdate, state.InitialBoundaryTraversalPending};
			Require(actual_words == clock_words && actual_flags == clock_flags && scene.Phases == phases && scene.Events == events, "native initial-traversal clock/stop/phase/event comparison differs at case " + std::to_string(case_index) + " step " + std::to_string(step));
			Require(scene.Slots().State().CurrentSlot == slot && scene.Slots().State().FrameIndices[0] == indices[0] && scene.Slots().State().FrameIndices[1] == indices[1], "native final selected slot cursor differs");
			for (std::size_t index = 0; index < controls.size(); ++index) {
				const auto& control = scene.Child().Control(index);
				const std::array<std::uint32_t, 5> actual_control = {static_cast<std::uint32_t>(control.Flags), control.DirectionMode, std::bit_cast<std::uint32_t>(control.DirectionOffset), static_cast<std::uint32_t>(control.CompleteDirection), std::bit_cast<std::uint32_t>(control.TimeOffset)};
				Require(actual_control == controls[index], "native final selected control preparation differs");
			}
		}
	}

	Require(stream.peek() == std::char_traits<char>::eof(), "extra initial-traversal native fixture bytes");
	std::cout << "native initial-boundary clock/event/selected-slot replay: 256 updates PASS\n";
}

static void ReplayAssignment(std::string_view path) {
	std::ifstream stream{std::string(path), std::ios::binary};
	Require(static_cast<bool>(stream), "cannot open initial-assignment native fixture");
	Reader reader(stream);
	Require(reader.Byte() == 'M' && reader.Byte() == 'I' && reader.Byte() == 'A' && reader.Byte() == '1', "unexpected initial-assignment fixture magic");
	const auto cases = reader.Word();
	Require(cases == 128, "unexpected initial-assignment fixture count");
	for (std::uint32_t index = 0; index < cases; ++index) {
		const auto playing = reader.Byte() != 0;
		const auto assigned = reader.Byte() != 0;
		const auto parent_assigned = reader.Byte() != 0;
		const auto prior_boundary = reader.Byte() != 0;
		const auto frame_index = reader.Byte();
		const auto loop = reader.Real();
		const auto parent_driver_time = reader.Real();
		const auto parent_delta = reader.Real();
		const auto native_time = reader.Real();
		const auto native_offset = reader.Real();
		const auto sample = reader.Word();
		const auto tick_bits = reader.Word();
		const auto evaluation_bits = reader.Word();
		const auto expected_assigned = reader.Byte() != 0;
		const auto expected_pending = reader.Byte() != 0;
		const auto expected_boundary = reader.Byte() != 0;
		auto input = DefaultInput();
		input.LoopTime = loop;
		input.ManagerSample = sample;
		input.TagFlags = {0, 0, 0};
		for (std::size_t frame = 0; frame < input.Frames.size(); ++frame) {
			input.Frames[frame].Offset = frame == 0 ? 3.0f : frame == 1 ? -4.0f
			                                                            : 0.5f;
		}
		Scene scene(input);
		if (!assigned) {
			scene.Step(false, false, 0, 0, 1);
		}
		if (!playing) {
			scene.Commands().Stop();
		}
		if (prior_boundary) {
			scene.Commands().RequestInitialBoundaryTraversal();
		}
		Require(frame_index < 4, "invalid native initial-assignment selected frame");
		scene.SelectHelperFrame(input.Frames[frame_index].Time);
		Require(std::bit_cast<std::uint32_t>(scene.Slots().CurrentFrame().Time) == std::bit_cast<std::uint32_t>(native_time) && std::bit_cast<std::uint32_t>(scene.Slots().CurrentControl().TimeOffset) == std::bit_cast<std::uint32_t>(native_offset), "native assignment must use actual selected frame/control owners");
		sgre::scene::MotionTimelineClockState parent;
		parent.TickCount = parent_driver_time;
		parent.EvaluationTime = -37;
		parent.DeltaTicks = parent_delta;
		parent.TickAssigned = parent_assigned;
		const auto written = sgre::scene::AssignMotionChildInitialTick(scene.Commands(), parent, scene.Slots().CurrentFrame(), scene.Slots().CurrentControl());
		const auto& actual = scene.Clock();
		Require(written == (playing && assigned) && std::bit_cast<std::uint32_t>(actual.TickCount) == tick_bits && std::bit_cast<std::uint32_t>(actual.EvaluationTime) == evaluation_bits && actual.TickAssigned == expected_assigned && actual.InitialTagDispatchPending == expected_pending && actual.InitialBoundaryTraversalPending == expected_boundary, "native initial-assignment playing/assigned/loop/parent-flag comparison differs");
	}

	Require(stream.peek() == std::char_traits<char>::eof(), "extra initial-assignment native fixture bytes");
	std::cout << "native selected-control initial-tick assignment replay: 128 cases PASS\n";
}

static void TestInitialAssignmentSafety() {
	auto input = DefaultInput();
	input.LoopTime = 2;
	Scene scene(input);
	scene.SelectHelperFrame(0);
	sgre::scene::MotionTimelineClockState parent;
	parent.TickCount = 14;
	parent.EvaluationTime = 3;
	parent.DeltaTicks = -1;
	parent.TickAssigned = false;
	Require(sgre::scene::AssignMotionChildInitialTick(scene.Commands(), parent, scene.Slots().CurrentFrame(), scene.Slots().CurrentControl()) && scene.Clock().TickCount == 2 && scene.Clock().InitialBoundaryTraversalPending, "reverse initial assignment must repeatedly subtract lastTime then add loopTime and request endpoint traversal");
	RequireFailure([&] {
		static_cast<void>(sgre::scene::AssignMotionChildInitialTick(scene.Commands(), parent, scene.Slots().CurrentFrame(), scene.Slots().CurrentControl(), 1));
	},
	    "bounded progressing");
	const auto tick = scene.Clock().TickCount;
	scene.Commands().Stop();
	parent.TickCount = 27;
	Require(!sgre::scene::AssignMotionChildInitialTick(scene.Commands(), parent, scene.Slots().CurrentFrame(), scene.Slots().CurrentControl()) && scene.Clock().TickCount == tick, "stopped child must not enter the initial assignment producer");
	Scene unassigned(input);
	unassigned.Step(false, false, 0, 0, 1);
	Require(!sgre::scene::AssignMotionChildInitialTick(unassigned.Commands(), parent, unassigned.Slots().CurrentFrame(), unassigned.Slots().CurrentControl()), "already-composed unassigned child must not reassign from parent timing");
}

int main(int argc, char** argv) {
	try {
		Require(argc >= 1 && argc <= 3, "usage: initial_traversal_tests [traversal fixture [assignment fixture]]");
		TestEndpointAndBlocking();
		TestInitialAssignmentSafety();
		if (argc >= 2) {
			Replay(argv[1]);
		}
		if (argc == 3) {
			ReplayAssignment(argv[2]);
		}
		std::cout << "independent initial-boundary traversal and stop fixtures PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
