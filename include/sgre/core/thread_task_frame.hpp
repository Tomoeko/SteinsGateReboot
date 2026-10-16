#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace sgre::core {

	class ThreadTaskFrame;
	class LooperFrame;
	class LooperContainingMutation;
	[[nodiscard]] std::uint32_t UpdateThreadTaskCost(std::uint32_t previous, std::uint32_t before, std::uint32_t after) noexcept;

	struct ThreadTaskReceiverState {
		std::uint32_t Cost = 0;
		bool UpdatePending = false;
		bool PostPending = false;
		bool Executing = false;
	};

	class ThreadTaskReceiver {
	public:
		// The concrete callback owners and frame service enclose this stable receiver. No resource/self pin is added.
		ThreadTaskReceiver(ThreadTaskFrame& frame, std::function<void(ThreadTaskReceiver&)> actual_update, std::function<void(ThreadTaskReceiver&)> actual_post);
		~ThreadTaskReceiver() noexcept;
		ThreadTaskReceiver(const ThreadTaskReceiver&) = delete;
		ThreadTaskReceiver& operator=(const ThreadTaskReceiver&) = delete;
		ThreadTaskReceiver(ThreadTaskReceiver&&) = delete;
		ThreadTaskReceiver& operator=(ThreadTaskReceiver&&) = delete;
		void QueueUpdate();
		void QueuePost();
		[[nodiscard]] ThreadTaskReceiverState State() const;
		// Typed controller identity is observed only while this exact live owner is idle on its containing thread.
		[[nodiscard]] const ThreadTaskFrame& FrameOwner() const;
		// Allocation from a bound heap writer is limited to this receiver's actual executing Update thread.
		void RequireExecutingUpdate() const;
		[[nodiscard]] bool CanClose() const noexcept;
		// Only quiescent retirement is admitted. Native pending cancellation's cross-counter decrement stays unsupported.
		void Close();

	private:
		friend class ThreadTaskFrame;
		ThreadTaskFrame& m_Frame;
		std::function<void(ThreadTaskReceiver&)> m_Update;
		std::function<void(ThreadTaskReceiver&)> m_Post;
		std::uint32_t m_Cost = 0;
		std::size_t m_PendingPosts = 0;
		std::thread::id m_ExecutingThread;
		bool m_UpdatePending = false;
		bool m_PostPending = false;
		bool m_Executing = false;
		bool m_Closed = false;
	};

	class ThreadTaskFrame {
	public:
		// Required raw worker counter source, sampled twice around each real Update. Low DWORDs retain original units.
		// This source must support simultaneous worker calls and must outlive explicit Close.
		ThreadTaskFrame(std::function<std::uint64_t()> actual_worker_counter, std::size_t worker_count, std::size_t maximum_receivers = 128);
		~ThreadTaskFrame() noexcept;
		ThreadTaskFrame(const ThreadTaskFrame&) = delete;
		ThreadTaskFrame& operator=(const ThreadTaskFrame&) = delete;
		// Observes the actual mutex-protected live owner/Idle phase before a containing frame mutates root or hooks.
		void RequireCanRunFrame() const;
		// Descending unsigned cost dispatch, complete worker fence, then live FIFO Post callbacks.
		// Async root/group selection and Windows semaphore/affinity behavior are not supplied by this owner.
		void RunFrame();
		[[nodiscard]] bool CanClose() const noexcept;
		// Receivers retire first. Stop/join is authored host cleanup, not a recovered native shutdown claim.
		void Close();

	private:
		friend class LooperFrame;
		void EnterContainingMutation(const LooperContainingMutation& actual_scope);
		void LeaveContainingMutation(const LooperContainingMutation& actual_scope);
		void RequireContainingMutation(const LooperContainingMutation& actual_scope) const;
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
		void Register(ThreadTaskReceiver& receiver);
		void RequireExecutingUpdate(const ThreadTaskReceiver& receiver) const;
		void QueueUpdate(ThreadTaskReceiver& receiver);
		void QueuePost(ThreadTaskReceiver& receiver);
		[[nodiscard]] ThreadTaskReceiverState State(const ThreadTaskReceiver& receiver) const;
		[[nodiscard]] bool CanRetire(const ThreadTaskReceiver& receiver) const noexcept;
		[[nodiscard]] bool Retire(ThreadTaskReceiver& receiver);
		friend class ThreadTaskReceiver;
	};

} // namespace sgre::core
