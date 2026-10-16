#include "sgre/scene/motion_priority_cursor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	MotionPriorityCursor::MotionPriorityCursor(const PreparedMotionTimeline& timeline) : m_Timeline(timeline) {
		if (timeline.Priority.size() < 2 || timeline.Priority.size() > 1000000 || !std::isfinite(timeline.LastTime) || timeline.LastTime < 0 || timeline.Priority.front().Time != 0 || timeline.Priority.back().Time < timeline.LastTime) {
			throw std::runtime_error("motion priority cursor requires a bounded ordinary track spanning zero through lastTime");
		}

		float previous = 0;
		for (const auto& frame : timeline.Priority) {
			if (!std::isfinite(frame.Time) || frame.Time < previous) {
				throw std::runtime_error("motion priority cursor track time is nonfinite or out of order");
			}

			previous = frame.Time;
		}
	}

	const PreparedMotionTimeline& MotionPriorityCursor::Preparation() const noexcept {
		return m_Timeline;
	}

	void MotionPriorityCursor::RequirePosition(float position) const {
		if (!std::isfinite(position) || position < 0 || position > m_Timeline.LastTime) {
			throw std::runtime_error("motion priority cursor position is outside its ordinary evaluation domain");
		}
	}

	void MotionPriorityCursor::Select(std::size_t index) noexcept {
		m_State.Index = index;
		m_State.CurrentTime = m_Timeline.Priority[index].Time;
		m_State.FollowingTime = m_Timeline.Priority[index + 1].Time;
		m_State.Initialized = true;
	}

	void MotionPriorityCursor::Seek(float position) {
		RequirePosition(position);
		std::size_t current = m_State.Index;
		for (std::size_t index = 0; index < m_Timeline.Priority.size(); ++index) {
			const auto time = m_Timeline.Priority[index].Time;
			if (time == position) {
				current = index;
				break;
			}

			if (time > position) {
				current = index - 1;
				break;
			}
		}

		Select(std::min(current, m_Timeline.Priority.size() - 2));
	}

	void MotionPriorityCursor::Forward(float position) {
		RequirePosition(position);
		if (!m_State.Initialized) {
			throw std::runtime_error("motion priority cursor requires initial seek before forward traversal");
		}

		while (m_State.Index < m_Timeline.Priority.size() - 2 && position >= m_State.FollowingTime) {
			Select(m_State.Index + 1);
		}
	}

	void MotionPriorityCursor::Reverse(float position) {
		RequirePosition(position);
		if (!m_State.Initialized) {
			throw std::runtime_error("motion priority cursor requires initial seek before reverse traversal");
		}

		while (position < m_State.CurrentTime) {
			Select(m_State.Index - 1);
		}
	}

	const MotionPriorityCursorState& MotionPriorityCursor::State() const noexcept {
		return m_State;
	}

	std::span<const std::uint32_t> MotionPriorityCursor::DrawOrder() const noexcept {
		if (!m_State.Initialized) {
			return {};
		}

		return m_Timeline.Priority[m_State.Index].DrawOrder;
	}

} // namespace sgre::scene
