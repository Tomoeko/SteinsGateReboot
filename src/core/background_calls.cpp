#include "sgre/core/background_calls.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <list>
#include <stdexcept>
#include <utility>

namespace sgre::core {

	static std::size_t LaneIndex(BackgroundCallLane lane) {
		const auto index = static_cast<std::size_t>(lane);
		if (index >= 4) {
			throw std::runtime_error("background call lane is outside the native four managers");
		}

		return index;
	}

	struct BackgroundCallQueue {
		std::mutex Mutex;
		std::condition_variable Changed;
		std::condition_variable Started;
		std::list<BackgroundCallTask*> Tasks;
		std::thread Worker;
		bool Ready = false;
		bool Wake = false;
		bool Stopping = false;
	};

	class BackgroundCallManager::Implementation {
	public:
		explicit Implementation(std::size_t max_live_tasks) : MaxLiveTasks(max_live_tasks), OwnerThread(std::this_thread::get_id()) {
			if (max_live_tasks == 0 || max_live_tasks > 65536) {
				throw std::runtime_error("background calls require a bounded positive live-task limit");
			}

			try {
				for (auto& queue : Queues) {
					queue.Worker = std::thread([&queue]() {
						Run(queue);
					});
					std::unique_lock lock(queue.Mutex);
					queue.Started.wait(lock, [&queue]() {
						return queue.Ready;
					});
				}
			} catch (...) {
				StopWorkers();
				throw;
			}
		}

		static void Run(BackgroundCallQueue& queue) {
			std::unique_lock queue_lock(queue.Mutex);
			queue.Ready = true;
			queue.Started.notify_one();
			while (true) {
				if (queue.Tasks.empty()) {
					queue.Changed.wait(queue_lock, [&queue]() {
						return queue.Wake || queue.Stopping;
					});
					queue.Wake = false;
					if (queue.Stopping) {
						return;
					}

					continue;
				}

				// Native selection acquires the job lock before releasing the manager lock.
				auto& task = *queue.Tasks.front();
				std::unique_lock task_lock(task.m_Mutex);
				queue.Tasks.pop_front();
				queue_lock.unlock();
				try {
					task.m_Execute();
				} catch (...) {
					task.m_Failure = std::current_exception();
				}

				task.m_Status.store(scene::TaskStatus::Retire, std::memory_order_release);
				task_lock.unlock();
				queue_lock.lock();
			}
		}

		void RequireOwnerThread() const {
			if (std::this_thread::get_id() != OwnerThread) {
				throw std::runtime_error("background manager mutation requires its actual owner thread");
			}
		}

		void StopWorkers() {
			for (auto& queue : Queues) {
				{
					std::lock_guard lock(queue.Mutex);
					queue.Stopping = true;
				}
				queue.Changed.notify_one();
			}

			for (auto& queue : Queues) {
				if (queue.Worker.joinable()) {
					queue.Worker.join();
				}
			}
		}

		std::array<BackgroundCallQueue, 4> Queues;
		std::size_t MaxLiveTasks = 0;
		std::size_t LiveTasks = 0;
		std::thread::id OwnerThread;
		bool Closed = false;
	};

	BackgroundCallManager::BackgroundCallManager(std::size_t max_live_tasks) : m_Implementation(std::make_unique<Implementation>(max_live_tasks)) {
	}

	BackgroundCallManager::~BackgroundCallManager() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void BackgroundCallManager::Register(BackgroundCallTask& task) {
		auto& owner = *m_Implementation;
		owner.RequireOwnerThread();
		static_cast<void>(LaneIndex(task.m_Lane));
		if (owner.Closed || owner.LiveTasks == owner.MaxLiveTasks) {
			throw std::runtime_error("background manager is closed or its live-task bound is full");
		}

		++owner.LiveTasks;
	}

