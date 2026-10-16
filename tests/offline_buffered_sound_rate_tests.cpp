#include "sgre/audio/offline_buffered_sound_pool.hpp"

#include "offline_sound_rate_fixture.hpp"

#include <cmath>
#include <limits>
#include <numeric>

static constexpr std::uint32_t kMasterRate = 48000;
static constexpr std::size_t kSourceFrames = 8192;
static constexpr std::size_t kFirstImpulse = 1024;
static constexpr std::size_t kSecondImpulse = 6144;

static sgre::audio::OfflineSoundMasteringDescription RateDescription() {
	sgre::audio::OfflineSoundMasteringDescription result;
	result.SamplesPerSecond = kMasterRate;
	result.Channels = 2;
	result.MaximumFrames = 1024;

	return result;
}

static CueVector Impulses(std::uint32_t rate, std::string_view name = "tone") {
	CueVector result;
	result.Name = name;
	// Native nosrc is still present, but3921a0 only disables conversion when the original rate is48000.
	result.SamplesPerSecond = rate;
	result.PcmSamples.assign(kSourceFrames, 0);
	result.PcmSamples[kFirstImpulse] = 16384;
	result.PcmSamples[kSecondImpulse] = -16384;

	return result;
}

static sgre::audio::BufferedSoundCueId Prepare(ActualCueRuntime& runtime, std::string_view name, bool paused = false) {
	const auto result = runtime.Prepare(name, 0, paused);
	Require(result.Status == BufferedSoundCuePreparationStatus::Prepared && result.Cue.has_value(), "actual differing-rate source failed to attach to the offline graph");

	return *result.Cue;
}

static std::vector<float> RenderSource(sgre::audio::OfflineBufferedSoundPool& engine, std::uint32_t source_rate, std::size_t slice) {
	// Include system SRC tail without changing the source's finite decoded buffer or logical sample count.
	const auto frames = (kSourceFrames * kMasterRate + source_rate - 1) / source_rate + 2048;
	std::vector<float> output(frames * 2, std::numeric_limits<float>::quiet_NaN());
	for (std::size_t offset = 0; offset < frames;) {
		const auto count = std::min(slice, frames - offset);
		Require(engine.Render(std::span(output).subspan(offset * 2, count * 2)) == count, "offline rate conversion changed the requested mastering frame count");
		offset += count;
	}
	Require(std::all_of(output.begin(), output.end(), [](float value) {
		return std::isfinite(value);
	}),
	    "offline rate conversion left output unwritten or nonfinite");

	return output;
}

struct ImpulseObservation {
	std::size_t FirstPeak = 0;
	std::size_t SecondPeak = 0;
};

static ImpulseObservation RequireImpulseTiming(std::span<const float> output, std::uint32_t source_rate) {
	const auto frames = output.size() / 2;
	const auto midpoint = ((kFirstImpulse + kSecondImpulse) * kMasterRate) / (static_cast<std::size_t>(source_rate) * 2);
	ImpulseObservation result;
	for (std::size_t frame = 0; frame < frames; ++frame) {
		Require(std::abs(output[frame * 2 + 1] - output[frame * 2] * 3) < 0.000001f, "source-rate conversion changed the independently specified pan/matrix channel ratio");
		if (frame < midpoint && output[frame * 2] > output[result.FirstPeak * 2]) {
			result.FirstPeak = frame;
		}
		if (frame >= midpoint && output[frame * 2] < output[result.SecondPeak * 2]) {
			result.SecondPeak = frame;
		}
	}
	Require(output[result.FirstPeak * 2] > 0 && output[result.SecondPeak * 2] < 0, "signed independent impulses disappeared during actual source conversion");
	const auto first_expected = static_cast<double>(kFirstImpulse) * kMasterRate / source_rate;
	const auto spacing_expected = static_cast<double>(kSecondImpulse - kFirstImpulse) * kMasterRate / source_rate;
	Require(std::abs(static_cast<double>(result.FirstPeak) - first_expected) <= 64, "source impulse was not placed on its actual source-rate timeline");
	Require(std::abs(static_cast<double>(result.SecondPeak - result.FirstPeak) - spacing_expected) <= 2, "source impulse spacing did not follow the independent rational rate ratio");
	const auto expected_area = 0.0625 * kMasterRate / source_rate;
	for (const auto peak : {result.FirstPeak, result.SecondPeak}) {
		const auto begin = peak > 128 ? peak - 128 : 0;
		const auto end = std::min(frames, peak + 129);
		double area = 0;
		for (auto frame = begin; frame < end; ++frame) {
			area += output[frame * 2];
		}
		Require(std::abs(std::abs(area) - expected_area) <= expected_area * 0.03, "source-rate conversion changed independent impulse area beyond the bounded system-filter test");
	}

	return result;
}

