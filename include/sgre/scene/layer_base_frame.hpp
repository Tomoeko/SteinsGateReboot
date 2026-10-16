#pragma once

#include "sgre/scene/common_process_tasks.hpp"
#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/scene/layers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace sgre::scene {

	class LayerBaseTasks {
	public:
		explicit LayerBaseTasks(std::size_t max_tasks_per_group = 64);
		[[nodiscard]] ParallelTaskSet& CommonFirst() noexcept;
		[[nodiscard]] ParallelTaskSet& CommonSecond() noexcept;
		[[nodiscard]] ParallelTaskSet& Children() noexcept;
		[[nodiscard]] bool CanClear() const noexcept;
		void Clear();

	private:
		ParallelTaskSet m_CommonFirst;
		ParallelTaskSet m_CommonSecond;
		ParallelTaskSet m_Children;
	};

	struct LayerBaseFrameState {
		MotionBounds BaseClip{1, 1, -1, -1};
		LayerCameraParameters Camera;
	};

	class LayerBaseFrame {
	public:
		using ReadState = std::function<LayerBaseFrameState()>;
		// Native22c360 constructs the common groups and clip/camera chain; concrete children attach to the actual camera group.
		// Tree, process, groups and renderer outlive this frame; it owns no script receiver or child Layer manager.
		LayerBaseFrame(CommonProcessTasks& process, LayerBaseTasks& groups, LayerTree& tree, LayerHandle layer, LayerCameraDrawScope& renderer, ReadState state);
		~LayerBaseFrame() noexcept;
		LayerBaseFrame(const LayerBaseFrame&) = delete;
		LayerBaseFrame& operator=(const LayerBaseFrame&) = delete;
		LayerBaseFrame(LayerBaseFrame&&) = delete;
		LayerBaseFrame& operator=(LayerBaseFrame&&) = delete;

		void Update();
		void Draw(std::uint32_t argument);
		[[nodiscard]] bool CanClose() const noexcept;
		// Detachment preserves concrete groups/children; only Close may transfer attached root adapters.
		void Close();
		// After containing-manager unlink and weak-parent drop, native base deletion retires parallel before serial.
		// Root callbacks clear actual CommonFirst, CommonSecond and camera children in their physical list order.
		void RetireParallel();

	private:
		void RegisterRoot(std::size_t index, ParallelTaskCallbacks callbacks);
		[[nodiscard]] ParallelTaskCallbacks GroupCallbacks(ParallelTaskSet& group);
		void RequireOpen() const;
		CommonProcessTasks& m_Process;
		LayerBaseTasks& m_Groups;
		LayerCameraDrawScope& m_Renderer;
		ReadState m_ReadState;
		ParallelTaskSet m_BaseClipChildren{1};
		std::array<ParallelTaskHandle, 3> m_Roots;
		std::array<bool, 3> m_RootReleased{};
		bool m_Detaching = false;
		bool m_Closed = false;
	};

} // namespace sgre::scene
