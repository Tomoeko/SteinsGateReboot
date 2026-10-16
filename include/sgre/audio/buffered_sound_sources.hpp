#pragma once

#include "sgre/audio/buffered_sound_source_data.hpp"
#include "sgre/audio/wma_stream_source.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace sgre::audio {
	class BufferedSoundOutput;

	enum class BufferedSoundStreamPreparation {
		Prefilling,
		Prepared,
		Stopped
	};

	struct BufferedSoundSourceLimits {
		std::size_t MaxSourceDecodedBytes = 64 * 1024 * 1024;
		std::size_t MaxTotalDecodedBytes = 256 * 1024 * 1024;
		WmaStreamSourceLimits Streaming;
	};

	struct BufferedSoundSampleState {
		std::uint32_t SamplesPerSecond = 0;
		std::uint16_t Channels = 0;
		std::size_t BufferFrames = 0;
		std::size_t CursorFrame = 0;
		// Cursor-to-end frames in the owned buffer; repetitions are reported separately.
		std::size_t RemainingBufferFrames = 0;
		std::uint32_t LoopRepeatsRemaining = 0;
		std::uint64_t ConsumedFrames = 0;
		bool Queued = false;
		bool Running = false;
		// Actual CPU consume/flush completion is distinct from the recovered null-context drain latch.
		bool CallbackActive = false;
	};

	struct BufferedSoundSourceInfo {
		BufferedSoundSampleState Samples;
		SoundVoiceState RecoveredSchedule;
		// Native3921a0 supplies null pContext; queued samples do not change this observation.
		bool NativeBufferContextPresent = false;
		// Present only for a genuine incremental source; absence is not engine/device readiness.
		std::optional<WmaStreamSourceInfo> Streaming;
		bool StreamingOutputAttached = false;
	};

	// A first-party decoded source buffer owner; no device, audio-service readiness or script Sound registration.
	// Calls stay on its preparation/consumption owner. Output frames retain the original source rate/channel layout.
	class BufferedSoundSources {
	public:
		explicit BufferedSoundSources(BufferedSoundSourceLimits limits = {});
		~BufferedSoundSources();
		BufferedSoundSources(const BufferedSoundSources&) = delete;
		BufferedSoundSources& operator=(const BufferedSoundSources&) = delete;
		BufferedSoundSources(BufferedSoundSources&&) = delete;
		BufferedSoundSources& operator=(BufferedSoundSources&&) = delete;
		// Connect a real output owner before any samples are prepared. Either owner clears the borrow before destruction.
		void ConnectOutput(BufferedSoundOutput& actual_output);
		// Releases physical source nodes without completing or flushing the still-owned decoded buffers.
		void DisconnectOutput(BufferedSoundOutput& actual_output);
		void ApplyOutputMix(SoundVoiceAddress address, const SoundMix& actual_mix);
		// Input archive/definition/spans enclose this preparation call. Decoded samples are owned afterward.
		// The future actual cue owner separately retains native archive borrowers through callback retirement.
		// Reuse retires the old buffer/source before its callback, then decodes the replacement outside ordinary frames.
		void Prepare(SoundVoiceAddress address, const BufferedSoundSourceData& data, std::optional<std::uint16_t> verified_wma_encode_options = std::nullopt);
		// Explicit streaming admission. Existing untyped output callbacks remain unsupported for this lane.
		// The actual encoded/archive owner encloses cancellation, worker acknowledgement and source retirement.
		void PrepareWmaStreaming(SoundVoiceAddress address, const BufferedSoundSourceData& data, std::uint16_t verified_encode_options);
		void PollWmaStreams();
		[[nodiscard]] BufferedSoundStreamPreparation CompleteWmaStreamPreparation(SoundVoiceAddress address);
		void ReleaseWmaStreamInputBorrow(SoundVoiceAddress address);
		void RethrowWmaStreamFailure(SoundVoiceAddress address) const;
		void RequestPause(SoundVoiceAddress address);
		void RequestResume(SoundVoiceAddress address);
		void RequestStop(SoundVoiceAddress address);
		void Commit();
		// Copies only real consumed interleaved frames; the unused destination tail is untouched. No allocation or I/O.
		// Synchronous CPU completion is authored policy, not measured XAudio2/device timing or resampling.
		[[nodiscard]] std::size_t ConsumeFrames(SoundVoiceAddress address, std::span<float> destination);
		[[nodiscard]] WmaStreamReadResult ConsumeWmaStreamFrames(SoundVoiceAddress address, std::span<float> destination) noexcept;
		[[nodiscard]] BufferedSoundSourceInfo Inspect(SoundVoiceAddress address) const;
		[[nodiscard]] std::size_t DecodedBytes() const;
		void Retire(SoundVoiceAddress address);
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::audio
