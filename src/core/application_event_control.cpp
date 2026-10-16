#include "sgre/core/application_event_control.hpp"

#include <stdexcept>
#include <utility>

namespace sgre::core {

	static constexpr std::size_t kMaximumEventCallbackReceivers = 64;

	static std::size_t EventSlotIndex(ApplicationEventSlot slot) {
		switch (slot) {
		case ApplicationEventSlot::Suspend:
			return 0;
		case ApplicationEventSlot::Resume:
			return 1;
		case ApplicationEventSlot::Close:
			return 2;
		case ApplicationEventSlot::Resize:
			return 3;
		}

		throw std::runtime_error("application event callback slot is outside the recovered four registrations");
	}

	ApplicationEventControl::ApplicationEventControl() {
		Reset();
	}

	ApplicationEventControl::~ApplicationEventControl() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void ApplicationEventControl::RequireOpen() const {
		if (m_Closed) {
			throw std::runtime_error("application event control requires its actual live owner");
		}
	}

	void ApplicationEventControl::RequireOwner() const {
		RequireOpen();
		if (std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("application event dispatch requires its actual serialized owner thread");
		}
	}

	void ApplicationEventControl::Reset() {
		const std::lock_guard lock(m_Mutex);
		RequireOwner();
		if (m_Flushing) {
			throw std::runtime_error("application event reset requires a stable dispatch boundary");
		}

		m_Callbacks.fill(nullptr);
		m_State.SuspendPending = false;
		m_State.ResumePending = false;
		m_State.ClosePending = false;
		m_State.WaitBreakPending = false;
		m_State.QuitPending = false;
		m_State.Suspended = false;
		m_State.Resumed = false;
		m_State.CloseAccepted = false;
		m_State.WaitBreak = false;
		m_State.Quit = false;
		m_State.CloseCode = 1;
		// Native4e0920 deliberately retains resize pending/payload and both request counters.
	}

	void ApplicationEventControl::RequestSuspend() {
		const std::lock_guard lock(m_Mutex);
		RequireOpen();
		m_State.SuspendPending = true;
	}

	void ApplicationEventControl::RequestResume() {
		const std::lock_guard lock(m_Mutex);
		RequireOpen();
		++m_State.ResumeRequests;
		m_State.ResumePending = true;
	}

	void ApplicationEventControl::RequestClose(std::uint32_t native_code) {
		const std::lock_guard lock(m_Mutex);
		RequireOpen();
		m_State.ClosePending = true;
		if (m_State.CloseCode != 0) {
			m_State.CloseCode = native_code;
		}
	}

	void ApplicationEventControl::RequestResize(ApplicationResizeNotice notice) {
		const std::lock_guard lock(m_Mutex);
		RequireOpen();
		++m_State.ResizeRequests;
		m_State.Resize = notice;
		m_State.ResizePending = true;
	}

	void ApplicationEventControl::RequestQuit() {
		const std::lock_guard lock(m_Mutex);
		RequireOpen();
		m_State.QuitPending = true;
	}

	void ApplicationEventControl::ClearClose() {
		const std::lock_guard lock(m_Mutex);
		RequireOpen();
		m_State.CloseAccepted = false;
	}

	void ApplicationEventControl::ClearSuspend() {
		const std::lock_guard lock(m_Mutex);
		RequireOpen();
		m_State.Suspended = false;
	}

	ApplicationEventState ApplicationEventControl::State() const {
		const std::lock_guard lock(m_Mutex);
		RequireOpen();

		return m_State;
	}

	ApplicationEventCallbacks* ApplicationEventControl::ExchangeCallback(ApplicationEventSlot slot, ApplicationEventCallbacks* actual_receiver) {
		const std::lock_guard lock(m_Mutex);
		RequireOpen();
		const auto index = EventSlotIndex(slot);
		if (actual_receiver != nullptr && (&actual_receiver->m_Control != this || actual_receiver->m_Closed)) {
			throw std::runtime_error("application event callback requires its actual live receiver and exact control owner");
		}

		auto* previous = m_Callbacks[index];
		m_Callbacks[index] = actual_receiver;

		return previous;
	}

	bool ApplicationEventControl::Invoke(ApplicationEventSlot slot, std::exception_ptr& failure) {
		auto* receiver = m_Callbacks[EventSlotIndex(slot)];
		if (receiver == nullptr) {
			return true;
		}

		m_CurrentCallback = receiver;
		bool accepted = true;
		try {
			switch (slot) {
			case ApplicationEventSlot::Suspend:
				receiver->m_Operations.Suspend();
				break;
			case ApplicationEventSlot::Resume:
				receiver->m_Operations.Resume();
				break;
			case ApplicationEventSlot::Close:
				accepted = receiver->m_Operations.Close();
				break;
			case ApplicationEventSlot::Resize:
				receiver->m_Operations.Resize(m_State.Resize);
				break;
			}
		} catch (...) {
			accepted = false;
			if (!failure) {
				failure = std::current_exception();
			}
		}
		m_CurrentCallback = nullptr;

		return accepted;
	}

