#include "sgre/audio/wma_stream_source.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static_assert(!std::is_default_constructible_v<sgre::audio::WmaStreamSource::Admission>);

class PacketBits {
public:
	void Append(std::uint32_t value, std::uint8_t width) {
		for (auto bit = width; bit != 0; --bit) {
			if (m_Count % 8 == 0) {
				m_Bytes.push_back(0);
			}
			m_Bytes.back() |= static_cast<std::uint8_t>(((value >> (bit - 1)) & 1) << (7 - m_Count % 8));
			++m_Count;
		}
	}

	std::vector<std::uint8_t> Finish() {
		m_Bytes.resize(1487);
		return m_Bytes;
	}

private:
	std::vector<std::uint8_t> m_Bytes;
	std::size_t m_Count = 0;
};

static std::vector<std::uint8_t> ImpulsePacket(bool impulse) {
	PacketBits bits;
	bits.Append(0, 4);
	bits.Append(6, 4);
	bits.Append(0, 12);
	bits.Append(0, 3);
	bits.Append(0, 3);
	for (std::size_t frame = 0; frame < 6; ++frame) {
		bits.Append(0, 3);
		bits.Append(1, 1);
		bits.Append(63, 7);
		for (std::size_t band = 0; band < 25; ++band) {
			bits.Append(0, 1);
		}
		if (impulse && frame == 2) {
			bits.Append(0x3e6, 11);
			bits.Append(128, 9);
			bits.Append(0, 11);
			bits.Append(1, 1);
		}
		bits.Append(0xf6, 9);
	}
	return bits.Finish();
}

static sgre::audio::BufferedSoundSourceData SourceData(const std::vector<std::uint8_t>& bytes, std::uint32_t final_bytes = 16384) {
	sgre::audio::BufferedSoundSourceData data;
	data.Description.Format.Encoding = sgre::resource::WaveEncoding::WmaStandard;
	data.Description.Format.Channels = 1;
	data.Description.Format.SamplesPerSecond = 44100;
	data.Description.Format.AverageBytesPerSecond = 4000;
	data.Description.Format.BlockAlignment = 1487;
	data.Description.Format.BitsPerSample = 16;
	data.Description.DecodedPacketCumulativeBytes.push_back(final_bytes);
	data.Description.EncodedData.Size = bytes.size();
	Require(bytes.size() <= std::numeric_limits<std::uint32_t>::max(), "fixture encoded extent exceeds its explicit submission field");
	data.Submission.EncodedBytes = static_cast<std::uint32_t>(bytes.size());
	data.Submission.EndOfStream = true;
	data.EncodedBytes = bytes;
	return data;
}

