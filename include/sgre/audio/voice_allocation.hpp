#pragma once

#include "sgre/audio/voice_schedule.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::audio {

	struct SoundSlotCandidate {
		std::uint32_t CueId = 0;
		SoundVoiceType Type = SoundVoiceType::Buffered32;
		float Priority = 0.0f;
		// The caller owns these slots throughout SelectSoundVoiceSlots.
		std::span<const std::uint8_t> Slots;
	};

	struct SoundSlotPlan {
		bool Accepted = false;
		std::array<std::uint8_t, 32> Slots = {};
		std::size_t SlotCount = 0;
		std::array<std::uint32_t, 41> EvictedCues = {};
		std::size_t EvictionCount = 0;
	};

	// Candidates are in native active-list order. Even a rejected plan retains its ordered evictions.
	// An accepted plan can contain extra slots from a multi-channel eviction; use only the requested prefix.
	[[nodiscard]] SoundSlotPlan SelectSoundVoiceSlots(SoundVoiceType type, std::size_t requested_channels, float priority, std::span<const bool> free_slots, std::span<const SoundSlotCandidate> active_cues);

} // namespace sgre::audio
