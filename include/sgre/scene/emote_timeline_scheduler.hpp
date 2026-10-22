#pragma once

#include "sgre/resource/psb.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scene {
	class EmoteVariableControls;

	struct EmoteTimelinePlaybackState {
		float Position = 0;
		bool Playing = false;
		bool Looped = false;
		std::uint32_t Flags = 0;
		float BlendRatio = 1;
	};

	struct EmoteTimelinePlaybackInfo {
		std::string Label;
		std::uint32_t Flags = 0;
		float BlendRatio = 1;
	};

	class EmoteTimelineScheduler {
	public:
		// The actual document, controls and their selected Motion registration enclose this scheduler.
		// Preparation remains lazy, so first playback snapshots the then-current instant-variable policy.
		EmoteTimelineScheduler(EmoteVariableControls& actual_controls, const resource::PsbDocument& actual_document, std::function<void()> require_mutable);
		~EmoteTimelineScheduler() noexcept;
		EmoteTimelineScheduler(const EmoteTimelineScheduler&) = delete;
		EmoteTimelineScheduler& operator=(const EmoteTimelineScheduler&) = delete;
		void Play(std::string_view name, std::uint32_t flags = 0);
		void Pass();
		void Skip();
		void Stop(std::string_view name = {});
		void RequireCanAssignState(const EmoteTimelineScheduler& source) const;
		// Copies active playback into matching destination metadata without dispatching variable keys.
		void AssignState(const EmoteTimelineScheduler& source);
		void SetBlendRatio(std::string_view name, float ratio, float duration = 0, float easing = 0, bool stop_when_finished = false);
		void FadeIn(std::string_view name, float duration = 0, float easing = 0);
		void FadeOut(std::string_view name, float duration = 0, float easing = 0);
		[[nodiscard]] float BlendRatio(std::string_view name) const;
		void RequirePublicationOwner(const EmoteVariableControls& actual_controls, std::size_t actual_value_count) const;
		[[nodiscard]] float ApplyDifference(std::string_view name, float base) const;
		[[nodiscard]] bool Animating() const;
		[[nodiscard]] bool IsPlaying(std::string_view name = {}) const;
		[[nodiscard]] std::optional<EmoteTimelinePlaybackState> State(std::string_view name) const;
		[[nodiscard]] std::vector<EmoteTimelinePlaybackInfo> PlayingInfoList() const;
		[[nodiscard]] std::size_t ActiveCount() const;
		[[nodiscard]] std::size_t CatalogCount() const;
		// Native2ccc50 runs once before channel processing, using the same actual containing elapsed ticks.
		void Update(float elapsed_ticks);

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
