#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace sgre::audio {

	struct SoundUniqueCandidate {
		std::uint32_t CueId = 0;
		float Priority = 0.0f;
		std::string_view Archive;
		std::string_view Key;
	};

	struct SoundUniquenessPlan {
		bool Accepted = true;
		std::array<std::uint32_t, 41> EvictedCues = {};
		std::size_t EvictionCount = 0;
	};

	// Strings borrow caller-owned cue metadata. Active order and prior evictions are preserved on rejection.
	[[nodiscard]] SoundUniquenessPlan ResolveSoundUniqueness(std::string_view archive, std::string_view key, float priority, std::span<const SoundUniqueCandidate> active_cues, bool limit_to_archive = true);

} // namespace sgre::audio
