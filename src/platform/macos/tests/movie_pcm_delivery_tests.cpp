#include "sgre/media/movie_pcm_delivery.hpp"

#include "pcm_output_callback.hpp"

#include "sgre/audio/buffered_sound_sources.hpp"
#include "sgre/audio/pcm_output_submission.hpp"
#include "sgre/media/movie_audio.hpp"
#include "sgre/platform/host_performance_counter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation&& operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::exception& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, "failure must preserve the actual expected first-cause domain");
		return;
	}
	throw std::runtime_error("actual invalid owner operation unexpectedly succeeded");
}

static sgre::audio::PcmOutputRingDescription RingDescription(std::size_t capacity) {
	sgre::audio::PcmOutputRingDescription result;
	result.CapacityFrames = capacity;
	result.MaximumTransferFrames = capacity;
	result.Channels = 2;
	return result;
}

static sgre::audio::OfflineSoundMasteringDescription SoundDescription() {
	sgre::audio::OfflineSoundMasteringDescription result;
	result.SamplesPerSecond = 48000;
	result.Channels = 2;
	result.MaximumFrames = 2048;
	return result;
}

class Fixture {
public:
	explicit Fixture(std::size_t capacity = 8192)
	    : Callback(RingDescription(capacity), true, 64), Producer(Callback.Ring, &*Callback.Deliveries, Callback.Enabled, Callback.ActiveCallbacks, Counter.Frequency()), Sound(Sources, SoundDescription()), Movies([this] {
		      return Now();
	      }) {
		Delivery = std::make_unique<sgre::media::MoviePcmDelivery>(sgre::media::MoviePcmSoftwareOnly::Explicit48kStereo, Movies, Sound, Producer);
		Require(!Delivery->IsClosed(), "fresh actual Movie delivery binding is live before source/output service");
	}
	~Fixture() {
		Callback.Enabled.store(false);
		try {
			Delivery->Close();
		} catch (...) {
			// An earlier fixture assertion/first render error is reported by the outer test boundary.
		}
		Delivery.reset();
		Movies.Close();
		Sound.Close();
		Sources.Close();
	}
	std::int64_t Now() const {
		sgre::audio::PcmContinuousClockTicks ticks;
		ticks.Value = Counter.ReadRawCounter();
		return sgre::audio::PcmContinuousTime100ns(ticks, Counter.Frequency());
	}
	std::unique_ptr<sgre::media::MovieAudioOutput> Voice(std::size_t block_frames, float sample = .25f, std::int64_t epoch = 0) {
		sgre::media::MoviePcmDescription description;
		description.Channels = 2;
		auto voice = Movies.Open(description);
		for (std::size_t index = 0; index != 4; ++index) {
			sgre::media::MovieAudioBlock block;
			block.Timestamp100ns = epoch + static_cast<std::int64_t>(index * block_frames * 10000000 / 48000);
			block.Samples.assign(block_frames * 2, sample);
			if (index == 3) {
				Require(!voice->TryQueue(block), "actual pending3 full-ring transition must start the source without consuming this fourth block");
			}
			Require(voice->TryQueue(block), "actual initial source block must enter its own native-shaped queue");
		}
		voice->MarkInputEnded();
		return voice;
	}
	std::span<const float> Read(std::uint32_t frames) {
		Require(static_cast<std::size_t>(frames) * 2 <= Samples.size(), "fixture callback extent must fit its actual local PCM output");
		auto samples = std::span(Samples).first(static_cast<std::size_t>(frames) * 2);
		std::fill(samples.begin(), samples.end(), -99.0f);
		AudioBufferList output{};
		output.mNumberBuffers = 1;
		output.mBuffers[0].mNumberChannels = 2;
		output.mBuffers[0].mDataByteSize = static_cast<UInt32>(samples.size_bytes());
		output.mBuffers[0].mData = samples.data();
		AudioUnitRenderActionFlags flags = 0;
		Callback.Enabled.store(true);
		Require(sgre::platform::RenderPcmDeviceOutput(&Callback, &flags, nullptr, 0, frames, &output) == noErr, "exact production callback must consume the real software output buffer");
		Require(Callback.ActiveCallbacks.load() == 0 && Callback.Failure.load() == sgre::platform::PcmDeviceCallbackFailure::None, "actual callback must retire its own active guard without first failure");
		return samples;
	}

	sgre::platform::HostPerformanceCounter Counter;
	sgre::platform::PcmDeviceCallbackState Callback;
	sgre::audio::PcmOutputSubmissionOwner Producer;
	sgre::audio::BufferedSoundSources Sources;
	sgre::audio::OfflineBufferedSoundPool Sound;
	sgre::media::MovieSilentAudioRegistry Movies;
	std::unique_ptr<sgre::media::MoviePcmDelivery> Delivery;
	std::array<float, 8192 * 2> Samples{};
};

