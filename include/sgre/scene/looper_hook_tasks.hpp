#pragma once

#include "sgre/scene/parallel_task_set.hpp"

#include <cstddef>
#include <cstdint>
#include <thread>

namespace sgre::scene {

	enum class LooperHookPhase : std::uint16_t {
		PreLoop = 0x80,
		PostLoop = 0x100
	};

	class LooperHookTasks {
	public:
		// These are the actual two embedded hook groups, not a second registry of hook receivers.
		// Their containing application and all hook parents establish the enclosing lifetime.
		explicit LooperHookTasks(std::size_t max_tasks_per_group = 1024);
		~LooperHookTasks() noexcept;
		LooperHookTasks(const LooperHookTasks&) = delete;
		LooperHookTasks& operator=(const LooperHookTasks&) = delete;
		LooperHookTasks(LooperHookTasks&&) = delete;
		LooperHookTasks& operator=(LooperHookTasks&&) = delete;

		[[nodiscard]] ParallelTaskSet& Group(LooperHookPhase phase);
		[[nodiscard]] const ParallelTaskSet& Group(LooperHookPhase phase) const;
		void Update(LooperHookPhase phase);
		[[nodiscard]] bool CanClose() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;
		// Actual hook parents retire their clock tasks before this containing owner.
		// This closes neither a whole MLooper nor an application/VM/Driver.
		void Close();

	private:
		void RequireOpen() const;
		ParallelTaskSet m_PreLoop;
		ParallelTaskSet m_PostLoop;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		bool m_Closed = false;
	};

} // namespace sgre::scene
