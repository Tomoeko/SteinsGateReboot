#pragma once

#include "sgre/media/movie_playback.hpp"
#include "sgre/media/webm.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace sgre::audio {
	class SoundResourceControl;
}

namespace sgre::media {

	struct MoviePcmDescription {
		std::uint32_t SamplesPerSecond = 48000;
		std::uint32_t Channels = 0;
	};

	struct MovieAudioBlock {
		std::int64_t Timestamp100ns = 0;
		// Interleaved frames in Description().Channels order. The decoder transfers the actual PCM owner.
		std::vector<float> Samples;
	};

	// Created, called and destroyed by the one real demux/decode worker.
	class MovieOpusDecoder {
	public:
		explicit MovieOpusDecoder(const WebmTrack& track);
		~MovieOpusDecoder();
		MovieOpusDecoder(const MovieOpusDecoder&) = delete;
		MovieOpusDecoder& operator=(const MovieOpusDecoder&) = delete;

		[[nodiscard]] const MoviePcmDescription& Description() const noexcept;
		[[nodiscard]] MovieAudioBlock Decode(const WebmPacket& packet);
		void Close() noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

	struct MovieAudioOutputSnapshot {
		MovieAudioProgress Progress;
		// Native +120: successfully assembled PCM blocks. The video throttle acknowledges this count separately.
		std::uint32_t AssembledBlocks = 0;
		std::uint64_t CompletedFrames = 0;
		bool OutputDrained = false;
		// Retained source-voice gain, sampled from Sound only at Open/SetVolume.
		float AppliedGain = 1;
	};

	class MovieSilentAudioRegistry;
	class MoviePcmDelivery;

	// Main-thread producer plus the registry's synchronous manual-render callback. No device output.
	class MovieAudioOutput {
	public:
		~MovieAudioOutput();
		MovieAudioOutput(const MovieAudioOutput&) = delete;
		MovieAudioOutput& operator=(const MovieAudioOutput&) = delete;

		// False leaves block intact, including native three-pending-block backpressure/start transition.
		[[nodiscard]] bool TryQueue(MovieAudioBlock& block);
		void MarkInputEnded();
		[[nodiscard]] MovieAudioOutputSnapshot Snapshot(std::int64_t now100ns) const;
		// Gates new assembly only; submitted buffers continue through the registry while paused.
		void SetPaused(bool paused);
		void SetVolume(float volume);
		void Close() noexcept;

	private:
		friend class MovieSilentAudioRegistry;
		class Implementation;
		explicit MovieAudioOutput(std::unique_ptr<Implementation> implementation);
		std::unique_ptr<Implementation> m_Implementation;
	};

	// The actual hidden/offline host owns this beside its existing mastering owner. Every host audio phase
	// renders registered movie voices even if their LayerMovie is paused, hidden or absent from the draw list.
	class MovieSilentAudioRegistry {
	public:
		// Low-level PCM transport: no containing Sound setting conversion.
		explicit MovieSilentAudioRegistry(std::function<std::int64_t()> read_clock100ns);
		// Actual Sound owner encloses every registered movie lease and this registry through Close.
		MovieSilentAudioRegistry(std::function<std::int64_t()> read_clock100ns, audio::SoundResourceControl& actual_sound);
		~MovieSilentAudioRegistry();
		MovieSilentAudioRegistry(const MovieSilentAudioRegistry&) = delete;
		MovieSilentAudioRegistry& operator=(const MovieSilentAudioRegistry&) = delete;

		[[nodiscard]] std::unique_ptr<MovieAudioOutput> Open(const MoviePcmDescription& description);
		// Same real-wall-clock frame budget as existing RenderSilentAudio; fixed 48 kHz stereo mastering.
		// The owned preallocated output is discarded. No outputNode/inputNode/device graph is opened.
		void RenderSilent(std::uint32_t frames);
		// Synchronous output from the same actual manual48k stereo mixer; preserves queue/gain/clock ownership.
		void RenderPcm(std::span<float> interleaved);
		[[nodiscard]] std::uint64_t RenderedFrames() const noexcept;
		void Close() noexcept;

	private:
		friend class MoviePcmDelivery;
		void RenderFrames(std::uint32_t frames, std::span<float> output);
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::media
