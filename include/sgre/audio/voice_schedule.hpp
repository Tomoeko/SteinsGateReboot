#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sgre::audio {

	enum class SoundVoiceType : std::uint8_t {
		Buffered32 = 0,
		Buffered8 = 1,
		Streaming1 = 2
	};

	struct SoundVoiceAddress {
		SoundVoiceType Type = SoundVoiceType::Buffered32;
		std::uint8_t Slot = 0;
	};

	enum class SoundPauseReason : std::uint32_t {
		Voice = 1,
		All = 2,
		Group = 4,
		Archive = 8
	};

	class SoundPauseState {
	public:
		// True requests a low-level stop/start; only Voice resume respects other remaining reasons.
		[[nodiscard]] bool Pause(SoundPauseReason reason) noexcept;
		[[nodiscard]] bool Resume(SoundPauseReason reason) noexcept;
		[[nodiscard]] std::uint32_t Reasons() const noexcept;

	private:
		std::uint32_t m_Reasons = 0;
	};

	struct SoundVoiceState {
		bool Exists = false;
		bool Active = false;
		bool RequestedRunning = false;
		bool AppliedRunning = false;
		bool PendingFlush = false;
		bool Draining = false;
	};

	// A platform or buffer-only implementation owns playback. This boundary carries no OS handles.
	class SoundVoiceOperations {
	public:
		virtual ~SoundVoiceOperations() = default;
		virtual void Start(SoundVoiceAddress voice, std::uint32_t operation_set) = 0;
		virtual void Stop(SoundVoiceAddress voice, std::uint32_t operation_set) = 0;
		virtual void CommitAll() = 0;
		[[nodiscard]] virtual bool CurrentBufferContextPresent(SoundVoiceAddress voice) = 0;
		virtual void Flush(SoundVoiceAddress voice) = 0;
	};

	class SoundVoiceSchedule {
	public:
		// Calls and queued callback events belong to one scheduling thread.
		void Register(SoundVoiceAddress voice);
		void Remove(SoundVoiceAddress voice);
		void RequestPause(SoundVoiceAddress voice);
		void RequestResume(SoundVoiceAddress voice);
		void RequestStop(SoundVoiceAddress voice);
		void BufferEnded(SoundVoiceAddress voice);
		void Commit(SoundVoiceOperations& operations);
		[[nodiscard]] const SoundVoiceState& Inspect(SoundVoiceAddress voice) const;
		[[nodiscard]] std::uint32_t OperationSet() const noexcept;

	private:
		[[nodiscard]] static std::size_t Index(SoundVoiceAddress voice);
		std::array<SoundVoiceState, 41> m_Voices;
		std::uint32_t m_OperationSet = 1;
	};

} // namespace sgre::audio
