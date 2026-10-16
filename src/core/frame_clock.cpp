#include "sgre/core/frame_clock.hpp"

#include <algorithm>

namespace sgre::core {

	FrameClock::FrameClock(std::uint64_t initial_milliseconds) : m_PreviousMilliseconds(initial_milliseconds) {
	}

	FrameClock FrameClock::NativeLooper(std::uint64_t initial_milliseconds) {
		FrameClock clock(initial_milliseconds);
		clock.m_VariableStep = 0;

		return clock;
	}

	void FrameClock::SetVariableFrame(bool enabled) {
		m_VariableFrame = enabled;
	}

	bool FrameClock::VariableFrame() const {
		return m_VariableFrame;
	}

	std::int32_t FrameClock::Advance(std::uint64_t milliseconds) {
		const auto elapsed = static_cast<std::uint32_t>(milliseconds) - static_cast<std::uint32_t>(m_PreviousMilliseconds);
		float ticks = static_cast<float>(elapsed);
		ticks = ticks * 60.0f;
		ticks = ticks / 1000.0f;
		ticks = ticks + m_ResidualTicks;
		const auto rounded = static_cast<std::int32_t>(ticks + 0.5f);
		const auto step = std::max(std::int32_t{1}, rounded);
		m_VariableStep = static_cast<float>(step);
		m_ResidualTicks = std::max(ticks - m_VariableStep, -2.0f);
		m_PreviousMilliseconds = milliseconds;

		return m_VariableFrame ? static_cast<std::int32_t>(m_VariableStep) : 1;
	}

	float FrameClock::VariableStep() const {
		return m_VariableStep;
	}

	float FrameClock::ResidualTicks() const {
		return m_ResidualTicks;
	}

} // namespace sgre::core
