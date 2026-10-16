#include "sgre/core/thread_task_frame.hpp"

#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::core {

	static constexpr std::size_t kMaximumFrameReceivers = 1024;
	static constexpr std::size_t kMaximumFrameWorkers = 16;

	std::uint32_t UpdateThreadTaskCost(std::uint32_t previous, std::uint32_t before, std::uint32_t after) noexcept {
		return (previous * 7u - before + after) >> 3u;
	}

	class ThreadTaskFrame::Implementation {
	public:
		enum class Phase {
			Idle,
			Workers,
			Posts
		};

		Implementation(std::function<std::uint64_t()> counter, std::size_t worker_count, std::size_t maximum_receivers) : Counter(std::move(counter)), MaximumReceivers(maximum_receivers) {
			if (!Counter || worker_count == 0 || worker_count > kMaximumFrameWorkers || maximum_receivers == 0 || maximum_receivers > kMaximumFrameReceivers) {
				throw std::runtime_error("thread frame requires its actual counter source and bounded positive worker/receiver counts");
			}

			Updates.reserve(maximum_receivers);
			Posts.reserve(maximum_receivers);
			Workers.reserve(worker_count);
			try {
				for (std::size_t index = 0; index < worker_count; ++index) {
					Workers.emplace_back([this] {
						Worker();
					});
				}
			} catch (...) {
				StopWorkers();
				throw;
			}
		}

		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::function<std::uint64_t()> Counter;
		std::size_t MaximumReceivers;
		mutable std::mutex Mutex;
		std::condition_variable Wake;
		std::condition_variable Completed;
		std::vector<ThreadTaskReceiver*> Updates;
		std::vector<ThreadTaskReceiver*> Posts;
		std::vector<std::thread> Workers;
		std::size_t LiveReceivers = 0;
		std::size_t NextUpdate = 0;
		std::size_t CompletedUpdates = 0;
		ThreadTaskReceiver* CurrentPost = nullptr;
		Phase CurrentPhase = Phase::Idle;
		std::exception_ptr WorkerFailure;
		bool Stopping = false;
		bool Closed = false;
		const LooperContainingMutation* ContainingMutation = nullptr;

		void RequireOwner() const {
			if (std::this_thread::get_id() != OwnerThread || Closed) {
				throw std::runtime_error("thread frame operation requires its live serialized owner thread");
			}
		}

		void RequireCanRunFrame() const {
			RequireOwner();
			if (CurrentPhase != Phase::Idle) {
				throw std::runtime_error("thread frame cannot reenter worker or post delivery");
			}
		}

		void RequireNoContainingMutation() const {
			if (ContainingMutation != nullptr) {
				throw std::runtime_error("actual worker delivery cannot enter a held containing mutation");
			}
		}

		void StopWorkers() {
			{
				const std::lock_guard lock(Mutex);
				Stopping = true;
			}
			Wake.notify_all();
			for (auto& worker : Workers) {
				worker.join();
			}
		}

		void Worker() noexcept {
			for (;;) {
				ThreadTaskReceiver* receiver = nullptr;
				{
					std::unique_lock lock(Mutex);
					Wake.wait(lock, [this] {
						return Stopping || (CurrentPhase == Phase::Workers && NextUpdate < Updates.size());
					});
					if (Stopping) {
						return;
					}
					receiver = Updates[NextUpdate++];
					receiver->m_Executing = true;
					receiver->m_ExecutingThread = std::this_thread::get_id();
				}

				std::exception_ptr failure;
				std::uint32_t before = 0;
				std::uint32_t after = 0;
				try {
					before = static_cast<std::uint32_t>(Counter());
					{
						const std::lock_guard lock(Mutex);
						receiver->m_UpdatePending = false;
					}
					receiver->m_Update(*receiver);
					after = static_cast<std::uint32_t>(Counter());
				} catch (...) {
					failure = std::current_exception();
				}

				{
					const std::lock_guard lock(Mutex);
					receiver->m_UpdatePending = false;
					if (!failure) {
						receiver->m_Cost = UpdateThreadTaskCost(receiver->m_Cost, before, after);
					}
					receiver->m_Executing = false;
					receiver->m_ExecutingThread = {};
					if (failure && !WorkerFailure) {
						WorkerFailure = failure;
					}
					++CompletedUpdates;
				}
				Completed.notify_one();
			}
		}
	};

	ThreadTaskFrame::ThreadTaskFrame(std::function<std::uint64_t()> actual_worker_counter, std::size_t worker_count, std::size_t maximum_receivers) : m_Implementation(std::make_unique<Implementation>(std::move(actual_worker_counter), worker_count, maximum_receivers)) {
	}

	ThreadTaskFrame::~ThreadTaskFrame() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void ThreadTaskFrame::Register(ThreadTaskReceiver&) {
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();
		if (state.CurrentPhase != Implementation::Phase::Idle || state.ContainingMutation != nullptr || state.LiveReceivers == state.MaximumReceivers) {
			throw std::runtime_error("thread receiver construction requires an idle bounded actual frame owner");
		}
		++state.LiveReceivers;
	}

	void ThreadTaskFrame::QueueUpdate(ThreadTaskReceiver& receiver) {
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();
		if (state.ContainingMutation != nullptr || receiver.m_Closed || receiver.m_UpdatePending || receiver.m_Executing || state.CurrentPhase != Implementation::Phase::Idle || state.Updates.size() == state.MaximumReceivers) {
			throw std::runtime_error("thread update requires a live unqueued receiver and idle bounded frame");
		}
		state.Updates.push_back(&receiver);
		receiver.m_UpdatePending = true;
	}

	void ThreadTaskFrame::QueuePost(ThreadTaskReceiver& receiver) {
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		if (state.ContainingMutation != nullptr || state.Closed || receiver.m_Closed || state.Posts.size() == state.MaximumReceivers || (std::this_thread::get_id() != state.OwnerThread && (!receiver.m_Executing || receiver.m_ExecutingThread != std::this_thread::get_id() || state.CurrentPhase != Implementation::Phase::Workers))) {
			throw std::runtime_error("thread post requires a live actual receiver, admitted producer and bounded pending storage");
		}
		state.Posts.push_back(&receiver);
		++receiver.m_PendingPosts;
		receiver.m_PostPending = true;
	}

	ThreadTaskReceiverState ThreadTaskFrame::State(const ThreadTaskReceiver& receiver) const {
		const auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		if (receiver.m_Closed || state.Closed) {
			throw std::runtime_error("thread receiver state requires its actual live frame owner");
		}
		ThreadTaskReceiverState result;
		result.Cost = receiver.m_Cost;
		result.UpdatePending = receiver.m_UpdatePending;
		result.PostPending = receiver.m_PostPending;
		result.Executing = receiver.m_Executing || state.CurrentPost == &receiver;

		return result;
	}

	void ThreadTaskFrame::RequireExecutingUpdate(const ThreadTaskReceiver& receiver) const {
		const auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		if (state.Closed || receiver.m_Closed || &receiver.m_Frame != this || state.CurrentPhase != Implementation::Phase::Workers || !receiver.m_Executing || receiver.m_ExecutingThread != std::this_thread::get_id() || receiver.m_UpdatePending) {
			throw std::runtime_error("heap writer requires its exact live executing Update receiver/thread");
		}
	}

	bool ThreadTaskFrame::CanRetire(const ThreadTaskReceiver& receiver) const noexcept {
		try {
			const auto& state = *m_Implementation;
			const std::lock_guard lock(state.Mutex);
			return receiver.m_Closed || (std::this_thread::get_id() == state.OwnerThread && !state.Closed && state.CurrentPhase == Implementation::Phase::Idle && !receiver.m_UpdatePending && !receiver.m_Executing && receiver.m_PendingPosts == 0);
		} catch (...) {
			return false;
		}
	}

	bool ThreadTaskFrame::Retire(ThreadTaskReceiver& receiver) {
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		if (receiver.m_Closed) {
			return false;
		}
		state.RequireOwner();
		if (state.CurrentPhase != Implementation::Phase::Idle || receiver.m_UpdatePending || receiver.m_Executing || receiver.m_PendingPosts != 0) {
			throw std::runtime_error("thread receiver must finish actual queued/running phases before retirement");
		}
		receiver.m_Closed = true;
		--state.LiveReceivers;

		return true;
	}

	void ThreadTaskFrame::EnterContainingMutation(const LooperContainingMutation& actual_scope) {
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireCanRunFrame();
		state.RequireNoContainingMutation();
		state.ContainingMutation = &actual_scope;
	}

	void ThreadTaskFrame::RequireContainingMutation(const LooperContainingMutation& actual_scope) const {
		const auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();
		if (state.ContainingMutation != &actual_scope || state.CurrentPhase != Implementation::Phase::Idle) {
			throw std::runtime_error("worker containing mutation requires its exact live scope and idle actual controller");
		}
	}

	void ThreadTaskFrame::LeaveContainingMutation(const LooperContainingMutation& actual_scope) {
		auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireOwner();
		if (state.ContainingMutation != &actual_scope || state.CurrentPhase != Implementation::Phase::Idle) {
			throw std::runtime_error("worker containing mutation release requires its exact live claim");
		}
		state.ContainingMutation = nullptr;
	}

	void ThreadTaskFrame::RequireCanRunFrame() const {
		const auto& state = *m_Implementation;
		const std::lock_guard lock(state.Mutex);
		state.RequireCanRunFrame();
	}

	void ThreadTaskFrame::RunFrame() {
		auto& state = *m_Implementation;
		std::unique_lock lock(state.Mutex);
		state.RequireCanRunFrame();
		state.RequireNoContainingMutation();

		// Bounded stable insertion preserves equal-cost native FIFO without allocating during the frame.
		for (std::size_t index = 1; index < state.Updates.size(); ++index) {
			auto* receiver = state.Updates[index];
			auto destination = index;
			while (destination > 0 && state.Updates[destination - 1]->m_Cost < receiver->m_Cost) {
				state.Updates[destination] = state.Updates[destination - 1];
				--destination;
			}
			state.Updates[destination] = receiver;
		}
		state.WorkerFailure = nullptr;
		state.NextUpdate = 0;
		state.CompletedUpdates = 0;
		state.CurrentPhase = Implementation::Phase::Workers;
		state.Wake.notify_all();
		state.Completed.wait(lock, [&state] {
			return state.CompletedUpdates == state.Updates.size();
		});
		state.Updates.clear();
		if (state.WorkerFailure) {
			const auto failure = state.WorkerFailure;
			state.CurrentPhase = Implementation::Phase::Idle;
			lock.unlock();
			std::rethrow_exception(failure);
		}
		state.CurrentPhase = Implementation::Phase::Posts;
		lock.unlock();

		std::exception_ptr failure;
		std::size_t index = 0;
		for (;;) {
			lock.lock();
			if (index == state.Posts.size()) {
				state.Posts.clear();
				state.CurrentPost = nullptr;
				state.CurrentPhase = Implementation::Phase::Idle;
				lock.unlock();
				break;
			}
			auto* receiver = state.Posts[index++];
			--receiver->m_PendingPosts;
			receiver->m_PostPending = false;
			state.CurrentPost = receiver;
			lock.unlock();
			try {
				receiver->m_Post(*receiver);
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}
		}
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

	bool ThreadTaskFrame::CanClose() const noexcept {
		try {
			const auto& state = *m_Implementation;
			const std::lock_guard lock(state.Mutex);
			return std::this_thread::get_id() == state.OwnerThread && (state.Closed || (state.ContainingMutation == nullptr && state.CurrentPhase == Implementation::Phase::Idle && state.LiveReceivers == 0 && state.Updates.empty() && state.Posts.empty()));
		} catch (...) {
			return false;
		}
	}

	void ThreadTaskFrame::Close() {
		auto& state = *m_Implementation;
		if (!CanClose()) {
			throw std::runtime_error("thread frame receivers and actual queued work must retire before host worker shutdown");
		}
		if (state.Closed) {
			return;
		}
		state.StopWorkers();
		const std::lock_guard lock(state.Mutex);
		state.Closed = true;
	}

	ThreadTaskReceiver::ThreadTaskReceiver(ThreadTaskFrame& frame, std::function<void(ThreadTaskReceiver&)> actual_update, std::function<void(ThreadTaskReceiver&)> actual_post) : m_Frame(frame), m_Update(std::move(actual_update)), m_Post(std::move(actual_post)) {
		if (!m_Update || !m_Post) {
			throw std::runtime_error("thread receiver requires both actual update and post operations");
		}
		m_Frame.Register(*this);
	}

	ThreadTaskReceiver::~ThreadTaskReceiver() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void ThreadTaskReceiver::QueueUpdate() {
		m_Frame.QueueUpdate(*this);
	}

	void ThreadTaskReceiver::QueuePost() {
		m_Frame.QueuePost(*this);
	}

	ThreadTaskReceiverState ThreadTaskReceiver::State() const {
		return m_Frame.State(*this);
	}

	const ThreadTaskFrame& ThreadTaskReceiver::FrameOwner() const {
		m_Frame.RequireCanRunFrame();
		static_cast<void>(State());

		return m_Frame;
	}

	void ThreadTaskReceiver::RequireExecutingUpdate() const {
		m_Frame.RequireExecutingUpdate(*this);
	}

	bool ThreadTaskReceiver::CanClose() const noexcept {
		return m_Frame.CanRetire(*this);
	}

	void ThreadTaskReceiver::Close() {
		if (!m_Frame.Retire(*this)) {
			return;
		}
		m_Update = nullptr;
		m_Post = nullptr;
	}

} // namespace sgre::core
