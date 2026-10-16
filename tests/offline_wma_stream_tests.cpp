#include "sgre/audio/offline_buffered_sound_pool.hpp"

#include "sgre/audio/buffered_sound_sources.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static std::vector<std::uint8_t> EncodedImpulse() {
	std::vector<std::uint8_t> bytes(1487);
	std::size_t position = 0;
	const auto append = [&](std::uint32_t value, std::uint8_t count) {
		for (auto bit = count; bit != 0; --bit) {
			bytes[position / 8] |= static_cast<std::uint8_t>(((value >> (bit - 1)) & 1) << (7 - position % 8));
			++position;
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
		if (frame == 2) {
			append(0x3e6, 11);
			append(128, 9);
			append(0, 11);
			append(1, 1);
		}
		append(0xf6, 9);
	}
	return bytes;
}

static sgre::audio::BufferedSoundSourceData SourceData(const std::vector<std::uint8_t>& encoded, std::uint32_t repeats) {
	sgre::audio::BufferedSoundSourceData data;
	data.Description.Format.Encoding = sgre::resource::WaveEncoding::WmaStandard;
	data.Description.Format.Channels = 1;
	data.Description.Format.SamplesPerSecond = 44100;
	data.Description.Format.AverageBytesPerSecond = 4000;
	data.Description.Format.BlockAlignment = 1487;
	data.Description.Format.BitsPerSample = 16;
	data.Description.DecodedPacketCumulativeBytes.push_back(16384);
	data.Description.EncodedData.Size = encoded.size();
	Require(encoded.size() == 1487, "offline stream fixture requires its bounded original-profile packet");
	data.Submission.EncodedBytes = 1487;
	data.Submission.EndOfStream = true;
	data.Submission.LoopRepeats = repeats;
	data.EncodedBytes = encoded;
	return data;
}

