#include "sgre/media/movie_audio.hpp"

#include "sgre/media/opus.hpp"

#import <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>

namespace sgre::media {

	static void RequireMovieCodecStatus(OSStatus status, const char* operation) {
		if (status != noErr) {
			throw std::runtime_error(std::string(operation) + " failed with system status " + std::to_string(status));
		}
	}

	static std::int64_t MovieSampleTime100ns(std::uint64_t frames) {
		if (frames > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / 10000000) {
			throw std::runtime_error("movie decoded sample clock exceeds its signed 100ns range");
		}

		return static_cast<std::int64_t>(frames * 10000000 / 48000);
	}

	static std::uint64_t MoviePaddingSamples(std::uint64_t padding_ns) {
		if (padding_ns > 1000000000) {
			throw std::runtime_error("movie Opus tail padding exceeds its supported bounded block extent");
		}
		const auto samples = (padding_ns * 48000 + 500000000) / 1000000000;
		const auto serialized_ns = (samples * 1000000000 + 24000) / 48000;
		if (serialized_ns != padding_ns) {
			throw std::runtime_error("movie Opus tail padding is not the recovered integer-nanosecond serialization of whole PCM frames");
		}

		return samples;
	}

	class MovieOpusDecoder::Implementation {
	public:
		explicit Implementation(const WebmTrack& track)
		    : OwnerThread(std::this_thread::get_id()), TrackNumber(track.Number), CodecDelayNanoseconds(track.CodecDelayNanoseconds) {
			const auto header = ReadOpusHeader(track.CodecPrivate);
			if (track.Type != 2 || track.Codec != "A_OPUS" || track.Number == 0 || track.SamplingFrequency != 48000 || track.Channels != header.Channels || (header.Channels != 1 && header.Channels != 2) || track.CodecPrivate.size() != 19 || header.OutputGainQ8Decibels != 0) {
				throw std::runtime_error("movie system Opus decoder requires the actual 48kHz mapping0 mono/stereo track with recovered zero header gain");
			}
			if (CodecDelayNanoseconds > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || CodecDelayNanoseconds % 100 != 0 || CodecDelayNanoseconds / 100 != static_cast<std::uint64_t>(MovieSampleTime100ns(header.PreSkipSamples))) {
				throw std::runtime_error("movie Opus CodecDelay must equal its actual header pre-skip at the recovered 100ns boundary");
			}
			Description.Channels = header.Channels;
			RemainingPreSkip = header.PreSkipSamples;
			AudioComponentDescription component_description{};
			component_description.componentType = kAudioDecoderComponentType;
			component_description.componentSubType = kAudioFormatOpus;
			const auto component = AudioComponentFindNext(nullptr, &component_description);
			if (component == nullptr) {
				throw std::runtime_error("system Opus decoder component is unavailable");
			}
			try {
				RequireMovieCodecStatus(AudioComponentInstanceNew(component, &Codec), "movie Opus component creation");
				// The system decoder accepts its exact leading trim before initialization. The converter's
				// separate PrimeInfo does not expose this state; no measured offset is added to PreSkip.
				AudioCodecPrimeInfo prime{};
				prime.leadingFrames = header.PreSkipSamples;
				RequireMovieCodecStatus(AudioCodecSetProperty(Codec, kAudioCodecPropertyPrimeInfo, sizeof(prime), &prime), "movie Opus declared leading trim");
				AudioStreamBasicDescription input{};
				input.mFormatID = kAudioFormatOpus;
				input.mSampleRate = 48000;
				input.mChannelsPerFrame = header.Channels;
				input.mFramesPerPacket = 960;
				AudioStreamBasicDescription output{};
				output.mFormatID = kAudioFormatLinearPCM;
				output.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
				output.mSampleRate = 48000;
				output.mChannelsPerFrame = header.Channels;
				output.mBitsPerChannel = 32;
				output.mFramesPerPacket = 1;
				output.mBytesPerFrame = static_cast<UInt32>(header.Channels) * static_cast<UInt32>(sizeof(float));
				output.mBytesPerPacket = output.mBytesPerFrame;
				RequireMovieCodecStatus(AudioCodecInitialize(Codec, &input, &output, track.CodecPrivate.data(), static_cast<UInt32>(track.CodecPrivate.size())), "movie Opus initialization");
				Initialized = true;
			} catch (...) {
				Close();
				throw;
			}
		}

		~Implementation() {
			Close();
		}

		void RequireWorker() const {
			if (std::this_thread::get_id() != OwnerThread || !Initialized || Failed) {
				throw std::runtime_error("movie Opus decoding requires its actual live serialized worker codec");
			}
		}

