#include "sgre/audio/offline_buffered_sound_pool.hpp"

#include "mastering_fixture.hpp"
#include "sgre/audio/sound_resource_control.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

template <typename Output>
concept PublicOutputConnectionHook = requires(Output& output, sgre::audio::BufferedSoundSources& sources) {
	output.ConnectSourceOwner(sources);
};

template <typename Output>
concept PublicOutputReleaseHook = requires(Output& output, sgre::audio::BufferedSoundSources& sources) {
	output.ReleaseSourceOwner(sources);
};

template <typename Output>
concept PublicOutputAttachHook = requires(Output& output, sgre::audio::BufferedSoundSources& sources, sgre::audio::SoundVoiceAddress address) {
	output.AttachSource(address, sources);
};

template <typename Output>
concept PublicOutputRetireHook = requires(Output& output, sgre::audio::SoundVoiceAddress address) {
	output.RetireSource(address);
};

template <typename Output>
concept PublicOutputMixHook = requires(Output& output, sgre::audio::SoundVoiceAddress address, const sgre::audio::SoundMix& mix) {
	output.ApplySourceMix(address, mix);
};

static_assert(!PublicOutputConnectionHook<sgre::audio::BufferedSoundOutput> && !PublicOutputConnectionHook<sgre::audio::OfflineBufferedSoundPool>);
static_assert(!PublicOutputReleaseHook<sgre::audio::BufferedSoundOutput> && !PublicOutputReleaseHook<sgre::audio::OfflineBufferedSoundPool>);
static_assert(!PublicOutputAttachHook<sgre::audio::BufferedSoundOutput> && !PublicOutputAttachHook<sgre::audio::OfflineBufferedSoundPool>);
static_assert(!PublicOutputRetireHook<sgre::audio::BufferedSoundOutput> && !PublicOutputRetireHook<sgre::audio::OfflineBufferedSoundPool>);
static_assert(!PublicOutputMixHook<sgre::audio::BufferedSoundOutput> && !PublicOutputMixHook<sgre::audio::OfflineBufferedSoundPool>);

static sgre::audio::OfflineSoundMasteringDescription PoolDescription() {
	sgre::audio::OfflineSoundMasteringDescription result;
	result.SamplesPerSecond = 48000;
	result.Channels = 2;
	result.MaximumFrames = 1024;

	return result;
}

static sgre::audio::BufferedSoundCueId PrepareRealCue(ActualCueRuntime& runtime, bool paused = false) {
	const auto result = runtime.Prepare("tone", 0, paused);
	Require(result.Status == BufferedSoundCuePreparationStatus::Prepared && result.Cue.has_value(), "dynamic fixture requires its actual decoded cue/source owner");

	return *result.Cue;
}

static void RequireZero(std::span<const float> output) {
	Require(std::all_of(output.begin(), output.end(), [](float value) {
		return value == 0;
	}),
	    "actual offline graph published nonzero idle/paused output");
}

static void RequireTwoSourceSamples(std::span<const float> output) {
	const std::array<float, 8> expected{0.0625f, 0.1875f, 0.125f, 0.375f, -0.125f, -0.375f, 0, 0};
	for (std::size_t index = 0; index < output.size(); ++index) {
		Require(output[index] == (index < expected.size() ? expected[index] : 0), "actual dynamic system output differs from independent signed PCM/matrix sum");
	}
}

static void TestIdleSourceOutlivesCueAndArchive(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> definitions{CueVector{"tone", 2}};
	ActualCueRuntime runtime(directory, definitions);
	sgre::audio::OfflineBufferedSoundPool engine(runtime.Cues, PoolDescription());
	Require(engine.SourceNodeCount() == 0 && engine.RenderedFrames() == 0, "fresh actual system graph contains an invented source");
	const auto first = PrepareRealCue(runtime);
	Require(engine.SourceNodeCount() == 2, "actual decoded source preparation did not attach both real nodes");
	const auto address = runtime.Cues.InspectChannel(first, 0).Source;
	std::array<float, 128> output;
	output.fill(9);
	Require(engine.Render(output) == 64 && engine.RenderedFrames() == 64, "actual offline pool did not render its first exact extent");
	RequireTwoSourceSamples(output);
	runtime.Cues.CommitAndRetire();
	Require(runtime.Cues.ActiveCount() == 0 && runtime.Cues.RetiringCount() == 0 && engine.SourceNodeCount() == 2, "cue retirement destroyed retained actual source slots or retained the cue ID");
	Require(runtime.Cues.UnloadArchive("alpha"), "actual completed archive did not enter retirement");
	runtime.Loads.AdvanceSerialLoads();
	Require(runtime.Loads.ArchiveOwnerCount() == 0, "completed actual callbacks retained their released archive borrow");
	Require(engine.Render(output) == 64, "retained idle nodes depended on retired cue/archive identity");
	RequireZero(output);
	const auto idle = runtime.Cues.InspectSource(address);
	Require(!idle.NativeBufferContextPresent && !idle.Samples.CallbackActive && idle.Samples.ConsumedFrames == 4, "idle node changed real completion or native null-context state");
	runtime.LoadArchive("alpha", definitions);
	const auto second = PrepareRealCue(runtime);
	const auto replacement = runtime.Cues.InspectChannel(second, 0).Source;
	Require(second.Value != first.Value && address.Type == replacement.Type && address.Slot == replacement.Slot && engine.SourceNodeCount() == 2, "native-style slot reuse failed to replace actual source nodes and publish a new ID");
	Require(engine.Render(output) == 64, "actual replacement source did not render");
	RequireTwoSourceSamples(output);
}

