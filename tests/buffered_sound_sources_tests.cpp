#include "sgre/audio/buffered_sound_sources.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFailure(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("decoded source fixture expected failure: " + std::string(expected));
}

static sgre::resource::PsbValue Integer(std::int64_t value) {
	sgre::resource::PsbValue result;
	result.Data = value;

	return result;
}

static sgre::resource::PsbValue Object(sgre::resource::PsbObject fields) {
	sgre::resource::PsbValue result;
	result.Data = std::move(fields);

	return result;
}

static sgre::resource::PsbValue Array(sgre::resource::PsbArray elements) {
	sgre::resource::PsbValue result;
	result.Data = std::move(elements);

	return result;
}

static sgre::resource::PsbValue Chunk(std::uint32_t index) {
	sgre::resource::PsbChunkReference reference;
	reference.Index = index;
	sgre::resource::PsbValue result;
	result.Data = reference;

	return result;
}

static void AppendWord(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
	bytes.push_back(static_cast<std::uint8_t>(value));
	bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

static void AppendDword(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
	for (std::size_t shift = 0; shift < 32; shift += 8) {
		bytes.push_back(static_cast<std::uint8_t>(value >> shift));
	}
}

static std::vector<std::uint8_t> PcmBytes(std::span<const std::int16_t> values) {
	std::vector<std::uint8_t> bytes;
	for (const auto value : values) {
		AppendWord(bytes, std::bit_cast<std::uint16_t>(value));
	}

	return bytes;
}

struct SourceFixture {
	std::unique_ptr<sgre::io::PublishedPsbFile> File;
	sgre::resource::SoundArchiveDefinitions Definitions;

	SourceFixture(std::uint16_t tag, std::uint16_t channels, std::uint16_t block_alignment, std::vector<std::uint8_t> encoded, std::int32_t device = 0, std::optional<sgre::resource::SampleLoop> loop = std::nullopt, std::span<const std::uint32_t> ledger = {}) {
		std::vector<std::uint8_t> bytes;
		AppendWord(bytes, tag);
		AppendWord(bytes, channels);
		AppendDword(bytes, tag == 0x161 ? 44100 : 48000);
		AppendDword(bytes, tag == 0x161 ? 4000 : 96000);
		AppendWord(bytes, block_alignment);
		AppendWord(bytes, tag == 2 ? 4 : 16);
		AppendWord(bytes, tag == 2 ? 8 : 0);
		if (tag == 2) {
			AppendWord(bytes, 4);
			AppendWord(bytes, 1);
			AppendWord(bytes, 256);
			AppendWord(bytes, 0);
		}

		sgre::resource::PsbDocument document;
		sgre::resource::PsbChunkRange format;
		format.Size = bytes.size();
		document.Chunks.push_back(format);
		sgre::resource::PsbChunkRange data;
		data.Offset = bytes.size();
		data.Size = encoded.size();
		document.Chunks.push_back(data);
		bytes.insert(bytes.end(), encoded.begin(), encoded.end());
		sgre::resource::PsbObject architecture{{"fmt", Chunk(0)}, {"data", Chunk(1)}, {"nosrc", Integer(256)}};
		if (loop) {
			architecture.emplace_back("loop", Array({Integer(loop->Begin), Integer(loop->Length)}));
		}
		if (!ledger.empty()) {
			sgre::resource::PsbChunkRange table;
			table.Offset = bytes.size();
			table.Size = ledger.size() * sizeof(std::uint32_t);
			document.Chunks.push_back(table);
			architecture.emplace_back("dpds", Chunk(2));
			for (const auto cumulative : ledger) {
				AppendDword(bytes, cumulative);
			}
		}

		const auto channel = Object({{"archData", Object(std::move(architecture))}});
		const auto voice = Object({{"device", Integer(device)}, {"channelList", Array({channel})}});
		document.Root = Object({{"voice", Object({{"selected", voice}})}});
		document.Header.Version = 3;
		document.SerializedSize = bytes.size();
		File = std::make_unique<sgre::io::PublishedPsbFile>(std::move(bytes), std::move(document));
		Definitions = sgre::resource::ReadSoundArchiveDefinitions(File->Document());
	}

	[[nodiscard]] sgre::audio::BufferedSoundSourceData Data(std::int32_t count = 1) const {
		return sgre::audio::ReadBufferedSoundSourceData(*File, Definitions.at("selected"), 0, count);
	}
};

static sgre::audio::SoundVoiceAddress Address(std::uint8_t slot = 0, sgre::audio::SoundVoiceType type = sgre::audio::SoundVoiceType::Buffered32) {
	sgre::audio::SoundVoiceAddress address;
	address.Type = type;
	address.Slot = slot;

	return address;
}

static void TestOwnedPcmConsumptionAndPause() {
	sgre::audio::BufferedSoundSources sources;
	const auto address = Address(31);
	{
		const std::array<std::int16_t, 5> values{-32768, -16384, 0, 16384, 32767};
		SourceFixture fixture(1, 1, 2, PcmBytes(values));
		const auto data = fixture.Data();
		Require(data.Submission.DisableRateConversion, "corrected full integer nosrc must reach preparation");
		sources.Prepare(address, data);
	}

	const auto prepared = sources.Inspect(address);
	Require(prepared.Samples.BufferFrames == 5 && prepared.Samples.CallbackActive && prepared.Samples.Queued && !prepared.Samples.Running && prepared.RecoveredSchedule.Exists && !prepared.NativeBufferContextPresent && sources.DecodedBytes() == 20, "real preparation state or native null context differs");
	std::array<float, 2> output{9, 9};
	Require(sources.ConsumeFrames(address, output) == 0 && output == std::array<float, 2>{9, 9}, "uncommitted source must leave output untouched");
	sources.Commit();
	Require(sources.ConsumeFrames(address, output) == 2 && output == std::array<float, 2>{-1, -0.5f}, "owned PCM after archive destruction must contain independent signed samples");
	sources.RequestPause(address);
	sources.Commit();
	output.fill(7);
	Require(sources.ConsumeFrames(address, output) == 0 && output == std::array<float, 2>{7, 7} && sources.Inspect(address).Samples.CursorFrame == 2 && sources.Inspect(address).Samples.CallbackActive, "pause must preserve queued frames and callback activity");
	sources.RequestResume(address);
	sources.Commit();
	std::array<float, 5> tail{8, 8, 8, 8, 8};
	Require(sources.ConsumeFrames(address, tail) == 3 && tail == std::array<float, 5>{0, 0.5f, 32767.0f / 32768, 8, 8}, "partial final PCM consume or destination tail differs");
	const auto completed = sources.Inspect(address);
	Require(!completed.Samples.Queued && !completed.Samples.CallbackActive && !completed.RecoveredSchedule.Active && completed.Samples.ConsumedFrames == 5 && completed.Samples.CursorFrame == 5 && sources.DecodedBytes() == 20, "actual final consume must complete while keeping idle source sample storage until retirement");
	sources.Retire(address);
	Require(sources.DecodedBytes() == 0 && !sources.Inspect(address).RecoveredSchedule.Exists, "retirement must release actual samples and schedule entry");
}

static void TestStereoAndIndependentAdpcm() {
	sgre::audio::BufferedSoundSources sources;
	const auto stereo = Address(7, sgre::audio::SoundVoiceType::Buffered8);
	const std::array<std::int16_t, 6> values{8192, -8192, 16384, -16384, 24576, -24576};
	SourceFixture fixture(1, 2, 4, PcmBytes(values), 1);
	sources.Prepare(stereo, fixture.Data());
	sources.Commit();
	std::array<float, 3> incomplete{};
	RequireFailure([&] {
		static_cast<void>(sources.ConsumeFrames(stereo, incomplete));
	},
	    "complete interleaved frames");
	std::array<float, 8> output{9, 9, 9, 9, 9, 9, 9, 9};
	Require(sources.ConsumeFrames(stereo, output) == 3 && output == std::array<float, 8>{0.25f, -0.25f, 0.5f, -0.5f, 0.75f, -0.75f, 9, 9} && sources.Inspect(stereo).Samples.ConsumedFrames == 3, "stereo frame counts must not become sample counts or lose interleave order");

	// One independently hand-computed Microsoft ADPCM block: predictor 256/0, delta16, sample1=1000, sample2=-1000, nibbles1/7.
	SourceFixture adpcm(2, 1, 8, {0, 16, 0, 0xe8, 3, 0x18, 0xfc, 0x17});
	const auto mono = Address();
	sources.Prepare(mono, adpcm.Data());
	sources.Commit();
	std::array<float, 4> decoded{};
	Require(sources.ConsumeFrames(mono, decoded) == 4 && decoded == std::array<float, 4>{-1000.0f / 32768, 1000.0f / 32768, 1016.0f / 32768, 1128.0f / 32768}, "actual ADPCM source consumption differs from independent predictor arithmetic");
	Require(!sources.Inspect(mono).Samples.CallbackActive && sources.Inspect(mono).Samples.ConsumedFrames == 4, "ADPCM completion must require four real decoded frames");
}

static void TestFiniteAndInfiniteLoops() {
	const std::array<std::int16_t, 4> values{0, 8192, 16384, 24576};
	sgre::resource::SampleLoop loop;
	loop.Begin = 1;
	loop.Length = 2;
	SourceFixture fixture(1, 1, 2, PcmBytes(values), 0, loop);
	sgre::audio::BufferedSoundSources sources;
	const auto address = Address();
	sources.Prepare(address, fixture.Data(3));
	sources.Commit();
	std::array<float, 3> prefix{};
	Require(sources.ConsumeFrames(address, prefix) == 3 && prefix == std::array<float, 3>{0, 0.25f, 0.5f} && sources.Inspect(address).Samples.CallbackActive, "first loop region must not complete the real source");
	std::array<float, 7> suffix{9, 9, 9, 9, 9, 9, 9};
	Require(sources.ConsumeFrames(address, suffix) == 5 && suffix == std::array<float, 7>{0.25f, 0.5f, 0.25f, 0.5f, 0.75f, 9, 9} && sources.Inspect(address).Samples.ConsumedFrames == 8 && !sources.Inspect(address).Samples.CallbackActive, "finite region repeats must retain prefix, repeated region and one final tail");

	SourceFixture whole(1, 1, 2, PcmBytes(values));
	sources.Prepare(address, whole.Data(-1));
	sources.Commit();
	std::array<float, 13> repeated{};
	Require(sources.ConsumeFrames(address, repeated) == repeated.size(), "infinite whole-buffer loop must fill requested real frames");
	for (std::size_t index = 0; index < repeated.size(); ++index) {
		Require(repeated[index] == static_cast<float>(index % 4) * 0.25f, "infinite whole-buffer loop sample order differs");
	}
	Require(sources.Inspect(address).Samples.CallbackActive && sources.Inspect(address).Samples.Queued && sources.Inspect(address).Samples.ConsumedFrames == 13, "infinite loop may not claim completion after one buffer span");
	sources.RequestStop(address);
	Require(sources.Inspect(address).Samples.CallbackActive, "requested stop alone may not pretend a completed flush");
	sources.Commit();
	Require(!sources.Inspect(address).Samples.CallbackActive && !sources.Inspect(address).Samples.Queued, "committed stopped-buffer flush must retire actual queued frames");
}

static void TestNativeNullContextIsSeparateFromSamples() {
	const std::array<std::int16_t, 4> values{0, 8192, 16384, 24576};
	SourceFixture fixture(1, 1, 2, PcmBytes(values));
	sgre::audio::BufferedSoundSources sources;
	const auto address = Address();
	sources.Prepare(address, fixture.Data());
	sources.Commit();
	std::array<float, 1> prefix{};
	Require(sources.ConsumeFrames(address, prefix) == 1, "null-context fixture must begin real current-buffer consumption");
	sources.RequestStop(address);
	sources.RequestResume(address);
	sources.Commit();
	const auto pending = sources.Inspect(address);
	Require(pending.RecoveredSchedule.Active && pending.RecoveredSchedule.Draining && pending.Samples.CallbackActive && pending.Samples.Queued && !pending.NativeBufferContextPresent, "started current buffer must survive flush while native null context remains null");
	sources.Commit();
	const auto drained_flag = sources.Inspect(address);
	Require(!drained_flag.RecoveredSchedule.Active && !drained_flag.RecoveredSchedule.Draining && drained_flag.Samples.CallbackActive && drained_flag.Samples.Queued && drained_flag.Samples.CursorFrame == 1 && drained_flag.Samples.RemainingBufferFrames == 3 && drained_flag.Samples.ConsumedFrames == 1 && !drained_flag.NativeBufferContextPresent, "native null-context drain quirk must not pretend actual queued samples were consumed or replace pContext with a buffer pointer");
	std::array<float, 3> tail{};
	Require(sources.ConsumeFrames(address, tail) == 3 && tail == std::array<float, 3>{0.25f, 0.5f, 0.75f} && !sources.Inspect(address).Samples.CallbackActive && sources.Inspect(address).Samples.ConsumedFrames == 4, "actual callback completion remains tied to remaining real frames after native drain flag cleared");
}

static void TestFlushReuseAndFailureRetirement() {
	const std::array<std::int16_t, 4> values{0, 8192, 16384, 24576};
	SourceFixture fixture(1, 1, 2, PcmBytes(values));
	sgre::audio::BufferedSoundSources sources;
	const auto address = Address();
	sources.Prepare(address, fixture.Data());
	sources.Commit();
	std::array<float, 1> prefix{};
	Require(sources.ConsumeFrames(address, prefix) == 1, "flush fixture needs one consumed real frame");
	sources.RequestStop(address);
	sources.Commit();
	const auto flushed = sources.Inspect(address);
	Require(!flushed.Samples.Queued && !flushed.Samples.CallbackActive && flushed.Samples.ConsumedFrames == 1 && flushed.Samples.CursorFrame == 4 && sources.DecodedBytes() == 16, "actual flush may discard remaining frames but may not add them to consumed count or destroy idle source storage");
	const std::array<std::int16_t, 2> replacement{-16384, 16384};
	SourceFixture next(1, 1, 2, PcmBytes(replacement));
	sources.Prepare(address, next.Data());
	Require(sources.DecodedBytes() == 8 && sources.Inspect(address).Samples.CallbackActive && sources.Inspect(address).Samples.ConsumedFrames == 0, "reuse must replace old sample and callback owners rather than append old frames");
	sources.Commit();
	std::array<float, 2> output{};
	Require(sources.ConsumeFrames(address, output) == 2 && output == std::array<float, 2>{-0.5f, 0.5f}, "reused slot emitted stale samples");
	sources.Prepare(address, next.Data());
	SourceFixture invalid(2, 1, 8, {2, 16, 0, 0xe8, 3, 0x18, 0xfc, 0x17});
	RequireFailure([&] {
		sources.Prepare(address, invalid.Data());
	},
	    "predictor exceeds");
	Require(sources.DecodedBytes() == 0 && !sources.Inspect(address).RecoveredSchedule.Exists && !sources.Inspect(address).Samples.CallbackActive, "replacement decode failure must leave the previous source/callback retired, as native reuse does");
	sources.Close();
	sources.Close();
	RequireFailure([&] {
		sources.Commit();
	},
	    "live serialized owner");
}

static void TestStrictPoolsLimitsAndOwners() {
	const std::array<std::int16_t, 4> values{0, 8192, 16384, 24576};
	SourceFixture fixture(1, 1, 2, PcmBytes(values));
	sgre::audio::BufferedSoundSourceLimits limits;
	limits.MaxSourceDecodedBytes = 16;
	limits.MaxTotalDecodedBytes = 24;
	sgre::audio::BufferedSoundSources sources(limits);
	RequireFailure([&] {
		sources.Prepare(Address(32), fixture.Data());
	},
	    "real pool0/1 slot");
	RequireFailure([&] {
		sources.Prepare(Address(8, sgre::audio::SoundVoiceType::Buffered8), fixture.Data());
	},
	    "real pool0/1 slot");
	RequireFailure([&] {
		sources.Prepare(Address(0, sgre::audio::SoundVoiceType::Streaming1), fixture.Data());
	},
	    "real pool0/1 slot");
	RequireFailure([&] {
		sources.Prepare(Address(0, sgre::audio::SoundVoiceType::Buffered8), fixture.Data());
	},
	    "matching actual buffered data");
	sources.Prepare(Address(), fixture.Data());
	RequireFailure([&] {
		sources.Prepare(Address(1), fixture.Data());
	},
	    "allocation limit");
	Require(sources.DecodedBytes() == 16 && sources.Inspect(Address()).Samples.CallbackActive && !sources.Inspect(Address(1)).RecoveredSchedule.Exists, "failed aggregate allocation must preserve another actual source");
	sources.Retire(Address());
	auto invalid = fixture.Data();
	invalid.Submission.LoopRepeats = 256;
	RequireFailure([&] {
		sources.Prepare(Address(), invalid);
	},
	    "loop count");
	invalid.Submission.LoopRepeats = 1;
	invalid.Submission.LoopBegin = 4;
	invalid.Submission.LoopLength = 1;
	RequireFailure([&] {
		sources.Prepare(Address(), invalid);
	},
	    "loop region");
	invalid.Submission.LoopBegin = 1;
	invalid.Submission.LoopLength = 0;
	RequireFailure([&] {
		sources.Prepare(Address(), invalid);
	},
	    "zero loop begin");
	invalid.Submission.LoopRepeats = 0;
	RequireFailure([&] {
		sources.Prepare(Address(), invalid);
	},
	    "zero native loop fields");
	SourceFixture empty(1, 1, 2, {});
	RequireFailure([&] {
		sources.Prepare(Address(), empty.Data());
	},
	    "real nonempty");
	SourceFixture partial(1, 1, 2, {0});
	RequireFailure([&] {
		sources.Prepare(Address(), partial.Data());
	},
	    "payload length");
	std::string owner_failure;
	std::thread other([&] {
		try {
			static_cast<void>(sources.Inspect(Address()));
		} catch (const std::runtime_error& error) {
			owner_failure = error.what();
		}
	});
	other.join();
	Require(owner_failure.find("serialized owner") != std::string::npos, "cross-thread control must reject before touching playback state");
}

class WmaBitFixture {
public:
	void Append(std::uint32_t value, std::uint8_t width) {
		for (std::uint8_t index = width; index != 0; --index) {
			if (m_Bits % 8 == 0) {
				m_Bytes.push_back(0);
			}
			m_Bytes.back() |= static_cast<std::uint8_t>(((value >> (index - 1)) & 1) << (7 - m_Bits % 8));
			++m_Bits;
		}
	}

	[[nodiscard]] std::vector<std::uint8_t> Packet() const {
		auto bytes = m_Bytes;
		bytes.resize(1487);

		return bytes;
	}

private:
	std::vector<std::uint8_t> m_Bytes;
	std::size_t m_Bits = 0;
};

static std::vector<std::uint8_t> IndependentWmaImpulsePacket() {
	// The first-party decoder fixture's independently captured six-frame frequency-zero impulse grammar.
	WmaBitFixture bits;
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
		if (frame == 2) {
			bits.Append(0x3e6, 11);
			bits.Append(128, 9);
			bits.Append(0, 11);
			bits.Append(1, 1);
		}
		bits.Append(0xf6, 9);
	}

	return bits.Packet();
}

