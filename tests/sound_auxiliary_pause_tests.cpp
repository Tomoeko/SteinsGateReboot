#include "sgre/audio/sound_resource_control.hpp"

#include "auxiliary_sound_fixture.hpp"
#include "sgre/core/application_clock_pump.hpp"
#include "sgre/core/looper_frame.hpp"

#include <array>
#include <cstdint>
#include <thread>

class ActualSoundControlFixture {
public:
	ActualSoundControlFixture(const std::filesystem::path& directory) : Runtime(directory, Vectors), Control(Runtime.Cache, Runtime.Mounts, Runtime.Processor, "win", "synthetic-seed", false) {
		Require(Control.Load("configuration.psb"), "actual control must acquire real worker-produced configuration");
		for (std::size_t visit = 0; visit < 8 && !Control.GetLoaded("configuration.psb"); ++visit) {
			Control.AdvancePostLoop();
		}
		Require(Control.GetLoaded("configuration.psb") && Control.LoadArchive("common"), "actual control archive must derive from actual configuration declaration");
		for (std::size_t visit = 0; visit < 8 && !Control.GetArchiveLoaded("common"); ++visit) {
			Control.AdvancePostLoop();
		}
		Require(Control.GetArchiveLoaded("common"), "actual control must receive actual retained archive");
	}

	~ActualSoundControlFixture() noexcept {
		try {
			Control.Close();
		} catch (...) {
			std::terminate();
		}
	}

	[[nodiscard]] sgre::audio::BufferedSoundCueId Prepare(bool voice_paused = false) {
		sgre::audio::BufferedSoundCueRequest request;
		request.ArchiveName = "common";
		request.CueName = "cue";
		request.StartPaused = voice_paused;
		sgre::audio::SoundMixParameters parameters;
		parameters.UseSourcePan = true;
		const auto prepared = Control.BufferedCues().Prepare(request, parameters, 2);
		Require(prepared.Cue && prepared.Status == sgre::audio::BufferedSoundCuePreparationStatus::Prepared, "actual auxiliary fixture requires real decoded PCM source");

		return *prepared.Cue;
	}

	std::array<CueVector, 1> Vectors{};
	ActualCueRuntime Runtime;
	sgre::audio::SoundResourceControl Control;
};

static void RequirePausedSamples(sgre::audio::SoundResourceControl& control, sgre::audio::BufferedSoundCueId cue, std::uint32_t mask, std::size_t remaining) {
	auto& cues = control.BufferedCues();
	const auto info = cues.Inspect(cue);
	const auto state = cues.InspectChannel(cue, 0).State;
	Require(info.Paused && info.PauseReasons == mask && state.Samples.CallbackActive && !state.Samples.Running && state.Samples.RemainingBufferFrames == remaining && !state.NativeBufferContextPresent, "real pause mask must preserve actual callback, remaining frames and null native context");
	std::array<float, 1> untouched{9};
	Require(cues.ConsumeChannelFrames(cue, 0, untouched) == 0 && untouched[0] == 9, "paused decoded source must consume no samples or overwrite destination");
}

static void TestIndependentVoiceAndAllReasons(const std::filesystem::path& directory) {
	ActualSoundControlFixture fixture(directory);
	const auto cue = fixture.Prepare();
	auto& cues = fixture.Control.BufferedCues();
	std::array<float, 1> sample{};
	Require(cues.ConsumeChannelFrames(cue, 0, sample) == 1 && sample[0] == 0.25f, "first real source sample differs");
	Require(fixture.Control.PauseAll(), "global pause must claim actual state");
	RequirePausedSamples(fixture.Control, cue, 2, 3);
	Require(cues.Pause(cue), "voice reason must be added while global reason already pauses source");
	Require(fixture.Control.ResumeAll(), "global resume must clear only actual global reason");
	RequirePausedSamples(fixture.Control, cue, 1, 3);
	Require(cues.Resume(cue), "voice resume must clear its actual reason");
	fixture.Control.AdvancePostLoop();
	Require(cues.ConsumeChannelFrames(cue, 0, sample) == 1 && sample[0] == 0.5f && cues.Inspect(cue).PauseReasons == 0, "last reason release must restart real remaining source without rewinding");
}

