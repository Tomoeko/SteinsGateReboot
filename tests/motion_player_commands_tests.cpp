#include "sgre/scene/motion_player_commands.hpp"

#include "sgre/scene/motion_timeline_variables.hpp"

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

static constexpr std::array<std::string_view, 3> kCharacters{"a", "A", "あ"};
static constexpr std::array<std::string_view, 2> kMotions{"idle", "alt"};
static constexpr std::array<float, 6> kLastTimes{20, 7, 12, 30, 4, 50};
static constexpr std::array<std::string_view, 10> kQueries{"", "a", "A", "あ", "idle", "alt", "missing", std::string_view("a\0suffix", 8), std::string_view("idle\0suffix", 11), std::string_view("alt\0suffix", 10)};

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

	throw std::runtime_error("unsupported motion command was accepted");
}

static std::shared_ptr<sgre::resource::MotionResource> Model() {
	auto resource = std::make_shared<sgre::resource::MotionResource>();
	for (std::size_t character_index = 0; character_index < kCharacters.size(); ++character_index) {
		sgre::resource::MotionObject character;
		character.Name = kCharacters[character_index];
		for (std::size_t motion_index = 0; motion_index < kMotions.size(); ++motion_index) {
			sgre::resource::MotionTimeline timeline;
			timeline.Name = kMotions[motion_index];
			timeline.LastTime = kLastTimes[character_index * kMotions.size() + motion_index];
			character.Timelines.push_back(std::move(timeline));
		}

		resource->Objects.push_back(std::move(character));
	}

	sgre::resource::MotionTimeline division;
	division.Name = "division";
	division.Kind = sgre::resource::MotionTimelineKind::Division;
	division.Division = {0};
	division.MotionList = {"idle"};
	resource->Objects[0].Timelines.push_back(std::move(division));

	return resource;
}

static sgre::scene::MotionProjectIdentity Identity() {
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = 1;
	identity.RootOffset = 128;

	return identity;
}

static sgre::scene::TextureCache EmptyTextureCache() {
	return sgre::scene::TextureCache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("source-free command fixture attempted a texture upload");
	},
	    [](auto) {
		    throw std::runtime_error("source-free command fixture attempted a texture release");
	    });
}