static void TestExplicitWmaDomainAndRealConsumption() {
	const std::array<std::uint32_t, 1> ledger{16384};
	SourceFixture fixture(0x161, 1, 1487, IndependentWmaImpulsePacket(), 0, std::nullopt, ledger);
	sgre::audio::BufferedSoundSources sources;
	const auto address = Address();
	RequireFailure([&] {
		sources.Prepare(address, fixture.Data());
	},
	    "explicitly verified options31");
	RequireFailure([&] {
		sources.Prepare(address, fixture.Data(), 7);
	},
	    "explicitly verified options31");
	sources.Prepare(address, fixture.Data(), 31);
	Require(sources.DecodedBytes() == 8192 * sizeof(float) && sources.Inspect(address).Samples.BufferFrames == 8192 && sources.Inspect(address).Samples.SamplesPerSecond == 44100, "verified WMA source must own its actual decoded ledger extent");
	sources.Commit();
	constexpr double coefficients = 2048;
	const float gain_argument = 64.0f * 0.05f;
	const double amplitude = static_cast<float>(std::pow(10.0, static_cast<double>(gain_argument))) * 128;
	std::array<float, 257> output{};
	std::size_t consumed = 0;
	bool nonzero = false;
	while (sources.Inspect(address).Samples.CallbackActive) {
		output.fill(9);
		const auto frames = sources.ConsumeFrames(address, output);
		Require(frames != 0, "real WMA callback activity must have remaining consumable frames");
		for (std::size_t offset = 0; offset < frames; ++offset) {
			const auto index = consumed + offset;
			double expected = 0;
			if (index < 4096) {
				const double position = static_cast<double>(index) + 0.5;
				expected = -amplitude / 32768 * (2 / coefficients) * std::cos(std::numbers::pi / coefficients * (position + coefficients / 2) * 0.5) * std::sin(std::numbers::pi / (2 * coefficients) * position);
			}
			Require(std::abs(static_cast<double>(output[offset]) - expected) < 2e-9, "consumed WMA samples differ from independent cosine/sine impulse definition");
			nonzero |= output[offset] != 0;
		}
		Require(std::all_of(output.begin() + static_cast<std::ptrdiff_t>(frames), output.end(), [](float value) {
			return value == 9;
		}),
		    "partial WMA consume must leave destination tail untouched");
		consumed += frames;
	}
	Require(nonzero && consumed == 8192 && !sources.Inspect(address).Samples.Queued && sources.Inspect(address).Samples.ConsumedFrames == consumed, "actual WMA completion must follow all decoded samples, including trimmed silent tail");
	auto unsupported = fixture.Data();
	unsupported.Description.Format.Encoding = sgre::resource::WaveEncoding::WmaProfessional;
	RequireFailure([&] {
		sources.Prepare(address, unsupported, 31);
	},
	    "WMA Professional");
	Require(sources.DecodedBytes() == 0 && !sources.Inspect(address).RecoveredSchedule.Exists, "unsupported replacement may not retain nominal ready source state");
}

