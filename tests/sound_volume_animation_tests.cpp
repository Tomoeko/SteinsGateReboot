#include "sgre/audio/sound_volume_animation.hpp"

#include "sgre/audio/sound_resource_control.hpp"
#include "sgre/audio/sound_volume.hpp"
#include "sgre/audio/sound_volume_animations.hpp"
#include "sgre/core/frame_clock.hpp"
#include "sgre/script/sound_bindings.hpp"

#include "voice_control_fixture.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

static std::uint32_t ReadNativeWord(std::istream& input) {
	std::uint32_t result = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto next = input.get();
		Require(next != std::char_traits<char>::eof(), "Sound animation native corpus is truncated");
		result |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(next)) << shift;
	}

	return result;
}

static void TestNativeNumericCorpus(const std::filesystem::path& corpus_path) {
	std::ifstream input(corpus_path, std::ios::binary);
	Require(input.is_open() && ReadNativeWord(input) == 0x4e415653 && ReadNativeWord(input) == 1 && ReadNativeWord(input) == 48, "Sound animation corpus identity or extent differs");
	for (std::size_t record = 0; record < 48; ++record) {
		Require(ReadNativeWord(input) <= 1, "native corpus contains an unknown animation task type");
		const auto initial = std::bit_cast<float>(ReadNativeWord(input));
		const auto target = std::bit_cast<float>(ReadNativeWord(input));
		const auto duration = std::bit_cast<std::int32_t>(ReadNativeWord(input));
		const auto curve = std::bit_cast<std::int32_t>(ReadNativeWord(input));
		Require(ReadNativeWord(input) == 12, "native corpus phase count differs");
		sgre::audio::SoundVolumeAnimation animation(initial, target, duration, curve);
		for (std::size_t phase = 0; phase < 12; ++phase) {
			const auto delta = std::bit_cast<float>(ReadNativeWord(input));
			const auto elapsed_low = ReadNativeWord(input);
			const auto elapsed = static_cast<std::uint64_t>(elapsed_low) | (static_cast<std::uint64_t>(ReadNativeWord(input)) << 32);
			const auto ordinal = ReadNativeWord(input);
			const auto status = ReadNativeWord(input);
			const auto emitted = ReadNativeWord(input);
			const auto gain_word = ReadNativeWord(input);
			Require(status <= 2 && emitted <= 1, "native phase exceeds bounded status/sample domain");
			const auto sample = animation.Advance(delta);
			const auto state = animation.State();
			Require(state.Elapsed == elapsed && state.Ordinal.value_or(0xffffffff) == ordinal && state.Complete == (status == 2) && sample.has_value() == (emitted != 0), "native elapsed/curve ordinal/emission/terminal state differs");
			if (sample) {
				const auto expected = std::bit_cast<float>(gain_word);
				Require(std::abs(*sample - expected) <= 0.000002f * std::max(1.0f, std::abs(expected)), "native loudness-coordinate sample exceeds the retained finite math tolerance");
				if (state.Complete) {
					Require(std::bit_cast<std::uint32_t>(*sample) == gain_word, "terminal sample must copy exact native target bits");
				}
			}
		}
	}
	Require(input.get() == std::char_traits<char>::eof(), "native animation corpus has trailing data");
	RequireFailure([] {
		static_cast<void>(sgre::audio::SoundVolumeAnimation(1, 0, -1, 0));
	},
	    "bounded native duration");
	RequireFailure([] {
		static_cast<void>(sgre::audio::SoundVolumeAnimation(1, 0, 4, 65));
	},
	    "bounded native duration");
	sgre::audio::SoundVolumeAnimation zero(1, -0.0f, 0, 0);
	Require(std::signbit(*zero.Advance(std::numeric_limits<float>::quiet_NaN())) && zero.State().Complete && !zero.Advance(1), "zero-duration native sampling bypasses the unused clock and retains target signed zero");
}

