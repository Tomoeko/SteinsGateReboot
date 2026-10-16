#pragma once

#include "sgre/scene/parallel_task_set.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace sgre::core {

	enum class BackgroundCallLane : std::uint32_t {
		AboveNormal = 0,
		Normal = 1,
		BelowNormal = 2,
		Lowest = 3
	};

	class BackgroundCallTask;

	class BackgroundCallManager {
	public:
		// Four distinct native FIFO managers; logical lanes do not claim an OS thread-priority mapping.
		explicit BackgroundCallManager(std::size_t max_live_tasks = 128);
		~BackgroundCallManager() noexcept;
		BackgroundCallManager(const BackgroundCallManager&) = delete;
		BackgroundCallManager& operator=(const BackgroundCallManager&) = delete;
		BackgroundCallManager(BackgroundCallManager&&) = delete;
		BackgroundCallManager& operator=(BackgroundCallManager&&) = delete;

		// The actual post-loop hook wakes only lanes with queued work. Task Update alone does not signal an idle worker.
		void PostLoop();
		[[nodiscard]] bool CanClose() const noexcept;
		// Owner-thread shutdown requires every concrete task to have retired. Joining threads is authored host cleanup.
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
		void Register(BackgroundCallTask& task);
		void Enqueue(BackgroundCallTask& task);
		void Retire(BackgroundCallTask& task);
		friend class BackgroundCallTask;
	};

	class BackgroundCallTask {
	public:
		// The manager and real Execute's borrowed owners enclose this task until Close completes.
		// Native MFileReadTask jobs use BelowNormal and execute the actual read-loop callback, not a substituted empty worker.
		BackgroundCallTask(BackgroundCallManager& manager, BackgroundCallLane lane, std::function<void()> execute);
		~BackgroundCallTask() noexcept;
		BackgroundCallTask(const BackgroundCallTask&) = delete;
		BackgroundCallTask& operator=(const BackgroundCallTask&) = delete;
		BackgroundCallTask(BackgroundCallTask&&) = delete;
		BackgroundCallTask& operator=(BackgroundCallTask&&) = delete;

		// Native task Update enqueues once; a completed job remains queued-once and cannot be resubmitted.
		void Update();
		[[nodiscard]] scene::TaskStatus Status() const noexcept;
		[[nodiscard]] bool Enqueued() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;
		[[nodiscard]] bool CanClose() const noexcept;
		// Unlinks an idle queued task or waits for the actual running callback before dropping its borrowed owners.
		// Self/foreign-thread cleanup rejects before mutation. Worker exceptions report after complete retirement.
		void Close();

	private:
		BackgroundCallManager& m_Manager;
		BackgroundCallLane m_Lane;
		std::function<void()> m_Execute;
		std::thread::id m_OwnerThread;
		std::mutex m_Mutex;
		std::atomic<scene::TaskStatus> m_Status = scene::TaskStatus::Active;
		std::atomic<bool> m_Enqueued = false;
		std::atomic<bool> m_Closed = false;
		std::exception_ptr m_Failure;
		friend class BackgroundCallManager;
	};

} // namespace sgre::core
