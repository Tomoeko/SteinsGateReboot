#pragma once

#include "sgre/core/application_clock_pump.hpp"
#include "sgre/input/frame_sample.hpp"
#include "sgre/input/key_mapping.hpp"
#include "sgre/input/mouse.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace sgre::render {
	class SpritePassPresentation;
	class GraphicsDisplayFrame;
} // namespace sgre::render

namespace sgre::platform {

	class MacApplicationPump final : public core::ApplicationPumpPlatform {
	public:
		// The actual event owner and existing hidden presentation enclose this main-thread platform borrower.
		MacApplicationPump(core::ApplicationEventControl& events, render::SpritePassPresentation& presentation, std::size_t maximum_events = 1024);
		~MacApplicationPump() override;
		MacApplicationPump(const MacApplicationPump&) = delete;
		MacApplicationPump& operator=(const MacApplicationPump&) = delete;
		void RequireLive() const override;
		[[nodiscard]] std::int64_t Frequency() const override;
		[[nodiscard]] std::int64_t ReadRawCounter() override;
		void DispatchEvents(core::ApplicationEventControl& events, core::WindowFramePacing& pacing) override;
		void PrecisionWaitOneMillisecond() override;
		void SuspendedWaitSixteenMilliseconds() override;
		[[nodiscard]] bool HasBoundPresentationTarget() const override;
		// The attached target is real; its current-frame producer and original SyncInterval join remain required.
		void Present(bool force_unsynchronized) override;
		// The exact Display and clock owners outlive this concrete borrow; detach before either retires.
		void BindPresentation(render::GraphicsDisplayFrame& actual_frames, core::ApplicationClockPump& actual_clock);
		void UnbindPresentation();
		// The actual focused window gates real OS key state. Capture once at the actual post-wait/VSync input hook.
		[[nodiscard]] input::KeyboardSample ReadFunctionKeySample() const;
		// Each actual Hub VSync resolves its mappings and publishes shared mouse state.
		// Connected HID controllers remain a precise unsupported sampling domain.
		[[nodiscard]] input::HubFrameSample ReadHubFrame(const input::KeyMappings& actual_mappings);
		// Each actual Input wrapper samples its own cursor from the last Hub button/wheel output.
		[[nodiscard]] input::MouseSample ReadMouseFrame() const;
		[[nodiscard]] std::size_t ObservedControllerCount() const;
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::platform
