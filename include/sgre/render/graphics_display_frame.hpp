#pragma once

#include "sgre/core/application_clock_pump.hpp"
#include "sgre/core/looper_frame.hpp"
#include "sgre/render/graphics_frame_cycle.hpp"
#include "sgre/render/presentation_synchronization.hpp"

#include <thread>

namespace sgre::platform {
	class MacApplicationPump;
}

namespace sgre::render {
	class GraphicsDisplayFrame {
	public:
		// Actual Looper/graphics/surface/workers/cache owners enclose this nonmoving Display frame and its clocks.
		GraphicsDisplayFrame(core::LooperFrame& actual_looper, SpriteGraphicsTargets& actual_graphics, SpritePassPresentation& actual_surface, core::ThreadTaskFrame& actual_workers, scene::TextureCache& actual_textures, RenderFrameCounters actual_current, RenderFrameCounters actual_previous, GraphicsFrameCycleLimits limits = {});
		~GraphicsDisplayFrame() noexcept;
		GraphicsDisplayFrame(const GraphicsDisplayFrame&) = delete;
		GraphicsDisplayFrame& operator=(const GraphicsDisplayFrame&) = delete;
		GraphicsDisplayFrame(GraphicsDisplayFrame&&) = delete;
		GraphicsDisplayFrame& operator=(GraphicsDisplayFrame&&) = delete;
		// Actual startup Layer registrations/bindings borrow the contained cycle until their explicit retirement.
		[[nodiscard]] GraphicsFrameCycle& Cycle();
		// The genuine root Draw callback invokes this after Update/workers and before drawing actual layers.
		void BeginRootDraw();
		// Invoke the real containing Looper; authored error cleanup preserves the original cause without draining work.
		[[nodiscard]] bool RunIteration();
		// Hold genuine containing mutation while resources retire, then unlink Post before Pre.
		void Close();
		[[nodiscard]] PresentationSynchronization& Synchronization();
		// The actual latest completed cycle must match the surface before a drawable can be requested.
		[[nodiscard]] SpritePresentationStatus PresentCurrent(bool force_unsynchronized);

	private:
		friend class platform::MacApplicationPump;
		void RequirePumpBorrowAdmission(const void* actual_pump, const SpritePassPresentation& actual_surface, const core::ApplicationClockPump& actual_clock, const core::ApplicationPumpPlatform& actual_platform) const;
		void RequirePumpBorrowRetirement(const void* actual_pump) const;
		void RegisterPumpBorrow(const void* actual_pump, const SpritePassPresentation& actual_surface, const core::ApplicationClockPump& actual_clock, const core::ApplicationPumpPlatform& actual_platform);
		void RequirePumpPhase(const core::ApplicationClockPump& actual_clock, const core::ApplicationPumpPlatform& actual_platform) const;
		void ReleasePumpBorrow(const void* actual_pump);
		void RequireOpen() const;
		void RegisterClock(scene::LooperHookPhase phase);
		void RetireClock(scene::LooperHookPhase phase);
		void RequireClockRetirement(scene::LooperHookPhase phase) const;
		[[nodiscard]] bool CanReleaseClock(scene::ParallelTaskHandle actual_clock) const noexcept;
		void ReleaseClock(scene::ParallelTaskHandle actual_clock);
		core::LooperFrame& m_Looper;
		core::ThreadTaskFrame& m_Workers;
		SpritePassPresentation& m_Surface;
		GraphicsFrameCycle m_Cycle;
		PresentationSynchronization m_Synchronization;
		const void* m_PumpBorrow = nullptr;
		scene::ParallelTaskHandle m_PreClock;
		scene::ParallelTaskHandle m_PostClock;
		const core::LooperContainingMutation* m_Mutation = nullptr;
		const std::thread::id m_Thread = std::this_thread::get_id();
		bool m_PreRegistered = false;
		bool m_PostRegistered = false;
		bool m_Retiring = false;
		bool m_Closed = false;
	};
} // namespace sgre::render