static void TestRequestsAndControls() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto model = Model();
	const auto project = manager.Register(Identity(), model, {});
	sgre::scene::MotionPlayerCommands commands(manager);
	commands.SetCharacter("あ", sgre::scene::MotionCharacterMode::Stealth);
	commands.Play("alt", sgre::scene::MotionPlayFlags::Stealth);
	Require(commands.Names().CurrentCharacter.empty() && commands.Names().RequestedCharacter.empty() && commands.Names().DeferredStealthCharacter == "あ" && commands.Names().DeferredStealthMotion == "alt", "stealth requests before the first character must remain deferred");
	commands.SetCharacter("a");
	Require(commands.Names().CurrentCharacter == "あ" && commands.Names().RequestedCharacter == "a" && commands.Names().DeferredStealthCharacter.empty(), "deferred stealth character must replace current character after the ordinary request");
	commands.Play("idle");
	Require(commands.Names().CurrentMotion == "alt" && commands.Names().RequestedMotion == "idle" && commands.Names().DeferredStealthMotion.empty() && commands.ClockState().LastTime == 50, "deferred stealth motion must run after the ordinary selection without replacing the requested name");
	commands.SetCharacter("a");
	Require(commands.Names().CurrentCharacter == "あ" && commands.ClockState().Playing, "a repeated ordinary request must compare the requested character, preserving a current stealth override");
	commands.SetTickCount(15);
	commands.Stop();
	commands.Play("idle");
	Require(!commands.ClockState().Playing && commands.ClockState().TickCount == 15 && commands.Names().CurrentMotion == "alt", "same requested motion without Force must do nothing after Stop");
	commands.Play("idle", sgre::scene::MotionPlayFlags::Force);
	Require(commands.ClockState().Playing && commands.ClockState().TickCount == 0 && commands.ClockState().LastTime == 4, "Force must replay and reset the actual current character's timeline");
	commands.SetTickCount(15);
	commands.Play("alt", sgre::scene::MotionPlayFlags::AsCan);
	Require(commands.Names().CurrentMotion == "idle" && commands.ClockState().TickCount == 15, "AsCan must block a motion selection while already playing");
	commands.Play("alt", sgre::scene::MotionPlayFlags::Force | sgre::scene::MotionPlayFlags::Chain | sgre::scene::MotionPlayFlags::AsCan);
	Require(commands.Names().RequestedMotion == "alt" && commands.ClockState().TickCount == 15 && commands.ClockState().EvaluationTime == 4 && commands.ClockState().LastTime == 50, "Force must override AsCan while Chain preserves both prior tick words");
	const auto* cached = commands.CachedTimeline();
	commands.Play("missing");
	commands.SetTickCount(3);
	Require(!commands.SelectedProject() && commands.CachedTimeline() == cached && !commands.ClockState().Playing && commands.ClockState().TickCount == 15 && commands.Names().CurrentMotion == "alt" && commands.Names().RequestedMotion == "alt", "failed selection must clear only the selected project and playing guard, retaining names, cached timeline and timing");
	commands.Play("alt");
	Require(!commands.SelectedProject(), "same-name request after failure must not silently reacquire a root");
	commands.Play("alt", sgre::scene::MotionPlayFlags::Force);
	Require(commands.SelectedProject() && commands.SelectedProject()->Project == project, "Force must reacquire the actual registered project");
	commands.SetTickCount(12);
	commands.SetCharacter("A");
	commands.SetTickCount(100);
	Require(commands.SelectedProject() && commands.CachedTimeline() == cached && !commands.ClockState().Playing && commands.ClockState().TickCount == 100 && commands.ClockState().EvaluationTime == 50 && commands.Names().CurrentMotion.empty(), "character change must retain the previous root/timeline while clearing motion names and playing");
	commands.Play("idle", sgre::scene::MotionPlayFlags::Delay1);
	commands.Play("alt", sgre::scene::MotionPlayFlags::Delay2 | sgre::scene::MotionPlayFlags::Chain);
	Require(commands.Names().DelayedMotion == "alt" && commands.Names().CurrentMotion.empty(), "the latest delayed command must replace the pending name without selecting immediately");
	const auto delayed = commands.ProcessDelayedPlay();
	Require(delayed.Consumed && delayed.SkipRemainingUpdate && commands.Names().DelayedMotion.empty() && commands.Names().DelayedFlags == (sgre::scene::MotionPlayFlags::Delay2 | sgre::scene::MotionPlayFlags::Chain) && commands.Names().CurrentMotion == "alt" && commands.ClockState().TickCount == 100 && commands.ClockState().EvaluationTime == 50 && commands.ClockState().LastTime == 30, "Delay2 consumption must retain queued flags, preserve Chain ticks, and stop the remaining update phase");
	commands.Play("", sgre::scene::MotionPlayFlags::Delay2);
	Require(!commands.ProcessDelayedPlay().Consumed, "empty delayed names must remain inactive at update entry");
	commands.SetCharacter("a");
	commands.SetSpeed(-2);
	Require(commands.ClockState().Speed == -2 && !commands.ClockState().Playing, "speed assignment must preserve the raw speed while character change only stops playing");
	commands.Play("idle", sgre::scene::MotionPlayFlags::Join);
	Require(commands.Names().CurrentMotion == "idle" && commands.ClockState().TickAssigned, "native JOIN must skip snapshot capture while an actual tick assignment remains pending");
	RequireFailure([&] {
		commands.Play("division");
	},
	    "division playback");
	RequireFailure([&] {
		commands.SetCharacter(std::string(65537, 'a'));
	},
	    "byte limit");
	commands.SetCharacter(std::string_view("a\0suffix", 8));
	commands.Play(std::string_view("idle\0suffix", 11), static_cast<sgre::scene::MotionPlayFlags>(0x80000000));
	Require(commands.Names().CurrentCharacter == "a" && commands.Names().CurrentMotion == "idle", "native names must stop at NUL while unconsumed high flag bits remain inert");
}

