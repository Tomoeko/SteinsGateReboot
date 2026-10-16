#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace sgre::core {
	class LooperFrame;
	class LooperContainingMutation;
} // namespace sgre::core

namespace sgre::scene {

	enum class TaskStatus : std::uint32_t {
		Active = 0,
		Complete = 1,
		Retire = 2
	};

	struct ParallelTaskHandle {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const ParallelTaskHandle&) const = default;
	};

	struct ParallelTaskState {
		TaskStatus Status = TaskStatus::Active;
		bool DrawPaused = false;
	};

	struct ParallelTaskCallbacks {
		std::function<void(ParallelTaskHandle)> Update;
		std::function<void(ParallelTaskHandle, std::uint32_t)> Draw;
		std::function<void(ParallelTaskHandle)> Release;
		// Authored observational lifetime preflight, not a native pin. It cannot mutate or reenter the set.
		// An absent callback preserves the existing unborrowed task protocol.
		std::function<bool(ParallelTaskHandle)> CanRelease;
	};

	class ParallelTaskLease {
	public:
		ParallelTaskLease() = default;
		~ParallelTaskLease();
		ParallelTaskLease(const ParallelTaskLease&) = delete;
		ParallelTaskLease& operator=(const ParallelTaskLease&) = delete;
		ParallelTaskLease(ParallelTaskLease&& other) noexcept;
		ParallelTaskLease& operator=(ParallelTaskLease&& other) noexcept;
		[[nodiscard]] bool IsEmpty() const noexcept;
		[[nodiscard]] ParallelTaskState State() const;
		// Detached ownership preserves callbacks and concrete resources without invoking Release.
		void Release();

	private:
		ParallelTaskCallbacks m_Callbacks;
		ParallelTaskState m_State;
		ParallelTaskHandle m_PreviousHandle;
		bool m_Owned = false;
		friend class ParallelTaskSet;
	};

	class ParallelTaskSet {
	public:
		// Callbacks own or borrow concrete task resources; borrowed services outlive this task set.
		explicit ParallelTaskSet(std::size_t max_tasks = 1024);
		~ParallelTaskSet();
		ParallelTaskSet(const ParallelTaskSet&) = delete;
		ParallelTaskSet& operator=(const ParallelTaskSet&) = delete;
		ParallelTaskSet(ParallelTaskSet&&) = delete;
		ParallelTaskSet& operator=(ParallelTaskSet&&) = delete;

		[[nodiscard]] ParallelTaskHandle Append(ParallelTaskCallbacks callbacks, ParallelTaskState initial = {});
		[[nodiscard]] ParallelTaskHandle Prepend(ParallelTaskCallbacks callbacks, ParallelTaskState initial = {});
		// Transfer requires stable source/destination boundaries; active intrusive reparenting remains unverified.
		[[nodiscard]] bool CanExtract(ParallelTaskHandle task) const noexcept;
		[[nodiscard]] ParallelTaskLease Extract(ParallelTaskHandle task);
		[[nodiscard]] bool CanAppendLease() const noexcept;
		[[nodiscard]] ParallelTaskHandle AppendLease(ParallelTaskLease&& task);
		[[nodiscard]] bool Contains(ParallelTaskHandle task) const noexcept;
		[[nodiscard]] ParallelTaskState State(ParallelTaskHandle task) const;
		void SetStatus(ParallelTaskHandle task, TaskStatus status);
		void SetDrawPaused(ParallelTaskHandle task, bool paused);
		void SetRetireWhenEmpty(bool enabled);
		[[nodiscard]] TaskStatus Status() const noexcept;
		[[nodiscard]] std::size_t Count() const noexcept;
		[[nodiscard]] std::size_t WriteOrder(std::span<ParallelTaskHandle> output) const;
		// Only Active tasks update. Complete tasks remain owned and drawable until explicit retirement.
		void Update();
		// Native drawing forwards one 32-bit argument, skipping paused and Retire tasks.
		void Draw(std::uint32_t argument);
		// Clear retires every task in list order, completing cleanup before reporting the first callback failure.
		// Composite owners preflight retirement before releasing any dependent resources.
		// Observes the actual traversal/release phase without invoking task retirement callbacks.
		// The serialized containing owner supplies thread and lifetime admission.
		void RequireIdleTraversal() const;
		[[nodiscard]] bool CanClear() const noexcept;
		void Clear();

	private:
		friend class core::LooperFrame;
		void EnterContainingMutation(const core::LooperContainingMutation& actual_scope);
		void LeaveContainingMutation(const core::LooperContainingMutation& actual_scope);
		void RequireContainingMutation(const core::LooperContainingMutation& actual_scope) const;
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