static void Await(sgre::audio::WmaStreamSource& source, auto predicate) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (true) {
		source.Poll();
		if (predicate(source.Inspect())) {
			return;
		}
		Require(std::chrono::steady_clock::now() < deadline, "WMA source fixture exceeded its bounded worker observation");
		// Test-only waiting, never the source's consumer/callback or an ordinary frame.
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

class SourceOwner {
public:
	SourceOwner(sgre::audio::WmaStreamWorker& worker, const sgre::audio::BufferedSoundSourceData& data) : Source(worker.Create(data, 31)) {
	}
	~SourceOwner() noexcept {
		try {
			Source->RequestStop();
			Await(*Source, [&](const auto&) {
				return Source->CanRetire();
			});
			Source->Close();
		} catch (...) {
			std::terminate();
		}
	}
	std::unique_ptr<sgre::audio::WmaStreamSource> Source;
};

static double IndependentImpulse(std::size_t index) {
	if (index >= 4096) {
		return 0;
	}
	const float native_gain_argument = 64.0f * 0.05f;
	const double amplitude = static_cast<float>(std::pow(10.0, static_cast<double>(native_gain_argument))) * 128.0;
	const double position = static_cast<double>(index) + 0.5;
	return -amplitude / 32768.0 * (2.0 / 2048.0) * std::cos(std::numbers::pi / 2048.0 * (position + 1024.0) * 0.5) * std::sin(std::numbers::pi / 4096.0 * position);
}

static void ConsumeImpulse(sgre::audio::WmaStreamSource& source, std::size_t frame_count, bool impulse = true) {
	std::array<float, 239> output;
	std::size_t position = 0;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (position < frame_count) {
		output.fill(123.0f);
		const auto read = source.ConsumeFrames(output);
		if (read.Status == sgre::audio::WmaStreamReadStatus::Samples) {
			Require(read.Frames <= output.size() && read.Frames <= frame_count - position, "WMA ring returned too many real frames");
			for (std::size_t index = 0; index < read.Frames; ++index) {
				const auto expected = impulse ? IndependentImpulse(position + index) : 0;
				Require(std::abs(static_cast<double>(output[index]) - expected) < 2e-9, "WMA ring output differs from independent impulse across packet/wrap boundaries");
			}
			for (std::size_t index = read.Frames; index < output.size(); ++index) {
				Require(output[index] == 123.0f, "WMA consumer overwrote its unconsumed output tail");
			}
			position += read.Frames;
		} else {
			Require(read.Status == sgre::audio::WmaStreamReadStatus::Prefilling || read.Status == sgre::audio::WmaStreamReadStatus::Starved, "WMA stream ended or failed before its real frame ledger");
			Require(std::all_of(output.begin(), output.end(), [](auto value) {
				return value == 123.0f;
			}),
			    "WMA prefill/starvation fabricated output samples");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		Require(std::chrono::steady_clock::now() < deadline, "WMA consume fixture exceeded its bounded worker observation");
	}
	Await(source, [](const auto& info) {
		return !info.CallbackActive;
	});
	const auto final = source.Inspect();
	Require(final.WorkerQuiescent && final.InputComplete && final.QueuedFrames == 0 && final.ConsumedFrames == frame_count && source.CanRetire(), "WMA terminal consume/worker/source ownership differs");
}

static void TestPrefillPauseWrapAndThread() {
	const auto encoded = ImpulsePacket(true);
	auto data = SourceData(encoded);
	sgre::audio::WmaStreamSourceLimits limits;
	limits.BufferFrames = 257;
	limits.PrefillFrames = 128;
	sgre::audio::WmaStreamWorker worker(limits);
	{
		SourceOwner owner(worker, data);
		owner.Source->Pause();
		// dpds is an owned copy; the temporary preparation description need not survive a worker read.
		data.Description.DecodedPacketCumulativeBytes.clear();
		Await(*owner.Source, [](const auto& info) {
			return info.Prefilled;
		});
		std::array<float, 113> untouched;
		untouched.fill(321.0f);
		const auto paused = owner.Source->ConsumeFrames(untouched);
		Require(paused.Status == sgre::audio::WmaStreamReadStatus::Paused && paused.Frames == 0 && owner.Source->Inspect().ConsumedFrames == 0, "WMA pause advanced the actual source cursor");
		sgre::audio::WmaStreamReadResult foreign_result;
		std::thread foreign([&] {
			foreign_result = owner.Source->ConsumeFrames(untouched);
		});
		foreign.join();
		Require(foreign_result.Status == sgre::audio::WmaStreamReadStatus::Rejected, "foreign consumer acquired the real SPSC source");
		Require(std::all_of(untouched.begin(), untouched.end(), [](auto value) {
			return value == 321.0f;
		}),
		    "WMA paused or rejected consumption altered samples");
		owner.Source->Resume();
		ConsumeImpulse(*owner.Source, 8192);
	}
	worker.Close();
}

static void TestInputEndAndFractionalDrain() {
	const auto encoded = ImpulsePacket(true);
	for (const auto final_bytes : {16384U, 17408U}) {
		auto data = SourceData(encoded, final_bytes);
		sgre::audio::WmaStreamSourceLimits limits;
		limits.BufferFrames = 16384;
		limits.PrefillFrames = 12000;
		sgre::audio::WmaStreamWorker worker(limits);
		{
			SourceOwner owner(worker, data);
			Await(*owner.Source, [](const auto& info) {
				return info.WorkerQuiescent;
			});
			const auto info = owner.Source->Inspect();
			Require(info.InputComplete && info.Prefilled && info.CallbackActive && !owner.Source->CanRetire(), "WMA decoder EOF was substituted for actual callback consumption");
			Require(info.QueuedFrames == final_bytes / 2 && info.SamplesPerSecond == 44100 && info.Channels == 1, "WMA exact source ledger/rate/layout differs");
			ConsumeImpulse(*owner.Source, final_bytes / 2);
		}
		worker.Close();
	}
}

static void TestZeroCountFinal() {
	auto encoded = ImpulsePacket(true);
	encoded.resize(2 * 1487);
	encoded[1487] = 0x10;
	auto data = SourceData(encoded);
	data.Description.DecodedPacketCumulativeBytes.push_back(16384);
	sgre::audio::WmaStreamWorker worker;
	{
		SourceOwner owner(worker, data);
		ConsumeImpulse(*owner.Source, 8192);
		Require(owner.Source->Inspect().DecodedPackets == 2, "zero-count final packet was skipped or counted as output EOF before decoding");
	}
	worker.Close();
}

static void TestStopQuiescenceAndReuse() {
	auto encoded = ImpulsePacket(true);
	auto data = SourceData(encoded);
	data.Submission.LoopRepeats = 255;
	sgre::audio::WmaStreamSourceLimits limits;
	limits.BufferFrames = 512;
	limits.PrefillFrames = 256;
	limits.MaxSources = 1;
	sgre::audio::WmaStreamWorker worker(limits);
	{
		SourceOwner owner(worker, data);
		Await(*owner.Source, [](const auto& info) {
			return info.Prefilled;
		});
		bool rejected = false;
		try {
			static_cast<void>(worker.Create(data, 31));
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Require(rejected && !worker.CanClose(), "a real outstanding source slot was reused or ignored during shutdown");
		owner.Source->RequestStop();
		Await(*owner.Source, [&](const auto&) {
			return owner.Source->CanRetire();
		});
		const auto stopped = owner.Source->Inspect();
		Require(stopped.WorkerQuiescent && !stopped.CallbackActive && stopped.QueuedFrames == 0 && !stopped.InputComplete && stopped.ConsumedFrames == 0, "WMA stop fabricated consumption/EOF or lost decoder quiescence");
	}
	// Releasing old encoded storage is safe only after that exact source's acknowledged close.
	encoded = ImpulsePacket(false);
	data = SourceData(encoded);
	{
		SourceOwner replacement(worker, data);
		ConsumeImpulse(*replacement.Source, 8192, false);
	}
	worker.Close();
}

static void TestFullRingFairnessAndFailure() {
	const auto encoded = ImpulsePacket(true);
	auto data = SourceData(encoded);
	sgre::audio::WmaStreamSourceLimits limits;
	limits.BufferFrames = 256;
	limits.PrefillFrames = 128;
	sgre::audio::WmaStreamWorker worker(limits);
	{
		SourceOwner full(worker, data);
		full.Source->Pause();
		SourceOwner next(worker, data);
		Await(*next.Source, [](const auto& info) {
			return info.Prefilled;
		});
		ConsumeImpulse(*next.Source, 8192);
		Require(full.Source->Inspect().ConsumedFrames == 0 && full.Source->Inspect().QueuedFrames <= limits.BufferFrames, "full source blocked its peer or exceeded bounded PCM storage");
	}
	auto corrupt = encoded;
	corrupt[2] |= 0x0f;
	const auto bad = SourceData(corrupt);
	{
		SourceOwner failed(worker, bad);
		Await(*failed.Source, [](const auto& info) {
			return info.WorkerQuiescent;
		});
		const auto info = failed.Source->Inspect();
		Require(info.Failed && !info.InputComplete && !info.CallbackActive && failed.Source->CanRetire(), "worker failure impersonated successful decoder/callback completion");
		bool observed = false;
		try {
			failed.Source->RethrowWorkerFailure();
		} catch (const std::runtime_error&) {
			observed = true;
		}
		Require(observed, "worker's real decoder first failure was lost");
	}
	worker.Close();
}

static void TestTerminalReadAndActualInputRelease() {
	auto encoded = ImpulsePacket(true);
	const auto data = SourceData(encoded);
	sgre::audio::WmaStreamSourceLimits limits;
	limits.BufferFrames = 16384;
	limits.PrefillFrames = 8192;
	sgre::audio::WmaStreamWorker worker(limits);
	{
		SourceOwner owner(worker, data);
		Await(*owner.Source, [](const auto& info) {
			return info.WorkerQuiescent;
		});
		bool rejected = false;
		try {
			owner.Source->ReleaseInputBorrow();
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		Require(rejected && owner.Source->Inspect().InputBorrowActive, "input EOF released a resource still owned by actual PCM consumption");
		std::array<float, 9000> output;
		output.fill(777.0f);
		const auto read = owner.Source->ConsumeFrames(output);
		Require(read.Frames == 8192 && read.InputComplete && read.Status == sgre::audio::WmaStreamReadStatus::Samples, "partial final read was confused with starvation");
		owner.Source->ReleaseInputBorrow();
		Require(!owner.Source->Inspect().InputBorrowActive && owner.Source->Inspect().ConsumedFrames == 8192, "real encoded-span retirement changed retained source metadata/cursor");
		encoded.clear();
		encoded.shrink_to_fit();
		const auto retained = owner.Source->ConsumeFrames(output);
		Require(retained.Status == sgre::audio::WmaStreamReadStatus::Finished && retained.InputComplete && retained.Frames == 0, "retained idle source reread released encoded input");
	}
	worker.Close();
}

int main() {
	try {
		TestPrefillPauseWrapAndThread();
		TestInputEndAndFractionalDrain();
		TestZeroCountFinal();
		TestStopQuiescenceAndReuse();
		TestFullRingFairnessAndFailure();
		TestTerminalReadAndActualInputRelease();
		std::cout << "WMA bounded worker/source lifetime and terminal fixtures passed; no device or native timing claim\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
