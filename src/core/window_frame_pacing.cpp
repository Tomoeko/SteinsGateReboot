#include "sgre/core/window_frame_pacing.hpp"

#include <bit>
#include <exception>
#include <limits>
#include <stdexcept>

namespace sgre::core {

	static std::int64_t SubtractCounterTicks(std::int64_t first, std::int64_t second) {
		return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(first) - std::bit_cast<std::uint64_t>(second));
	}

	static void RequireCounter(std::int64_t counter) {
		if (counter < 0) {
			throw std::runtime_error("window frame pacing counter is outside the recovered nonnegative signed domain");
		}
	}

	WindowFramePacing::WindowFramePacing(ApplicationEventControl& control, std::int64_t actual_frequency, std::int64_t actual_initial_counter) : m_Control(control) {
		if (actual_frequency <= 0 || actual_frequency > std::numeric_limits<std::int64_t>::max() - 500) {
			throw std::runtime_error("window frame pacing frequency is outside the recovered positive signed arithmetic domain");
		}
		RequireCounter(actual_initial_counter);
		m_Control.RequireCanFlush();
		m_State.PreviousCounter = actual_initial_counter;
		m_State.EarlyMarginTicks = (actual_frequency + 500) / 1000;
		auto interval = static_cast<double>(actual_frequency);
		interval = interval + 30.0;
		interval = interval / 60.0;
		m_State.IntervalTicks = static_cast<std::int64_t>(interval);
	}

	WindowFramePacing::~WindowFramePacing() noexcept {
		if (!CanClose()) {
			std::terminate();
		}
	}

	void WindowFramePacing::RequireOpen() const {
		if (m_Closed || std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("window frame pacing requires its live serialized owner thread");
		}
		m_Control.RequireCanFlush();
	}

	void WindowFramePacing::SetEscapePressed(bool pressed) {
		RequireOpen();
		if (m_Phase != Phase::Idle) {
			throw std::runtime_error("window Escape state changes require the event dispatch phase");
		}

		m_State.EscapePressed = pressed;
	}

	bool WindowFramePacing::BeginAfterEventDispatch() {
		RequireOpen();
		if (m_Phase != Phase::Idle) {
			throw std::runtime_error("window frame pacing cannot begin another unfinished cycle");
		}

		// Native getter calls acquire separate locks and short-circuit in this order.
		m_ActiveGate = !m_Control.State().Suspended || m_Control.State().CloseAccepted || m_Control.State().Quit;
		m_ForceUnsynchronized = false;
		if (m_ActiveGate) {
			++m_State.ActiveCalls;
			m_ForceUnsynchronized = !m_State.EscapePressed;
			m_Phase = Phase::Pacing;
		} else {
			m_Phase = Phase::AfterPresentation;
		}

		return m_ActiveGate;
	}

	WindowPacingSample WindowFramePacing::ObservePacingCounter(std::int64_t actual_counter) {
		RequireOpen();
		if (m_Phase != Phase::Pacing) {
			throw std::runtime_error("window pacing counter sample requires its active pacing phase");
		}
		RequireCounter(actual_counter);

		if (m_State.IntervalTicks <= 0) {
			m_State.PreviousCounter = actual_counter;
			m_State.CarryTicks = 0;
		} else {
			auto elapsed = SubtractCounterTicks(SubtractCounterTicks(actual_counter, m_State.CarryTicks), m_State.PreviousCounter);
			if (actual_counter < m_State.PreviousCounter) {
				elapsed = SubtractCounterTicks(elapsed, 2);
			}
			if (elapsed < m_State.IntervalTicks - m_State.EarlyMarginTicks) {
				return WindowPacingSample::RequirePrecisionWaitOneMillisecond;
			}

			m_State.CarryTicks = SubtractCounterTicks(elapsed, m_State.IntervalTicks);
			if (m_State.CarryTicks >= m_State.IntervalTicks) {
				m_ForceUnsynchronized = true;
				m_State.CarryTicks = m_State.CarryTicks % m_State.IntervalTicks;
			}
			m_State.PreviousCounter = actual_counter;
		}
		m_Phase = Phase::AfterPresentation;

		return WindowPacingSample::ReadyForPresentation;
	}

	bool WindowFramePacing::ForceUnsynchronizedPresent() const {
		RequireOpen();
		if (m_Phase != Phase::AfterPresentation || !m_ActiveGate) {
			throw std::runtime_error("window presentation decision requires its completed active pacing phase");
		}

		return m_ForceUnsynchronized;
	}

	void WindowFramePacing::RecordAfterPresentationCounter(std::int64_t actual_counter) {
		RequireOpen();
		if (m_Phase != Phase::AfterPresentation) {
			throw std::runtime_error("window lateness sample requires its completed pacing/presentation phase");
		}
		RequireCounter(actual_counter);

		if (m_State.PreviousLatenessCounter == 0) {
			m_State.PreviousLatenessCounter = SubtractCounterTicks(actual_counter, m_State.IntervalTicks);
		}
		const auto lateness = SubtractCounterTicks(SubtractCounterTicks(actual_counter, m_State.PreviousLatenessCounter), m_State.IntervalTicks);
		m_State.AccumulatedLatenessTicks = std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(m_State.AccumulatedLatenessTicks) + std::bit_cast<std::uint64_t>(lateness));
		m_State.PreviousLatenessCounter = actual_counter;
		m_Phase = Phase::Idle;
		m_ActiveGate = false;
	}

	WindowFramePacingState WindowFramePacing::State() const {
		RequireOpen();

		return m_State;
	}

	bool WindowFramePacing::UsesControl(const ApplicationEventControl& control) const {
		RequireOpen();

		return &m_Control == &control;
	}

	bool WindowFramePacing::CanClose() const noexcept {
		return std::this_thread::get_id() == m_OwnerThread && (m_Closed || m_Phase == Phase::Idle);
	}

	void WindowFramePacing::AbandonCycle() noexcept {
		m_Phase = Phase::Idle;
		m_ActiveGate = false;
		m_ForceUnsynchronized = false;
	}

	void WindowFramePacing::Close() {
		if (!CanClose()) {
			throw std::runtime_error("window frame pacing must finish its current cycle before close");
		}

		m_Closed = true;
	}

} // namespace sgre::core
