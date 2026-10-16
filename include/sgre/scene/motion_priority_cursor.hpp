#pragma once

#include "sgre/scene/motion_evaluator.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::scene {

	struct MotionPriorityCursorState {
		std::size_t Index = 0;
		float CurrentTime = 0;
		float FollowingTime = 0;
		bool Initialized = false;
	};

	class MotionPriorityCursor {
	public:
		// Prepared timeline and its priority orders remain immutable and outlive this cursor.
		explicit MotionPriorityCursor(const PreparedMotionTimeline& timeline);
		void Seek(float position);
		void Forward(float position);
		void Reverse(float position);
		[[nodiscard]] const MotionPriorityCursorState& State() const noexcept;
		[[nodiscard]] const PreparedMotionTimeline& Preparation() const noexcept;
		// Prepared orders already reverse native stored indices for 50ad30 publication.
		[[nodiscard]] std::span<const std::uint32_t> DrawOrder() const noexcept;

	private:
		friend class MotionSceneEvaluation;
		void RequirePosition(float position) const;
		void Select(std::size_t index) noexcept;
		const PreparedMotionTimeline& m_Timeline;
		MotionPriorityCursorState m_State;
	};

} // namespace sgre::scene