static void Await(sgre::audio::BufferedSoundSources& sources, auto predicate) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (true) {
		sources.PollWmaStreams();
		if (predicate(sources.Inspect({}))) {
			return;
		}
		Require(std::chrono::steady_clock::now() < deadline, "offline stream fixture exceeded actual producer observation bound");
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

class ActualOfflineOwner {
public:
	ActualOfflineOwner(std::uint32_t master_rate, sgre::audio::BufferedSoundSourceLimits limits) : Sources(limits) {
		sgre::audio::OfflineSoundMasteringDescription description;
		description.SamplesPerSecond = master_rate;
		description.Channels = 2;
		description.MaximumFrames = 512;
		Output = std::make_unique<sgre::audio::OfflineBufferedSoundPool>(Sources, description);
	}
	~ActualOfflineOwner() noexcept {
		try {
			Output->Close();
			Sources.RequestStop({});
			Sources.Commit();
			Await(Sources, [](const auto& info) {
				return !info.Samples.CallbackActive;
			});
			Sources.Close();
		} catch (...) {
			std::terminate();
		}
	}
	sgre::audio::BufferedSoundSources Sources;
	std::unique_ptr<sgre::audio::OfflineBufferedSoundPool> Output;
};

static std::vector<float> Render(bool streaming, std::uint32_t master_rate) {
	const auto encoded = EncodedImpulse();
	const auto data = SourceData(encoded, 1);
	sgre::audio::BufferedSoundSourceLimits limits;
	limits.Streaming.BufferFrames = 4097;
	limits.Streaming.PrefillFrames = 4000;
	ActualOfflineOwner owner(master_rate, limits);
	if (streaming) {
		owner.Sources.PrepareWmaStreaming({}, data, 31);
	} else {
		owner.Sources.Prepare({}, data, 31);
	}
	sgre::audio::SoundMixParameters parameters;
	const auto mix = sgre::audio::EvaluateSoundMix(parameters, 1, 2, false);
	owner.Sources.ApplyOutputMix({}, mix);
	owner.Sources.Commit();
	if (streaming) {
		Await(owner.Sources, [](const auto& info) {
			return info.Streaming->Prefilled;
		});
		Require(owner.Sources.CompleteWmaStreamPreparation({}) == sgre::audio::BufferedSoundStreamPreparation::Prepared && owner.Output->SourceNodeCount() == 1, "real prefetched source failed its actual offline node/matrix attachment");
	}
	const auto total = (16384 * static_cast<std::size_t>(master_rate) + 44099) / 44100 + 2048;
	std::vector<float> output(total * 2, 999.0f);
	std::uint64_t source_frames = 0;
	for (std::size_t offset = 0; offset < total;) {
		const auto frames = std::min<std::size_t>(173, total - offset);
		if (streaming) {
			// Fixture-only scheduling waits on real producer state, never synthesizing ready samples or clock ticks.
			Await(owner.Sources, [](const auto& info) {
				return info.Streaming->WorkerQuiescent || info.Streaming->QueuedFrames == 4097;
			});
			const auto report = owner.Output->RenderTyped(std::span(output).subspan(offset * 2, frames * 2));
			Require(report.Status == sgre::audio::OfflineSoundRenderStatus::Rendered && report.MasterFrames == frames && report.MasterFrameAfter - report.MasterFrameBefore == frames, "typed comparison unexpectedly starved or changed actual master clock");
			Require(report.SourceCount == 1 && report.Sources[0].SamplesPerSecond == 44100 && report.Sources[0].Streaming, "typed render replaced original source clock/layout");
			source_frames += report.Sources[0].ConsumedFrames;
		} else {
			Require(owner.Output->Render(std::span(output).subspan(offset * 2, frames * 2)) == frames, "existing whole-PCM offline render changed actual output extent");
		}
		offset += frames;
	}
	const auto state = owner.Sources.Inspect({});
	Require(state.Samples.ConsumedFrames == 16384 && state.Samples.CursorFrame == 8192 && !state.Samples.CallbackActive, "loop completion changed to mastering units or lost actual source frames");
	if (streaming) {
		Require(source_frames == 16384, "typed reports invented/lost source consumption across converter/ring boundaries");
		owner.Sources.ReleaseWmaStreamInputBorrow({});
		Require(!owner.Sources.Inspect({}).Streaming->InputBorrowActive && owner.Output->SourceNodeCount() == 1, "actual input release destroyed the retained converter node/history");
	}
	return output;
}

static void TestSameRateAndConvertedWholePcm() {
	for (const auto master_rate : {44100U, 48000U}) {
		const auto whole = Render(false, master_rate);
		const auto stream = Render(true, master_rate);
		Require(whole.size() == stream.size(), "typed/whole actual master extents differ");
		for (std::size_t index = 0; index < whole.size(); ++index) {
			Require(std::isfinite(stream[index]) && std::abs(whole[index] - stream[index]) < 1e-6f, "typed stream lost source conversion history/interpolation neighbor across ring wrap");
		}
	}
}

static void TestExplicitStarvationAndCancellation() {
	const auto encoded = EncodedImpulse();
	const auto data = SourceData(encoded, 255);
	sgre::audio::BufferedSoundSourceLimits limits;
	limits.Streaming.BufferFrames = 1;
	limits.Streaming.PrefillFrames = 1;
	ActualOfflineOwner owner(44100, limits);
	owner.Sources.PrepareWmaStreaming({}, data, 31);
	sgre::audio::SoundMixParameters parameters;
	owner.Sources.ApplyOutputMix({}, sgre::audio::EvaluateSoundMix(parameters, 1, 2, false));
	owner.Sources.Commit();
	Await(owner.Sources, [](const auto& info) {
		return info.Streaming->Prefilled;
	});
	Require(owner.Sources.CompleteWmaStreamPreparation({}) == sgre::audio::BufferedSoundStreamPreparation::Prepared, "starvation fixture never acquired a real source node");
	std::array<float, 128> output;
	const auto before = owner.Sources.Inspect({}).Samples.ConsumedFrames;
	const auto report = owner.Output->RenderTyped(output);
	const auto after = owner.Sources.Inspect({});
	Require(report.Status == sgre::audio::OfflineSoundRenderStatus::Starved && report.SourceCount == 1 && report.Sources[0].RequestedFrames > report.Sources[0].ConsumedFrames, "underflow was hidden as successful source completion");
	Require(after.Samples.ConsumedFrames - before == report.Sources[0].ConsumedFrames && !report.Sources[0].InputComplete && report.Sources[0].SilencedFrames == report.Sources[0].RequestedFrames - report.Sources[0].ConsumedFrames, "physical underflow silence invented source progress or EOF");
	owner.Sources.RequestStop({});
	owner.Sources.Commit();
	Await(owner.Sources, [](const auto& info) {
		return !info.Samples.CallbackActive;
	});
	Require(owner.Sources.Inspect({}).Streaming->WorkerQuiescent, "actual offline stop released callback before worker acknowledgement");
	owner.Sources.ReleaseWmaStreamInputBorrow({});
}

int main() {
	try {
		TestSameRateAndConvertedWholePcm();
		TestExplicitStarvationAndCancellation();
		std::cout << "Typed actual offline WMA/whole-PCM rate, starvation and cancellation fixtures passed; no hardware output\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