class ActualSoundAnimationFixture {
public:
	explicit ActualSoundAnimationFixture(const std::filesystem::path& directory, bool configure_clock = true) : Runtime(directory, Vectors), Control(Runtime.Cache, Runtime.Mounts, Runtime.Processor, "win", "synthetic-seed", false) {
		Runtime.LoadArchive("beta", Vectors);
		Require(Control.Load("configuration.psb"), "Sound animation must acquire the actual cache-backed configuration");
		for (std::size_t visit = 0; visit < 8 && !Control.GetLoaded("configuration.psb"); ++visit) {
			Control.AdvancePostLoop();
		}
		Require(Control.GetLoaded("configuration.psb"), "Sound configuration did not complete");
		for (const auto archive : {"alpha", "beta"}) {
			Require(Control.LoadArchive(archive), "Sound animation archive request was not created");
			for (std::size_t visit = 0; visit < 8 && !Control.GetArchiveLoaded(archive); ++visit) {
				Control.AdvancePostLoop();
			}
			Require(Control.GetArchiveLoaded(archive), "Sound animation archive did not complete");
		}
		Ids[0] = Prepare("alpha", "tone", "BGM");
		Ids[1] = Prepare("beta", "tone", "bgm");
		Ids[2] = Prepare("alpha", "long-cue-name-20bytes", "BGM");
		if (configure_clock) {
			Control.ConfigureVolumeAnimations(Clock);
		}
		Bindings = std::make_unique<sgre::script::ScriptSoundBindings>(Vm, Control);
		Class = Bindings->SoundClass();
	}

	~ActualSoundAnimationFixture() noexcept {
		try {
			Bindings.reset();
			Class = {};
			Vm.Close();
			Control.RequestClose();
			for (std::size_t visit = 0; visit < 32 && !Control.CanClose(); ++visit) {
				Control.AdvancePostLoop();
			}
			Control.Close();
		} catch (...) {
			std::terminate();
		}
	}

	[[nodiscard]] sgre::audio::BufferedSoundCueId Prepare(std::string_view archive, std::string_view name, std::string_view group) {
		sgre::audio::BufferedSoundCueRequest request;
		request.ArchiveName = archive;
		request.CueName = name;
		request.Group = group;
		sgre::audio::SoundMixParameters parameters;
		parameters.UseSourcePan = true;
		const auto prepared = Control.BufferedCues().Prepare(request, parameters, 2);
		Require(prepared.Cue && prepared.Status == sgre::audio::BufferedSoundCuePreparationStatus::Prepared, "Sound animation fixture requires real decoded PCM sources");

		return *prepared.Cue;
	}

	[[nodiscard]] sgre::script::SquirrelValue Invoke(std::string_view method, std::initializer_list<sgre::script::SquirrelValue> operands) {
		std::vector<sgre::script::SquirrelValue> arguments{Class};
		arguments.insert(arguments.end(), operands.begin(), operands.end());
		const auto callback = Vm.GetSlot(Class, sgre::script::SquirrelString(std::string(method)));

		return Vm.Invoke(callback, arguments).Value;
	}

	std::array<CueVector, 2> Vectors{CueVector{}, CueVector{"long-cue-name-20bytes"}};
	ActualCueRuntime Runtime;
	sgre::core::FrameClock Clock;
	sgre::audio::SoundResourceControl Control;
	sgre::script::SquirrelVm Vm;
	std::unique_ptr<sgre::script::ScriptSoundBindings> Bindings;
	sgre::script::SquirrelValue Class;
	std::array<sgre::audio::BufferedSoundCueId, 3> Ids{};
};