static void TestPauseReasonsAndResume(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> definitions{CueVector{}};
	ActualCueRuntime runtime(directory, definitions);
	sgre::audio::OfflineBufferedSoundPool engine(runtime.Cues, PoolDescription());
	Require(runtime.Cues.PauseAll(), "actual all-pause reason was not set");
	const auto cue = PrepareRealCue(runtime);
	std::array<float, 64> output;
	Require(engine.Render(output) == 32, "actual paused graph did not render");
	RequireZero(output);
	Require(runtime.Cues.InspectChannel(cue, 0).State.Samples.RemainingBufferFrames == 4, "global pause consumed queued samples");
	Require(runtime.Cues.ApplyControl(cue, sgre::audio::BufferedSoundCueControl::Pause), "actual independent voice pause was not set");
	Require(runtime.Cues.ResumeAll(), "actual all-pause reason was not cleared");
	Require(engine.Render(output) == 32, "actual remaining voice pause did not render");
	RequireZero(output);
	Require(runtime.Cues.Inspect(cue).PauseReasons == 1 && runtime.Cues.InspectChannel(cue, 0).State.Samples.CallbackActive, "all-resume restarted a source with another actual pause reason");
	Require(runtime.Cues.ApplyControl(cue, sgre::audio::BufferedSoundCueControl::Resume), "actual voice resume was not set");
	Require(engine.Render(output) == 32, "actual resumed source did not render");
	Require(output[0] == 0.03125f && output[1] == 0.09375f, "actual resumed system source differs from its PCM/matrix factors");
	Require(runtime.Cues.InspectChannel(cue, 0).State.Samples.ConsumedFrames == 4, "actual resumed source callback did not consume its real buffer");
}

static void TestStopFlushAndReuse(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> definitions{CueVector{}};
	ActualCueRuntime runtime(directory, definitions);
	sgre::audio::OfflineBufferedSoundPool engine(runtime.Cues, PoolDescription());
	const auto first = PrepareRealCue(runtime, true);
	const auto address = runtime.Cues.InspectChannel(first, 0).Source;
	Require(runtime.Cues.ApplyControl(first, sgre::audio::BufferedSoundCueControl::Stop), "actual stop did not remove the ID");
	const auto flushed = runtime.Cues.InspectSource(address);
	Require(!flushed.Samples.CallbackActive && !flushed.Samples.Queued && flushed.Samples.ConsumedFrames == 0 && !flushed.NativeBufferContextPresent, "stop/flush substituted context or consumption for actual callback completion");
	Require(runtime.Cues.ActiveCount() == 0 && runtime.Cues.RetiringCount() == 0 && engine.SourceNodeCount() == 1, "stop destroyed the idle physical source before slot reuse");
	std::array<float, 64> output;
	Require(engine.Render(output) == 32, "actual stopped idle source did not render");
	RequireZero(output);
	const auto second = PrepareRealCue(runtime);
	Require(second.Value != first.Value && engine.SourceNodeCount() == 1, "actual source reuse duplicated nodes or reused the cue ID");
	Require(engine.Render(output) == 32 && output[0] == 0.03125f, "flushed old samples survived actual replacement attachment");
}