	void ApplicationEventControl::RequireCanFlush() const {
		const std::lock_guard lock(m_Mutex);
		RequireOwner();
		if (m_Flushing) {
			throw std::runtime_error("application event flush cannot recursively deliver live callbacks");
		}
	}

	void ApplicationEventControl::Flush() {
		const std::lock_guard lock(m_Mutex);
		RequireCanFlush();

		m_Flushing = true;
		std::exception_ptr failure;
		if (!m_State.Quit) {
			if (m_State.SuspendPending) {
				m_State.Suspended = true;
				m_State.Resumed = false;
				static_cast<void>(Invoke(ApplicationEventSlot::Suspend, failure));
				m_State.SuspendPending = false;
			}
			if (m_State.ResumePending) {
				m_State.Suspended = false;
				m_State.Resumed = true;
				static_cast<void>(Invoke(ApplicationEventSlot::Resume, failure));
				m_State.ResumePending = false;
			}
			if (m_State.ClosePending) {
				m_State.CloseAccepted = true;
				if (!Invoke(ApplicationEventSlot::Close, failure)) {
					m_State.CloseAccepted = false;
				}
				m_State.ClosePending = false;
			}
			if (m_State.ResizePending) {
				static_cast<void>(Invoke(ApplicationEventSlot::Resize, failure));
				m_State.ResizePending = false;
			}
		}
		if (m_State.WaitBreakPending) {
			m_State.WaitBreak = true;
			m_State.WaitBreakPending = false;
		}
		if (m_State.QuitPending) {
			m_State.Quit = true;
			// Native78 remains null: exact xrefs establish no installed quit callback.
			m_State.QuitPending = false;
		}
		m_Flushing = false;
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

	void ApplicationEventControl::Register(ApplicationEventCallbacks&) {
		const std::lock_guard lock(m_Mutex);
		RequireOwner();
		if (m_Flushing || m_LiveReceivers == kMaximumEventCallbackReceivers) {
			throw std::runtime_error("application event receiver construction requires an idle bounded actual owner");
		}
		++m_LiveReceivers;
	}

	bool ApplicationEventControl::CanRetire(const ApplicationEventCallbacks& receiver) const noexcept {
		try {
			const std::lock_guard lock(m_Mutex);
			return receiver.m_Closed || (std::this_thread::get_id() == m_OwnerThread && !m_Closed && m_CurrentCallback != &receiver);
		} catch (...) {
			return false;
		}
	}

	bool ApplicationEventControl::Retire(ApplicationEventCallbacks& receiver) {
		const std::lock_guard lock(m_Mutex);
		if (receiver.m_Closed) {
			return false;
		}
		RequireOwner();
		if (m_CurrentCallback == &receiver) {
			throw std::runtime_error("application event receiver must finish its actual callback before retirement");
		}

		for (auto*& slot : m_Callbacks) {
			if (slot == &receiver) {
				slot = nullptr;
			}
		}
		receiver.m_Closed = true;
		--m_LiveReceivers;

		return true;
	}

	bool ApplicationEventControl::CanClose() const noexcept {
		try {
			const std::lock_guard lock(m_Mutex);
			return std::this_thread::get_id() == m_OwnerThread && (m_Closed || (!m_Flushing && m_LiveReceivers == 0));
		} catch (...) {
			return false;
		}
	}

	void ApplicationEventControl::Close() {
		const std::lock_guard lock(m_Mutex);
		if (!CanClose()) {
			throw std::runtime_error("application event callback owners must retire before control shutdown");
		}
		m_Closed = true;
	}

	ApplicationEventCallbacks::ApplicationEventCallbacks(ApplicationEventControl& control, ApplicationEventCallbackOperations actual_operations) : m_Control(control), m_Operations(std::move(actual_operations)) {
		if (!m_Operations.Suspend || !m_Operations.Resume || !m_Operations.Close || !m_Operations.Resize) {
			throw std::runtime_error("application event receiver requires all four actual operations");
		}
		m_Control.Register(*this);
	}

	ApplicationEventCallbacks::~ApplicationEventCallbacks() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	bool ApplicationEventCallbacks::CanClose() const noexcept {
		return m_Control.CanRetire(*this);
	}

	void ApplicationEventCallbacks::Close() {
		if (!m_Control.Retire(*this)) {
			return;
		}
		m_Operations = {};
	}

} // namespace sgre::core
