#include "sgre/scene/motion_child_frame_selection.hpp"

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
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Value>
static sgre::resource::PsbValue MakeValue(Value value) {
	sgre::resource::PsbValue result;
	result.Data = std::move(value);

	return result;
}

static const std::array<std::string, 10> kNames = {"", "a", "A", "あ", "idle", "alt", "missing", std::string("a\0suffix", 8), std::string("idle\0suffix", 11), std::string("alt\0suffix", 10)};
static const std::array<float, 6> kLastTimes = {20, 7, 12, 30, 4, 50};

static std::shared_ptr<const sgre::resource::MotionResource> MakeResource() {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	for (std::size_t character = 0; character < 3; ++character) {
		sgre::resource::MotionObject object;
		object.Name = kNames[character + 1];
		for (std::size_t motion = 0; motion < 2; ++motion) {
			sgre::resource::MotionTimeline timeline;
			timeline.Name = kNames[motion + 4];
			timeline.LastTime = kLastTimes[character * 2 + motion];
			timeline.LoopTime = -1;
			object.Timelines.push_back(std::move(timeline));
		}

		resource->Objects.push_back(std::move(object));
	}

	return resource;
}

struct Input {
	bool Changed = true;
	bool Dirty = true;
	bool Hidden = false;
	std::size_t Source = 1;
	std::size_t Icon = 5;
	bool Playing = true;
	bool ParentAssigned = false;
	std::uint32_t Flags = 0;
	float FrameTime = 2;
	float TimeOffset = 1;
	float ParentTick = 17;
	float ParentEvaluation = 3;
	float ParentDelta = 2;
};

class Scene {
public:
	explicit Scene(const Input& input) : m_Cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		                                     throw std::runtime_error("source-free selection fixture attempted a texture upload");
	                                     },
	                                         [](auto) {
		                                         throw std::runtime_error("source-free selection fixture attempted a texture release");
	                                         }),
	                                     m_Manager(m_Cache), m_Resource(MakeResource()), m_Parent(m_Manager), m_Random(m_Seed) {
		Require(input.Source < kNames.size() && input.Icon < kNames.size(), "fixture source/icon ordinal exceeds its bounded names");
		sgre::scene::MotionProjectIdentity identity;
		identity.DocumentInstance = 1;
		identity.RootOffset = 128;
		m_Registration = m_Manager.Register(identity, m_Resource, {});
		m_Parent.SetCharacter("a");
		m_Parent.Play("idle");
		m_Raw.Type = 3;
		m_Raw.Label = "Synthetic child";
		m_Raw.TransformOrder = {0, 3, 2, 1};
		m_Raw.Properties.emplace_back("motionIndependentLayerInherit", MakeValue(false));
		m_Raw.Properties.emplace_back("parameterize", sgre::resource::PsbValue{});
		sgre::resource::MotionFrame frame;
		frame.Type = input.Hidden ? 0 : 2;
		frame.Content = MakeValue(sgre::resource::PsbObject{{"mask", MakeValue(std::int64_t{0x80000})}, {"src", MakeValue(kNames[input.Source])}, {"icon", MakeValue(kNames[input.Icon])}, {"motion", MakeValue(sgre::resource::PsbObject{{"mask", MakeValue(std::int64_t{1})}, {"flags", MakeValue(static_cast<std::int64_t>(input.Flags))}, {"timeOffset", MakeValue(static_cast<double>(input.TimeOffset))}})}});
		m_Raw.Frames.push_back(frame);
		frame.Time = input.FrameTime;
		m_Raw.Frames.push_back(std::move(frame));
		sgre::resource::MotionFrame terminal;
		terminal.Time = 50;
		terminal.Type = 0;
		m_Raw.Frames.push_back(std::move(terminal));
		m_Prepared = sgre::scene::PrepareMotionLayer(m_Raw, *m_Resource);
		sgre::scene::MotionChildConstructionServices construction;
		construction.ReadManagerRandom = [&](const auto& manager) {
			Require(&manager == &m_Manager, "constructor must sample the actual parent manager");

			return m_Random.NextUnitFloat();
		};
		m_Child = std::make_unique<sgre::scene::MotionChildConstructionState>(m_Parent, m_Root, m_Render, m_Raw, m_Prepared, construction);
		Commands().SetCharacter("a");
		Commands().Play("idle");
		Commands().SetTickCount(3);
		if (!input.Playing) {
			Commands().Stop();
		}
		sgre::scene::MotionChildFrameSlotServices slots;
		slots.EnqueueAction = [&](auto layer, auto action) {
			m_Actions.emplace_back(layer, action);
		};
		m_Slots = std::make_unique<sgre::scene::MotionChildFrameSlots>(m_Raw, m_Prepared, 50, *m_Child, std::move(slots));
		m_Slots->Select(sgre::scene::MotionFrameTraversal::Seek, input.FrameTime);
		if (!input.Dirty) {
			// This isolated cursor fixture supplies the prior successful-composition acknowledgement.
			m_Slots->CompleteComposition();
		}
		m_ParentClock.TickCount = input.ParentTick;
		m_ParentClock.EvaluationTime = input.ParentEvaluation;
		m_ParentClock.DeltaTicks = input.ParentDelta;
		m_ParentClock.TickAssigned = input.ParentAssigned;
	}

	[[nodiscard]] sgre::scene::MotionPlayerCommands& Commands() {
		return m_Child->Commands();
	}

	[[nodiscard]] sgre::scene::MotionChildFrameSlots& Slots() {
		return *m_Slots;
	}

	[[nodiscard]] sgre::scene::MotionChildFrameSelection Select(bool changed, std::uint32_t parameter_flags = 0) {
		return sgre::scene::SelectMotionChildFrameCommands(*m_Slots, m_ParentClock, changed, parameter_flags, [&] {
			Require(!Commands().ClockState().Playing, "reset must stop before actual selected descendant retirement");
			Require(Commands().Names().CurrentMotion == "idle" && Commands().Names().RequestedMotion == "idle", "reset must retire before clearing motion names");
			++m_Retirements;
		});
	}

	[[nodiscard]] std::size_t Retirements() const {
		return m_Retirements;
	}

	[[nodiscard]] std::uint8_t CachedOrdinal() {
		if (Commands().CachedTimeline() == nullptr) {
			return 0;
		}
		for (std::size_t character = 0; character < 3; ++character) {
			for (std::size_t motion = 0; motion < 2; ++motion) {
				if (Commands().CachedTimeline() == &m_Resource->Objects[character].Timelines[motion]) {
					return static_cast<std::uint8_t>(character * 2 + motion + 1);
				}
			}
		}

		throw std::runtime_error("selected cached timeline lost its actual registered provenance");
	}

