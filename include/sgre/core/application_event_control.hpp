#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace sgre::core {

	struct ApplicationResizeNotice {
		std::uint32_t Code = 0;
		float Width = 0;
		float Height = 0;
	};

	struct ApplicationEventState {
		bool SuspendPending = false;
		bool ResumePending = false;
		bool ClosePending = false;
		// Native5b has no observed request writer; this phase does not invent one.
		bool WaitBreakPending = false;
		bool QuitPending = false;
		bool Suspended = false;
		bool Resumed = false;
		bool CloseAccepted = false;
		bool WaitBreak = false;
		bool Quit = false;
		bool ResizePending = false;
		std::uint32_t CloseCode = 1;
		std::uint32_t ResumeRequests = 0;
		ApplicationResizeNotice Resize;
		std::uint32_t ResizeRequests = 0;
	};

	enum class ApplicationEventSlot {
		Suspend,
		Resume,
		Close,
		Resize
	};

	struct ApplicationEventCallbackOperations {
		std::function<void()> Suspend;
		std::function<void()> Resume;
		std::function<bool()> Close;
		std::function<void(ApplicationResizeNotice)> Resize;
	};

	class ApplicationEventCallbacks;

	class ApplicationEventControl {
	public:
		// This actual registry and its recursive lock enclose stable callback receivers and platform request producers.
		// Native null slots are admitted. No script callback, OS bridge or platform pump is installed automatically.
		ApplicationEventControl();
		~ApplicationEventControl() noexcept;
		ApplicationEventControl(const ApplicationEventControl&) = delete;
		ApplicationEventControl& operator=(const ApplicationEventControl&) = delete;
		void Reset();
		void RequestSuspend();
		void RequestResume();
		void RequestClose(std::uint32_t native_code);
		void RequestResize(ApplicationResizeNotice notice);
		void RequestQuit();
		void ClearClose();
		void ClearSuspend();
		[[nodiscard]] ApplicationEventState State() const;
		// The returned previous receiver is borrowed; it retains no resource or owner lifetime.
		[[nodiscard]] ApplicationEventCallbacks* ExchangeCallback(ApplicationEventSlot slot, ApplicationEventCallbacks* actual_receiver);
		// Containing dispatch phases preflight the actual owner/mutex/idle invariant without mutation.
		void RequireCanFlush() const;
		// Native requests and slot changes made by an earlier callback are visible to subsequent stages.
		void Flush();
		[[nodiscard]] bool CanClose() const noexcept;
		void Close();

	private:
		friend class ApplicationEventCallbacks;
		void RequireOpen() const;
		void RequireOwner() const;
		void Register(ApplicationEventCallbacks& receiver);
		[[nodiscard]] bool CanRetire(const ApplicationEventCallbacks& receiver) const noexcept;
		[[nodiscard]] bool Retire(ApplicationEventCallbacks& receiver);
		[[nodiscard]] bool Invoke(ApplicationEventSlot slot, std::exception_ptr& failure);
		mutable std::recursive_mutex m_Mutex;
		ApplicationEventState m_State;
		std::array<ApplicationEventCallbacks*, 4> m_Callbacks{};
		ApplicationEventCallbacks* m_CurrentCallback = nullptr;
		std::size_t m_LiveReceivers = 0;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		bool m_Flushing = false;
		bool m_Closed = false;
	};

	class ApplicationEventCallbacks {
	public:
		// All four concrete operations are required for a receiver; individual native slots may remain null.
		// Borrowed script/UI/resource owners outlive this stable receiver and every installed callback visit.
		ApplicationEventCallbacks(ApplicationEventControl& control, ApplicationEventCallbackOperations actual_operations);
		~ApplicationEventCallbacks() noexcept;
		ApplicationEventCallbacks(const ApplicationEventCallbacks&) = delete;
		ApplicationEventCallbacks& operator=(const ApplicationEventCallbacks&) = delete;
		ApplicationEventCallbacks(ApplicationEventCallbacks&&) = delete;
		ApplicationEventCallbacks& operator=(ApplicationEventCallbacks&&) = delete;
		[[nodiscard]] bool CanClose() const noexcept;
		// Removes only actual slots still pointing to this receiver, after its active callback finishes.
		void Close();

	private:
		friend class ApplicationEventControl;
		ApplicationEventControl& m_Control;
		ApplicationEventCallbackOperations m_Operations;
		bool m_Closed = false;
	};

} // namespace sgre::core
