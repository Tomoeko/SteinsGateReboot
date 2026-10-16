#pragma once

#include "sgre/core/application_event_control.hpp"
#include "sgre/core/window_frame_pacing.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>

namespace sgre::platform {
	class MacApplicationPump;
}

namespace sgre::core {

	class LooperFrame;
	class ApplicationAuxiliaryRegistration;

	// A concrete platform owner supplies these recovered boundaries; there is no default OS/backend implementation.
	class ApplicationPumpPlatform {
	public:
		virtual ~ApplicationPumpPlatform() = default;
		virtual void RequireLive() const = 0;
		[[nodiscard]] virtual std::int64_t Frequency() const = 0;
		[[nodiscard]] virtual std::int64_t ReadRawCounter() = 0;
		virtual void DispatchEvents(ApplicationEventControl& events, WindowFramePacing& pacing) = 0;
		virtual void PrecisionWaitOneMillisecond() = 0;
		virtual void SuspendedWaitSixteenMilliseconds() = 0;
		[[nodiscard]] virtual bool HasBoundPresentationTarget() const = 0;
		virtual void Present(bool force_unsynchronized) = 0;
	};

	class ApplicationClockPump {
	public:
		// The actual event registry and platform owner enclose this stable pump and its attached typed Looper.
		// Initialization performs native535e30 reset/frequency/counter work, then initializes genuine window pacing.
		ApplicationClockPump(ApplicationEventControl& events, ApplicationPumpPlatform& actual_platform, std::size_t maximum_boundary_visits = 65536);
		~ApplicationClockPump() noexcept;
		ApplicationClockPump(const ApplicationClockPump&) = delete;
		ApplicationClockPump& operator=(const ApplicationClockPump&) = delete;
		[[nodiscard]] WindowFramePacing& Pacing();
		// Exact current-frame binding observes these actual attached owners, without changing clock/event state.
		void RequireAttachment(const LooperFrame& actual_looper, const ApplicationPumpPlatform& actual_platform) const;
		void RequirePresentationPhase(const LooperFrame& actual_looper, const ApplicationPumpPlatform& actual_platform) const;
		// Native535e70 owns the callable wrapper. Captured concrete receiver/resource owners outlive its registration.
		void SetAuxiliary(std::function<void(bool)> actual_callback, std::function<void()> actual_receiver_preflight);
		void ClearAuxiliary();
		[[nodiscard]] bool CanClose() const noexcept;
		void Close();

	private:
		friend class LooperFrame;
		friend class ApplicationAuxiliaryRegistration;
		friend class platform::MacApplicationPump;
		struct InitialClock {
			std::int64_t Frequency = 0;
			std::int64_t WindowCounter = 0;
			std::uint64_t Milliseconds = 0;
		};

		ApplicationClockPump(ApplicationEventControl& events, ApplicationPumpPlatform& actual_platform, std::size_t maximum_boundary_visits, InitialClock initial);
		[[nodiscard]] static InitialClock InitializeClock(ApplicationEventControl& events, ApplicationPumpPlatform& actual_platform, std::size_t maximum_boundary_visits);
		void RequireOpen() const;
		void RegisterAuxiliary(ApplicationAuxiliaryRegistration& actual_owner, std::function<void(bool)> actual_callback, std::function<void()> actual_receiver_preflight);
		void RetireAuxiliary(const ApplicationAuxiliaryRegistration& actual_owner);
		void RequirePresentationBorrowAdmission(const ApplicationPumpPlatform& actual_platform) const;
		void RegisterPresentationBorrow(const ApplicationPumpPlatform& actual_platform) noexcept;
		void RequirePresentationBorrowRetirement(const ApplicationPumpPlatform& actual_platform) const;
		void ReleasePresentationBorrow(const ApplicationPumpPlatform& actual_platform) noexcept;
		void VisitBoundary();
		void PollWindow();
		void InvokeAuxiliary(bool suspended);
		void Attach(LooperFrame& looper);
		[[nodiscard]] bool CanDetach(const LooperFrame& actual_looper) const noexcept;
		void Detach(LooperFrame& looper);
		void Run(LooperFrame& looper);
		[[nodiscard]] std::function<void(LooperFrame&)> Callback();
		ApplicationEventControl& m_Events;
		ApplicationPumpPlatform& m_Platform;
		WindowFramePacing m_Pacing;
		std::function<void(bool)> m_Auxiliary;
		std::function<void()> m_AuxiliaryPreflight;
		LooperFrame* m_Looper = nullptr;
		const ApplicationPumpPlatform* m_PresentationBorrow = nullptr;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		const std::int64_t m_Frequency;
		const std::uint64_t m_InitialMilliseconds;
		const std::size_t m_MaximumBoundaryVisits;
		std::size_t m_BoundaryVisits = 0;
		bool m_Running = false;
		bool m_Presenting = false;
		bool m_Closed = false;
		const ApplicationAuxiliaryRegistration* m_AuxiliaryOwner = nullptr;
	};

	class ApplicationAuxiliaryRegistration {
	public:
		// This exact token owns one real callable/preflight pair in the actual pump's otherwise vacant auxiliary slot.
		// Pump and captured receiver services enclose this stable token; retire it before those owners.
		ApplicationAuxiliaryRegistration(ApplicationClockPump& actual_pump, std::function<void(bool)> actual_callback, std::function<void()> actual_receiver_preflight);
		~ApplicationAuxiliaryRegistration() noexcept;
		ApplicationAuxiliaryRegistration(const ApplicationAuxiliaryRegistration&) = delete;
		ApplicationAuxiliaryRegistration& operator=(const ApplicationAuxiliaryRegistration&) = delete;
		ApplicationAuxiliaryRegistration(ApplicationAuxiliaryRegistration&&) = delete;
		ApplicationAuxiliaryRegistration& operator=(ApplicationAuxiliaryRegistration&&) = delete;
		[[nodiscard]] bool IsAttached() const noexcept;
		void Close();

	private:
		ApplicationClockPump& m_Pump;
		bool m_Closed = false;
	};

} // namespace sgre::core