static void TestRate(const std::filesystem::path& directory, std::uint32_t rate, std::size_t slice) {
	const std::array<CueVector, 1> definitions{Impulses(rate)};
	ActualCueRuntime runtime(directory, definitions);
	sgre::audio::OfflineBufferedSoundPool engine(runtime.Cues, RateDescription());
	const auto cue = Prepare(runtime, "tone");
	const auto address = runtime.Cues.InspectChannel(cue, 0).Source;
	const auto before = runtime.Cues.InspectSource(address).Samples;
	Require(before.SamplesPerSecond == rate && before.CursorFrame == 0 && before.BufferFrames == kSourceFrames, "attachment replaced the original format rate, cursor or decoded frame count");
	const auto output = RenderSource(engine, rate, slice);
	const auto observation = RequireImpulseTiming(output, rate);
	const auto after = runtime.Cues.InspectSource(address).Samples;
	Require(after.SamplesPerSecond == rate && after.CursorFrame == kSourceFrames && after.ConsumedFrames == kSourceFrames && !after.CallbackActive && !after.Queued, "converted source completion used mastering units instead of actual source frames");
	Require(engine.RenderedFrames() == output.size() / 2 && engine.Description().SamplesPerSecond == kMasterRate, "mastering timeline changed to the original source rate");
	runtime.Cues.CommitAndRetire();
	Require(runtime.Cues.ActiveCount() == 0 && runtime.Cues.RetiringCount() == 0 && engine.SourceNodeCount() == 1, "completed converted cue did not retain its actual idle physical slot");
	if (rate == kMasterRate) {
		Require(observation.FirstPeak == kFirstImpulse && observation.SecondPeak == kSecondImpulse && output[kFirstImpulse * 2] == 0.0625f && output[kSecondImpulse * 2] == -0.0625f, "unchanged-rate path acquired a sample offset or altered its original exact matrix result");
	}
	std::cout << "rate=" << rate << " slice=" << slice << " sourceFrames=" << after.ConsumedFrames << " masterFrames=" << engine.RenderedFrames() << " peaks=" << observation.FirstPeak << ',' << observation.SecondPeak << '\n';
}

static void TestPausedOutputRelease(const std::filesystem::path& directory) {
	const std::array<CueVector, 1> definitions{Impulses(47968)};
	ActualCueRuntime runtime(directory, definitions);
	sgre::audio::OfflineBufferedSoundPool engine(runtime.Cues, RateDescription());
	const auto cue = Prepare(runtime, "tone", true);
	std::array<float, 512> silence;
	Require(engine.Render(silence) == 256 && std::all_of(silence.begin(), silence.end(), [](float value) {
		return value == 0;
	}),
	    "initially paused converted source rendered samples");
	Require(runtime.Cues.InspectChannel(cue, 0).State.Samples.ConsumedFrames == 0, "paused converter requests consumed actual source frames");
	engine.Close();
	Require(runtime.Cues.ApplyControl(cue, sgre::audio::BufferedSoundCueControl::Resume), "real source did not survive offline output retirement");
	std::vector<float> source(kSourceFrames, 9);
	Require(runtime.Cues.ConsumeChannelFrames(cue, 0, source) == kSourceFrames && source[kFirstImpulse] == 0.5f && source[kSecondImpulse] == -0.5f, "output retirement destroyed or resampled the retained portable source buffer");
}

static void TestSourceReleaseAndSlotReplacement(const std::filesystem::path& directory) {
	const std::array<CueVector, 2> definitions{Impulses(47968), Impulses(32000, "other")};
	ActualCueRuntime runtime(directory, definitions);
	sgre::audio::OfflineBufferedSoundPool engine(runtime.Cues, RateDescription());
	const auto first = Prepare(runtime, "tone");
	const auto original_address = runtime.Cues.InspectChannel(first, 0).Source;
	RequireImpulseTiming(RenderSource(engine, 47968, 173), 47968);
	runtime.Cues.CommitAndRetire();
	const auto second = Prepare(runtime, "other");
	const auto replacement = runtime.Cues.InspectChannel(second, 0).Source;
	Require(original_address.Type == replacement.Type && original_address.Slot == replacement.Slot && second.Value != first.Value && engine.SourceNodeCount() == 1, "different-rate source replacement did not reuse its actual native-style physical slot");
	RequireImpulseTiming(RenderSource(engine, 32000, 1024), 32000);
	runtime.Cues.Close();
	Require(engine.SourceNodeCount() == 0, "source owner retirement retained a converter callback into dead slots");
	std::array<float, 256> silence;
	Require(engine.Render(silence) == 128 && std::all_of(silence.begin(), silence.end(), [](float value) {
		return value == 0;
	}),
	    "empty offline graph retained converted source data after actual source-owner close");
}

int main(int argc, char** argv) {
	try {
		Require(argc == 2, "usage: offline_buffered_sound_rate_tests owned_fixture_directory");
		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		for (const auto rate : {24000u, 32000u, 44100u, 47968u, 48000u, 96000u, 192000u}) {
			TestRate(directory, rate, 1024);
			TestRate(directory, rate, 173);
		}
		TestPausedOutputRelease(directory);
		TestSourceReleaseAndSlotReplacement(directory);
		std::cout << "14 actual source-rate impulse/matrix/timeline cases and2 reciprocal ownership/replacement groups passed; silent offline receiving only, XAudio2 filter/timing parity open\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
