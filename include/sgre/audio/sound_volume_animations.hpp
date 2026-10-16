#pragma once

#include "sgre/audio/buffered_sound_cues.hpp"
#include "sgre/audio/sound_volume_animation.hpp"
#include "sgre/core/frame_clock.hpp"
#include "sgre/scene/parallel_task_set.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace sgre::audio {

	// Actual Sound Parallel tasks own entries. Cues, selected clock and Parallel enclose every callback/lease.
	class SoundVolumeAnimations {
	public:
		SoundVolumeAnimations(BufferedSoundCues& actual_cues, const core::FrameClock& actual_selected_clock, scene::ParallelTaskSet& actual_parallel);
		SoundVolumeAnimations(const SoundVolumeAnimations&) = delete;
		SoundVolumeAnimations& operator=(const SoundVolumeAnimations&) = delete;
		void AppendVoice(BufferedSoundCueId id, float target_linear, std::int32_t duration, std::int32_t curve);
		void AppendGroup(std::string_view exact_group, float target_linear, std::int32_t duration, std::int32_t curve);
		// Authored containing shutdown marks only these actual tasks for their normal Parallel retirement.
		void CancelAll();
		[[nodiscard]] bool CanClose() const noexcept;

	private:
		using Target = std::variant<BufferedSoundCueId, std::string>;
		struct Entry;
		void RequireOwner() const;
		void Append(Target target, SoundVolumeAnimation animation);
		BufferedSoundCues& m_Cues;
		const core::FrameClock& m_Clock;
		scene::ParallelTaskSet& m_Parallel;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		std::vector<std::weak_ptr<Entry>> m_Entries;
	};

} // namespace sgre::audio
