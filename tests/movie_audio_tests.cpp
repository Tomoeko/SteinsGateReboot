#include "sgre/media/movie_audio.hpp"
#include "sgre/media/webm_playback.hpp"

#include "movie_audio_clock_native_vectors.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
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
		work();
	} catch (const std::exception&) {
		failed = true;
	}
	Require(failed, message);
}

static sgre::media::WebmTrack Track(std::uint16_t pre_skip = 312) {
	sgre::media::WebmTrack track;
	track.Number = 1;
	track.Type = 2;
	track.Codec = "A_OPUS";
	track.Channels = 2;
	track.SamplingFrequency = 48000;
	track.CodecPrivate = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 2, static_cast<std::uint8_t>(pre_skip), static_cast<std::uint8_t>(pre_skip >> 8), 0x80, 0xbb, 0, 0, 0, 0, 0};
	track.CodecDelayNanoseconds = static_cast<std::uint64_t>(pre_skip) * 1000000000 / 48000;

	return track;
}

static sgre::media::WebmPacket Packet(std::uint32_t ordinal) {
	sgre::media::WebmPacket packet;
	packet.TrackNumber = 1;
	packet.TimestampNanoseconds = static_cast<std::int64_t>(ordinal) * 20000000;
	packet.Frames.push_back({0xf8, 0xff, 0xfe});

	return packet;
}

static void TestActualSystemCodecDeclaredTrim() {
	for (const auto skip : {std::uint16_t{0}, std::uint16_t{120}, std::uint16_t{312}, std::uint16_t{480}}) {
		sgre::media::MovieOpusDecoder decoder(Track(skip));
		std::size_t frames = 0;
		for (std::uint32_t index = 0; index < 4; ++index) {
			const auto block = decoder.Decode(Packet(index));
			Require(block.Samples.size() == static_cast<std::size_t>(960 - (index == 0 ? skip : 0)) * 2, "system codec must consume the declared leading samples before initialization");
			Require(block.Timestamp100ns == (index == 0 ? 0 : static_cast<std::int64_t>(index) * 200000 - static_cast<std::int64_t>(skip) * 10000000 / 48000), "decoded PCM timestamp must reflect actual CodecDelay and removed leading frames");
			for (const auto sample : block.Samples) {
				Require(std::isfinite(sample), "actual Opus decode must produce finite PCM");
			}
			frames += block.Samples.size() / 2;
		}
		Require(frames == std::size_t{4} * 960 - skip, "declared trim must be applied exactly once across actual encoded packets");
		decoder.Close();
		RequireFailure([&] {
			static_cast<void>(decoder.Decode(Packet(4)));
		},
		    "closed actual codec must reject a later packet");
	}
	auto unsupported = Track();
	unsupported.CodecPrivate[16] = 1;
	RequireFailure([&] {
		sgre::media::MovieOpusDecoder decoder(unsupported);
	},
	    "unmeasured nonzero codec gain must remain explicit");
	sgre::media::MovieOpusDecoder decoder(Track());
	auto wrong = Packet(0);
	wrong.TrackNumber = 2;
	RequireFailure([&] {
		static_cast<void>(decoder.Decode(wrong));
	},
	    "wrong actual track must reject before consuming codec state");
	Require(decoder.Decode(Packet(0)).Samples.size() == 648 * 2, "rejected packet must preserve actual first-packet trim state");
}

static void TestTailPaddingRoundTrip() {
	for (const auto& pair : {std::pair<std::int64_t, std::size_t>{166667, 8}, {2833333, 136}, {8166667, 392}, {12166667, 584}, {13500000, 648}}) {
		sgre::media::MovieOpusDecoder decoder(Track(0));
		auto packet = Packet(0);
		packet.DiscardPaddingNanoseconds = pair.first;
		const auto block = decoder.Decode(packet);
		Require(block.Samples.size() == (960 - pair.second) * 2, "actual whole-sample padding serialization must remove the intended integer PCM frame count");
	}
	sgre::media::MovieOpusDecoder decoder(Track(0));
	auto packet = Packet(0);
	packet.DiscardPaddingNanoseconds = 1000;
	RequireFailure([&] {
		static_cast<void>(decoder.Decode(packet));
	},
	    "unwitnessed fractional-frame padding must reject before codec mutation");
	Require(decoder.Decode(Packet(0)).Samples.size() == 1920, "tail-padding admission failure must preserve the unconsumed codec");
}