static void TestInheritedGlobalAndAuxiliaryOwnership(const std::filesystem::path& directory) {
	ActualSoundControlFixture fixture(directory);
	Require(fixture.Control.PauseAll() && !fixture.Control.PauseAll(), "repeated global pause must retain one actual flag");
	const auto cue = fixture.Prepare();
	RequirePausedSamples(fixture.Control, cue, 2, 4);
	fixture.Control.InvokeAuxiliaryPause(true);
	fixture.Control.InvokeAuxiliaryPause(true);
	Require(!fixture.Control.AuxiliaryOwnsPause(), "auxiliary may not take ownership of an earlier manual global pause");
	fixture.Control.InvokeAuxiliaryPause(false);
	RequirePausedSamples(fixture.Control, cue, 2, 4);
	Require(fixture.Control.ResumeAll() && !fixture.Control.ResumeAll(), "explicit global resume must restore its exact flag");
	std::array<float, 4> samples{};
	Require(fixture.Control.BufferedCues().ConsumeChannelFrames(cue, 0, samples) == 4 && samples == std::array<float, 4>{0.25f, 0.5f, -0.5f, 0}, "inherited pause must retain genuine source bytes until real resume");
}

static void TestOwnedPauseAndExternalResume(const std::filesystem::path& directory) {
	ActualSoundControlFixture fixture(directory);
	const auto cue = fixture.Prepare(true);
	fixture.Control.InvokeAuxiliaryPause(true);
	Require(fixture.Control.AuxiliaryOwnsPause() && fixture.Control.GetAllPaused(), "auxiliary must own its own global transition");
	RequirePausedSamples(fixture.Control, cue, 3, 4);
	fixture.Control.InvokeAuxiliaryPause(true);
	fixture.Control.InvokeAuxiliaryPause(false);
	Require(!fixture.Control.AuxiliaryOwnsPause() && !fixture.Control.GetAllPaused(), "auxiliary must release only its own global transition");
	RequirePausedSamples(fixture.Control, cue, 1, 4);
	fixture.Control.InvokeAuxiliaryPause(true);
	Require(fixture.Control.ResumeAll() && fixture.Control.AuxiliaryOwnsPause(), "external global resume must not rewrite auxiliary ownership latch");
	fixture.Control.InvokeAuxiliaryPause(false);
	Require(!fixture.Control.AuxiliaryOwnsPause(), "owned resume must clear latch even when external code already resumed all");
	RequirePausedSamples(fixture.Control, cue, 1, 4);
}

class AuxiliaryFixturePlatform final : public sgre::core::ApplicationPumpPlatform {
public:
	AuxiliaryFixturePlatform(sgre::core::ApplicationEventControl& events, sgre::audio::SoundResourceControl& sound, sgre::audio::BufferedSoundCueId cue) : Events(events), Sound(sound), Cue(cue) {
	}

	void RequireLive() const override {
		Require(std::this_thread::get_id() == OwnerThread, "synthetic platform fixture requires its actual owning thread");
		Sound.RequireAuxiliaryReceiver();
	}
	std::int64_t Frequency() const override {
		return 1000;
	}
	std::int64_t ReadRawCounter() override {
		Counter += 17;

		return Counter;
	}
	void DispatchEvents(sgre::core::ApplicationEventControl& events, sgre::core::WindowFramePacing&) override {
		Require(&events == &Events, "synthetic platform fixture must dispatch its actual event owner");
		++Dispatches;
	}
	void PrecisionWaitOneMillisecond() override {
		++Counter;
	}
	void SuspendedWaitSixteenMilliseconds() override {
		++SuspendedWaits;
		Require(Sound.AuxiliaryOwnsPause() && Sound.GetAllPaused(), "actual pump suspended phase must reach the real Sound receiver");
		RequirePausedSamples(Sound, Cue, 2, 4);
		Events.RequestResume();
	}
	bool HasBoundPresentationTarget() const override {
		return false;
	}
	void Present(bool) override {
		throw std::runtime_error("silent CPU fixture has no presentation target");
	}