static void TestNativeCloneCommandState() {
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto model = Model();
	const auto project = manager.Register(Identity(), model, {});
	sgre::scene::MotionPlayerCommands source(manager);
	source.SetCharacter("a");
	source.Play("idle");
	source.SetTickCount(13.5f);
	source.SetSpeed(-2.0f);
	source.SetMeshDivisionRatio(0.375f);
	source.ParameterValues().WriteLocal("value", 4.5f, 0);
	source.ParameterValues().WriteLocal("nested::value", 0.75f, 0);
	source.Play("alt", sgre::scene::MotionPlayFlags::Delay2 | sgre::scene::MotionPlayFlags::Chain);
	std::size_t installations = 0;
	sgre::scene::MotionPlayerCommands clone(manager, [&](const auto&, auto) {
		++installations;
	});
	clone.CopyForClone(source);
	Require(installations == 0 && clone.Names().CurrentMotion.empty() && clone.Names().RequestedMotion == "idle" && clone.Names().DelayedMotion == "alt" && clone.Names().DelayedFlags == source.Names().DelayedFlags, "native command clone must preserve requested/delayed strings without replay or copying currentMotion+90");
	Require(clone.ClockState().TickCount == 13.5f && clone.ClockState().EvaluationTime == source.ClockState().EvaluationTime && clone.ClockState().Speed == -2.0f && clone.ClockState().Playing == source.ClockState().Playing && clone.ClockState().TickAssigned == source.ClockState().TickAssigned && clone.SelectedProject()->Project == project && clone.CachedTimeline() == source.CachedTimeline() && clone.SelectionSerial() == source.SelectionSerial() && clone.MeshDivisionRatio() == 0.375f, "native command clone must preserve selected timeline and complete retained clock/policy state");
	Require(clone.TimelineVariables() != source.TimelineVariables() && clone.TimelineVariables()->Uses(clone, *clone.CachedTimeline()) && clone.ParameterValues().ReadLocal("value") == 4.5f && clone.ParameterValues().ReadLocal("nested/value") == 0.75f && clone.ParameterValues().RegisteredParameterCount() == 0, "cloned current-variable records and stored descriptor values must have independent actual ownership before tree rebinding");
	source.SetTickCount(2);
	source.ParameterValues().WriteLocal("value", 99, 0);
	source.ParameterValues().WriteLocal("nested::value", 33, 0);
	Require(clone.ClockState().TickCount == 13.5f && clone.ParameterValues().ReadLocal("value") == 4.5f && clone.ParameterValues().ReadLocal("nested::value") == 0.75f, "outgoing command/map mutation must not alter the clone");
	clone.Play("idle");
	Require(clone.Names().CurrentMotion.empty() && installations == 0, "native omitted currentMotion string must coexist with same requested-name early return");
	RequireFailure([&] {
		clone.CopyForClone(source);
	},
	    "fresh destination");
	sgre::scene::MotionResourceManager other_manager(cache);
	auto unrelated_identity = Identity();
	unrelated_identity.DocumentInstance += 1;
	static_cast<void>(other_manager.Register(unrelated_identity, model, {}));
	const auto destination_project = other_manager.Register(Identity(), model, {});
	sgre::scene::MotionPlayerCommands cross_manager(other_manager);
	cross_manager.CopyForClone(source);
	Require(cross_manager.SelectedProject()->Project == destination_project && destination_project != source.SelectedProject()->Project && cross_manager.CachedTimeline() == source.CachedTimeline() && cross_manager.Names().CurrentMotion.empty() && cross_manager.ClockState().TickCount == source.ClockState().TickCount, "native destination manager override must use the exact immutable resource alias rather than a colliding source project slot");
	sgre::scene::MotionResourceManager missing_manager(cache);
	static_cast<void>(missing_manager.Register(unrelated_identity, model, {}));
	sgre::scene::MotionPlayerCommands missing_alias(missing_manager);
	RequireFailure([&] {
		missing_alias.CopyForClone(source);
	},
	    "destination alias");
	Require(missing_alias.Names().CurrentCharacter.empty() && missing_alias.ParameterValues().LocalValueCount() == 0, "a missing actual destination resource alias must reject before destination publication");
	sgre::scene::MotionResourceManager replaced_manager(cache);
	static_cast<void>(replaced_manager.Register(Identity(), std::make_shared<sgre::resource::MotionResource>(*model), {}));
	sgre::scene::MotionPlayerCommands replaced_alias(replaced_manager);
	RequireFailure([&] {
		replaced_alias.CopyForClone(source);
	},
	    "does not alias");
	Require(replaced_alias.Names().CurrentCharacter.empty(), "same-named redecoded resource must not replace the actual immutable source owner");
}

static std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
	if (offset > bytes.size() || 4 > bytes.size() - offset) {
		throw std::runtime_error("truncated native command fixture word");
	}

	std::uint32_t value = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
	}

	return value;
}

template <std::size_t Size>
static std::array<std::uint8_t, Size> ReadRecord(std::ifstream& input) {
	std::array<char, Size> raw{};
	input.read(raw.data(), static_cast<std::streamsize>(raw.size()));
	Require(input.gcount() == static_cast<std::streamsize>(raw.size()), "truncated native command fixture record");
	std::array<std::uint8_t, Size> result{};
	for (std::size_t index = 0; index < Size; ++index) {
		result[index] = static_cast<std::uint8_t>(static_cast<unsigned char>(raw[index]));
	}

	return result;
}