static sgre::media::MovieAudioBlock PcmBlock(std::int64_t timestamp100ns, float value) {
	sgre::media::MovieAudioBlock block;
	block.Timestamp100ns = timestamp100ns;
	block.Samples.assign(960 * 2, value);

	return block;
}

static void TestActualSilentRegistryRingAndPausedOutput() {
	std::int64_t clock100ns = 100000000;
	sgre::media::MovieSilentAudioRegistry registry([&] {
		return clock100ns;
	});
	sgre::media::MoviePcmDescription description;
	description.Channels = 2;
	auto output = registry.Open(description);
	Require(!output->Snapshot(clock100ns).Progress.Position100ns, "new source has no fabricated output clock before real callbacks");
	for (std::uint32_t index = 0; index < 3; ++index) {
		auto block = PcmBlock(static_cast<std::int64_t>(index) * 200000, 0.125f * static_cast<float>(index + 1));
		Require(output->TryQueue(block), "three actual PCM owners must enter the native pending ring");
	}
	registry.RenderSilent(960);
	Require(output->Snapshot(clock100ns).CompletedFrames == 0, "three pending blocks do not invent native source start before full-ring encounter");
	auto fourth = PcmBlock(600000, 0.5f);
	const auto* fourth_storage = fourth.Samples.data();
	Require(!output->TryQueue(fourth) && fourth.Samples.data() == fourth_storage && fourth.Samples.size() == 1920, "full native ring starts actual output and leaves the retained fourth owner unchanged");
	output->SetPaused(true);
	clock100ns += 200000;
	registry.RenderSilent(960);
	const auto paused = output->Snapshot(clock100ns);
	Require(paused.CompletedFrames == 960 && paused.Progress.Position100ns == 200000 && paused.AssembledBlocks == 3, "paused movie assembly must preserve real submitted-buffer completion and callback clock");
	Require(!output->TryQueue(fourth) && fourth.Samples.data() == fourth_storage, "paused producer must not consume its actual retained PCM block");
	output->SetPaused(false);
	Require(output->TryQueue(fourth), "unpaused producer must resume assembly after real callback creates ring capacity");
	output->MarkInputEnded();
	Require(output->Snapshot(clock100ns).Progress.InputEnded && !output->Snapshot(clock100ns).OutputDrained, "actual source EOF must remain distinct from output completion");
	clock100ns += 400000;
	registry.RenderSilent(1920);
	clock100ns += 200000;
	registry.RenderSilent(960);
	const auto completed = output->Snapshot(clock100ns);
	Require(completed.CompletedFrames == 3840 && completed.OutputDrained && completed.Progress.InputEnded && completed.Progress.Position100ns == 800000, "all four actual submitted blocks must retire through silent system callbacks with their sample epoch");
	Require(completed.Progress.AssembledBlocks == 4 && registry.RenderedFrames() == 4800, "native assembled counter and independent manual host timeline must remain distinct");
	bool foreign_rejected = false;
	std::thread foreign([&] {
		try {
			static_cast<void>(output->Snapshot(clock100ns));
		} catch (const std::exception&) {
			foreign_rejected = true;
		}
	});
	foreign.join();
	Require(foreign_rejected, "actual output lease must reject a foreign producer thread");
	output->Close();
	RequireFailure([&] {
		static_cast<void>(output->Snapshot(clock100ns));
	},
	    "retired movie lease must not access registry/source state");
	registry.RenderSilent(960);
	registry.Close();
	registry.Close();
}

