#include "sgre/scene/captured_opacity_animation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::scene {
	static constexpr std::size_t kMaximumOpacityAnimations = 1024;

	static std::uint32_t OpacityPower(std::uint32_t value, std::uint32_t factor, std::uint32_t denominator, std::uint32_t iterations) {
		if (denominator == 0) {
			throw std::runtime_error("Captured opacity curve has no actual integer interval");
		}
		for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
			// Native5358a0 keeps the low32 IMUL product before zero-extension and signed64 IDIV.
			const std::uint32_t product = value * factor;
			value = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) / denominator);
		}

		return value;
	}

	static std::uint32_t OpacityOrdinal(std::uint32_t ordinal, std::uint32_t count, std::int32_t curve) {
		if (curve == 0) {
			return ordinal;
		}
		const auto last = count - 1;
		if (curve >= -10 && curve <= 10) {
			if (curve > 0) {
				return OpacityPower(ordinal, ordinal, last, static_cast<std::uint32_t>(curve));
			}
			const auto complement = last - ordinal;

			return last - OpacityPower(complement, complement, last, static_cast<std::uint32_t>(-curve));
		}
		const auto half = count >> 1;
		if (curve > 10) {
			const auto power = static_cast<std::uint32_t>(curve - 10);
			if (ordinal < half) {
				return OpacityPower(ordinal, ordinal, half - 1, power);
			}
			const auto complement = last - ordinal;

			return last - OpacityPower(complement, complement, count - half - 1, power);
		}
		const auto power = static_cast<std::uint32_t>(-10 - curve);
		if (ordinal < half) {
			const auto complement = half - 1 - ordinal;

			return half - 1 - OpacityPower(complement, complement, half - 1, power);
		}
		const auto relative = ordinal - half;

		return half + OpacityPower(relative, relative, count - half - 1, power);
	}

	CapturedOpacityAnimation::CapturedOpacityAnimation(float initial, std::int32_t target, std::int32_t duration, std::int32_t curve) : m_Initial(initial), m_Target(static_cast<float>(target)), m_Duration(static_cast<std::uint32_t>(duration)), m_Count(duration == 0 ? 1u : static_cast<std::uint32_t>(duration) * 16u), m_Curve(curve) {
		if (!std::isfinite(initial) || initial < 0 || initial > 255 || duration < 0 || duration > std::numeric_limits<std::int32_t>::max() / 16 || curve < -64 || curve > 64) {
			throw std::runtime_error("Captured opacity animation requires bounded native duration/curve and actual clamped initial value");
		}
	}

	std::optional<float> CapturedOpacityAnimation::Advance(float actual_frame_ticks) {
		if (m_State.Complete) {
			return {};
		}
		if (m_Duration == 0) {
			m_State.Sampled = m_Target;
			m_State.Complete = true;

			return m_Target;
		}
		if (!std::isfinite(actual_frame_ticks) || actual_frame_ticks < 1 || actual_frame_ticks > 4294967296.0f) {
			throw std::runtime_error("Captured opacity animation requires its bounded actual shared frame delta");
		}
		m_State.Elapsed += static_cast<std::uint64_t>(actual_frame_ticks);
		if (m_State.Elapsed >= m_Duration) {
			m_State.Elapsed = m_Duration;
			m_State.Complete = true;
		}
		const auto base = static_cast<std::uint32_t>((static_cast<std::uint64_t>(m_Count - 1) * m_State.Elapsed) / m_Duration);
		const auto ordinal = OpacityOrdinal(base, m_Count, m_Curve);
		if (m_State.Ordinal && *m_State.Ordinal == ordinal) {
			return {};
		}
		m_State.Ordinal = ordinal;
		m_State.Sampled = m_Target;
		if (ordinal != m_Count - 1) {
			const auto ratio = static_cast<float>(ordinal) / static_cast<float>(m_Count - 1);
			m_State.Sampled = (1.0f - ratio) * m_Initial + ratio * m_Target;
		}

		return m_State.Sampled;
	}

	CapturedOpacityAnimationState CapturedOpacityAnimation::State() const noexcept {
		return m_State;
	}

	struct CapturedOpacityAnimations::Entry {
		explicit Entry(CapturedOpacityAnimation animation) : Animation(std::move(animation)) {
		}
		CapturedOpacityAnimation Animation;
		ParallelTaskHandle Handle;
	};

	CapturedOpacityAnimations::CapturedOpacityAnimations(CapturedTask& actual_task, const core::FrameClock& actual_clock, ParallelTaskSet& actual_common_first) : m_Task(actual_task), m_Clock(actual_clock), m_Parent(actual_common_first) {
		m_Entries.reserve(kMaximumOpacityAnimations);
	}

	void CapturedOpacityAnimations::Append(std::int32_t target, std::int32_t duration, std::int32_t curve) {
		m_Parent.RequireIdleTraversal();
		if (m_Task.State().Closed || !m_Task.CanClose()) {
			throw std::runtime_error("Captured opacity animation requires its actual idle camera-child owner");
		}
		auto entry = std::make_shared<Entry>(CapturedOpacityAnimation(m_Task.State().Opacity, target, duration, curve));
		std::erase_if(m_Entries, [](const auto& previous) {
			return previous.expired();
		});
		if (m_Entries.size() == kMaximumOpacityAnimations) {
			throw std::runtime_error("Captured opacity animation task count exceeds its bounded actual CommonFirst group");
		}
		ParallelTaskCallbacks callbacks;
		callbacks.Update = [entry, &task = m_Task, &clock = m_Clock, &parent = m_Parent](auto handle) {
			const auto ticks = clock.VariableFrame() ? std::max(clock.VariableStep(), 1.0f) : 1.0f;
			if (const auto value = entry->Animation.Advance(ticks)) {
				task.SetOpacity(*value);
			}
			if (entry->Animation.State().Complete) {
				parent.SetStatus(handle, TaskStatus::Retire);
			}
		};
		callbacks.Draw = [](auto, auto) {
		};
		callbacks.Release = [](auto) {
		};
		for (const auto& weak : m_Entries) {
			if (const auto previous = weak.lock(); previous && m_Parent.Contains(previous->Handle)) {
				// Native269e50 compares SQAnimateHandler type0 before appending the new handler at the tail.
				m_Parent.SetStatus(previous->Handle, TaskStatus::Retire);
			}
		}
		entry->Handle = m_Parent.Append(std::move(callbacks));
		m_Entries.push_back(entry);
	}
} // namespace sgre::scene
