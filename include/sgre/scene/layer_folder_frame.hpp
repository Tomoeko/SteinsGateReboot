#pragma once

#include "sgre/scene/common_process_tasks.hpp"
#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/scene/layer_manager_frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace sgre::scene {

	class LayerFolderTasks {
	public:
		explicit LayerFolderTasks(std::size_t max_tasks_per_group = 64);
		[[nodiscard]] ParallelTaskSet& CommonFirst() noexcept;
		[[nodiscard]] ParallelTaskSet& CommonSecond() noexcept;
		[[nodiscard]] bool CanClear() const noexcept;
		void Clear();

	private:
		ParallelTaskSet m_CommonFirst;
		ParallelTaskSet m_CommonSecond;
	};

	struct LayerFolderFrameState {
		MotionBounds BaseClip{1, 1, -1, -1};
		LayerCameraParameters Camera;
	};

	class LayerFolderFrame {
	public:
		using ReadState = std::function<LayerFolderFrameState()>;
		// Actual Folder owns process/groups and its existing Tree child list. Renderer/services outlive this attachment.
		// clear_children performs the existing native weak-owner/child-manager clear; it adds no receiver roots.
		LayerFolderFrame(CommonProcessTasks& process, LayerFolderTasks& groups, LayerTree& tree, LayerHandle folder, LayerCameraDrawScope& renderer, ReadState state, LayerManagerFrameOperations children, std::function<void()> clear_children);
		~LayerFolderFrame() noexcept;
		LayerFolderFrame(const LayerFolderFrame&) = delete;
		LayerFolderFrame& operator=(const LayerFolderFrame&) = delete;
		LayerFolderFrame(LayerFolderFrame&&) = delete;
		LayerFolderFrame& operator=(LayerFolderFrame&&) = delete;

		void Update();
		void Draw(std::uint32_t argument);
		[[nodiscard]] bool CanClose() const noexcept;
		// Detachment preserves concrete groups/children; only Close may transfer attached root adapters.
		void Close();
		// After containing-manager unlink and weak-parent drop, native base deletion retires parallel before serial.
		// Root callbacks clear actual CommonFirst, CommonSecond and child manager in their physical list order.
		void RetireParallel();

	private:
		void RegisterRoot(std::size_t index, ParallelTaskCallbacks callbacks);
		[[nodiscard]] ParallelTaskCallbacks GroupCallbacks(ParallelTaskSet& group);
		void RequireOpen() const;
		CommonProcessTasks& m_Process;
		LayerFolderTasks& m_Groups;
		LayerCameraDrawScope& m_Renderer;
		ReadState m_ReadState;
		std::function<void()> m_ClearChildren;
		LayerManagerFrame m_Children;
		ParallelTaskSet m_BaseClipChildren{1};
		ParallelTaskSet m_CameraChildren{1};
		std::array<ParallelTaskHandle, 3> m_Roots;
		std::array<bool, 3> m_RootReleased{};
		bool m_Detaching = false;
		bool m_Closed = false;
	};

} // namespace sgre::scene