private:
	sgre::scene::TextureCache m_Cache;
	sgre::scene::MotionResourceManager m_Manager;
	std::shared_ptr<const sgre::resource::MotionResource> m_Resource;
	sgre::scene::MotionProjectId m_Registration;
	sgre::scene::MotionPlayerCommands m_Parent;
	sgre::scene::MotionRootScene m_Root;
	sgre::scene::MotionPlayerRenderControls m_Render;
	sgre::resource::MotionLayer m_Raw;
	sgre::scene::PreparedMotionLayer m_Prepared;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> m_Seed{};
	sgre::scene::MotionSharedRandom m_Random;
	std::unique_ptr<sgre::scene::MotionChildConstructionState> m_Child;
	std::unique_ptr<sgre::scene::MotionChildFrameSlots> m_Slots;
	sgre::scene::MotionTimelineClockState m_ParentClock;
	std::vector<std::pair<std::string_view, std::string_view>> m_Actions;
	std::size_t m_Retirements = 0;
};

static void TestActualCommandsAndGates() {
	const Input input;
	Scene selected(input);
	Require(&selected.Slots().Child().Commands() == &selected.Commands(), "selection must use its matching actual slot/child command owner");
	Require(selected.Select(true) == sgre::scene::MotionChildFrameSelection::BindRoot && selected.Commands().Names().CurrentCharacter == "a" && selected.Commands().Names().CurrentMotion == "alt", "selected src/icon must drive ordinary character then real motion commands");
	Require(selected.Commands().ClockState().TickCount == 16 && selected.Commands().ClockState().EvaluationTime == 7 && selected.Commands().ClockState().InitialBoundaryTraversalPending, "initial tick must use parent TickCount and preserve separate clamped child evaluation");
	Scene unchanged(input);
	Require(unchanged.Select(false) == sgre::scene::MotionChildFrameSelection::ContinueUpdate && unchanged.Commands().Names().CurrentMotion == "idle" && unchanged.Commands().ClockState().TickCount == 3, "unchanged node must bypass command and initial-tick phases");
	auto clean_input = input;
	clean_input.Dirty = false;
	Scene clean(clean_input);
	Require(clean.Select(true) == sgre::scene::MotionChildFrameSelection::BindRoot && clean.Commands().Names().CurrentMotion == "idle" && clean.Commands().ClockState().TickCount == 3, "changed clean frame must bind its root without selecting or assigning tick");
	auto hidden_input = input;
	hidden_input.Hidden = true;
	Scene hidden(hidden_input);
	Require(hidden.Select(true) == sgre::scene::MotionChildFrameSelection::Reset && hidden.Retirements() == 1 && hidden.Commands().Names().CurrentMotion.empty() && hidden.Commands().CachedTimeline() != nullptr, "hidden selection must invoke actual ordered reset without clearing cached timeline");
	bool rejected = false;
	try {
		static_cast<void>(clean.Select(false, 1));
	} catch (const std::runtime_error& error) {
		rejected = std::string_view(error.what()).find("parameter") != std::string_view::npos;
	}

	Require(rejected && clean.Commands().Names().CurrentMotion == "idle", "unrecovered parameter flags must reject before command mutation");
	Scene missing_reset(hidden_input);
	rejected = false;
	try {
		static_cast<void>(sgre::scene::SelectMotionChildFrameCommands(missing_reset.Slots(), {}, true, 0, {}));
	} catch (const std::runtime_error& error) {
		rejected = std::string_view(error.what()).find("retirement") != std::string_view::npos;
	}

	Require(rejected && missing_reset.Commands().Names().CurrentMotion == "idle", "hidden reset must require real selected-tree retirement");
}

