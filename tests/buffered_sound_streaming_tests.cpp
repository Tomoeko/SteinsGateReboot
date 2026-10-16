#include "sgre/audio/buffered_sound_sources.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static std::vector<std::uint8_t> EncodedSilence() {
	std::vector<std::uint8_t> bytes(1487);
	std::size_t count = 0;
	const auto append = [&](std::uint32_t value, std::uint8_t width) {
		for (auto bit = width; bit != 0; --bit) {
			bytes[count / 8] |= static_cast<std::uint8_t>(((value >> (bit - 1)) & 1) << (7 - count % 8));
			++count;
		}
	};
	append(0, 4);
	append(6, 4);
	append(0, 12);
	append(0, 3);
	append(0, 3);
	for (std::size_t frame = 0; frame < 6; ++frame) {
		append(0, 3);
		append(1, 1);
		append(63, 7);
		for (std::size_t band = 0; band < 25; ++band) {
			append(0, 1);
		}
		append(0xf6, 9);
	}
	return bytes;
}

static sgre::audio::BufferedSoundSourceData Data(const std::vector<std::uint8_t>& encoded) {
	sgre::audio::BufferedSoundSourceData data;
	data.Pool = sgre::audio::SoundVoiceType::Buffered8;
	data.Description.Format.Encoding = sgre::resource::WaveEncoding::WmaStandard;
	data.Description.Format.Channels = 1;
	data.Description.Format.SamplesPerSecond = 44100;
	data.Description.Format.AverageBytesPerSecond = 4000;
	data.Description.Format.BlockAlignment = 1487;
	data.Description.Format.BitsPerSample = 16;
	data.Description.DecodedPacketCumulativeBytes.push_back(16384);
	data.Description.EncodedData.Size = encoded.size();
	Require(encoded.size() <= std::numeric_limits<std::uint32_t>::max(), "fixture encoded extent exceeds its explicit submission field");
	data.Submission.EncodedBytes = static_cast<std::uint32_t>(encoded.size());
	data.Submission.EndOfStream = true;
	data.EncodedBytes = encoded;
	return data;
}

static sgre::audio::SoundVoiceAddress Address() {
	sgre::audio::SoundVoiceAddress result;
	result.Type = sgre::audio::SoundVoiceType::Buffered8;
	return result;
}

