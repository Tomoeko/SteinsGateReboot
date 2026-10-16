#pragma once

#include "sgre/audio/buffered_sound_output.hpp"
#include "sgre/audio/offline_sound_mastering.hpp"
#include "sgre/audio/wma_stream_source.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace sgre::audio {
	class SoundResourceControl;

	enum class OfflineSoundRenderStatus : std::uint8_t {
		Rendered,
		Prefilling,
		Starved,
		SourceFailed
	};

	struct OfflineSoundSourceRender {
		SoundVoiceAddress Source;
		std::uint32_t SamplesPerSecond = 0;
		std::uint64_t RequestedFrames = 0;
		std::uint64_t ConsumedFrames = 0;
		std::uint64_t SilencedFrames = 0;
		OfflineSoundRenderStatus Status = OfflineSoundRenderStatus::Rendered;
		WmaStreamReadStatus LastReadStatus = WmaStreamReadStatus::Rejected;
		bool Streaming = false;
		bool InputComplete = false;
		bool CallbackObserved = false;
	};

	struct OfflineSoundRenderResult {
		OfflineSoundRenderStatus Status = OfflineSoundRenderStatus::Rendered;
		std::size_t MasterFrames = 0;
		std::uint64_t MasterFrameBefore = 0;
		std::uint64_t MasterFrameAfter = 0;
		std::array<OfflineSoundSourceRender, 40> Sources;
		std::size_t SourceCount = 0;
	};

	// Real system nodes attach to decoded pool slots. Logical cue retirement may retain these idle nodes.
	// Both owners are serialized; closing either side removes node callbacks before releasing its borrow.
	// Manual offline mode is established before mixer/output node access. No hardware or native service readiness.
	// Original source-rate PCM feeds system conversion into the explicit master; XAudio2 filter parity remains open.
	class OfflineBufferedSoundPool final : public BufferedSoundOutput {
	public:
		OfflineBufferedSoundPool(BufferedSoundSources& actual_sources, OfflineSoundMasteringDescription description);
		OfflineBufferedSoundPool(BufferedSoundCues& actual_controller, OfflineSoundMasteringDescription description);
		OfflineBufferedSoundPool(SoundResourceControl& actual_controller, OfflineSoundMasteringDescription description);
		~OfflineBufferedSoundPool();
		OfflineBufferedSoundPool(const OfflineBufferedSoundPool&) = delete;
		OfflineBufferedSoundPool& operator=(const OfflineBufferedSoundPool&) = delete;
		OfflineBufferedSoundPool(OfflineBufferedSoundPool&&) = delete;
		OfflineBufferedSoundPool& operator=(OfflineBufferedSoundPool&&) = delete;
		[[nodiscard]] std::size_t Render(std::span<float> output);
		// Missing physical output is explicit silence with Prefilling/Starved, never invented source progress/EOF.
		// Per-source counts use original source rates; the master frame clock is the actual engine sample time.
		[[nodiscard]] OfflineSoundRenderResult RenderTyped(std::span<float> output);
		[[nodiscard]] OfflineSoundMasteringDescription Description() const;
		[[nodiscard]] std::uint64_t RenderedFrames() const;
		// Counts actual nodes whose engine identity still matches this live graph, including idle retained slots.
		[[nodiscard]] std::size_t SourceNodeCount() const;
		void Close() noexcept;

	private:
		void ConnectSourceOwner(BufferedSoundSources& actual_sources) override;
		void ReleaseSourceOwner(BufferedSoundSources& actual_sources) noexcept override;
		void AttachSource(SoundVoiceAddress address, BufferedSoundSources& actual_sources) override;
		void RetireSource(SoundVoiceAddress address) noexcept override;
		void ApplySourceMix(SoundVoiceAddress address, const SoundMix& actual_mix) override;
		void RequireWmaStreaming(const BufferedSoundSources& actual_sources, SoundVoiceAddress address) const override;

		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::audio