static void TestPartialSourceFailure(const std::filesystem::path& directory) {
	CueVector definition{"tone", 2};
	definition.InvalidChannelMask = 2;
	const std::array<CueVector, 1> definitions{definition};
	ActualCueRuntime runtime(directory, definitions);
	sgre::audio::OfflineBufferedSoundPool engine(runtime.Cues, PoolDescription());
	const auto result = runtime.Prepare("tone");
	Require(result.Status == BufferedSoundCuePreparationStatus::PartiallyPrepared && result.Cue.has_value(), "actual failed channel suppressed native-style logical cue publication");
	Require(engine.SourceNodeCount() == 1 && runtime.Cues.InspectChannel(*result.Cue, 1).Error.size() != 0, "partial source failure attached a nominal node or lost its diagnostic");
	std::array<float, 64> output;
	Require(engine.Render(output) == 32 && output[0] == 0.03125f && output[1] == 0.09375f, "actual accepted source did not survive another channel's failure");
	runtime.Cues.CommitAndRetire();
	Require(runtime.Cues.ActiveCount() == 0 && engine.SourceNodeCount() == 1, "partial failure retirement replaced actual callback lifetime with cue counts");
}

static void TestUnsupportedPitchRollback(const std::filesystem::path& directory) {
	CueVector definition;
	definition.DisableRateConversion = false;
	const std::array<CueVector, 1> definitions{definition};
	ActualCueRuntime runtime(directory, definitions);
	sgre::audio::OfflineBufferedSoundPool engine(runtime.Cues, PoolDescription());
	sgre::audio::SoundMixParameters parameters;
	parameters.VoicePitchOctaves = 1;
	const auto rejected = runtime.Prepare("tone", 0, false, "alpha", true, parameters);
	Require(rejected.Status == BufferedSoundCuePreparationStatus::FailedSources && rejected.Cue.has_value() && engine.SourceNodeCount() == 0, "unrecovered pitch admitted an actual source or suppressed the logical failure cue");
	Require(runtime.Cues.InspectChannel(*rejected.Cue, 0).Error.find("unit unchanged frequency ratio") != std::string_view::npos, "unsupported source mix lost its precise diagnostic");
	const auto accepted = PrepareRealCue(runtime);
	Require(accepted.Value != rejected.Cue->Value && engine.SourceNodeCount() == 1, "source-specific unsupported mix poisoned the actual engine or ID order");
	std::array<float, 64> output;
	Require(engine.Render(output) == 32 && output[0] == 0.03125f, "actual unchanged-rate replacement did not survive unsupported pitch rollback");
}

static void TestBothOwnerReleaseOrders(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> definitions{CueVector{}};
	ActualCueRuntime runtime(directory, definitions);
	{
		sgre::audio::OfflineBufferedSoundPool engine(runtime.Cues, PoolDescription());
		const auto cue = PrepareRealCue(runtime, true);
		engine.Close();
		const auto source = runtime.Cues.InspectChannel(cue, 0).State.Samples;
		Require(source.Queued && source.CallbackActive && source.RemainingBufferFrames == 4, "output release fabricated CPU consume/flush completion");
		Require(runtime.Cues.ApplyControl(cue, sgre::audio::BufferedSoundCueControl::Resume), "actual CPU source did not survive output disconnect");
		std::array<float, 4> samples;
		Require(runtime.Cues.ConsumeChannelFrames(cue, 0, samples) == 4 && samples[0] == 0.25f, "output teardown released the actual decoded CPU buffer");
		RequireFailure([&] {
			static_cast<void>(engine.Render(samples));
		},
		    "actual live serialized system owner");
	}
	ActualCueRuntime source_first(directory, definitions);
	sgre::audio::OfflineBufferedSoundPool survivor(source_first.Cues, PoolDescription());
	static_cast<void>(PrepareRealCue(source_first));
	source_first.Cues.Close();
	Require(survivor.SourceNodeCount() == 0, "source owner destruction retained a foreign callback into its dead slots");
	survivor.Close();
}

