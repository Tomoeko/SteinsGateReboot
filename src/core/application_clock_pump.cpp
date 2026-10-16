#include "sgre/core/application_clock_pump.hpp"

#include "sgre/core/looper_frame.hpp"
#include "sgre/core/performance_counter.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::core {

	ApplicationClockPump::InitialClock ApplicationClockPump::InitializeClock(ApplicationEventControl& events, ApplicationPumpPlatform& actual_platform, std::size_t maximum_boundary_visits) {
		if (maximum_boundary_visits == 0 || maximum_boundary_visits > 65536) {
			throw std::runtime_error("application clock pump requires a bounded positive platform boundary budget");
		}
		events.RequireCanFlush();
		actual_platform.RequireLive();
		events.Reset();
		InitialClock initial;
		initial.Frequency = actual_platform.Frequency();
		initial.Milliseconds = PerformanceCounterMilliseconds(actual_platform.ReadRawCounter(), initial.Frequency);
		initial.WindowCounter = actual_platform.ReadRawCounter();
		if (actual_platform.Frequency() != initial.Frequency) {
			throw std::runtime_error("application clock pump requires the same actual immutable counter frequency for clock and window pacing");
		}

		return initial;
	}

	ApplicationClockPump::ApplicationClockPump(ApplicationEventControl& events, ApplicationPumpPlatform& actual_platform, std::size_t maximum_boundary_visits) : ApplicationClockPump(events, actual_platform, maximum_boundary_visits, InitializeClock(events, actual_platform, maximum_boundary_visits)) {
	}

	ApplicationClockPump::ApplicationClockPump(ApplicationEventControl& events, ApplicationPumpPlatform& actual_platform, std::size_t maximum_boundary_visits, InitialClock initial) : m_Events(events), m_Platform(actual_platform), m_Pacing(events, initial.Frequency, initial.WindowCounter), m_Frequency(initial.Frequency), m_InitialMilliseconds(initial.Milliseconds), m_MaximumBoundaryVisits(maximum_boundary_visits) {
		if (maximum_boundary_visits == 0 || maximum_boundary_visits > 65536) {
			throw std::runtime_error("application clock pump requires a bounded positive platform boundary budget");
		}
	}

	ApplicationClockPump::~ApplicationClockPump() noexcept {
		if (!CanClose()) {
			std::terminate();
		}
	}

	void ApplicationClockPump::RequireOpen() const {
		if (m_Closed || std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("application clock pump requires its live serialized owner thread");
		}
		m_Events.RequireCanFlush();
	}

	WindowFramePacing& ApplicationClockPump::Pacing() {
		RequireOpen();

		return m_Pacing;
	}

	void ApplicationClockPump::RequireAttachment(const LooperFrame& actual_looper, const ApplicationPumpPlatform& actual_platform) const {
		RequireOpen();
		if (m_Looper != &actual_looper || &m_Platform != &actual_platform) {
			throw std::runtime_error("application presentation requires its exact attached Looper and platform owners");
		}
		// Native final wait/presentation remains valid after root retirement, before containing detach.
		static_cast<void>(actual_looper.FixedBaseCount());
	}

	void ApplicationClockPump::RequirePresentationPhase(const LooperFrame& actual_looper, const ApplicationPumpPlatform& actual_platform) const {
		RequireAttachment(actual_looper, actual_platform);
		if (!m_Running || !m_Presenting || !actual_looper.InWait() || m_PresentationBorrow != &actual_platform) {
			throw std::runtime_error("application presentation requires its exact bound platform call during the actual attached Looper wait");
		}
	}

	void ApplicationClockPump::RequirePresentationBorrowAdmission(const ApplicationPumpPlatform& actual_platform) const {
		RequireOpen();
		if (m_Running || m_Presenting || m_Looper == nullptr || &actual_platform != &m_Platform || m_PresentationBorrow != nullptr) {
			throw std::runtime_error("application presentation binding requires its actual quiescent attached clock and exclusive platform borrower");
		}
		RequireAttachment(*m_Looper, actual_platform);
	}

	void ApplicationClockPump::RegisterPresentationBorrow(const ApplicationPumpPlatform& actual_platform) noexcept {
		// The exact Mac borrower preflights both owners before this final nonthrowing publication.
		if (m_Closed || std::this_thread::get_id() != m_OwnerThread || m_Running || m_Presenting || m_Looper == nullptr || &actual_platform != &m_Platform || m_PresentationBorrow != nullptr) {
			std::terminate();
		}
		m_PresentationBorrow = &actual_platform;
	}

	void ApplicationClockPump::RequirePresentationBorrowRetirement(const ApplicationPumpPlatform& actual_platform) const {
		RequireOpen();
		if (m_Running || m_Presenting || m_PresentationBorrow != &actual_platform || &actual_platform != &m_Platform) {
			throw std::runtime_error("application presentation retirement requires its exact quiescent platform borrower");
		}
	}

	void ApplicationClockPump::ReleasePresentationBorrow(const ApplicationPumpPlatform& actual_platform) noexcept {
		if (m_Closed || std::this_thread::get_id() != m_OwnerThread || m_Running || m_Presenting || m_PresentationBorrow != &actual_platform || &actual_platform != &m_Platform) {
			std::terminate();
		}
		m_PresentationBorrow = nullptr;
	}

	void ApplicationClockPump::VisitBoundary() {
		if (m_BoundaryVisits == m_MaximumBoundaryVisits) {
			throw std::runtime_error("application clock pump exceeded its bounded actual platform work");
		}

		++m_BoundaryVisits;
	}

	void ApplicationClockPump::PollWindow() {
		VisitBoundary();
		m_Platform.DispatchEvents(m_Events, m_Pacing);
		const auto active = m_Pacing.BeginAfterEventDispatch();
		if (active) {
			while (true) {
				VisitBoundary();
				if (m_Pacing.ObservePacingCounter(m_Platform.ReadRawCounter()) == WindowPacingSample::ReadyForPresentation) {
					break;
				}

				VisitBoundary();
				m_Platform.PrecisionWaitOneMillisecond();
			}
			if (m_Platform.HasBoundPresentationTarget()) {
				VisitBoundary();
				const bool force_unsynchronized = m_Pacing.ForceUnsynchronizedPresent();
				m_Presenting = true;
				try {
					m_Platform.Present(force_unsynchronized);
					m_Presenting = false;
				} catch (...) {
					m_Presenting = false;
					throw;
				}
			}
		}
		VisitBoundary();
		m_Pacing.RecordAfterPresentationCounter(m_Platform.ReadRawCounter());
	}

	void ApplicationClockPump::InvokeAuxiliary(bool suspended) {
		if (m_Auxiliary) {
			VisitBoundary();
			m_AuxiliaryPreflight();
			m_Auxiliary(suspended);
		}
	}

	void ApplicationClockPump::Run(LooperFrame& looper) {
		RequireOpen();
		if (m_Running || m_Looper != &looper || !looper.InWait()) {
			throw std::runtime_error("application clock pump requires its exact attached Looper wait phase");
		}

		m_Running = true;
		m_BoundaryVisits = 0;
		try {
			m_Platform.RequireLive();
			PollWindow();
			bool sample_clock = !m_Events.State().Suspended;
			while (!sample_clock && !m_Events.State().CloseAccepted) {
				if (m_Events.State().Quit || m_Events.State().WaitBreak) {
					break;
				}

				InvokeAuxiliary(true);
				VisitBoundary();
				m_Platform.SuspendedWaitSixteenMilliseconds();
				m_Events.Flush();
				PollWindow();
				sample_clock = !m_Events.State().Suspended;
			}
			if (sample_clock) {
				VisitBoundary();
				static_cast<void>(looper.Clock().Advance(PerformanceCounterMilliseconds(m_Platform.ReadRawCounter(), m_Frequency)));
				InvokeAuxiliary(false);
			}

			if (m_Events.State().Quit) {
				looper.MarkCurrentTaskRetire();
			} else if (m_Events.State().CloseAccepted) {
				m_Events.ClearClose();
				m_Events.RequestQuit();
			}
			m_Events.Flush();
			m_Running = false;
		} catch (...) {
			m_Pacing.AbandonCycle();
			m_Running = false;
			throw;
		}
	}

	std::function<void(LooperFrame&)> ApplicationClockPump::Callback() {
		RequireOpen();

		return [this](LooperFrame& looper) {
			Run(looper);
		};
	}

	void ApplicationClockPump::Attach(LooperFrame& looper) {
		RequireOpen();
		if (m_Running || m_Looper != nullptr || !looper.HasTask()) {
			throw std::runtime_error("application clock pump attachment requires its exact unbound actual root phase");
		}
		m_Running = true;
		try {
			m_Platform.RequireLive();
			m_Looper = &looper;
			m_Running = false;
		} catch (...) {
			m_Running = false;
			throw;
		}
	}

	bool ApplicationClockPump::CanDetach(const LooperFrame& actual_looper) const noexcept {
		return !m_Running && !m_Presenting && m_PresentationBorrow == nullptr && m_AuxiliaryOwner == nullptr && m_Looper == &actual_looper && std::this_thread::get_id() == m_OwnerThread;
	}

	void ApplicationClockPump::Detach(LooperFrame& looper) {
		if (!CanDetach(looper)) {
			throw std::runtime_error("application clock pump detachment requires its quiescent exact Looper owner and retired presentation/auxiliary borrowers");
		}

		m_Running = true;
		m_Auxiliary = {};
		m_AuxiliaryPreflight = {};
		m_Looper = nullptr;
		m_Running = false;
	}

	void ApplicationClockPump::SetAuxiliary(std::function<void(bool)> actual_callback, std::function<void()> actual_receiver_preflight) {
		RequireOpen();
		if (m_AuxiliaryOwner != nullptr) {
			throw std::runtime_error("application auxiliary callback is owned by its exact live registration");
		}
		if (m_Running || !actual_callback || !actual_receiver_preflight) {
			throw std::runtime_error("application auxiliary registration requires a quiescent actual live receiver");
		}
		m_Running = true;
		try {
			actual_receiver_preflight();
			m_Auxiliary = std::move(actual_callback);
			m_AuxiliaryPreflight = std::move(actual_receiver_preflight);
			m_Running = false;
		} catch (...) {
			m_Running = false;
			throw;
		}
	}

	void ApplicationClockPump::ClearAuxiliary() {
		RequireOpen();
		if (m_AuxiliaryOwner != nullptr) {
			throw std::runtime_error("application auxiliary callback is owned by its exact live registration");
		}
		if (m_Running) {
			throw std::runtime_error("application auxiliary retirement requires a quiescent callback phase");
		}

		m_Running = true;
		m_Auxiliary = {};
		m_AuxiliaryPreflight = {};
		m_Running = false;
	}

	void ApplicationClockPump::RegisterAuxiliary(ApplicationAuxiliaryRegistration& actual_owner, std::function<void(bool)> actual_callback, std::function<void()> actual_receiver_preflight) {
		RequireOpen();
		if (m_Running || m_Presenting || m_AuxiliaryOwner != nullptr || m_Auxiliary || m_AuxiliaryPreflight || !actual_callback || !actual_receiver_preflight) {
			throw std::runtime_error("owned application auxiliary registration requires its actual vacant inactive slot and concrete receiver callbacks");
		}

		actual_receiver_preflight();
		if (m_Running || m_Presenting || m_AuxiliaryOwner != nullptr || m_Auxiliary || m_AuxiliaryPreflight) {
			throw std::runtime_error("application auxiliary slot changed during its actual receiver preflight");
		}

		RequireOpen();
		m_Auxiliary = std::move(actual_callback);
		m_AuxiliaryPreflight = std::move(actual_receiver_preflight);
		m_AuxiliaryOwner = &actual_owner;
	}

	void ApplicationClockPump::RetireAuxiliary(const ApplicationAuxiliaryRegistration& actual_owner) {
		RequireOpen();
		if (m_Running || m_Presenting || m_AuxiliaryOwner != &actual_owner || !m_Auxiliary || !m_AuxiliaryPreflight) {
			throw std::runtime_error("application auxiliary retirement requires its exact inactive actual registration");
		}

		m_Running = true;
		m_Auxiliary = {};
		m_AuxiliaryPreflight = {};
		m_AuxiliaryOwner = nullptr;
		m_Running = false;
	}

	bool ApplicationClockPump::CanClose() const noexcept {
		return std::this_thread::get_id() == m_OwnerThread && m_AuxiliaryOwner == nullptr && m_PresentationBorrow == nullptr && !m_Presenting && (m_Closed || (!m_Running && m_Looper == nullptr && m_Pacing.CanClose()));
	}

	void ApplicationClockPump::Close() {
		if (!CanClose()) {
			throw std::runtime_error("application clock pump requires completed window pacing and retired Looper/presentation borrowers");
		}
		if (m_Closed) {
			return;
		}

		m_Running = true;
		try {
			m_Auxiliary = {};
			m_AuxiliaryPreflight = {};
			m_Pacing.Close();
			m_Closed = true;
			m_Running = false;
		} catch (...) {
			m_Running = false;
			throw;
		}
	}

	ApplicationAuxiliaryRegistration::ApplicationAuxiliaryRegistration(ApplicationClockPump& actual_pump, std::function<void(bool)> actual_callback, std::function<void()> actual_receiver_preflight) : m_Pump(actual_pump) {
		m_Pump.RegisterAuxiliary(*this, std::move(actual_callback), std::move(actual_receiver_preflight));
	}

	ApplicationAuxiliaryRegistration::~ApplicationAuxiliaryRegistration() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	bool ApplicationAuxiliaryRegistration::IsAttached() const noexcept {
		return !m_Closed && m_Pump.m_AuxiliaryOwner == this;
	}

	void ApplicationAuxiliaryRegistration::Close() {
		if (m_Closed) {
			return;
		}

		m_Pump.RetireAuxiliary(*this);
		m_Closed = true;
	}

} // namespace sgre::core
