#pragma once

#include "sgre/scene/parallel_task_set.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace sgre::scene {

	class SerialTask;

	struct SerialTaskCallbacks {
		std::function<void(SerialTask&)> Update;
		std::function<void(SerialTask&, std::uint32_t)> Draw;
		std::function<void()> Release;
		// Observational concrete-owner gate; false or failure rejects cleanup before structural mutation.
		std::function<bool()> CanRelease{};
	};

	class SerialTask {
	public:
		explicit SerialTask(SerialTaskCallbacks callbacks, ParallelTaskState initial = {});
		~SerialTask();
		SerialTask(const SerialTask&) = delete;
		SerialTask& operator=(const SerialTask&) = delete;
		SerialTask(SerialTask&&) = delete;
		SerialTask& operator=(SerialTask&&) = delete;

		[[nodiscard]] ParallelTaskState State() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;
		// A genuine containing frame requires concrete operations, rather than optional base-task callbacks.
		void RequireFrameCallbacks() const;
		void SetStatus(TaskStatus status);
		void SetDrawPaused(bool paused);
		void Update();
		void Draw(std::uint32_t argument);
		[[nodiscard]] bool CanClose() const noexcept;
		// Explicit Close preflights concrete ownership, then reports release failures after retirement.
		void Close();

	private:
		SerialTaskCallbacks m_Callbacks;
		ParallelTaskState m_State;
		bool m_Closed = false;
		bool m_Calling = false;
	};

	struct SerialTaskHandle {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const SerialTaskHandle&) const = default;
	};

	class SerialTaskSet {
	public:
		using Completed = std::function<void(std::unique_ptr<SerialTask>)>;
		// Status 1 leaves native ownership with an external caller. The receiver must retain that owner.
		explicit SerialTaskSet(std::size_t max_tasks = 1024, Completed completed = {});
		~SerialTaskSet();
		SerialTaskSet(const SerialTaskSet&) = delete;
		SerialTaskSet& operator=(const SerialTaskSet&) = delete;
		SerialTaskSet(SerialTaskSet&&) = delete;
		SerialTaskSet& operator=(SerialTaskSet&&) = delete;

		[[nodiscard]] SerialTaskHandle Append(std::unique_ptr<SerialTask> task);
		[[nodiscard]] SerialTaskHandle Prepend(std::unique_ptr<SerialTask> task);
		[[nodiscard]] bool Contains(SerialTaskHandle task) const noexcept;
		[[nodiscard]] ParallelTaskState State(SerialTaskHandle task) const;
		void SetStatus(SerialTaskHandle task, TaskStatus status);
		void SetDrawPaused(SerialTaskHandle task, bool paused);
		void SetRetireWhenEmpty(bool enabled);
		[[nodiscard]] TaskStatus Status() const noexcept;
		[[nodiscard]] std::size_t Count() const noexcept;
		[[nodiscard]] std::size_t WriteOrder(std::span<SerialTaskHandle> output) const;
		// Head tasks run until one stays Active. Retire closes; Complete transfers without Close.
		void Update();
		// Drawing only visits the current unpaused head, skipping Retire state.
		void Draw(std::uint32_t argument);
		// Composite owners preflight retirement before releasing any dependent resources.
		[[nodiscard]] bool CanClear() const noexcept;
		void Clear();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