static void TestNativeCompletedBufferClock() {
	std::int64_t now100ns = 100000000;
	sgre::media::MovieSilentAudioRegistry registry([&] {
		return now100ns;
	});
	sgre::media::MoviePcmDescription description;
	description.Channels = 2;
	for (const auto& original : kNativeMovieAudioClockCases) {
		auto output = registry.Open(description);
		auto first = PcmBlock(original.SampleEpoch100ns, 0.25f);
		first.Samples.assign(static_cast<std::size_t>(original.CompletedFrames) * 2, 0.25f);
		Require(output->TryQueue(first), "original clock case must queue its actual first PCM owner");
		for (std::uint32_t i = 0; i < 2; ++i) {
			auto block = PcmBlock(0, 0.25f);
			Require(output->TryQueue(block), "original clock case must fill native pending ring");
		}
		auto fourth = PcmBlock(0, 0.25f);
		Require(!output->TryQueue(fourth), "original clock case must encounter the native full-ring start transition");
		now100ns = original.EndClock100ns;
		auto remaining = original.CompletedFrames;
		while (remaining != 0) {
			const auto count = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, 2048));
			registry.RenderSilent(count);
			remaining -= count;
		}
		const auto actual = output->Snapshot(original.QueryClock100ns);
		Require(actual.CompletedFrames == original.CompletedFrames && actual.Progress.Position100ns.has_value(), "native clock comparison must consume the actual completed PCM frame count");
		const auto seconds = static_cast<double>(static_cast<float>(static_cast<double>(*actual.Progress.Position100ns) / 10000000.0));
		Require(std::bit_cast<std::uint64_t>(seconds) == original.PositionSecondsWord, "actual native callback/sample-epoch position must match whole original video-clock consumer");
		output->Close();
	}
}

static void TestShortInputAndNegativeClock() {
	std::int64_t now100ns = 1000000;
	sgre::media::MovieSilentAudioRegistry registry([&] {
		return now100ns;
	});
	sgre::media::MoviePcmDescription description;
	description.Channels = 2;
	auto output = registry.Open(description);
	auto block = PcmBlock(0, 0.25f);
	Require(output->TryQueue(block), "actual short input enters pending ring");
	output->MarkInputEnded();
	registry.RenderSilent(960);
	const auto short_input = output->Snapshot(now100ns);
	Require(short_input.Progress.InputEnded && !short_input.Progress.Position100ns && short_input.CompletedFrames == 0 && !short_input.OutputDrained, "native short EOF does not fabricate full-ring Start or conflate source EOF with output drain");
	RequireFailure([&] {
		static_cast<void>(output->Snapshot(-1));
	},
	    "negative snapshot clock must reject before signed subtraction");
	output->Close();
}

