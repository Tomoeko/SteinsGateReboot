#include "sgre/audio/sound_volume_animations.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::audio {
	static constexpr std::size_t kMaximumSoundVolumeAnimations = 1024;

	struct SoundVolumeAnimations::Entry {
		Entry(Target target, SoundVolumeAnimation animation) : Key(std::move(target)), Animation(std::move(animation)) {
		}
		Target Key;
		SoundVolumeAnimation Animation;
		scene::ParallelTaskHandle Handle;

		[[nodiscard]] bool Matches(const Target& other) const {
			if (const auto* id = std::get_if<BufferedSoundCueId>(&Key)) {
				const auto* other_id = std::get_if<BufferedSoundCueId>(&other);

				return other_id && id->Value == other_id->Value;
			}

			const auto* group = std::get_if<std::string>(&other);

			return group && std::get<std::string>(Key) == *group;
		}
	};

	SoundVolumeAnimations::SoundVolumeAnimations(BufferedSoundCues& actual_cues, const core::FrameClock& actual_selected_clock, scene::ParallelTaskSet& actual_parallel) : m_Cues(actual_cues), m_Clock(actual_selected_clock), m_Parallel(actual_parallel) {
		m_Parallel.RequireIdleTraversal();
		m_Entries.reserve(kMaximumSoundVolumeAnimations);
	}

	void SoundVolumeAnimations::RequireOwner() const {
		if (std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("Sound volume animation requires its serialized containing owner");
		}
	}

	void SoundVolumeAnimations::AppendVoice(BufferedSoundCueId id, float target_linear, std::int32_t duration, std::int32_t curve) {
		RequireOwner();
		if (id.Value == std::numeric_limits<std::uint32_t>::max()) {
			return;
		}

		// Native380720 leaves the caller's initialized unity gain for a missing integer ID.
		Append(id, SoundVolumeAnimation(m_Cues.VoiceVolume(id), target_linear, duration, curve));
	}

	void SoundVolumeAnimations::AppendGroup(std::string_view exact_group, float target_linear, std::int32_t duration, std::int32_t curve) {
		RequireOwner();
		const auto initial = m_Cues.GroupVolume(exact_group);
		Append(std::string(exact_group), SoundVolumeAnimation(initial, target_linear, duration, curve));
	}

	void SoundVolumeAnimations::Append(Target target, SoundVolumeAnimation animation) {
		m_Parallel.RequireIdleTraversal();
		std::erase_if(m_Entries, [](const auto& previous) {
			return previous.expired();
		});
		if (m_Entries.size() == kMaximumSoundVolumeAnimations) {
			throw std::runtime_error("Sound volume animation count exceeds its actual bounded Parallel owner");
		}
		for (const auto& weak : m_Entries) {
			if (const auto previous = weak.lock(); previous && previous->Matches(target) && !m_Parallel.Contains(previous->Handle)) {
				throw std::runtime_error("Sound volume replacement requires its actual task rather than a detached callback lease");
			}
		}

		auto entry = std::make_shared<Entry>(std::move(target), std::move(animation));
		scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [entry, &cues = m_Cues, &clock = m_Clock, &parallel = m_Parallel](auto handle) {
			const auto ticks = clock.VariableFrame() ? std::max(clock.VariableStep(), 1.0f) : 1.0f;
			if (const auto gain = entry->Animation.Advance(ticks)) {
				if (const auto* id = std::get_if<BufferedSoundCueId>(&entry->Key)) {
					static_cast<void>(cues.SetVoiceVolume(*id, *gain));
				} else {
					cues.SetGroupVolume(std::get<std::string>(entry->Key), *gain);
				}
			}
			if (entry->Animation.State().Complete) {
				parallel.SetStatus(handle, scene::TaskStatus::Retire);
			}
		};
		// Allocate/publish before retiring predecessors; no callback occurs between these serialized writes.
		// This preserves old tasks on authored allocation failure, whose native null-allocation path remains open.
		entry->Handle = m_Parallel.Append(std::move(callbacks));
		for (const auto& weak : m_Entries) {
			if (const auto previous = weak.lock(); previous && previous->Matches(entry->Key) && m_Parallel.Contains(previous->Handle)) {
				m_Parallel.SetStatus(previous->Handle, scene::TaskStatus::Retire);
			}
		}
		m_Entries.push_back(entry);
	}

	void SoundVolumeAnimations::CancelAll() {
		RequireOwner();
		m_Parallel.RequireIdleTraversal();
		for (const auto& weak : m_Entries) {
			if (const auto entry = weak.lock(); entry && m_Parallel.Contains(entry->Handle)) {
				m_Parallel.SetStatus(entry->Handle, scene::TaskStatus::Retire);
			}
		}
	}

	bool SoundVolumeAnimations::CanClose() const noexcept {
		if (std::this_thread::get_id() != m_OwnerThread) {
			return false;
		}

		// A real extracted lease still captures Cues/clock even when the task left this Parallel list.
		return std::all_of(m_Entries.begin(), m_Entries.end(), [](const auto& entry) {
			return entry.expired();
		});
	}

} // namespace sgre::audio
