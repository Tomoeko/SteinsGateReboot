#include "sgre/render/graphics_display_frame.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::render {
	GraphicsDisplayFrame::GraphicsDisplayFrame(core::LooperFrame& actual_looper, SpriteGraphicsTargets& actual_graphics, SpritePassPresentation& actual_surface, core::ThreadTaskFrame& actual_workers, scene::TextureCache& actual_textures, RenderFrameCounters actual_current, RenderFrameCounters actual_previous, GraphicsFrameCycleLimits limits)
	    : m_Looper(actual_looper), m_Workers(actual_workers), m_Surface(actual_surface), m_Cycle(actual_graphics, actual_surface, actual_workers, actual_textures, actual_current, actual_previous, limits), m_Synchronization(actual_looper, actual_surface) {
		const core::LooperContainingMutation mutation(m_Looper, m_Workers);
		m_Mutation = &mutation;
		try {
			// Original Display4c4540 appends its own Pre clock, then Post, with enabled byte one.
			RegisterClock(scene::LooperHookPhase::PreLoop);
			RegisterClock(scene::LooperHookPhase::PostLoop);
		} catch (...) {
			const auto first = std::current_exception();
			m_Retiring = true;
			try {
				m_Cycle.Close();
				RetireClock(scene::LooperHookPhase::PostLoop);
				RetireClock(scene::LooperHookPhase::PreLoop);
			} catch (...) {
				// A constructor cannot leave callbacks borrowing an object that will never exist.
				std::terminate();
			}
			m_Mutation = nullptr;
			std::rethrow_exception(first);
		}
		m_Mutation = nullptr;
	}

	GraphicsDisplayFrame::~GraphicsDisplayFrame() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void GraphicsDisplayFrame::RequireOpen() const {
		if (std::this_thread::get_id() != m_Thread || m_Closed || m_Retiring || !m_PreRegistered || !m_PostRegistered || !m_Looper.Hooks().Group(scene::LooperHookPhase::PreLoop).Contains(m_PreClock) || !m_Looper.Hooks().Group(scene::LooperHookPhase::PostLoop).Contains(m_PostClock)) {
			throw std::runtime_error("Display frame requires its actual live serialized owner and both registered clocks");
		}
	}

	void GraphicsDisplayFrame::RegisterClock(scene::LooperHookPhase phase) {
		m_Mutation->RequireOwner(m_Looper, m_Workers);
		scene::ParallelTaskCallbacks callbacks;
		callbacks.Update = [this, phase](auto actual_clock) {
			RequireOpen();
			const auto expected = phase == scene::LooperHookPhase::PreLoop ? m_PreClock : m_PostClock;
			if (actual_clock != expected) {
				throw std::runtime_error("Display clock callback belongs to another actual registration");
			}
			if (phase == scene::LooperHookPhase::PreLoop) {
				m_Cycle.PreFrame();
			} else {
				m_Cycle.PostFrame();
			}
		};
		callbacks.CanRelease = [this](auto actual_clock) {
			return CanReleaseClock(actual_clock);
		};
		callbacks.Release = [this](auto actual_clock) {
			ReleaseClock(actual_clock);
		};
		const auto handle = m_Looper.Hooks().Group(phase).Append(std::move(callbacks));
		if (phase == scene::LooperHookPhase::PreLoop) {
			m_PreClock = handle;
			m_PreRegistered = true;
		} else {
			m_PostClock = handle;
			m_PostRegistered = true;
		}
	}

	bool GraphicsDisplayFrame::CanReleaseClock(scene::ParallelTaskHandle actual_clock) const noexcept {
		try {
			if (std::this_thread::get_id() != m_Thread || !m_Retiring || m_Mutation == nullptr) {
				return false;
			}
			m_Mutation->RequireOwner(m_Looper, m_Workers);
			return (m_PreRegistered && actual_clock == m_PreClock) || (m_PostRegistered && actual_clock == m_PostClock);
		} catch (...) {
			return false;
		}
	}

	void GraphicsDisplayFrame::ReleaseClock(scene::ParallelTaskHandle actual_clock) {
		if (!CanReleaseClock(actual_clock)) {
			throw std::runtime_error("Display clock release requires its exact containing resource retirement scope");
		}
		if (actual_clock == m_PostClock) {
			m_PostRegistered = false;
		} else {
			m_PreRegistered = false;
		}
	}

	void GraphicsDisplayFrame::RequireClockRetirement(scene::LooperHookPhase phase) const {
		const auto registered = phase == scene::LooperHookPhase::PreLoop ? m_PreRegistered : m_PostRegistered;
		if (!registered) {
			return;
		}
		const auto handle = phase == scene::LooperHookPhase::PreLoop ? m_PreClock : m_PostClock;
		const auto& group = m_Looper.Hooks().Group(phase);
		if (!group.Contains(handle) || !group.CanExtract(handle)) {
			throw std::runtime_error("Display exact clock cannot retire at its actual containing boundary");
		}
	}

	void GraphicsDisplayFrame::RetireClock(scene::LooperHookPhase phase) {
		const auto registered = phase == scene::LooperHookPhase::PreLoop ? m_PreRegistered : m_PostRegistered;
		if (!registered) {
			return;
		}
		RequireClockRetirement(phase);
		const auto handle = phase == scene::LooperHookPhase::PreLoop ? m_PreClock : m_PostClock;
		auto lease = m_Looper.Hooks().Group(phase).Extract(handle);
		lease.Release();
	}

	GraphicsFrameCycle& GraphicsDisplayFrame::Cycle() {
		RequireOpen();
		return m_Cycle;
	}

	void GraphicsDisplayFrame::BeginRootDraw() {
		RequireOpen();
		m_Cycle.BeginDrawing();
	}

	bool GraphicsDisplayFrame::RunIteration() {
		RequireOpen();
		// Reject reentry before claiming an iteration or cleaning up somebody else's live draw.
		m_Looper.RequireIdleContainingPhase();
		try {
			return m_Looper.RunIteration();
		} catch (...) {
			const auto first = std::current_exception();
			try {
				m_Cycle.AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsDisplayFrame::RequirePumpBorrowAdmission(const void* actual_pump, const SpritePassPresentation& actual_surface, const core::ApplicationClockPump& actual_clock, const core::ApplicationPumpPlatform& actual_platform) const {
		RequireOpen();
		m_Looper.RequireIdleContainingPhase();
		if (actual_pump == nullptr || m_PumpBorrow != nullptr || &actual_surface != &m_Surface) {
			throw std::runtime_error("Display presentation pump requires its exclusive exact actual surface connection");
		}
		actual_clock.RequireAttachment(m_Looper, actual_platform);
	}

	void GraphicsDisplayFrame::RequirePumpBorrowRetirement(const void* actual_pump) const {
		RequireOpen();
		m_Looper.RequireIdleContainingPhase();
		if (m_PumpBorrow != actual_pump) {
			throw std::runtime_error("Display presentation borrow belongs to another actual pump");
		}
	}

	void GraphicsDisplayFrame::RegisterPumpBorrow(const void* actual_pump, const SpritePassPresentation& actual_surface, const core::ApplicationClockPump& actual_clock, const core::ApplicationPumpPlatform& actual_platform) {
		RequirePumpBorrowAdmission(actual_pump, actual_surface, actual_clock, actual_platform);
		m_PumpBorrow = actual_pump;
	}

	void GraphicsDisplayFrame::RequirePumpPhase(const core::ApplicationClockPump& actual_clock, const core::ApplicationPumpPlatform& actual_platform) const {
		RequireOpen();
		actual_clock.RequirePresentationPhase(m_Looper, actual_platform);
	}

	void GraphicsDisplayFrame::ReleasePumpBorrow(const void* actual_pump) {
		RequirePumpBorrowRetirement(actual_pump);
		m_PumpBorrow = nullptr;
	}

	PresentationSynchronization& GraphicsDisplayFrame::Synchronization() {
		RequireOpen();
		return m_Synchronization;
	}

	SpritePresentationStatus GraphicsDisplayFrame::PresentCurrent(bool force_unsynchronized) {
		RequireOpen();
		const auto frame = m_Cycle.LastSubmission();
		if (!frame) {
			throw std::runtime_error("Display presentation requires its actual completed current-frame producer");
		}
		const auto actual = m_Surface.CurrentSubmission();
		if (frame->Owner != actual.Owner || frame->Identity != actual.Identity) {
			throw std::runtime_error("Display completed frame differs from its actual latest surface submission");
		}

		return m_Surface.TryPresentCurrent(m_Synchronization, force_unsynchronized);
	}

	void GraphicsDisplayFrame::Close() {
		if (m_Closed) {
			return;
		}
		RequireOpen();
		if (m_PumpBorrow != nullptr) {
			throw std::runtime_error("Display frame retirement requires its actual presentation pump to detach first");
		}
		const core::LooperContainingMutation mutation(m_Looper, m_Workers);
		m_Mutation = &mutation;
		m_Retiring = true;
		try {
			RequireClockRetirement(scene::LooperHookPhase::PostLoop);
			RequireClockRetirement(scene::LooperHookPhase::PreLoop);
			// Original Display4c2460 disposes frame/pool resources before Post/Pre clock unlink.
			m_Cycle.Close();
			RetireClock(scene::LooperHookPhase::PostLoop);
			RetireClock(scene::LooperHookPhase::PreLoop);
			m_Closed = true;
		} catch (...) {
			m_Mutation = nullptr;
			m_Retiring = false;
			throw;
		}
		m_Mutation = nullptr;
		m_Retiring = false;
	}
} // namespace sgre::render
