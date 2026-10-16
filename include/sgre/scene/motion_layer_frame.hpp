#pragma once

#include "sgre/scene/common_process_tasks.hpp"
#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/scene/motion_layer_tasks.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace sgre::scene {

	struct MotionLayerFrameState {
		MotionBounds BaseClip{1, 1, -1, -1};
		LayerCameraParameters Camera;
		MotionBounds ObjectClip{1, 1, -1, -1};
	};

	struct MotionLayerSourceDrawing {
		std::function<void(std::uint32_t)> Secondary;
		std::function<void(std::uint32_t)> Primary;
	};

	class MotionLayerFrame {
	public:
		using ReadState = std::function<MotionLayerFrameState()>;
		// The actual Layer owns process/groups, and its renderer owns scope/source drawing services.
		// All services and this attachment remain alive through every callback; callbacks borrow VM identities.
		// Only Close may detach/transfer the three root adapters while attached.
		MotionLayerFrame(CommonProcessTasks& process, MotionLayerTasks& groups, LayerCameraDrawScope& renderer, ReadState state, MotionLayerSourceDrawing drawing);
		// Unsupported active destruction fails fast before borrowed callbacks can dangle.
		~MotionLayerFrame() noexcept;
		MotionLayerFrame(const MotionLayerFrame&) = delete;
		MotionLayerFrame& operator=(const MotionLayerFrame&) = delete;
		MotionLayerFrame(MotionLayerFrame&&) = delete;
		MotionLayerFrame& operator=(MotionLayerFrame&&) = delete;

		// The native child manager provides containing process status and visibility gates.
		// Ordinary SQLayer uses the original return-only onProcess virtual; it performs no VM callback.
		void Update();
		void Update(const std::function<void()>& on_process);
		void Draw(std::uint32_t argument);
		[[nodiscard]] bool CanClose() const noexcept;
		// Unregister borrowed frame adapters before actual Layer-owned resource/task retirement.
		// Require CanClose before destruction begins; this checks adapters, not all borrowed group/source owners.
		void Close();

	private:
		void RegisterRoot(std::size_t index, ParallelTaskCallbacks callbacks);
		[[nodiscard]] ParallelTaskCallbacks GroupCallbacks(MotionTaskGroup group);
		void RequireOpen() const;
		CommonProcessTasks& m_Process;
		MotionLayerTasks& m_Groups;
		LayerCameraDrawScope& m_Renderer;
		ReadState m_ReadState;
		MotionLayerSourceDrawing m_Drawing;
		ParallelTaskSet m_BaseClipChildren{1};
		ParallelTaskSet m_CameraChildren{4};
		ParallelTaskSet m_ObjectClipChildren{1};
		std::array<ParallelTaskHandle, 3> m_Roots;
		std::array<bool, 3> m_RootReleased{};
		bool m_Closed = false;
	};

} // namespace sgre::scene
