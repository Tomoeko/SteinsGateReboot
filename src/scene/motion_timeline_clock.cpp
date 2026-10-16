#include "sgre/scene/motion_timeline_clock.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>

namespace sgre::scene {

	static std::string_view NativeTickLabel(std::string_view label) {
		return label.substr(0, label.find('\0'));
	}

	void MotionTimelineClock::ApplyTimeline(const resource::MotionTimeline& timeline, MotionTimelineContinuation continuation) {
		if (timeline.Kind != resource::MotionTimelineKind::Frames || !std::isfinite(timeline.LastTime) || !std::isfinite(timeline.LoopTime) || timeline.LastTime < 0 || timeline.LoopTime < -1 || timeline.LoopTime > timeline.LastTime) {
			throw std::runtime_error("motion clock requires an ordinary timeline with bounded terminal/loop times");
		}

		if (continuation != MotionTimelineContinuation::Reset && continuation != MotionTimelineContinuation::Chain) {
			throw std::runtime_error("unsupported motion timeline continuation");
		}

		m_Timeline = &timeline;
		m_State.LastTime = timeline.LastTime;
		m_State.LoopTime = timeline.LoopTime;
		m_State.Playing = true;
		m_State.SyncWaiting = false;
		if (continuation == MotionTimelineContinuation::Reset) {
			SetTickCount(0);
		} else {
			m_State.InitialTagDispatchPending = true;
		}
	}

	void MotionTimelineClock::InvalidateSelection() noexcept {
		m_Timeline = nullptr;
		m_State.Playing = false;
	}

	void MotionTimelineClock::SetTickCount(float ticks) {
		if (m_Timeline == nullptr) {
			return;
		}

		AssignTickCount(ticks);
	}

	void MotionTimelineClock::AssignTickCount(float ticks) {
		// SSE maxss with zero as its second operand converts NaN, negative values and signed zero to +0.
		m_State.TickCount = ticks > 0 ? ticks : 0;
		m_State.EvaluationTime = m_State.LastTime > m_State.TickCount ? m_State.TickCount : m_State.LastTime;
		m_State.TickAssigned = true;
		m_State.InitialTagDispatchPending = true;
	}

	bool MotionTimelineClock::SetTickLabel(std::string_view label) {
		if (m_Timeline == nullptr) {
			return false;
		}

		label = NativeTickLabel(label);
		for (const auto& tag : m_Timeline->Tags) {
			if (tag.Type != 1) {
				continue;
			}

			const auto* serialized = tag.Content.Find("label");
			const auto* name = serialized == nullptr ? nullptr : std::get_if<std::string>(&serialized->Data);
			if (name == nullptr) {
				throw std::runtime_error("motion tick label requires a tag content label string");
			}

			if (NativeTickLabel(*name) != label) {
				continue;
			}

			if (tag.Time < 0) {
				return false;
			}

			SetTickCount(tag.Time);

			return true;
		}

		return false;
	}

	void MotionTimelineClock::SetSpeed(float speed) noexcept {
		m_State.Speed = speed;
	}

	void MotionTimelineClock::Stop() noexcept {
		m_State.Playing = false;
	}

	bool MotionTimelineClock::HasTimeline() const noexcept {
		return m_Timeline != nullptr;
	}

	const MotionTimelineClockState& MotionTimelineClock::State() const noexcept {
		return m_State;
	}

} // namespace sgre::scene
