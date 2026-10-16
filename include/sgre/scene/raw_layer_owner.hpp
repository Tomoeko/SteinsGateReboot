#pragma once

#include "sgre/scene/common_process_tasks.hpp"
#include "sgre/scene/layers.hpp"
#include "sgre/scene/raw_texture_task.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>

namespace sgre::scene {

	class RawLayerFrame;

	struct RawLayerAttachment {
		std::optional<LayerHandle> Folder;
		std::optional<LayerScreenRootHandle> Screen;
	};

	struct RawLayerRetirement {
		// Script bridges drop the actual weak parent and Object base at these distinct phases.
		// Callbacks borrow the owner; its storage remains alive until Close returns.
		std::function<void()> DropParent;
		std::function<void()> ReleaseObject;
	};

	enum class RawLayerTaskGroup {
		CommonFirst,
		CommonSecond
	};

	class RawLayerOwner {
	public:
		// Tree, background driver and script bridge services outlive this exclusive common owner.
		// The driver owns the background task; this owner marks it for delayed retirement.
		RawLayerOwner(LayerTree& tree, std::unique_ptr<RawTextureTask> texture_task, ParallelTaskSet& background_driver, ParallelTaskHandle background_task, RawLayerRetirement retirement, RawLayerAttachment attachment = {}, std::size_t max_tasks_per_group = 64, SerialTaskSet::Completed completed = {});
		~RawLayerOwner();
		RawLayerOwner(const RawLayerOwner&) = delete;
		RawLayerOwner& operator=(const RawLayerOwner&) = delete;
		RawLayerOwner(RawLayerOwner&&) = delete;
		RawLayerOwner& operator=(RawLayerOwner&&) = delete;

		[[nodiscard]] LayerHandle Handle() const noexcept;
		[[nodiscard]] bool IsClosed() const noexcept;
		[[nodiscard]] bool CanClose() const noexcept;
		[[nodiscard]] RawTextureTask& TextureTask();
		[[nodiscard]] ParallelTaskSet& Group(RawLayerTaskGroup group);
		[[nodiscard]] SerialTaskSet& SerialTasks();
		[[nodiscard]] CommonProcessTasks& CommonTasks();
		// Implements the verified SQRawTex/common lifetime; does not perform scene drawing.
		void Close();

	private:
		void RequireOpen() const;
		friend class RawLayerFrame;
		LayerTree& m_Tree;
		ParallelTaskSet& m_BackgroundDriver;
		ParallelTaskHandle m_BackgroundTask;
		LayerHandle m_Handle;
		ParallelTaskSet m_CommonFirst;
		ParallelTaskSet m_CommonSecond;
		CommonProcessTasks m_Process;
		RawLayerFrame* m_Frame = nullptr;
		std::unique_ptr<RawTextureTask> m_TextureTask;
		RawLayerRetirement m_Retirement;
		bool m_Closed = false;
	};

} // namespace sgre::scene