static void Await(sgre::audio::BufferedSoundSources& sources, auto predicate) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (true) {
		sources.PollWmaStreams();
		if (predicate(sources.Inspect(Address()))) {
			return;
		}
		Require(std::chrono::steady_clock::now() < deadline, "buffered WMA integration exceeded its bounded source observation");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

class SourceOwner {
public:
	explicit SourceOwner(sgre::audio::BufferedSoundSourceLimits limits = {}) : Sources(limits) {
	}
	~SourceOwner() noexcept {
		try {
			Sources.RequestStop(Address());
			Sources.Commit();
			Await(Sources, [](const auto& info) {
				return !info.Samples.CallbackActive;
			});
			Sources.Retire(Address());
			Sources.Close();
		} catch (...) {
			std::terminate();
		}
	}
	sgre::audio::BufferedSoundSources Sources;
};

static void TestActualSlotAndTypedConsumption() {
	const auto encoded = EncodedSilence();
	const auto data = Data(encoded);
	sgre::audio::BufferedSoundSourceLimits limits;
	limits.Streaming.BufferFrames = 257;
	limits.Streaming.PrefillFrames = 128;
	SourceOwner owner(limits);
	auto& sources = owner.Sources;
	sources.PrepareWmaStreaming(Address(), data, 31);
	Require(sources.DecodedBytes() == 257 * sizeof(float), "actual source slot charged a whole-track PCM buffer");
	Require(sources.Inspect(Address()).Streaming.has_value() && !sources.Inspect(Address()).NativeBufferContextPresent, "real streamed source identity replaced native null context");
	sources.Commit();
	sources.RequestPause(Address());
	sources.Commit();
	Await(sources, [](const auto& info) {
		return info.Streaming->Prefilled;
	});
	std::array<float, 101> output;
	output.fill(333.0f);
	Require(sources.ConsumeWmaStreamFrames(Address(), output).Status == sgre::audio::WmaStreamReadStatus::Paused && sources.Inspect(Address()).Samples.ConsumedFrames == 0, "committed pause advanced real streamed samples");
	bool untyped_rejected = false;
	try {
		static_cast<void>(sources.ConsumeFrames(Address(), output));
	} catch (const std::runtime_error&) {
		untyped_rejected = true;
	}
	Require(untyped_rejected, "untyped source consumption silently admitted a potentially starved stream");
	sources.RequestResume(Address());
	sources.Commit();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	std::size_t consumed = 0;
	while (consumed < 8192) {
		output.fill(333.0f);
		const auto result = sources.ConsumeWmaStreamFrames(Address(), output);
		if (result.Status == sgre::audio::WmaStreamReadStatus::Samples) {
			Require(result.Frames <= 8192 - consumed, "actual streamed slot exceeded its exact decoder ledger");
			for (std::size_t index = 0; index < output.size(); ++index) {
				Require(output[index] == (index < result.Frames ? 0.0f : 333.0f), "real WMA silence or untouched consumer tail differs");
			}
			consumed += result.Frames;
		} else {
			Require(result.Status == sgre::audio::WmaStreamReadStatus::Starved, "actual streamed slot returned an unexpected non-sample state");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		Require(std::chrono::steady_clock::now() < deadline, "actual streamed slot consumption timed out");
	}
	Await(sources, [](const auto& info) {
		return !info.Samples.CallbackActive;
	});
	const auto info = sources.Inspect(Address());
	Require(info.Streaming->WorkerQuiescent && info.Streaming->InputComplete && info.Samples.CursorFrame == 8192 && info.Samples.ConsumedFrames == 8192 && !info.RecoveredSchedule.Active && !info.NativeBufferContextPresent, "actual stream drain/schedule/worker/source identities differ");
}

static void TestNativeDrainObservationAndAcknowledgedStop() {
	const auto encoded = EncodedSilence();
	auto data = Data(encoded);
	data.Submission.LoopRepeats = 255;
	SourceOwner owner;
	auto& sources = owner.Sources;
	sources.PrepareWmaStreaming(Address(), data, 31);
	sources.Commit();
	Await(sources, [](const auto& info) {
		return info.Streaming->Prefilled;
	});
	bool early_reuse_rejected = false;
	try {
		sources.Retire(Address());
	} catch (const std::runtime_error&) {
		early_reuse_rejected = true;
	}
	Require(early_reuse_rejected && sources.Inspect(Address()).Samples.CallbackActive, "actual live worker/source was retired before stop/quiescence");
	sources.RequestStop(Address());
	sources.Commit();
	sources.Commit();
	Require(!sources.Inspect(Address()).RecoveredSchedule.Active && !sources.Inspect(Address()).NativeBufferContextPresent, "native null-context drain quirk changed to queued-buffer identity");
	Await(sources, [](const auto& info) {
		return !info.Samples.CallbackActive;
	});
	const auto info = sources.Inspect(Address());
	Require(info.Streaming->WorkerQuiescent && info.Streaming->QueuedFrames == 0 && !info.Streaming->InputComplete && info.Samples.ConsumedFrames == 0, "stop fabricated EOF/consumption or released actual worker borrow early");
}

static void TestCompleteBufferLimitUnchanged() {
	const auto encoded = EncodedSilence();
	auto data = Data(encoded);
	// Deliberately oversized synthetic ledger exercises admission before any decoder allocation or packet work.
	data.Description.DecodedPacketCumulativeBytes.back() = 34 * 1024 * 1024;
	SourceOwner owner;
	bool rejected = false;
	try {
		owner.Sources.Prepare(Address(), data, 31);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && owner.Sources.DecodedBytes() == 0 && !owner.Sources.Inspect(Address()).Streaming, "normal complete-buffer bound changed or silently selected streaming");
}

int main() {
	try {
		TestActualSlotAndTypedConsumption();
		TestNativeDrainObservationAndAcknowledgedStop();
		TestCompleteBufferLimitUnchanged();
		std::cout << "Actual portable WMA source slots/typed consumer/acknowledged retirement fixtures passed; no output/cue/script registration\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
