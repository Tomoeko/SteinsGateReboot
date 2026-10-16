#pragma once

#include "sgre/core/frame_clock.hpp"
#include "sgre/core/thread_task_frame.hpp"
#include "sgre/scene/looper_hook_tasks.hpp"
#include "sgre/scene/serial_task_set.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace sgre::core {

	class ApplicationClockPump;
	class LooperFrame;

	class LooperContainingMutation {
	public:
		// These exact actual owners enclose the stable scope. No iteration or worker delivery may reenter its cleanup.
		LooperContainingMutation(LooperFrame& actual_looper, ThreadTaskFrame& actual_workers);
		~LooperContainingMutation() noexcept;
		LooperContainingMutation(const LooperContainingMutation&) = delete;
		LooperContainingMutation& operator=(const LooperContainingMutation&) = delete;
		LooperContainingMutation(LooperContainingMutation&&) = delete;
		LooperContainingMutation& operator=(LooperContainingMutation&&) = delete;
		// Qualified cleanup can observe its own live claim without requiring clocks or all receivers already retired.
		void RequireOwner(const LooperFrame& actual_looper, const ThreadTaskFrame& actual_workers) const;

	private:
		LooperFrame& m_Looper;
		ThreadTaskFrame& m_Workers;
	};

	struct LooperTaskState {
		scene::TaskStatus Status = scene::TaskStatus::Active;
		bool DrawPaused = false;
		bool Asynchronous = false;
	};

	struct LooperTaskOperations {
		std::function<LooperTaskState()> ReadState;
		std::function<void()> Update;
		std::function<void(std::uint32_t)> Draw;
		std::function<bool()> CanRetire;
		// The real owner completes structural retirement before reporting a failure.
		std::function<void()> Retire;
	};

	enum class LooperFrameGroup : std::uint16_t {
		VSync = 0x40,
		AfterUpdate = 0xc0,
		AfterWait = 0x140
	};

	class LooperFrame {
	public:
		// The actual task, worker frame and event/clock pump enclose this serialized phase owner.
		// The pump must implement its real platform event/wait policy; this owner supplies no replacement pump.
		LooperFrame(ThreadTaskFrame& workers, LooperTaskOperations actual_task, std::function<void(LooperFrame&)> actual_pump, std::uint64_t initial_milliseconds, std::size_t max_tasks_per_group = 1024);
		// The concrete root object and its real callback dependencies enclose this typed frame through retirement.
		LooperFrame(ThreadTaskFrame& workers, scene::SerialTask& actual_root, ApplicationClockPump& actual_pump, std::size_t max_tasks_per_group = 1024);
		~LooperFrame() noexcept;
		LooperFrame(const LooperFrame&) = delete;
		LooperFrame& operator=(const LooperFrame&) = delete;
		[[nodiscard]] FrameClock& Clock();
		[[nodiscard]] scene::LooperHookTasks& Hooks();
		[[nodiscard]] scene::ParallelTaskSet& Group(LooperFrameGroup group);
		// Executes one original 4e0660 iteration, including the final wait/VSync phase after task retirement.
		// Returns true only while that exact task remains Active.
		[[nodiscard]] bool RunIteration();
		// Actual containing cleanup 4e0620 is distinct from state2 retirement inside the frame.
		void ReleaseTask();
		// Original535f20 writes status2 to this exact current root while the containing wait phase is active.
		void MarkCurrentTaskRetire();
		void SkipNextIteration();
		void ConsumeFixedTick();
		// Original4ca110 reads the immutable constructor base +8, independently of residual +0c and variable +20.
		[[nodiscard]] std::uint32_t FixedBaseCount() const;
		[[nodiscard]] std::uint32_t RemainingFixedTicks() const;
		[[nodiscard]] std::uint32_t FrameNumber() const;
		[[nodiscard]] double AccumulatedTicks() const;
		[[nodiscard]] bool InWait() const;
		[[nodiscard]] bool SkipPending() const;
		[[nodiscard]] bool HasTask() const;
		// Live containing admission for resource/hook mutation; registered clocks need not already be gone.
		// This checks actual phase/traversal owners, not resource borrowers, pending work or whole-app readiness.
		void RequireIdleContainingPhase() const;
		[[nodiscard]] bool CanClose() const noexcept;
		// The real task and every hook/group callback must retire before the containing phase owner.
		void Close();

	private:
		friend class LooperContainingMutation;
		void EnterContainingMutation(const LooperContainingMutation& actual_scope, ThreadTaskFrame& actual_workers);
		void LeaveContainingMutation(const LooperContainingMutation& actual_scope);
		void RequireContainingMutation(const LooperContainingMutation& actual_scope, const ThreadTaskFrame& actual_workers) const;
		void RequireOpen() const;
		[[nodiscard]] std::array<scene::ParallelTaskSet*, 5> ContainingMutationGroups();
		void UpdateAndDraw();
		void RetireTask();
		void AfterWait();
		ThreadTaskFrame& m_Workers;
		LooperTaskOperations m_Task;
		scene::SerialTask* m_ActualRoot = nullptr;
		ApplicationClockPump* m_ActualPump = nullptr;
		std::function<void(LooperFrame&)> m_Pump;
		FrameClock m_Clock;
		scene::LooperHookTasks m_Hooks;
		scene::ParallelTaskSet m_VSync;
		scene::ParallelTaskSet m_AfterUpdate;
		scene::ParallelTaskSet m_AfterWait;
		std::mutex m_AfterWaitMutex;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		const std::uint32_t m_FixedBaseCount = 1;
		std::uint32_t m_RemainingFixedTicks = m_FixedBaseCount;
		std::uint32_t m_FrameNumber = 0;
		double m_AccumulatedTicks = 0;
		bool m_HasTask = true;
		bool m_SkipNext = false;
		bool m_InWait = false;
		bool m_Running = false;
		bool m_Closed = false;
		const LooperContainingMutation* m_ContainingMutation = nullptr;
	};

} // namespace sgre::core