static void TestActualReplacementAndLease(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> vectors{CueVector{}};
	ActualCueRuntime runtime(directory, vectors);
	const auto prepared = runtime.Prepare("tone");
	Require(prepared.Cue.has_value(), "actual lease fixture requires its decoded cue");
	sgre::core::FrameClock clock;
	sgre::scene::ParallelTaskSet parallel(8);
	sgre::audio::SoundVolumeAnimations animations(runtime.Cues, clock, parallel);
	const auto id = *prepared.Cue;
	const sgre::audio::BufferedSoundCueId absent{12345};
	animations.AppendVoice(id, 0, 9, 0);
	animations.AppendVoice(absent, 0, 9, 0);
	animations.AppendGroup("BGM", 0, 9, 0);
	animations.AppendGroup("bgm", 0, 9, 0);
	animations.AppendVoice(id, 0.5f, 9, 0);
	animations.AppendGroup("BGM", 0.5f, 9, 0);
	std::array<sgre::scene::ParallelTaskHandle, 8> order{};
	Require(parallel.WriteOrder(order) == 6, "actual task order must retain predecessor rows until real retirement");
	const std::array<sgre::scene::TaskStatus, 6> expected{sgre::scene::TaskStatus::Retire, sgre::scene::TaskStatus::Active, sgre::scene::TaskStatus::Retire, sgre::scene::TaskStatus::Active, sgre::scene::TaskStatus::Active, sgre::scene::TaskStatus::Active};
	for (std::size_t index = 0; index < expected.size(); ++index) {
		Require(parallel.State(order[index]).Status == expected[index], "native same-type/exact-key retirement differs");
	}
	parallel.Update();
	Require(parallel.Count() == 4 && !animations.CanClose() && runtime.Cues.Inspect(id).CallbackActive, "actual animation tasks may not consume PCM or pretend source completion");
	const auto retained_gain = runtime.Cues.VoiceVolume(id);
	static_cast<void>(parallel.WriteOrder(order));
	auto lease = parallel.Extract(order[2]);
	RequireFailure([&] {
		animations.AppendVoice(id, 0.25f, 4, 0);
	},
	    "detached callback lease");
	Require(runtime.Cues.VoiceVolume(id) == retained_gain && parallel.Count() == 3, "failed detached-key replacement must not publish or sample");
	animations.CancelAll();
	parallel.Update();
	Require(parallel.Count() == 0 && !animations.CanClose(), "actual extracted callback lease must keep its borrowed services alive after list retirement");
	lease.Release();
	Require(animations.CanClose(), "callback lease destruction must release the last actual animation borrower");
	bool foreign_rejected = false;
	std::thread foreign([&] {
		try {
			animations.AppendGroup("BGM", 0, 1, 0);
		} catch (const std::runtime_error&) {
			foreign_rejected = true;
		}
	});
	foreign.join();
	Require(foreign_rejected && parallel.Count() == 0, "foreign animation publication must fail before mutation");
	sgre::scene::ParallelTaskCallbacks callback;
	callback.Update = [&](auto handle) {
		RequireFailure([&] {
			animations.AppendGroup("BGM", 0, 1, 0);
		},
		    "inactive actual group");
		RequireFailure([&] {
			animations.CancelAll();
		},
		    "inactive actual group");
		parallel.SetStatus(handle, sgre::scene::TaskStatus::Retire);
	};
	static_cast<void>(parallel.Append(std::move(callback)));
	parallel.Update();
	Require(parallel.Count() == 0 && animations.CanClose(), "actual traversal rejection must leave no invented animation row");
	sgre::scene::ParallelTaskSet limited(1);
	sgre::audio::SoundVolumeAnimations bounded(runtime.Cues, clock, limited);
	bounded.AppendVoice(id, 0, 9, 0);
	static_cast<void>(limited.WriteOrder(order));
	RequireFailure([&] {
		bounded.AppendVoice(id, 0.5f, 9, 0);
	},
	    "capacity is exhausted");
	Require(limited.Count() == 1 && limited.State(order[0]).Status == sgre::scene::TaskStatus::Active && runtime.Cues.VoiceVolume(id) == retained_gain, "failed actual Parallel publication must preserve its unsampled predecessor");
	bounded.CancelAll();
	limited.Update();
	Require(bounded.CanClose(), "bounded publication fixture must retire its real task");
}

