#pragma once

#include "sgre/scene/bitmap_font_assets.hpp"
#include "sgre/scene/bitmap_indicator.hpp"
#include "sgre/scene/common_process_tasks.hpp"
#include "sgre/scene/default_driver_scene.hpp"
#include "sgre/scene/looper_hook_tasks.hpp"

#include <cstdint>
#include <memory>
#include <thread>

namespace sgre::scene {

	struct ResourceMeterCounter {
		std::uint32_t Value = 0;
		// Native532800 increments exactly one DWORD; overflow wraps independently of scheduling.
		void Advance() noexcept;
	};

	struct ResourceMeterViewport {
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
	};

	class ResourceMeterProcess {
	public:
		// Each Meter creates its actual fresh PSB descriptor/provider and retains the same supplied resident backing.
		// Actual hook groups and physical font cache enclose this default-mode0 owner and all graph callbacks.
		ResourceMeterProcess(LooperHookTasks& actual_hooks, TextureCache& actual_cache, std::shared_ptr<const LoadedBitmapFontAsset> actual_embedded_font, ResourceMeterViewport actual_renderer_viewport);
		~ResourceMeterProcess() noexcept;
		ResourceMeterProcess(const ResourceMeterProcess&) = delete;
		ResourceMeterProcess& operator=(const ResourceMeterProcess&) = delete;
		ResourceMeterProcess(ResourceMeterProcess&&) = delete;
		ResourceMeterProcess& operator=(ResourceMeterProcess&&) = delete;

		[[nodiscard]] const CommonProcessTasks& Tasks() const noexcept;
		[[nodiscard]] const BitmapIndicator& Indicator() const;
		[[nodiscard]] const BitmapFontTextureOwner& FontTextures() const;
		[[nodiscard]] std::uint32_t ProcessingCount() const;
		[[nodiscard]] static constexpr bool DefaultDrawPaused() noexcept {
			return true;
		}
		// These operations borrow this real owner. Their scene registration remains DrawPaused as in23b020.
		[[nodiscard]] DefaultDriverOwnedTaskOperations DriverTaskOperations();
		void Update();
		// Native task traversal returns before this callback while DrawPaused. Delivered unpaused drawing is unsupported.
		void Draw(std::uint32_t argument);
		[[nodiscard]] bool CanClose() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;
		// Preflight both actual registrations/Common before any mutation, then Post, Pre, font, elements, Common.
		void Close();

	private:
		class CallbackLifetime;
		class CallbackScope;
		void RequireOpen() const;
		[[nodiscard]] bool CanRetireHook(LooperHookPhase phase, ParallelTaskHandle task, bool retired) const noexcept;
		void RetireHook(LooperHookPhase phase, ParallelTaskHandle task, bool retired);
		LooperHookTasks& m_Hooks;
		CommonProcessTasks m_Common;
		std::unique_ptr<BitmapFontTextureOwner> m_FontTextures;
		std::unique_ptr<BitmapIndicator> m_Indicator;
		ParallelTaskHandle m_PreLoopClock;
		ParallelTaskHandle m_PostLoopClock;
		ResourceMeterCounter m_Counter;
		std::shared_ptr<CallbackLifetime> m_CallbackLifetime;
		const std::thread::id m_OwnerThread = std::this_thread::get_id();
		bool m_PreLoopRetired = true;
		bool m_PostLoopRetired = true;
		bool m_Calling = false;
		bool m_Closed = false;
	};

} // namespace sgre::scene
