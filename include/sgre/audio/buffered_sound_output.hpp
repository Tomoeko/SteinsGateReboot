#pragma once

#include "sgre/audio/sound_playback.hpp"
#include "sgre/audio/voice_schedule.hpp"

#include <stdexcept>

namespace sgre::audio {
	class BufferedSoundSources;

	// An actual output owner borrows the one serialized decoded pool until either side disconnects.
	// Source destruction precedes decoded buffer/callback destruction; logical cue retirement need not destroy idle slots.
	class BufferedSoundOutput {
	public:
		virtual ~BufferedSoundOutput() = default;

	private:
		// Only the actual source owner may publish both sides of the serialized connection.
		friend class BufferedSoundSources;
		virtual void ConnectSourceOwner(BufferedSoundSources& actual_sources) = 0;
		virtual void ReleaseSourceOwner(BufferedSoundSources& actual_sources) noexcept = 0;
		virtual void AttachSource(SoundVoiceAddress source, BufferedSoundSources& actual_sources) = 0;
		virtual void RetireSource(SoundVoiceAddress source) noexcept = 0;
		virtual void ApplySourceMix(SoundVoiceAddress source, const SoundMix& actual_mix) = 0;
		// Existing integer-consuming outputs reject streaming before any actual source replacement.
		virtual void RequireWmaStreaming(const BufferedSoundSources&, SoundVoiceAddress) const {
			throw std::runtime_error("buffered output has no genuine typed WMA streaming consumer");
		}
	};
} // namespace sgre::audio