static void TestActualSoundResourceController(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> definitions{CueVector{"tone", 2}};
	ActualCueRuntime prepared(directory, definitions);
	// Reuse the actual worker-produced cache documents; this containing Sound owner acquires its own references.
	sgre::audio::SoundResourceControl sound(prepared.Cache, prepared.Mounts, prepared.Processor, "win", "bounded-fixture-seed", false);
	sgre::audio::OfflineBufferedSoundPool engine(sound, PoolDescription());
	Require(sound.Load("configuration.psb"), "actual Sound configuration cache reference was not requested");
	sound.AdvancePostLoop();
	Require(sound.GetLoaded("configuration.psb") && sound.LoadArchive("alpha"), "actual Sound configuration did not create its declared archive request");
	sound.AdvancePostLoop();
	Require(sound.GetArchiveLoaded("alpha"), "actual Sound archive reference did not reach its real completion latch");
	sgre::audio::BufferedSoundCueRequest request;
	request.ArchiveName = "alpha";
	request.CueName = "tone";
	request.PlayCount = 0; // Native playVoice's omitted count; the nonlooping resource remains finite.
	sgre::audio::SoundMixParameters mix;
	mix.UseSourcePan = true;
	const auto result = sound.BufferedCues().Prepare(request, mix, 2);
	Require(result.Status == BufferedSoundCuePreparationStatus::Prepared && result.Cue.has_value(), "actual containing Sound owner did not attach decoded sources");
	std::array<float, 128> output;
	Require(engine.Render(output) == 64, "actual containing Sound graph did not render");
	RequireTwoSourceSamples(output);
	sound.AdvancePostLoop();
	Require(sound.BufferedCues().ActiveCount() == 0 && engine.SourceNodeCount() == 2, "containing postloop confused logical and physical source retirement");
	sound.Close();
	Require(engine.SourceNodeCount() == 0, "actual Sound close retained nodes borrowing its destroyed decoded pool");
}

static void TestAdmissionAndForeignThread(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> definitions{CueVector{}};
	ActualCueRuntime runtime(directory, definitions);
	sgre::audio::OfflineBufferedSoundPool engine(runtime.Cues, PoolDescription());
	const auto cue = PrepareRealCue(runtime, true);
	std::array<float, 64> output;
	output.fill(9);
	bool rejected = false;
	std::thread foreign([&] {
		try {
			static_cast<void>(engine.Render(output));
		} catch (const std::runtime_error&) {
			rejected = true;
		}
	});
	foreign.join();
	Require(rejected && runtime.Cues.InspectChannel(cue, 0).State.Samples.ConsumedFrames == 0, "foreign thread reached actual source consumption");
	RequireFailure([&] {
		static_cast<void>(engine.Render(std::span(output).first(3)));
	},
	    "complete-frame extent");
	RequireFailure([&] {
		sgre::audio::OfflineBufferedSoundPool duplicate(runtime.Cues, PoolDescription());
	},
	    "fresh actual source/controller owner");
	Require(engine.SourceNodeCount() == 1 && engine.Render(output) == 32, "rejected attachment changed the original actual graph");
	RequireZero(output);
}

static void TestReciprocalPoolReattachment(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> definitions{CueVector{}};
	ActualCueRuntime runtime(directory, definitions);
	sgre::audio::OfflineBufferedSoundPool engine(runtime.Cues, PoolDescription());
	static_cast<void>(PrepareRealCue(runtime));
	runtime.Cues.Close();
	Require(engine.SourceNodeCount() == 0, "original controller close did not release its actual pool connection");
	{
		sgre::audio::BufferedSoundCues replacement(runtime.Loads);
		// The only accessible protocol path publishes both borrows; direct output hook calls are inaccessible.
		replacement.ConnectOutput(engine);
		sgre::audio::BufferedSoundCueRequest request;
		request.ArchiveName = "alpha";
		request.CueName = "tone";
		request.StartPaused = true;
		sgre::audio::SoundMixParameters mix;
		mix.UseSourcePan = true;
		const auto result = replacement.Prepare(request, mix, 2);
		Require(result.Status == BufferedSoundCuePreparationStatus::Prepared && result.Cue.has_value() && engine.SourceNodeCount() == 1, "actual reciprocal replacement owner did not prepare its real source");
	}
	Require(engine.SourceNodeCount() == 0, "replacement destruction left a callback borrowing its released decoded source owner");
	std::array<float, 64> output;
	output.fill(9);
	Require(engine.Render(output) == 32, "actual empty graph retained a released source after reciprocal teardown");
	RequireZero(output);
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: offline_buffered_sound_pool_tests owned_fixture_directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directory(directory);
		TestIdleSourceOutlivesCueAndArchive(directory);
		TestPauseReasonsAndResume(directory);
		TestStopFlushAndReuse(directory);
		TestPartialSourceFailure(directory);
		TestUnsupportedPitchRollback(directory);
		TestBothOwnerReleaseOrders(directory);
		TestActualSoundResourceController(directory);
		TestAdmissionAndForeignThread(directory);
		TestReciprocalPoolReattachment(directory);
		std::cout << "9 real dynamic offline source/controller/matrix/retirement groups passed; hardware and playVoice remain open\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