static void TestEncryptedAudioWorker(const std::filesystem::path& path, const std::filesystem::path& seed_path) {
	std::ifstream seed_input(seed_path);
	std::string seed;
	if (!std::getline(seed_input, seed) || seed.empty()) {
		throw std::runtime_error("optional actual movie audio fixture requires its retained seed file");
	}
	sgre::media::WebmReader reader(path, seed);
	const sgre::media::WebmTrack* audio = nullptr;
	for (const auto& track : reader.Tracks()) {
		if (track.Codec == "A_OPUS") {
			audio = &track;
		}
	}
	Require(audio != nullptr, "optional actual movie must contain its real Opus track");
	sgre::media::MovieOpusDecoder decoder(*audio);
	std::size_t packets = 0;
	std::size_t frames = 0;
	while (packets < 400) {
		const auto packet = reader.NextPacket();
		Require(packet.has_value(), "actual packet prefix must remain complete");
		if (packet->TrackNumber == audio->Number) {
			const auto block = decoder.Decode(*packet);
			frames += block.Samples.size() / decoder.Description().Channels;
			++packets;
		}
	}
	Require(frames == 400 * 960 - 312, "original packet prefix must preserve declared312 trim without any fixed platform offset");
	std::int64_t now100ns = 100000000;
	sgre::media::MovieSilentAudioRegistry registry([&] {
		return now100ns;
	});
	auto stream = sgre::media::OpenWebmMoviePlayback(path, seed, registry);
	Require(stream->Description().HasAudio && stream->Description().HasVideo, "actual factory overload must retain both original tracks");
	bool actual_clock = false;
	std::size_t video_samples = 0;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
	while ((!actual_clock || video_samples < 12) && std::chrono::steady_clock::now() < deadline) {
		now100ns += 10000;
		registry.RenderSilent(48);
		const auto progress = stream->PumpAudio(now100ns);
		actual_clock = actual_clock || (progress.Position100ns.has_value() && progress.AssembledBlocks >= 4);
		const auto video = stream->PollVideo();
		if (video.Status == sgre::media::MovieVideoReadStatus::Sample) {
			Require(video.Sample.Frame && video.Sample.Rgb32, "actual independent worker must retain real decoded and converted video owners");
			++video_samples;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	Require(actual_clock && video_samples >= 12, "bounded real worker observation must reach actual silent output callbacks and decoded video publication");
	stream->SetPaused(true);
	now100ns += 200000;
	registry.RenderSilent(960);
	Require(stream->PumpAudio(now100ns).Position100ns.has_value(), "actual host output must keep its callback clock while movie assembly is paused");
	stream->Close();
	stream->Close();
	registry.Close();
}

static void TestReadableMovieMixerPcm() {
	std::int64_t clock100ns = 100000000;
	sgre::media::MovieSilentAudioRegistry registry([&] {
		return clock100ns;
	});
	sgre::media::MoviePcmDescription description;
	description.Channels = 2;
	auto output = registry.Open(description);
	for (std::uint32_t index = 0; index < 3; ++index) {
		auto block = PcmBlock(static_cast<std::int64_t>(index) * 200000, .125f);
		Require(output->TryQueue(block), "readable mixer retains original three-block pending policy");
	}
	std::array<float, 128> samples{};
	samples.fill(7);
	registry.RenderPcm(samples);
	for (const auto sample : samples) {
		Require(sample == 0, "readout cannot fabricate native playback before full-ring encounter");
	}
	auto fourth = PcmBlock(600000, .5f);
	Require(!output->TryQueue(fourth), "real full-ring encounter starts the same actual submitted movie owner");
	output->SetVolume(.5f);
	output->SetPaused(true);
	registry.RenderPcm(samples);
	for (const auto sample : samples) {
		Require(sample == .0625f, "readable manual PCM retains real source gain and submitted playback during assembly pause");
	}
	Require(registry.RenderedFrames() == 128 && output->Snapshot(clock100ns).CompletedFrames == 0 && output->Snapshot(clock100ns).AppliedGain == .5f, "PCM copy does not double-consume buffers or change actual manual sample clock");
	std::array<float, 3> partial{};
	RequireFailure([&] {
		registry.RenderPcm(partial);
	},
	    "partial stereo readout must reject before any source/render clock mutation");
	Require(registry.RenderedFrames() == 128, "invalid readout preserves actual render phase");
	output->Close();
	registry.RenderPcm(samples);
	for (const auto sample : samples) {
		Require(sample == 0, "source-first close cannot retain PCM storage through readout");
	}
	registry.Close();
}

int main(int argc, char** argv) {
	try {
		TestActualSystemCodecDeclaredTrim();
		TestTailPaddingRoundTrip();
		TestActualSilentRegistryRingAndPausedOutput();
		TestShortInputAndNegativeClock();
		TestNativeCompletedBufferClock();
		TestReadableMovieMixerPcm();
		if (argc == 3) {
			TestEncryptedAudioWorker(argv[1], argv[2]);
		}
		std::cout << "actual movie Opus trim and silent output owner tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
