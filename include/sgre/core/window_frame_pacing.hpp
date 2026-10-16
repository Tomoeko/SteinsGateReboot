#pragma once

#include "sgre/core/application_event_control.hpp"

#include <cstdint>
#include <thread>

namespace sgre::core {

	class ApplicationClockPump;

	struct WindowFramePacingState {
		std::int64_t CarryTicks = 0;
		std::int64_t PreviousCounter = 0;
		std::int64_t IntervalTicks = 0;
		std::int64_t EarlyMarginTicks = 0;
		std::uint64_t ActiveCalls = 0;
		std::int64_t PreviousLatenessCounter = 0;
		std::int64_t AccumulatedLatenessTicks = 0;
		bool EscapePressed = false;
	};

	enum class WindowPacingSample {
		RequirePrecisionWaitOneMillisecond,
		ReadyForPresentation
	};

	class WindowFramePacing {
	public:
		// The actual event registry encloses this window pacing owner. Inputs are its real signed counter/frequency samples.
		// OS dispatch, precision waits and graphics presentation remain required containing operations.
		WindowFramePacing(ApplicationEventControl& control, std::int64_t actual_frequency, std::int64_t actual_initial_counter);
		~WindowFramePacing() noexcept;
		WindowFramePacing(const WindowFramePacing&) = delete;
		WindowFramePacing& operator=(const WindowFramePacing&) = delete;
		void SetEscapePressed(bool pressed);
		// Call after actual OS event dispatch. An inactive gate still requires the final raw counter sample.
		[[nodiscard]] bool BeginAfterEventDispatch();
		[[nodiscard]] WindowPacingSample ObservePacingCounter(std::int64_t actual_counter);
		[[nodiscard]] bool ForceUnsynchronizedPresent() const;
		void RecordAfterPresentationCounter(std::int64_t actual_counter);
		[[nodiscard]] WindowFramePacingState State() const;
		[[nodiscard]] bool UsesControl(const ApplicationEventControl& control) const;
		[[nodiscard]] bool CanClose() const noexcept;
		void Close();

	private:
		friend class ApplicationClockPump;
		// Authored exception cleanup invalidates an incomplete decision; it does not publish a completed native cycle.
		void AbandonCycle() noexcept;
		enum class Phase {
			Idle,
			Pacing,
			AfterPresentation
		};

		void RequireOpen() const;
		ApplicationEventControl& m_Control;
		WindowFramePacingState m_State;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		Phase m_Phase = Phase::Idle;
		bool m_ActiveGate = false;
		bool m_ForceUnsynchronized = false;
		bool m_Closed = false;
	};

} // namespace sgre::core