	sgre::core::ApplicationEventControl& Events;
	sgre::audio::SoundResourceControl& Sound;
	sgre::audio::BufferedSoundCueId Cue;
	std::int64_t Counter = 900;
	std::uint32_t SuspendedWaits = 0;
	std::uint32_t Dispatches = 0;
	const std::thread::id OwnerThread = std::this_thread::get_id();
};

static void TestContainingActualPumpJoin(const std::filesystem::path& directory) {
	ActualSoundControlFixture fixture(directory);
	const auto cue = fixture.Prepare();
	sgre::core::ApplicationEventControl events;
	AuxiliaryFixturePlatform platform(events, fixture.Control, cue);
	sgre::core::ApplicationClockPump pump(events, platform);
	pump.SetAuxiliary([&](bool suspended) {
		fixture.Control.InvokeAuxiliaryPause(suspended);
	},
	    [&]() {
		    fixture.Control.RequireAuxiliaryReceiver();
	    });
	sgre::core::ThreadTaskFrame workers([&]() {
		return static_cast<std::uint64_t>(platform.ReadRawCounter());
	},
	    1, 4);
	std::uint32_t updates = 0;
	std::uint32_t draws = 0;
	std::uint32_t releases = 0;
	auto payload = std::make_unique<std::uint32_t>(0);
	sgre::scene::SerialTaskCallbacks callbacks;
	callbacks.Update = [&](auto& task) {
		Require(payload != nullptr, "actual root payload must enclose callback update");
		++*payload;
		++updates;
		task.SetStatus(sgre::scene::TaskStatus::Complete);
	};
	callbacks.Draw = [&](auto&, std::uint32_t) {
		Require(payload != nullptr, "actual root payload must enclose callback draw");
		++draws;
	};
	callbacks.Release = [&]() {
		Require(payload != nullptr, "actual root payload must retire once");
		payload.reset();
		++releases;
	};
	callbacks.CanRelease = [&]() {
		return payload != nullptr;
	};
	sgre::scene::SerialTask root(std::move(callbacks));
	sgre::core::LooperFrame looper(workers, root, pump, 4);
	try {
		events.RequestSuspend();
		events.Flush();
		static_cast<void>(looper.RunIteration());
		Require(platform.SuspendedWaits == 1 && platform.Dispatches >= 2 && updates == 1 && draws == 1 && !fixture.Control.GetAllPaused() && !fixture.Control.AuxiliaryOwnsPause(), "actual typed pump suspend/resume must join real Sound ownership without extra phases");
		std::array<float, 4> samples{};
		Require(fixture.Control.BufferedCues().ConsumeChannelFrames(cue, 0, samples) == 4 && samples == std::array<float, 4>{0.25f, 0.5f, -0.5f, 0}, "actual pump resume must preserve every real decoded frame");
	} catch (...) {
		looper.ReleaseTask();
		looper.Close();
		pump.ClearAuxiliary();
		pump.Close();
		workers.Close();
		events.Close();
		throw;
	}
	looper.ReleaseTask();
	looper.Close();
	Require(releases == 1, "actual root must retire once");
	pump.ClearAuxiliary();
	pump.Close();
	workers.Close();
	events.Close();
}

static void TestStopAndReceiverRetirement(const std::filesystem::path& directory) {
	ActualSoundControlFixture fixture(directory);
	const auto cue = fixture.Prepare();
	const auto source = fixture.Control.BufferedCues().InspectChannel(cue, 0).Source;
	fixture.Control.InvokeAuxiliaryPause(true);
	Require(fixture.Control.BufferedCues().Stop(cue), "actual paused cue must leave ID lookup before retirement");
	fixture.Control.AdvancePostLoop();
	Require(!fixture.Control.BufferedCues().InspectSource(source).Samples.CallbackActive && fixture.Control.BufferedCues().RetiringCount() == 0, "actual stopped-buffer flush must finish before cue/resource retirement");
	fixture.Control.Close();
	RequireFailure([&]() {
		fixture.Control.RequireAuxiliaryReceiver();
	},
	    "actual live serialized receiving owner");
	RequireFailure([&]() {
		fixture.Control.InvokeAuxiliaryPause(false);
	},
	    "actual live serialized receiving owner");
}