static void Replay(const std::string& path) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	Require(static_cast<bool>(stream), "cannot read child command selection native fixture");
	const auto length = stream.tellg();
	Require(length >= 8 && length <= 65536, "child command selection fixture length exceeds its bound");
	std::vector<char> bytes(static_cast<std::size_t>(length));
	stream.seekg(0);
	stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	Require(static_cast<bool>(stream) && std::string_view(bytes.data(), 4) == "MCS1", "child command selection fixture magic/read failed");
	std::size_t offset = 4;
	const auto read_byte = [&]() {
		Require(offset < bytes.size(), "child command selection fixture is truncated");

		return static_cast<std::uint8_t>(bytes[offset++]);
	};
	const auto read_word = [&]() {
		std::uint32_t result = 0;
		for (std::size_t shift = 0; shift < 32; shift += 8) {
			result |= static_cast<std::uint32_t>(read_byte()) << shift;
		}

		return result;
	};
	const auto read_float = [&]() {
		return std::bit_cast<float>(read_word());
	};
	Require(read_word() == 512, "child command selection fixture count changed");
	for (std::size_t index = 0; index < 512; ++index) {
		Input input;
		input.Changed = read_byte() != 0;
		input.Dirty = read_byte() != 0;
		input.Hidden = read_byte() != 0;
		input.Source = read_byte();
		input.Icon = read_byte();
		input.Playing = read_byte() != 0;
		input.ParentAssigned = read_byte() != 0;
		const auto expected_result = read_byte();
		input.Flags = read_word();
		input.FrameTime = read_float();
		input.TimeOffset = read_float();
		input.ParentTick = read_float();
		input.ParentEvaluation = read_float();
		input.ParentDelta = read_float();
		Scene scene(input);
		Require(static_cast<std::uint8_t>(scene.Select(input.Changed)) == expected_result, "native child selection phase gate differs");
		const auto& names = scene.Commands().Names();
		for (const auto* text : {&names.CurrentCharacter, &names.RequestedCharacter, &names.DeferredStealthCharacter, &names.CurrentMotion, &names.RequestedMotion, &names.DeferredStealthMotion, &names.DelayedMotion}) {
			const auto expected_name = read_byte();
			Require(expected_name < kNames.size() && *text == std::string_view(kNames[expected_name]).substr(0, kNames[expected_name].find('\0')), "native child selection name differs");
		}

		Require(scene.Commands().PreferredProject().has_value() == (read_byte() != 0) && scene.CachedOrdinal() == read_byte(), "native child preferred/cached selection identity differs");
		const auto& clock = scene.Commands().ClockState();
		Require(clock.Playing == (read_byte() != 0) && clock.TickAssigned == (read_byte() != 0) && clock.InitialTagDispatchPending == (read_byte() != 0), "native child selection control flags differ");
		Require(static_cast<std::uint32_t>(names.DelayedFlags) == read_word(), "native child selection delayed flags differ");
		for (const auto value : {clock.TickCount, clock.EvaluationTime, clock.LastTime, clock.LoopTime, clock.Speed}) {
			Require(std::bit_cast<std::uint32_t>(value) == read_word(), "native child selection clock field differs");
		}

		Require(clock.InitialBoundaryTraversalPending == (read_byte() != 0), "native parent-assigned to child initial boundary flag differs");
	}

	Require(offset == bytes.size(), "child command selection fixture has trailing bytes");
}

int main(int argc, char** argv) {
	try {
		TestActualCommandsAndGates();
		if (argc == 2) {
			Replay(argv[1]);
		}

		std::cout << "Motion selected-child command gates and 512 native comparisons passed\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