static void TestActualGainsAndSourcePosition(const std::filesystem::path& directory) {
	ActualSoundAnimationFixture fixture(directory);
	auto& cues = fixture.Control.BufferedCues();
	fixture.Control.SetLoudnessVolumeEnabled(false);
	const auto before = cues.InspectChannel(fixture.Ids[0], 0);
	const auto other = cues.InspectChannel(fixture.Ids[1], 0).Mix->Matrix;
	fixture.Control.AnimateGroupVolume("BGM", 0, 4, 0);
	fixture.Control.AdvancePostLoop();
	const auto expected = sgre::audio::SoundVolumeToLinear(1.0f - 15.0f / 63.0f, sgre::audio::SoundVolumeUnit::Loudness);
	const auto channel = cues.InspectChannel(fixture.Ids[0], 0);
	Require(std::abs(fixture.Control.GroupVolume("BGM") - expected) < 0.000001f && std::abs(channel.Mix->Matrix[0] - expected * 0.125f) < 0.000001f, "actual group task must sample nonlinear native gain despite the current setting flag");
	Require(cues.InspectChannel(fixture.Ids[1], 0).Mix->Matrix == other && channel.State.Samples.RemainingBufferFrames == before.State.Samples.RemainingBufferFrames && channel.State.Samples.CallbackActive, "case-distinct group and actual source sample position must survive volume sampling");
	fixture.Control.AnimateVoiceVolume(fixture.Ids[0], -0.5f, 0, 0);
	fixture.Control.AdvancePostLoop();
	Require(cues.VoiceVolume(fixture.Ids[0]) == -0.5f && cues.VoiceVolume(fixture.Ids[2]) == 1, "zero-duration voice task must commit only its actual cue gain");
	Require(cues.Stop(fixture.Ids[0]), "actual cue stop must retire its decoded source");
	fixture.Control.AnimateVoiceVolume(fixture.Ids[0], 0.25f, 2, 0);
	fixture.Control.AdvancePostLoop();
	fixture.Control.AdvancePostLoop();
	Require(cues.ActiveCount() == 2 && cues.VoiceVolume(fixture.Ids[0]) == 1, "stale integer-ID animation must finish without retaining or recreating a stopped cue");
	fixture.Clock.SetVariableFrame(true);
	static_cast<void>(fixture.Clock.Advance(50));
	const auto step = fixture.Clock.VariableStep();
	const auto residual = fixture.Clock.ResidualTicks();
	fixture.Control.AnimateGroupVolume("fresh", 0, 2, 0);
	fixture.Control.AdvancePostLoop();
	Require(fixture.Control.GroupVolume("fresh") == 0 && fixture.Clock.VariableStep() == step && fixture.Clock.ResidualTicks() == residual, "actual selected variable delta is sampled without advancing the clock a second time");
}