static void TestOriginalAuxiliaryControlCorpus(const std::filesystem::path& directory, const std::filesystem::path& corpus_path) {
	std::ifstream corpus(corpus_path, std::ios::binary);
	Require(corpus.is_open(), "original auxiliary control corpus is required");
	const auto read_byte = [&]() {
		const auto value = corpus.get();
		Require(value != std::char_traits<char>::eof(), "original auxiliary corpus is truncated");

		return static_cast<std::uint8_t>(value);
	};
	for (const auto expected : std::array<std::uint8_t, 8>{'A', 'U', 'X', '1', 32, 0, 0, 0}) {
		Require(read_byte() == expected, "original auxiliary corpus header differs");
	}
	std::uint32_t snapshots = 0;
	for (std::uint32_t record = 0; record < 32; ++record) {
		const auto voice = read_byte();
		const auto manual = read_byte();
		const auto steps = read_byte();
		Require(voice <= 1 && manual <= 1 && steps >= 1 && steps <= 8, "original auxiliary corpus record is outside bounded domain");
		ActualSoundControlFixture fixture(directory);
		if (manual != 0) {
			Require(fixture.Control.PauseAll(), "corpus manual pause must enter actual owner");
		}
		const auto cue = fixture.Prepare(voice != 0);
		for (std::uint8_t step = 0; step < steps; ++step) {
			const auto operation = read_byte();
			const auto mask = read_byte();
			const auto all = read_byte();
			const auto auxiliary = read_byte();
			Require(mask <= 3 && all <= 1 && auxiliary <= 1, "original auxiliary corpus output is outside bounded domain");
			switch (operation) {
			case 0:
				static_cast<void>(fixture.Control.PauseAll());
				break;
			case 1:
				static_cast<void>(fixture.Control.ResumeAll());
				break;
			case 2:
				fixture.Control.InvokeAuxiliaryPause(true);
				break;
			case 3:
				fixture.Control.InvokeAuxiliaryPause(false);
				break;
			default:
				throw std::runtime_error("original auxiliary corpus operation is unsupported");
			}
			const auto info = fixture.Control.BufferedCues().Inspect(cue);
			const auto source = fixture.Control.BufferedCues().InspectChannel(cue, 0).State;
			Require(info.PauseReasons == mask && fixture.Control.GetAllPaused() == (all != 0) && fixture.Control.AuxiliaryOwnsPause() == (auxiliary != 0), "actual owner differs from original auxiliary/global mask and ownership snapshot");
			Require(source.Samples.Running == (mask == 0) && source.Samples.CallbackActive && source.Samples.RemainingBufferFrames == 4 && !source.NativeBufferContextPresent, "actual decoded source readiness or retained remaining frames differs from pause result");
			++snapshots;
		}
	}
	Require(snapshots == 148 && corpus.get() == std::char_traits<char>::eof(), "original auxiliary corpus count or exact EOF differs");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 3, "auxiliary pause tests require an owned fixture directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestIndependentVoiceAndAllReasons(directory);
		TestInheritedGlobalAndAuxiliaryOwnership(directory);
		TestOwnedPauseAndExternalResume(directory);
		TestContainingActualPumpJoin(directory);
		TestStopAndReceiverRetirement(directory);
		TestOriginalAuxiliaryControlCorpus(directory, std::filesystem::path(argv[2]));
		std::cout << "six groups; 32 original auxiliary cases/148 snapshots; five actual decoded-source/global/auxiliary/pump ownership groups passed; synthetic platform fixture, no device/GPU/audio\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
