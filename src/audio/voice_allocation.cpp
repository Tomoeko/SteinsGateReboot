#include "sgre/audio/voice_allocation.hpp"

#include <stdexcept>

namespace sgre::audio {

	static std::size_t Capacity(SoundVoiceType type) {
		static constexpr std::array<std::size_t, 3> kCapacities = {32, 8, 1};
		const auto index = static_cast<std::size_t>(type);
		if (index >= kCapacities.size()) {
			throw std::runtime_error("sound allocation has an unknown voice pool");
		}

		return kCapacities[index];
	}

	static void ValidateCandidates(SoundVoiceType type, std::span<const bool> free_slots, std::span<const SoundSlotCandidate> candidates) {
		if (candidates.size() > 41) {
			throw std::runtime_error("active sound cues exceed the recovered combined slot capacity");
		}

		std::array<std::uint32_t, 3> used = {};
		for (const auto& candidate : candidates) {
			const auto capacity = Capacity(candidate.Type);
			if (candidate.Slots.empty() || candidate.Slots.size() > capacity) {
				throw std::runtime_error("active sound cue has an invalid channel extent");
			}

			for (const auto slot : candidate.Slots) {
				if (slot >= capacity) {
					throw std::runtime_error("active sound cue slot exceeds its pool");
				}

				const std::uint32_t bit = std::uint32_t{1} << slot;
				auto& occupied = used[static_cast<std::size_t>(candidate.Type)];
				if ((occupied & bit) != 0 || (candidate.Type == type && free_slots[slot])) {
					throw std::runtime_error("active sound cue slots overlap another cue or the free table");
				}

				occupied |= bit;
			}
		}
	}

	SoundSlotPlan SelectSoundVoiceSlots(SoundVoiceType type, std::size_t requested_channels, float priority, std::span<const bool> free_slots, std::span<const SoundSlotCandidate> active_cues) {
		if (requested_channels == 0 || requested_channels > 64 || free_slots.size() != Capacity(type)) {
			throw std::runtime_error("sound allocation request or free table exceeds its recovered range");
		}

		ValidateCandidates(type, free_slots, active_cues);
		SoundSlotPlan plan;
		for (std::size_t slot = 0; slot < free_slots.size() && plan.SlotCount < requested_channels; ++slot) {
			if (free_slots[slot]) {
				plan.Slots[plan.SlotCount++] = static_cast<std::uint8_t>(slot);
			}
		}

		if (plan.SlotCount < requested_channels) {
			for (const auto& candidate : active_cues) {
				if (candidate.Type != type || candidate.Priority > priority) {
					continue;
				}

				for (const auto slot : candidate.Slots) {
					plan.Slots[plan.SlotCount++] = slot;
				}

				plan.EvictedCues[plan.EvictionCount++] = candidate.CueId;
				if (plan.SlotCount >= requested_channels) {
					break;
				}
			}
		}

		plan.Accepted = plan.SlotCount >= requested_channels;
		if (!plan.Accepted) {
			// Native failure frees the temporary selection after already stopping eligible cues.
			plan.Slots.fill(0);
			plan.SlotCount = 0;
		}

		return plan;
	}

} // namespace sgre::audio