static void RequireNativeState(const sgre::scene::MotionPlayerCommands& commands, const sgre::resource::MotionResource& model, std::span<const std::uint8_t> expected) {
	const auto& names = commands.Names();
	const std::array<std::string_view, 7> actual_names{names.CurrentCharacter, names.RequestedCharacter, names.DeferredStealthCharacter, names.CurrentMotion, names.RequestedMotion, names.DeferredStealthMotion, names.DelayedMotion};
	for (std::size_t index = 0; index < actual_names.size(); ++index) {
		Require(expected[index] < kQueries.size() && actual_names[index] == kQueries[expected[index]], "command owner name differs from native transition");
	}

	std::size_t cached = 0;
	for (std::size_t character_index = 0; character_index < kCharacters.size(); ++character_index) {
		for (std::size_t motion_index = 0; motion_index < kMotions.size(); ++motion_index) {
			if (commands.CachedTimeline() == &model.Objects[character_index].Timelines[motion_index]) {
				cached = character_index * kMotions.size() + motion_index + 1;
			}
		}
	}

	Require(commands.SelectedProject().has_value() == (expected[7] != 0) && cached == expected[8], "selected root and cached ordinary timeline differ from native transition");
	const auto& clock = commands.ClockState();
	Require(clock.Playing == (expected[9] != 0) && clock.TickAssigned == (expected[10] != 0) && clock.InitialTagDispatchPending == (expected[11] != 0), "command owner clock flags differ from native transition");
	Require(static_cast<std::uint32_t>(names.DelayedFlags) == ReadU32(expected, 12), "delayed command flags differ from native transition");
	const std::array<float, 5> floats{clock.TickCount, clock.EvaluationTime, clock.LastTime, clock.LoopTime, clock.Speed};
	for (std::size_t index = 0; index < floats.size(); ++index) {
		Require(std::bit_cast<std::uint32_t>(floats[index]) == ReadU32(expected, 16 + index * 4), "command owner clock words differ from native transition");
	}
}

static void ReplayNative(const char* path) {
	std::ifstream input(path, std::ios::binary);
	Require(input.is_open(), "cannot open native command fixture");
	const auto header = ReadRecord<12>(input);
	Require(header[0] == 'M' && header[1] == 'P' && header[2] == 'C' && header[3] == '1', "invalid native command fixture signature");
	const auto scenarios = ReadU32(header, 4);
	const auto steps = ReadU32(header, 8);
	Require(scenarios > 0 && scenarios <= 256 && steps > 0 && steps <= 24, "native command fixture work exceeds its bounds");
	auto cache = EmptyTextureCache();
	sgre::scene::MotionResourceManager manager(cache);
	const auto model = Model();
	static_cast<void>(manager.Register(Identity(), model, {}));
	for (std::uint32_t scenario = 0; scenario < scenarios; ++scenario) {
		sgre::scene::MotionPlayerCommands commands(manager);
		for (std::uint32_t step = 0; step < steps; ++step) {
			const auto record = ReadRecord<47>(input);
			Require(record[1] < kQueries.size() && record[10] <= 1, "native command fixture input is out of range");
			const auto flags = static_cast<sgre::scene::MotionPlayFlags>(ReadU32(record, 2));
			bool skip = false;
			switch (record[0]) {
			case 0:
				commands.SetCharacter(kQueries[record[1]], (ReadU32(record, 2) & 16) != 0 ? sgre::scene::MotionCharacterMode::Stealth : sgre::scene::MotionCharacterMode::Ordinary);
				break;
			case 1:
				commands.Play(kQueries[record[1]], flags);
				break;
			case 2:
				commands.Stop();
				break;
			case 3:
				commands.SetTickCount(std::bit_cast<float>(ReadU32(record, 6)));
				break;
			case 4:
				skip = commands.ProcessDelayedPlay().SkipRemainingUpdate;
				break;
			default:
				throw std::runtime_error("unsupported native command fixture operation");
			}

			Require(skip == (record[10] != 0), "delayed update skip differs from native transition");
			RequireNativeState(commands, *model, std::span<const std::uint8_t>(record).subspan(11));
		}
	}

	Require(input.peek() == std::char_traits<char>::eof(), "native command fixture has trailing data");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 2, "usage: motion_player_commands_tests [native-command-fixture]");
		TestRequestsAndControls();
		TestNativeCloneCommandState();
		if (argc == 2) {
			ReplayNative(argv[1]);
		}

		std::cout << "motion player command tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