static void TestOriginalPhysicalResultsAndValidation(const std::filesystem::path& directory) {
	ActualSoundAnimationFixture fixture(directory);
	fixture.Control.SetLoudnessVolumeEnabled(false);
	const auto id = std::bit_cast<std::int32_t>(fixture.Ids[0].Value);
	Require(std::get<std::int32_t>(fixture.Invoke("animateGroupVolume", {sgre::script::SquirrelString("BGM"), 0.5f, std::int32_t{6}, std::int32_t{-4}})) == -4, "group callback must transfer its physical final integer");
	Require(std::get<float>(fixture.Invoke("animateVoiceVolume", {id, 0.5f, 6.9f, 3.75f})) == 3.75f, "voice integer conversion must preserve the actual final float result");
	Require(!std::get<bool>(fixture.Invoke("animateVoiceVolume", {id, 0.5f, true, false})), "voice integer conversion must preserve the actual final Boolean result");
	const auto rows = fixture.Control.ParallelTasks().Count();
	const sgre::script::SquirrelString last("final_physical_string");
	for (const auto& selector : std::array<sgre::script::SquirrelValue, 2>{std::int32_t{-1}, sgre::script::SquirrelString("missing")}) {
		const auto result = fixture.Invoke("animateVoiceVolume", {selector, sgre::script::SquirrelString("invalid_volume"), sgre::script::SquirrelString("invalid_duration"), last});
		Require(std::get<sgre::script::SquirrelString>(result).SharesIdentity(last) && fixture.Control.ParallelTasks().Count() == rows, "sentinel/missing-name must skip conversion and retain the exact final String identity");
	}
	RequireFailure([&] {
		static_cast<void>(fixture.Invoke("animateGroupVolume", {sgre::script::SquirrelString("BGM"), 0.5f, true, std::int32_t{2}}));
	},
	    "invalid argument");
	RequireFailure([&] {
		static_cast<void>(fixture.Invoke("animateVoiceVolume", {id, true, std::int32_t{2}, std::int32_t{0}}));
	},
	    "invalid argument");
	RequireFailure([&] {
		static_cast<void>(fixture.Invoke("animateVoiceVolume", {id, 0.5f, std::int32_t{-1}, std::int32_t{0}}));
	},
	    "bounded native duration");
	Require(fixture.Control.ParallelTasks().Count() == rows && fixture.Control.BufferedCues().VoiceVolume(fixture.Ids[0]) == 1, "invalid conversions must preserve pending rows and unsampled real gains");
	ActualSoundAnimationFixture unconfigured(directory, false);
	RequireFailure([&] {
		unconfigured.Control.AnimateGroupVolume("BGM", 0, 3, 0);
	},
	    "actual selected Looper clock");
	Require(unconfigured.Control.ParallelTasks().Count() == 0, "absent actual clock must not publish a stand-in task");
}

static void TestContainingCloseAndTraversal(const std::filesystem::path& directory) {
	ActualSoundAnimationFixture fixture(directory);
	fixture.Control.AnimateGroupVolume("BGM", 0, 100, 0);
	bool inspected = false;
	sgre::scene::ParallelTaskCallbacks callback;
	callback.Update = [&](auto handle) {
		RequireFailure([&] {
			fixture.Control.RequestClose();
		},
		    "inactive actual group");
		RequireFailure([&] {
			fixture.Control.AnimateGroupVolume("after_rejection", 0, 1, 0);
		},
		    "inactive actual group");
		inspected = true;
		fixture.Control.ParallelTasks().SetStatus(handle, sgre::scene::TaskStatus::Retire);
	};
	static_cast<void>(fixture.Control.ParallelTasks().Append(std::move(callback)));
	fixture.Control.ParallelTasks().Update();
	Require(inspected, "containing traversal guard was not exercised");
	fixture.Control.AnimateGroupVolume("after_rejection", 0, 100, 0);
	fixture.Control.RequestClose();
	Require(!fixture.Control.CanClose() && fixture.Control.ParallelTasks().Count() == 2, "close request must retain marked real animation rows until actual PostLoop retirement");
	RequireFailure([&] {
		fixture.Control.AnimateGroupVolume("forbidden", 0, 1, 0);
	},
	    "pending close request");
	fixture.Control.AdvancePostLoop();
	Require(fixture.Control.ParallelTasks().Count() == 0 && fixture.Control.CanClose() && fixture.Control.GroupVolume("after_rejection") == 1, "actual close retirement must release callbacks without sampling a cancelled target");
	fixture.Control.Close();
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "Sound animation tests require the owned fixture directory and frozen native corpus");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestNativeNumericCorpus(std::filesystem::path(argv[2]));
		TestActualReplacementAndLease(directory);
		TestActualGainsAndSourcePosition(directory);
		TestOriginalPhysicalResultsAndValidation(directory);
		TestContainingCloseAndTraversal(directory);
		std::cout << "five Sound animation groups;48 native tasks/576 phases;actual cache/cues/Parallel/VM gain/result/lease/close ownership passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
