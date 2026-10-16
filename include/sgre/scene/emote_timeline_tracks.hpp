#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/scene/emote_timeline_transition.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scene {
	class EmoteVariableControls;

	struct EmoteTimelineKey {
		float Time = 0;
		bool Hidden = true;
		float Value = 0;
		float Easing = 0;
	};

	struct EmoteTimelineTrack {
		std::string Name;
		// Native lazy preparation snapshots membership; later policy changes do not rewrite this track.
		bool Instant = false;
		std::vector<EmoteTimelineKey> Keys;
	};

	struct EmoteTimelineTrackDefinition {
		float LoopBegin = 0;
		float LoopEnd = 0;
		float LastTime = 0;
		std::vector<EmoteTimelineTrack> Tracks;
	};

	class EmoteTimelineTracks {
	public:
		// Actual controls and their selected Motion owner enclose this active-track component.
		// The containing timeline scheduler supplies real positions; this object does not invent a clock/play/fade owner.
		EmoteTimelineTracks(EmoteVariableControls& controls, const resource::PsbValue& actual_definition, std::uint32_t flags = 0);
		EmoteTimelineTracks(const EmoteTimelineTracks&) = delete;
		EmoteTimelineTracks& operator=(const EmoteTimelineTracks&) = delete;
		// Native2bee90 selects all cursors, then optionally dispatches the last visible key at each actual position.
		void Seek(float position, bool dispatch = true);
		// Native2c5670 resets playback state while retaining the first prepared metadata/instant-policy snapshot.
		void Reset(std::uint32_t flags = 0);
		// Native assignment retains destination keys/policy/cache and recomputes cursors without dispatch.
		void RequireCanAssignState(const EmoteTimelineTracks& source) const;
		void AssignState(const EmoteTimelineTracks& source, std::uint32_t flags);
		// Native Pass reissues future visible keys with their stored time as duration, retaining cursors.
		void Pass();
		void AdvanceDifference(float elapsed_ticks);
		[[nodiscard]] float ApplyDifference(std::string_view name, float base, float blend) const;
		// Native2bf9b0 crosses every reached key; false excludes a key exactly at the supplied boundary.
		void CrossTo(float position, bool inclusive = true);
		[[nodiscard]] const EmoteTimelineTrackDefinition& Definition() const noexcept;
		[[nodiscard]] std::span<const std::size_t> Cursors() const noexcept;
		[[nodiscard]] float Position() const noexcept;

	private:
		void RequireOperation(float position) const;
		void Dispatch(std::size_t track, std::size_t key, float position);
		[[nodiscard]] bool Skipped(std::size_t track) const noexcept;
		EmoteVariableControls& m_Controls;
		EmoteTimelineTrackDefinition m_Definition;
		std::vector<std::size_t> m_Cursors;
		std::vector<EmoteTimelineTransition> m_Difference;
		std::vector<bool> m_HasDifference;
		std::vector<float> m_DifferenceValues;
		std::uint32_t m_Flags = 0;
		float m_Position = 0;
		bool m_Initialized = false;
		bool m_Operating = false;
		bool m_Valid = true;
	};
} // namespace sgre::scene