	void BackgroundCallManager::Enqueue(BackgroundCallTask& task) {
		auto& owner = *m_Implementation;
		owner.RequireOwnerThread();
		if (owner.Closed || task.IsClosed()) {
			throw std::runtime_error("background call update requires its open concrete owner");
		}
		if (task.Enqueued()) {
			return;
		}

		auto& queue = owner.Queues[LaneIndex(task.m_Lane)];
		{
			std::lock_guard lock(queue.Mutex);
			queue.Tasks.push_back(&task);
		}
		task.m_Enqueued.store(true, std::memory_order_release);
	}

	void BackgroundCallManager::Retire(BackgroundCallTask& task) {
		auto& owner = *m_Implementation;
		owner.RequireOwnerThread();
		auto& queue = owner.Queues[LaneIndex(task.m_Lane)];
		{
			std::lock_guard lock(queue.Mutex);
			const auto found = std::find(queue.Tasks.begin(), queue.Tasks.end(), &task);
			if (found != queue.Tasks.end()) {
				queue.Tasks.erase(found);
			}
		}

		std::unique_lock task_lock(task.m_Mutex);
		task.m_Closed.store(true, std::memory_order_release);
		--owner.LiveTasks;
		const auto failure = std::exchange(task.m_Failure, {});
		task.m_Execute = {};
		task_lock.unlock();
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

	void BackgroundCallManager::PostLoop() {
		auto& owner = *m_Implementation;
		owner.RequireOwnerThread();
		if (owner.Closed) {
			throw std::runtime_error("background post-loop requires its open manager");
		}

		for (auto& queue : owner.Queues) {
			{
				std::lock_guard lock(queue.Mutex);
				if (queue.Tasks.empty()) {
					continue;
				}

				queue.Wake = true;
			}
			queue.Changed.notify_one();
		}
	}

	bool BackgroundCallManager::CanClose() const noexcept {
		const auto& owner = *m_Implementation;
		return std::this_thread::get_id() == owner.OwnerThread && owner.LiveTasks == 0;
	}

	void BackgroundCallManager::Close() {
		auto& owner = *m_Implementation;
		owner.RequireOwnerThread();
		if (owner.Closed) {
			return;
		}
		if (!CanClose()) {
			throw std::runtime_error("background manager shutdown requires every actual task to retire first");
		}

		owner.StopWorkers();
		owner.Closed = true;
	}

	BackgroundCallTask::BackgroundCallTask(BackgroundCallManager& manager, BackgroundCallLane lane, std::function<void()> execute) : m_Manager(manager), m_Lane(lane), m_Execute(std::move(execute)), m_OwnerThread(std::this_thread::get_id()) {
		if (!m_Execute) {
			throw std::runtime_error("background call requires its real Execute operation and lifetime owners");
		}

		m_Manager.Register(*this);
	}

	BackgroundCallTask::~BackgroundCallTask() noexcept {
		try {
			Close();
		} catch (...) {
			if (!IsClosed()) {
				std::terminate();
			}
		}
	}

	void BackgroundCallTask::Update() {
		if (std::this_thread::get_id() != m_OwnerThread) {
			throw std::runtime_error("background call update requires its actual owner thread");
		}

		m_Manager.Enqueue(*this);
	}

	scene::TaskStatus BackgroundCallTask::Status() const noexcept {
		return m_Status.load(std::memory_order_acquire);
	}

	bool BackgroundCallTask::Enqueued() const noexcept {
		return m_Enqueued.load(std::memory_order_acquire);
	}

	bool BackgroundCallTask::IsClosed() const noexcept {
		return m_Closed.load(std::memory_order_acquire);
	}

	bool BackgroundCallTask::CanClose() const noexcept {
		return IsClosed() || std::this_thread::get_id() == m_OwnerThread;
	}

	void BackgroundCallTask::Close() {
		if (!CanClose()) {
			throw std::runtime_error("background call self/foreign-thread retirement is outside the supported owner domain");
		}
		if (IsClosed()) {
			return;
		}

		m_Manager.Retire(*this);
	}

} // namespace sgre::core
