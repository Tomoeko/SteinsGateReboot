#include "sgre/media/movie_gain.hpp"

#include "movie_gain_native_vectors.hpp"

#include "sgre/audio/sound_resource_control.hpp"
#include "sgre/core/background_calls.hpp"
#include "sgre/io/file_read_processor.hpp"
#include "sgre/media/movie_audio.hpp"
#include "sgre/resource/archive_mounts.hpp"
#include "sgre/resource/resource_cache.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Work>
static void RequireFailure(Work&& work, const char* message) {
	bool failed = false;
	try {
		std::forward<Work>(work)();
	} catch (const std::exception&) {
		failed = true;
	}
	Require(failed, message);
}

static void RequireWord(float actual, std::uint32_t expected, const std::string& context) {
	const auto actual_word = std::bit_cast<std::uint32_t>(actual);
	if (actual_word != expected) {
		std::ostringstream message;
		message << context << ": expected0x" << std::hex << expected << ", actual0x" << actual_word;
		throw std::runtime_error(message.str());
	}
}

static std::uint32_t NativeGain(float movie, float master_linear, bool loudness) {
	const auto movie_word = std::bit_cast<std::uint32_t>(movie);
	const auto master_word = std::bit_cast<std::uint32_t>(master_linear);
	for (const auto& row : kNativeMovieGainCases) {
		if (row.MovieWord == movie_word && row.MasterLinearWord == master_word && row.LoudnessEnabled == loudness && !row.MasterBypassed) {
			return row.GainWord;
		}
	}

	throw std::runtime_error("connected movie gain case is absent from its independent original-instruction oracle");
}

static void TestWholeNativeSetterGain() {
	for (std::size_t index = 0; index < kNativeMovieGainCases.size(); ++index) {
		const auto& row = kNativeMovieGainCases[index];
		sgre::media::MovieSoundVolumeState state;
		state.MasterLinear = std::bit_cast<float>(row.MasterLinearWord);
		state.LoudnessEnabled = row.LoudnessEnabled;
		state.MasterBypassed = row.MasterBypassed;
		const auto actual = sgre::media::CalculateMovieSourceGain(std::bit_cast<float>(row.MovieWord), state);
		RequireWord(actual, row.GainWord, "whole original3903b0 source gain case" + std::to_string(index));
	}
}

static sgre::media::MovieAudioBlock PcmBlock(std::int64_t timestamp100ns) {
	sgre::media::MovieAudioBlock block;
	block.Timestamp100ns = timestamp100ns;
	block.Samples.assign(960 * 2, 0.25f);

	return block;
}

static void TestActualSoundSamplingAndRetainedSource() {
	sgre::resource::ResourceCache cache(1024 * 1024);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::io::FileReadProcessor processor(cache, mounts, background);
	sgre::audio::SoundResourceControl sound(cache, mounts, processor, "fixture-spec", "fixture-seed", false);
	Require(sound.LoudnessVolumeEnabled(), "actual Sound constructor must retain native default loudness policy");
	sound.SetMasterVolume(0.125f);
	std::int64_t clock100ns = 100000000;
	sgre::media::MovieSilentAudioRegistry registry([&] {
		return clock100ns;
	},
	    sound);
	sgre::media::MoviePcmDescription description;
	description.Channels = 2;
	auto output = registry.Open(description);
	RequireWord(output->Snapshot(clock100ns).AppliedGain, NativeGain(1, 0.125f, true), "Open samples the actual master through its native inverse conversion");
	output->SetVolume(0.5f);
	RequireWord(output->Snapshot(clock100ns).AppliedGain, NativeGain(0.5f, 0.125f, true), "SetVolume converts raw movie and current actual master separately");
	const auto previous = output->Snapshot(clock100ns).AppliedGain;
	sound.SetMasterVolume(0.5f);
	RequireWord(output->Snapshot(clock100ns).AppliedGain, std::bit_cast<std::uint32_t>(previous), "global master mutation must leave an existing movie source gain retained");
	auto later = registry.Open(description);
	RequireWord(later->Snapshot(clock100ns).AppliedGain, NativeGain(1, 0.5f, true), "a later real Open samples the newly retained master");
	later->Close();
	output->SetVolume(0.5f);
	RequireWord(output->Snapshot(clock100ns).AppliedGain, NativeGain(0.5f, 0.5f, true), "an unchanged raw movie setting still refreshes native source gain");
	const auto loud_gain = output->Snapshot(clock100ns).AppliedGain;
	sound.SetLoudnessVolumeEnabled(false);
	RequireWord(output->Snapshot(clock100ns).AppliedGain, std::bit_cast<std::uint32_t>(loud_gain), "Sound policy mutation must not synthesize a movie volume setter");
	output->SetVolume(0.5f);
	RequireWord(output->Snapshot(clock100ns).AppliedGain, NativeGain(0.5f, 0.5f, false), "explicit movie setter samples the current loudness policy");
	output->SetVolume(-0.0f);
	RequireWord(output->Snapshot(clock100ns).AppliedGain, NativeGain(-0.0f, 0.5f, false), "native source gain preserves negative zero through the actual owner");
	output->SetVolume(0.5f);
	const auto retained_gain = output->Snapshot(clock100ns).AppliedGain;
	for (std::uint32_t index = 0; index < 3; ++index) {
		auto block = PcmBlock(static_cast<std::int64_t>(index) * 200000);
		Require(output->TryQueue(block), "actual PCM owner must enter the native pending ring");
	}
	auto fourth = PcmBlock(600000);
	Require(!output->TryQueue(fourth), "actual full pending ring starts the source without consuming the fourth owner");
	sound.Close();
	clock100ns += 200000;
	registry.RenderSilent(960);
	Require(output->Snapshot(clock100ns).CompletedFrames == 960, "silent source callback must use retained gain without reading the closed Sound control");
	RequireWord(output->Snapshot(clock100ns).AppliedGain, std::bit_cast<std::uint32_t>(retained_gain), "closing Sound must not mutate the already applied voice gain");
	RequireFailure([&] {
		output->SetVolume(1);
	},
	    "a later setter must reject its actually closed Sound owner before source mutation");
	RequireWord(output->Snapshot(clock100ns).AppliedGain, std::bit_cast<std::uint32_t>(retained_gain), "failed Sound read must preserve the actual source gain");
	RequireFailure([&] {
		static_cast<void>(registry.Open(description));
	},
	    "later movie Open must not publish a source against the closed Sound owner");
	output->Close();
	registry.Close();
	RequireFailure([&] {
		sgre::media::MovieSilentAudioRegistry invalid([&] {
			return clock100ns;
		},
		    sound);
	},
	    "registry must validate its actual Sound borrow before creating an offline system graph");
	processor.Close();
	background.Close();
}

int main() {
	try {
		TestWholeNativeSetterGain();
		TestActualSoundSamplingAndRetainedSource();
		std::cout << "movie native gain:128 original words and actual Sound/source sampling lifetime passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