static void TestActualPrefixPartialEndAndSilence() {
	Fixture fixture;
	static_cast<void>(fixture.Delivery->RenderAndSubmit(16));
	auto voice = fixture.Voice(8);
	voice->SetVolume(.5f);
	static_cast<void>(fixture.Delivery->RenderAndSubmit(24));
	Require(fixture.Movies.RenderedFrames() == 40 && voice->Snapshot(fixture.Now()).CompletedFrames == 0 && !voice->Snapshot(fixture.Now()).Progress.Position100ns, "offline source progress must not fabricate a successful delivery clock");
	const auto prefix = fixture.Read(16);
	Require(std::all_of(prefix.begin(), prefix.end(), [](float value) {
		return value == 0;
	}),
	    "pre-voice master frames remain actual silent mixer output");
	Require(fixture.Delivery->Poll() == 1 && !voice->Snapshot(fixture.Now()).Progress.Position100ns, "unrelated prefix delivery cannot start this Movie clock");
	const auto first = fixture.Read(1);
	Require(first[0] == .125f && first[1] == .125f, "actual source volume and planar mixer readout must reach unchanged callback samples");
	static_cast<void>(fixture.Delivery->Poll());
	Require(voice->Snapshot(fixture.Now()).CompletedFrames == 0 && voice->Snapshot(fixture.Now()).Progress.Position100ns.has_value(), "real first copied source frame starts the block clock without inventing block completion");
	static_cast<void>(fixture.Read(7));
	static_cast<void>(fixture.Delivery->Poll());
	Require(voice->Snapshot(fixture.Now()).CompletedFrames == 8 && !voice->Snapshot(fixture.Now()).OutputDrained, "actual block end at the callback boundary is inclusive while the next start remains pending");
	static_cast<void>(fixture.Delivery->RenderAndSubmit(8));
	static_cast<void>(fixture.Read(32));
	static_cast<void>(fixture.Delivery->Poll());
	Require(voice->Snapshot(fixture.Now()).CompletedFrames == 32 && voice->Snapshot(fixture.Now()).OutputDrained && fixture.Callback.SilentFrames.load() == 8, "receipt crossing submissions must complete all real blocks and exclude silent underflow frames");
	static_cast<void>(fixture.Read(4));
	static_cast<void>(fixture.Delivery->Poll());
	Require(voice->Snapshot(fixture.Now()).CompletedFrames == 32 && fixture.Callback.SilentFrames.load() == 12 && fixture.Delivery->PendingBlockEvents() == 0, "later physical silence cannot invent source progress, new block events or EOF");
	voice->Close();
}

static void TestClosedSourceGenerationAndDifferentCallbackChunks() {
	Fixture fixture;
	auto old = fixture.Voice(1, .25f);
	static_cast<void>(fixture.Delivery->RenderAndSubmit(4));
	old->Close();
	old.reset();
	auto current = fixture.Voice(2, .5f, 10000000);
	static_cast<void>(fixture.Delivery->RenderAndSubmit(8));
	static_cast<void>(fixture.Read(5));
	static_cast<void>(fixture.Delivery->Poll());
	Require(current->Snapshot(fixture.Now()).CompletedFrames == 0 && current->Snapshot(fixture.Now()).Progress.Position100ns.value_or(0) >= 10000000, "closed-source receipts must not become the replacement source's completed frames or sample epoch");
	static_cast<void>(fixture.Read(7));
	static_cast<void>(fixture.Delivery->Poll());
	Require(current->Snapshot(fixture.Now()).CompletedFrames == 8 && current->Snapshot(fixture.Now()).OutputDrained, "new exact source identity survives callback chunks crossing its start/end boundaries");
	current->Close();
}

