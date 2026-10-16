#pragma once

#include "sgre/core/frame_clock.hpp"
#include "sgre/scene/captured_task.hpp"
#include "sgre/scene/parallel_task_set.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace sgre::scene {
	struct CapturedOpacityAnimationState {
		std::uint64_t Elapsed = 0;
		std::optional<std::uint32_t> Ordinal;
		float Sampled = 0;
		bool Complete = false;
	};

	// Native4faed0/5356f0/535570: one default-repeat MValueAnime, before MCaptured's output clamp.
	class CapturedOpacityAnimation {
	public:
		CapturedOpacityAnimation(float initial, std::int32_t target, std::int32_t duration, std::int32_t curve);
		[[nodiscard]] std::optional<float> Advance(float actual_frame_ticks);
		[[nodiscard]] CapturedOpacityAnimationState State() const noexcept;

	private:
		float m_Initial = 0;
		float m_Target = 0;
		std::uint32_t m_Duration = 0;
		std::uint32_t m_Count = 1;
		std::int32_t m_Curve = 0;
		CapturedOpacityAnimationState m_State;
	};

	class CapturedOpacityAnimations {
	public:
		// The actual CommonFirst group retires before its camera-child task; the Looper clock encloses both.
		CapturedOpacityAnimations(CapturedTask& actual_task, const core::FrameClock& actual_clock, ParallelTaskSet& actual_common_first);
		void Append(std::int32_t target, std::int32_t duration, std::int32_t curve);

	private:
		struct Entry;
		CapturedTask& m_Task;
		const core::FrameClock& m_Clock;
		ParallelTaskSet& m_Parent;
		std::vector<std::weak_ptr<Entry>> m_Entries;
	};
} // namespace sgre::scene
