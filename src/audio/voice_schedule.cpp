#include "sgre/audio/voice_schedule.hpp"

#include <stdexcept>

namespace sgre::audio {

	bool SoundPauseState::Pause(SoundPauseReason reason) noexcept {
		const auto previous = m_Reasons;
		m_Reasons |= static_cast<std::uint32_t>(reason);

		return previous == 0;
	}

	bool SoundPauseState::Resume(SoundPauseReason reason) noexcept {
		m_Reasons &= ~static_cast<std::uint32_t>(reason);

		// Group/archive/global resume request a start even if another pause reason remains.
		return reason != SoundPauseReason::Voice || m_Reasons == 0;
	}

	std::uint32_t SoundPauseState::Reasons() const noexcept {
		return m_Reasons;
	}

	std::size_t SoundVoiceSchedule::Index(SoundVoiceAddress voice) {
		static constexpr std::array<std::uint8_t, 3> kCapacities = {32, 8, 1};
		static constexpr std::array<std::size_t, 3> kOffsets = {0, 32, 40};
		const auto type = static_cast<std::size_t>(voice.Type);
		if (type >= kCapacities.size() || voice.Slot >= kCapacities[type]) {
			throw std::runtime_error("sound voice address exceeds its recovered pool capacity");
		}

		return kOffsets[type] + voice.Slot;
	}

	void SoundVoiceSchedule::Register(SoundVoiceAddress voice) {
		SoundVoiceState state;
		state.Exists = true;
		state.Active = true;
		state.RequestedRunning = true;
		m_Voices[Index(voice)] = state;
	}

	void SoundVoiceSchedule::Remove(SoundVoiceAddress voice) {
		m_Voices[Index(voice)] = SoundVoiceState{};
	}

	void SoundVoiceSchedule::RequestPause(SoundVoiceAddress voice) {
		auto& state = m_Voices[Index(voice)];
		if (state.Exists) {
			state.RequestedRunning = false;
		}
	}

	void SoundVoiceSchedule::RequestResume(SoundVoiceAddress voice) {
		auto& state = m_Voices[Index(voice)];
		if (state.Exists) {
			state.RequestedRunning = true;
		}
	}

	void SoundVoiceSchedule::RequestStop(SoundVoiceAddress voice) {
		auto& state = m_Voices[Index(voice)];
		if (state.Exists) {
			state.RequestedRunning = false;
			state.PendingFlush = true;
		}
	}

	void SoundVoiceSchedule::BufferEnded(SoundVoiceAddress voice) {
		m_Voices[Index(voice)].Active = false;
	}

	static SoundVoiceAddress Address(std::size_t index) {
		SoundVoiceAddress address;
		address.Type = index < 32 ? SoundVoiceType::Buffered32 : index < 40 ? SoundVoiceType::Buffered8
		                                                                    : SoundVoiceType::Streaming1;
		address.Slot = static_cast<std::uint8_t>(index < 32 ? index : index < 40 ? index - 32
		                                                                         : index - 40);

		return address;
	}

	void SoundVoiceSchedule::Commit(SoundVoiceOperations& operations) {
		for (std::size_t index = 0; index < m_Voices.size(); ++index) {
			auto& state = m_Voices[index];
			if (state.RequestedRunning != state.AppliedRunning) {
				if (state.Exists) {
					if (state.RequestedRunning) {
						operations.Start(Address(index), m_OperationSet);
					} else {
						operations.Stop(Address(index), m_OperationSet);
					}
				}

				state.AppliedRunning = state.RequestedRunning;
			}
		}

		operations.CommitAll();
		for (std::size_t index = 0; index < m_Voices.size(); ++index) {
			auto& state = m_Voices[index];
			if (!state.Exists) {
				continue;
			}

			if (state.Active && state.Draining && !operations.CurrentBufferContextPresent(Address(index))) {
				state.Active = false;
				state.Draining = false;
			}

			if (state.PendingFlush) {
				operations.Flush(Address(index));
				state.PendingFlush = false;
				state.Draining = true;
			}
		}

		++m_OperationSet;
		if (m_OperationSet == 0) {
			m_OperationSet = 1;
		}
	}

	const SoundVoiceState& SoundVoiceSchedule::Inspect(SoundVoiceAddress voice) const {
		return m_Voices[Index(voice)];
	}

	std::uint32_t SoundVoiceSchedule::OperationSet() const noexcept {
		return m_OperationSet;
	}

} // namespace sgre::audio
