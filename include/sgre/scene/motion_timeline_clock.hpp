#pragma once

#include "sgre/resource/motion.hpp"

#include <string_view>

namespace sgre::scene {

	enum class MotionTimelineContinuation {
		Reset,
		Chain
	};

	struct MotionTimelineClockState {
		float TickCount = 0;
		float EvaluationTime = 0;
		float LastTime = 0;
		float LoopTime = 0;
		float Speed = 1;
		float DeltaTicks = 0;
		bool Playing = false;
		bool TickAssigned = true;
		// Native +192 survives commands and clears only after the first actual composition.
		bool InitialCompositionPending = true;
		bool InitialTagDispatchPending = false;
		// Native +194 requests endpoint seek and traversal to the assigned initial position; +193 consumes it.
		bool InitialBoundaryTraversalPending = false;
		bool SyncWaiting = false;
		bool AlignedThisUpdate = false;
	};

	class MotionTimelineClock {
	public:
		// The selected immutable timeline outlives this control phase, or is invalidated before retirement.
		// Applying controls is one phase of native play; it does not prepare layers or dispatch pending tags.
		void ApplyTimeline(const resource::MotionTimeline& timeline, MotionTimelineContinuation continuation = MotionTimelineContinuation::Reset);
		void InvalidateSelection() noexcept;
		void SetTickCount(float ticks);
		[[nodiscard]] bool SetTickLabel(std::string_view label);
		void SetSpeed(float speed) noexcept;
		void Stop() noexcept;
		[[nodiscard]] bool HasTimeline() const noexcept;
		[[nodiscard]] const MotionTimelineClockState& State() const noexcept;

	private:
		friend class MotionPlayerUpdate;
		friend class MotionPlayerCommands;
		void AssignTickCount(float ticks);
		const resource::MotionTimeline* m_Timeline = nullptr;
		MotionTimelineClockState m_State;
	};

} // namespace sgre::scene