		MovieAudioBlock Decode(const WebmPacket& packet) {
			RequireWorker();
			if (packet.TrackNumber != TrackNumber || packet.Frames.empty() || packet.Frames.size() > 256 || packet.Invisible || !packet.Additions.empty()) {
				throw std::runtime_error("movie Opus packet does not belong to its actual supported audio track");
			}
			if (packet.DiscardPaddingNanoseconds < 0) {
				throw std::runtime_error("movie Opus negative block discard padding requires its unrecovered native input trim");
			}
			const auto padding_ns = static_cast<std::uint64_t>(packet.DiscardPaddingNanoseconds);
			const auto removed_trailing = MoviePaddingSamples(padding_ns);
			std::size_t input_frames = 0;
			for (const auto& bytes : packet.Frames) {
				const auto info = ReadOpusPacket(bytes);
				if (bytes.size() > 61440 || info.TotalSamples != 960 || input_frames > 65536 - info.TotalSamples) {
					throw std::runtime_error("movie system Opus packet requires the measured 960-frame bounded original domain");
				}
				input_frames += info.TotalSamples;
			}
			const auto removed_leading = std::min<std::size_t>(RemainingPreSkip, input_frames);
			if (removed_trailing > input_frames - removed_leading || packet.TimestampNanoseconds < 0 || packet.TimestampNanoseconds < std::numeric_limits<std::int64_t>::min() + static_cast<std::int64_t>(CodecDelayNanoseconds)) {
				throw std::runtime_error("movie Opus padding/timestamp exceeds its actual block extent");
			}
			const auto delayed_timestamp = (packet.TimestampNanoseconds - static_cast<std::int64_t>(CodecDelayNanoseconds)) / 100;
			const auto trim_time = MovieSampleTime100ns(removed_leading);
			if (delayed_timestamp > std::numeric_limits<std::int64_t>::max() - trim_time) {
				throw std::runtime_error("movie Opus trimmed timestamp exceeds signed 100ns range");
			}
			MovieAudioBlock block;
			block.Timestamp100ns = delayed_timestamp + trim_time;
			block.Samples.reserve((input_frames - removed_leading) * Description.Channels);
			try {
				for (const auto& bytes : packet.Frames) {
					DecodePacket(bytes, block);
				}
				if (block.Samples.size() != (input_frames - removed_leading) * Description.Channels) {
					throw std::runtime_error("system Opus decoded frame count differs from the actual declared leading trim");
				}
				block.Samples.resize(block.Samples.size() - static_cast<std::size_t>(removed_trailing) * Description.Channels);
				RemainingPreSkip -= static_cast<std::uint32_t>(removed_leading);
			} catch (...) {
				Failed = true;
				throw;
			}

			return block;
		}

		void DecodePacket(std::span<const std::uint8_t> bytes, MovieAudioBlock& block) {
			AudioStreamPacketDescription packet_description{};
			packet_description.mDataByteSize = static_cast<UInt32>(bytes.size());
			packet_description.mVariableFramesInPacket = 960;
			UInt32 input_bytes = static_cast<UInt32>(bytes.size());
			UInt32 packets = 1;
			RequireMovieCodecStatus(AudioCodecAppendInputData(Codec, bytes.data(), &input_bytes, &packets, &packet_description), "movie Opus packet append");
			if (packets != 1 || input_bytes != bytes.size()) {
				throw std::runtime_error("system Opus decoder did not consume its exact supplied packet");
			}
			UInt32 output_bytes = static_cast<UInt32>(Workspace.size() * sizeof(float));
			UInt32 output_frames = 5760;
			UInt32 status = 0;
			RequireMovieCodecStatus(AudioCodecProduceOutputPackets(Codec, Workspace.data(), &output_bytes, &output_frames, nullptr, &status), "movie Opus packet decode");
			if ((status != kAudioCodecProduceOutputPacketSuccess && status != kAudioCodecProduceOutputPacketNeedsMoreInputData) || output_frames > 960 || output_bytes != output_frames * Description.Channels * sizeof(float)) {
				throw std::runtime_error("system Opus packet decode returned an unsupported buffered extent/status");
			}
			block.Samples.insert(block.Samples.end(), Workspace.begin(), Workspace.begin() + static_cast<std::ptrdiff_t>(output_frames * Description.Channels));
		}

		void Close() noexcept {
			if (std::this_thread::get_id() != OwnerThread) {
				std::terminate();
			}
			if (Codec != nullptr) {
				if (Initialized) {
					AudioCodecUninitialize(Codec);
				}
				AudioComponentInstanceDispose(Codec);
				Codec = nullptr;
			}
			Initialized = false;
		}

		const std::thread::id OwnerThread;
		const std::uint64_t TrackNumber;
		const std::uint64_t CodecDelayNanoseconds;
		MoviePcmDescription Description;
		AudioCodec Codec = nullptr;
		std::array<float, 5760 * 2> Workspace{};
		std::uint32_t RemainingPreSkip = 0;
		bool Initialized = false;
		bool Failed = false;
	};

	MovieOpusDecoder::MovieOpusDecoder(const WebmTrack& track)
	    : m_Implementation(std::make_unique<Implementation>(track)) {
	}

	MovieOpusDecoder::~MovieOpusDecoder() = default;

	const MoviePcmDescription& MovieOpusDecoder::Description() const noexcept {
		return m_Implementation->Description;
	}

	MovieAudioBlock MovieOpusDecoder::Decode(const WebmPacket& packet) {
		return m_Implementation->Decode(packet);
	}

	void MovieOpusDecoder::Close() noexcept {
		m_Implementation->Close();
	}

} // namespace sgre::media