static void TestReciprocalBypassAndRealQuiescenceGuards() {
	Fixture fixture;
	std::array<float, 4> samples{};
	RequireFailure([&] {
		static_cast<void>(fixture.Producer.Submit(samples));
	},
	    "exclusively bound");
	RequireFailure([&] {
		static_cast<void>(fixture.Producer.ReadDelivery());
	},
	    "exclusively bound");
	RequireFailure([&] {
		fixture.Producer.FinishInput();
	},
	    "exclusively bound");
	RequireFailure([&] {
		fixture.Producer.Close();
	},
	    "actual Movie delivery binding");
	RequireFailure([&] {
		fixture.Movies.RenderPcm(samples);
	},
	    "containing delivery/submit phase");
	RequireFailure([&] {
		fixture.Movies.RenderSilent(2);
	},
	    "containing delivery/submit phase");
	RequireFailure([&] {
		sgre::media::MoviePcmDelivery duplicate(sgre::media::MoviePcmSoftwareOnly::Explicit48kStereo, fixture.Movies, fixture.Sound, fixture.Producer);
	},
	    "fresh unbound");
	RequireFailure([&] {
		static_cast<void>(fixture.Delivery->RenderAndSubmit(0));
	},
	    "nonempty actual bounded");
	Require(fixture.Sound.RenderedFrames() == 0 && fixture.Movies.RenderedFrames() == 0, "correctable extent and duplicate-owner failures must precede actual source advancement");
	static_cast<void>(fixture.Delivery->RenderAndSubmit(2));
	static_cast<void>(fixture.Read(2));
	static_cast<void>(fixture.Delivery->Poll());
	RequireFailure([&] {
		fixture.Delivery->Close();
	},
	    "disabled idle callback");
	Require(!fixture.Delivery->IsClosed(), "failed quiescence admission must keep the actual output binding live");
	fixture.Callback.Enabled.store(false);
	fixture.Delivery->Close();
	Require(fixture.Delivery->IsClosed(), "successful actual sink retirement releases the binding before reporting closed");
	fixture.Delivery->Close();
	RequireFailure([&] {
		static_cast<void>(fixture.Delivery->Poll());
	},
	    "live idle serialized");
	RequireFailure([&] {
		fixture.Movies.RenderSilent(2);
	},
	    "containing delivery/submit phase");
}

static void TestPartialProgressFailureIsNeverRetried() {
	Fixture fixture(8);
	auto voice = fixture.Voice(2);
	static_cast<void>(fixture.Delivery->RenderAndSubmit(8));
	Require(fixture.Producer.SubmittedFrames() == 8, "actual first mix must fill its finite software ring");
	RequireFailure([&] {
		static_cast<void>(fixture.Delivery->RenderAndSubmit(8));
	},
	    "actual PCM output ring rejected");
	Require(fixture.Sound.RenderedFrames() == 16 && fixture.Movies.RenderedFrames() == 16 && fixture.Producer.SubmittedFrames() == 8, "failed ring write preserves real source advancement and only the earlier successful interval");
	RequireFailure([&] {
		static_cast<void>(fixture.Delivery->RenderAndSubmit(1));
	},
	    "actual PCM output ring rejected");
	RequireFailure([&] {
		static_cast<void>(fixture.Delivery->Poll());
	},
	    "actual PCM output ring rejected");
	RequireFailure([&] {
		fixture.Delivery->Close();
	},
	    "actual PCM output ring rejected");
	Require(fixture.Delivery->IsClosed(), "retained first render failure is rethrown only after actual sink retirement has closed this owner");
	Require(fixture.Sound.RenderedFrames() == 16 && fixture.Movies.RenderedFrames() == 16, "failure/cleanup must not replay the consumed source interval");
	fixture.Delivery->Close();
	voice->Close();
}

static void TestUnboundHeadlessClockRemainsProducerBased() {
	const std::int64_t clock = 123000000;
	sgre::media::MovieSilentAudioRegistry movies([&] {
		return clock;
	});
	sgre::media::MoviePcmDescription description;
	description.Channels = 2;
	auto voice = movies.Open(description);
	for (std::size_t index = 0; index != 4; ++index) {
		sgre::media::MovieAudioBlock block;
		block.Timestamp100ns = 3000000 + static_cast<std::int64_t>(index * 8 * 10000000 / 48000);
		block.Samples.assign(16, .25f);
		if (index == 3) {
			Require(!voice->TryQueue(block), "unbound legacy start still requires its actual full pending ring");
		}
		Require(voice->TryQueue(block), "unbound legacy assembly retains its existing real block ownership");
	}
	std::array<float, 32> output{};
	movies.RenderPcm(output);
	const auto snapshot = voice->Snapshot(clock + 17);
	Require(snapshot.CompletedFrames == 16 && snapshot.Progress.Position100ns == 3000000 + 3333 + 17 && !snapshot.OutputDrained, "explicitly unbound headless output must retain its exact existing completed-render/QPC projection");
	Require(output[0] == .25f && output[31] == .25f, "unbound PCM/gain/mixer path is unchanged");
	voice->Close();
	voice.reset();
	movies.Close();
}

int main() {
	try {
		TestActualPrefixPartialEndAndSilence();
		TestClosedSourceGenerationAndDifferentCallbackChunks();
		TestReciprocalBypassAndRealQuiescenceGuards();
		TestPartialProgressFailureIsNeverRetried();
		TestUnboundHeadlessClockRemainsProducerBased();
		std::cout << "actual Movie/Sound manual mixers plus production software callback delivery:5 groups passed; no explicit hardware-output owner construction/start\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