static void TestAdpcmSampleStorageCapacityAndAccounting() {
	const sgre::audio::BufferedSoundSourceLimits defaults;
	// Whole original sound metadata supplies these decoded-float extents, independently from the default policy.
	constexpr std::size_t kLargestWholeSource = 119451648;
	constexpr std::size_t kLargestOtherWholeSource = 16217488;
	constexpr std::size_t kStereoStreamingRing = 32768 * 2 * sizeof(float);
	Require(defaults.MaxSourceDecodedBytes >= kLargestWholeSource && defaults.MaxTotalDecodedBytes >= 2 * kLargestWholeSource + 6 * kLargestOtherWholeSource + 32 * kStereoStreamingRing, "default sample capacity rejects the recovered long source or overlapping sound owners");
	const std::array<std::array<std::size_t, 2>, 4> invalid_sizes{{{0, 32}, {257 * 1024 * 1024, 32}, {16, 0}, {16, 513 * 1024 * 1024}}};
	for (const auto& boundary : invalid_sizes) {
		sgre::audio::BufferedSoundSourceLimits invalid;
		invalid.MaxSourceDecodedBytes = boundary[0];
		invalid.MaxTotalDecodedBytes = boundary[1];
		RequireFailure([&] {
			sgre::audio::BufferedSoundSources rejected(invalid);
		},
		    "bounded positive sample-storage limits");
	}

	// Two genuine decoded ADPCM owners exhaust a small independently selected aggregate bound exactly.
	SourceFixture adpcm(2, 1, 8, {0, 16, 0, 0xe8, 3, 0x18, 0xfc, 0x17});
	sgre::audio::BufferedSoundSourceLimits limits;
	limits.MaxSourceDecodedBytes = 16;
	limits.MaxTotalDecodedBytes = 32;
	sgre::audio::BufferedSoundSources sources(limits);
	sources.Prepare(Address(0), adpcm.Data());
	sources.Prepare(Address(1), adpcm.Data());
	Require(sources.DecodedBytes() == 32, "two live ADPCM allocations must share the same exact aggregate ledger");
	RequireFailure([&] {
		sources.Prepare(Address(2), adpcm.Data());
	},
	    "sample-storage budget is exhausted");
	Require(sources.DecodedBytes() == 32 && sources.Inspect(Address(0)).Samples.CallbackActive && sources.Inspect(Address(1)).Samples.CallbackActive && !sources.Inspect(Address(2)).RecoveredSchedule.Exists, "aggregate rejection must retain both real source owners without fabricated replacement readiness");
	sources.Commit();
	for (const auto address : {Address(0), Address(1)}) {
		std::array<float, 4> output{};
		Require(sources.ConsumeFrames(address, output) == 4 && output == std::array<float, 4>{-1000.0f / 32768, 1000.0f / 32768, 1016.0f / 32768, 1128.0f / 32768}, "overlapping ADPCM owners lost their independently decoded sample sequence");
	}
	Require(sources.DecodedBytes() == 32, "callback completion must not pretend retained idle buffers were freed");
	sources.Retire(Address(0));
	Require(sources.DecodedBytes() == 16, "actual source retirement must return its precise decoded allocation");
	sources.Prepare(Address(2), adpcm.Data());
	Require(sources.DecodedBytes() == 32 && sources.Inspect(Address(2)).Samples.CallbackActive, "freed aggregate capacity must permit a real subsequent source");
	sources.Retire(Address(1));
	sources.Retire(Address(2));
	Require(sources.DecodedBytes() == 0, "retired ADPCM sources leaked sample-storage accounting");

	limits.MaxSourceDecodedBytes = 15;
	sgre::audio::BufferedSoundSources narrow(limits);
	RequireFailure([&] {
		narrow.Prepare(Address(), adpcm.Data());
	},
	    "ADPCM decoded audio exceeds its allocation limit");
	Require(narrow.DecodedBytes() == 0 && !narrow.Inspect(Address()).RecoveredSchedule.Exists, "per-source rejection must happen before readiness or allocation publication");
}

int main() {
	try {
		TestOwnedPcmConsumptionAndPause();
		TestStereoAndIndependentAdpcm();
		TestFiniteAndInfiniteLoops();
		TestNativeNullContextIsSeparateFromSamples();
		TestFlushReuseAndFailureRetirement();
		TestStrictPoolsLimitsAndOwners();
		TestExplicitWmaDomainAndRealConsumption();
		TestAdpcmSampleStorageCapacityAndAccounting();
		std::cout << "decoded buffered sources: 8 fixture groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
