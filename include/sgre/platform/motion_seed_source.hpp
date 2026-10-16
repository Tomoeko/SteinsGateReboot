#pragma once

#include "sgre/core/looper_frame.hpp"
#include "sgre/platform/host_performance_counter.hpp"
#include "sgre/scene/motion_seed_buffer.hpp"

#include <thread>

namespace sgre::platform {

	class MacMotionSeedSource {
	public:
		// The actual application dispatches this Looper and keeps these owners alive through the final VSync.
		MacMotionSeedSource(core::LooperFrame& actual_looper, core::ThreadTaskFrame& actual_workers, const HostPerformanceCounter& actual_counter);
		~MacMotionSeedSource() noexcept;
		MacMotionSeedSource(const MacMotionSeedSource&) = delete;
		MacMotionSeedSource& operator=(const MacMotionSeedSource&) = delete;
		MacMotionSeedSource(MacMotionSeedSource&&) = delete;
		MacMotionSeedSource& operator=(MacMotionSeedSource&&) = delete;
		// Configured Layers borrow this stable buffer; retire them before closing or destroying this source.
		[[nodiscard]] const scene::MotionSeedBuffer& SeedBuffer() const;
		[[nodiscard]] bool IsClosed() const noexcept;
		// Retire only this exact VSync clock at a genuine inactive containing boundary.
		void Close();

	private:
		void RequireOpen() const;
		void RegisterClock();
		void UpdateClock(scene::ParallelTaskHandle actual_clock);
		[[nodiscard]] bool CanReleaseClock(scene::ParallelTaskHandle actual_clock) const noexcept;
		void ReleaseClock(scene::ParallelTaskHandle actual_clock);
		core::LooperFrame& m_Looper;
		core::ThreadTaskFrame& m_Workers;
		const HostPerformanceCounter& m_Counter;
		scene::MotionSeedBuffer m_Seed;
		scene::ParallelTaskHandle m_Clock;
		const core::LooperContainingMutation* m_Mutation = nullptr;
		const std::thread::id m_Thread = std::this_thread::get_id();
		bool m_Registered = false;
		bool m_Updating = false;
		bool m_Retiring = false;
		bool m_Closed = false;
	};

} // namespace sgre::platform
