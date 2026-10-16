#pragma once

#include "sgre/audio/buffered_sound_source_data.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>

namespace sgre::audio {

	struct WmaStreamSourceLimits {
		std::size_t BufferFrames = 32768;
		std::size_t PrefillFrames = 8192;
		std::size_t MaxSources = 40;
	};

	enum class WmaStreamReadStatus {
		Samples,
		Paused,
		Prefilling,
		Starved,
		Finished,
		StopPending,
		Cancelled,
		Failed,
		Rejected
	};

	struct WmaStreamReadResult {
		WmaStreamReadStatus Status = WmaStreamReadStatus::Rejected;
		std::size_t Frames = 0;
		// A partial read is terminal only after the real producer acknowledged input completion.
		bool InputComplete = false;
	};

	struct WmaStreamSourceInfo {
		std::uint32_t SamplesPerSecond = 0;
		std::uint16_t Channels = 0;
		std::uint64_t FramesPerPlay = 0;
		std::uint64_t ConsumedFrames = 0;
		std::size_t QueuedFrames = 0;
		std::uint32_t DecodedPackets = 0;
		bool Prefilled = false;
		bool Running = false;
		bool InputComplete = false;
		bool WorkerQuiescent = false;
		bool CallbackActive = true;
		bool StopRequested = false;
		bool Failed = false;
		bool InputBorrowActive = true;
	};

	class WmaStreamSource;

	// One dedicated host decoder worker services bounded real sources fairly, without using the resource FIFO.
	// The worker policy is authored; native3921a0 submits compressed WMA to XAudio2, not this host worker.
	class WmaStreamWorker {
	public:
		explicit WmaStreamWorker(WmaStreamSourceLimits limits = {});
		~WmaStreamWorker() noexcept;
		WmaStreamWorker(const WmaStreamWorker&) = delete;
		WmaStreamWorker& operator=(const WmaStreamWorker&) = delete;
		// The actual encoded resource owner encloses the source until ReleaseInputBorrow/Close, including cancelled reads.
		// Format and dpds metadata are copied; encoded samples remain the exact acquired resource span.
		[[nodiscard]] std::unique_ptr<WmaStreamSource> Create(const BufferedSoundSourceData& actual_data, std::uint16_t verified_encode_options);
		[[nodiscard]] bool CanClose() const;
		// Shutdown-only join. Ordinary source stop/retire never waits for or joins the decoder thread.
		void Close();

	private:
		friend class WmaStreamSource;
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
		void Retire(WmaStreamSource& actual_source);
	};

	class WmaStreamSource {
		struct State;

	public:
		class Admission {
			friend class WmaStreamWorker;
			Admission() = default;
		};

		WmaStreamSource(Admission, WmaStreamWorker& actual_worker, std::shared_ptr<State> state);
		~WmaStreamSource() noexcept;
		WmaStreamSource(const WmaStreamSource&) = delete;
		WmaStreamSource& operator=(const WmaStreamSource&) = delete;
		// Owner-thread control preserves the original source rate/cursor. Pause does not cancel bounded prefill.
		void Pause();
		void Resume();
		void RequestStop();
		// Observes actual producer completion, then flushes cancelled/failed PCM before completing the callback.
		void Poll();
		// No decode, allocation, lock, wait, notification or fallback samples. Unused output tail stays untouched.
		// The one actual callback/consumer stays on this source's serialized owner, as the current CPU backend does.
		[[nodiscard]] WmaStreamReadResult ConsumeFrames(std::span<float> destination) noexcept;
		[[nodiscard]] WmaStreamSourceInfo Inspect() const;
		void RethrowWorkerFailure() const;
		// Drop the actual encoded span after both gates, retaining owned PCM/metadata for real converter tail/reuse.
		void ReleaseInputBorrow();
		[[nodiscard]] bool CanRetire() const noexcept;
		// Retire only after output consume/flush AND actual input-worker quiescence; preserves borrowed archive life.
		void Close();

	private:
		friend class WmaStreamWorker;
		void RequireOwner() const;
		void ObserveCompletion() noexcept;
		WmaStreamWorker& m_Worker;
		std::shared_ptr<State> m_State;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		std::uint64_t m_ReadFrames = 0;
		std::uint64_t m_ConsumedFrames = 0;
		bool m_Prefilled = false;
		bool m_Running = true;
		bool m_CallbackActive = true;
		bool m_StopRequested = false;
		bool m_Closed = false;
	};

} // namespace sgre::audio
