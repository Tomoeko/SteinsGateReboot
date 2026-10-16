#include "sgre/audio/offline_sound_mastering.hpp"

#include "mastering_fixture.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

void TestOfflineSoundSystemBoundaryFailures();

static sgre::audio::OfflineSoundMasteringDescription Description() {
	sgre::audio::OfflineSoundMasteringDescription result;
	result.SamplesPerSecond = 48000;
	result.Channels = 2;
	result.MaximumFrames = 1024;

	return result;
}

static sgre::audio::BufferedSoundCueId Prepare(ActualCueRuntime& runtime, bool paused = false) {
	const auto result = runtime.Prepare("tone", 0, paused);
	Require(result.Status == BufferedSoundCuePreparationStatus::Prepared && result.Cue.has_value(), "mastering fixture requires actual decoded source ownership");

	return *result.Cue;
}

static void TestActualSystemMasteringAndCompletion(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> definitions{CueVector{"tone", 2}};
	ActualCueRuntime runtime(directory, definitions);
	const std::array<sgre::audio::BufferedSoundCueId, 1> cues{Prepare(runtime)};
	sgre::audio::OfflineSoundMastering engine(runtime.Cues, cues, Description());
	const auto description = engine.Description();
	Require(description.SamplesPerSecond == 48000 && description.Channels == 2 && description.MaximumFrames == 1024, "actual system manual format differs from the explicit decoded source format");
	std::array<float, 128> output;
	output.fill(9);
	Require(engine.Render(output) == 64 && engine.RenderedFrames() == 64, "actual manual system timeline did not produce exactly its requested frames");
	// Two real mono sources, each gain .5 and destination pan .25/.75; original PCM samples .25/.5/-.5/0.
	const std::array<float, 8> expected{0.0625f, 0.1875f, 0.125f, 0.375f, -0.125f, -0.375f, 0, 0};
	for (std::size_t index = 0; index < output.size(); ++index) {
		const auto wanted = index < expected.size() ? expected[index] : 0.0f;
		Require(output[index] == wanted, "actual system mastering samples differ from the independent signed PCM/matrix sum");
	}
	for (std::size_t channel = 0; channel < 2; ++channel) {
		const auto info = runtime.Cues.InspectChannel(cues[0], channel);
		const auto source = info.State.Samples;
		Require(!info.State.NativeBufferContextPresent, "actual system rendering changed the separate recovered null pContext observation");
		Require(source.ConsumedFrames == 4 && source.RemainingBufferFrames == 0 && !source.CallbackActive, "actual system pull did not consume and complete the real source callback");
	}
	Require(runtime.Cues.ActiveCount() == 1, "mastering must not replace actual cue retirement with a source/count stand-in");
	engine.Close();
	runtime.Cues.CommitAndRetire();
	Require(runtime.Cues.ActiveCount() == 0 && runtime.Cues.RetiringCount() == 0, "actual postloop must release the completed cue after platform batch close");
}

static void TestActualPauseWithoutConsumption(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> definitions{CueVector{}};
	ActualCueRuntime runtime(directory, definitions);
	const std::array<sgre::audio::BufferedSoundCueId, 1> cues{Prepare(runtime, true)};
	sgre::audio::OfflineSoundMastering engine(runtime.Cues, cues, Description());
	std::array<float, 64> output;
	output.fill(7);
	Require(engine.Render(output) == 32 && engine.RenderedFrames() == 32, "actual paused system batch must still render its requested offline timeline");
	Require(std::all_of(output.begin(), output.end(), [](float value) {
		return value == 0;
	}),
	    "actual paused sources produced nonzero system output");
	const auto source = runtime.Cues.InspectChannel(cues[0], 0).State.Samples;
	Require(source.ConsumedFrames == 0 && source.CallbackActive && source.RemainingBufferFrames == 4, "paused system pull must preserve real queued sample/callback owners");
	engine.Close();
	Require(runtime.Cues.InspectChannel(cues[0], 0).State.Samples.CallbackActive, "closing the offline graph must not fabricate source consume/flush completion");
}

static void TestInputsAndRealBorrowFailures(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> definitions{CueVector{}};
	ActualCueRuntime runtime(directory, definitions);
	const std::array<sgre::audio::BufferedSoundCueId, 1> cues{Prepare(runtime)};
	RequireFailure([&] {
		sgre::audio::OfflineSoundMastering missing(runtime.Cues, {}, Description());
	},
	    "actual bounded source batch");
	auto wrong_format = Description();
	wrong_format.SamplesPerSecond = 44100;
	RequireFailure([&] {
		sgre::audio::OfflineSoundMastering wrong(runtime.Cues, cues, wrong_format);
	},
	    "explicit unchanged rate/mix format");
	const std::array<sgre::audio::BufferedSoundCueId, 2> duplicates{cues[0], cues[0]};
	RequireFailure([&] {
		sgre::audio::OfflineSoundMastering duplicate(runtime.Cues, duplicates, Description());
	},
	    "distinct actual device0/1 source slots");
	Require(runtime.Cues.InspectChannel(cues[0], 0).State.Samples.ConsumedFrames == 0, "rejected mastering construction consumed actual source data");
	sgre::audio::OfflineSoundMastering engine(runtime.Cues, cues, Description());
	std::array<float, 128> output;
	output.fill(7);
	bool rejected = false;
	std::thread foreign([&] {
		try {
			static_cast<void>(engine.Render(output));
		} catch (const std::runtime_error&) {
			rejected = true;
		}
	});
	foreign.join();
	Require(rejected && runtime.Cues.InspectChannel(cues[0], 0).State.Samples.ConsumedFrames == 0, "foreign owner thread reached the real source/system render");
	static_cast<void>(runtime.Cues.Stop(cues[0]));
	runtime.Cues.CommitAndRetire();
	RequireFailure([&] {
		static_cast<void>(engine.Render(output));
	},
	    "no actual active ID lookup");
	Require(std::all_of(output.begin(), output.end(), [](float value) {
		return value == 7;
	}),
	    "failed actual source borrow published nominal output samples");
	RequireFailure([&] {
		static_cast<void>(engine.Render(output));
	},
	    "no actual active ID lookup");
	RequireFailure([&] {
		static_cast<void>(engine.Description());
	},
	    "no actual active ID lookup");
	RequireFailure([&] {
		static_cast<void>(engine.RenderedFrames());
	},
	    "no actual active ID lookup");
	engine.Close();
	RequireFailure([&] {
		static_cast<void>(engine.Render(output));
	},
	    "actual live serialized manual system engine");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: offline_sound_mastering_tests owned_fixture_directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directory(directory);
		TestActualSystemMasteringAndCompletion(directory);
		TestActualPauseWithoutConsumption(directory);
		TestInputsAndRealBorrowFailures(directory);
		TestOfflineSoundSystemBoundaryFailures();
		std::cout << "3 actual offline system engine/source/matrix/lifetime groups and 1 Objective-C/C++ first-failure boundary group passed; no hardware, NativeReady or playVoice\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
