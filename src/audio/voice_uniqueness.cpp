#include "sgre/audio/voice_uniqueness.hpp"

#include <stdexcept>

namespace sgre::audio {

	SoundUniquenessPlan ResolveSoundUniqueness(std::string_view archive, std::string_view key, float priority, std::span<const SoundUniqueCandidate> active_cues, bool limit_to_archive) {
		if (active_cues.size() > 41) {
			throw std::runtime_error("active sound uniqueness candidates exceed the recovered slot capacity");
		}

		SoundUniquenessPlan plan;
		if (key.empty()) {
			return plan;
		}

		for (const auto& candidate : active_cues) {
			if ((limit_to_archive && candidate.Archive != archive) || candidate.Key != key) {
				continue;
			}

			if (candidate.Priority > priority) {
				plan.Accepted = false;
				break;
			}

			plan.EvictedCues[plan.EvictionCount++] = candidate.CueId;
		}

		return plan;
	}

} // namespace sgre::audio
