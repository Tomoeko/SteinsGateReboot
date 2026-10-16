#pragma once

#include "sgre/scene/layers.hpp"
#include "sgre/scene/parallel_task_set.hpp"

#include <cstdint>
#include <functional>
#include <variant>

namespace sgre::scene {

	using LayerFrameManager = std::variant<std::monostate, LayerHandle, LayerScreenRootHandle>;

	struct LayerFrameProcessState {
		TaskStatus Status = TaskStatus::Active;
		bool ThreadedUpdate = false;
	};

	enum class LayerManagerDrawMode : std::uint8_t {
		ResetRenderer,
		PreserveRenderer
	};

	struct LayerManagerFrameOperations {
		std::function<LayerFrameProcessState(LayerHandle)> State;
		std::function<void(LayerHandle)> Update;
		std::function<void(LayerHandle, std::uint32_t)> Draw;
		// The concrete manager supplies its recovered renderer-reset branch, after priority preparation.
		std::function<void()> PrepareDraw;
	};

	class LayerManagerFrame {
	public:
		// Callbacks resolve actual Layer owners from their existing adapter; this object retains no script registry.
		// Tree, this driver and callback services outlive traversal. State and PrepareDraw are observational services.
		// Callbacks borrow VM identities; resolving them into owned receivers must not add a manager self lease.
		// Last-root loss of the current Layer/manager owner is outside the supported lifetime domain.
		// Native Folder managers preserve their surrounding camera; root managers require the concrete reset branch.
		LayerManagerFrame(LayerTree& tree, LayerFrameManager manager, LayerManagerFrameOperations operations, LayerManagerDrawMode draw_mode = LayerManagerDrawMode::ResetRenderer);
		LayerManagerFrame(const LayerManagerFrame&) = delete;
		LayerManagerFrame& operator=(const LayerManagerFrame&) = delete;
		LayerManagerFrame(LayerManagerFrame&&) = delete;
		LayerManagerFrame& operator=(LayerManagerFrame&&) = delete;
		void Update();
		void Draw(std::uint32_t argument);

	private:
		void Visit(const std::function<void(LayerHandle)>& operation);
		void PrepareOrder();
		[[nodiscard]] LayerFrameProcessState State(LayerHandle layer) const;
		LayerTree& m_Tree;
		LayerFrameManager m_Manager;
		LayerManagerFrameOperations m_Operations;
		LayerManagerDrawMode m_DrawMode;
		std::function<void(LayerHandle)> m_UpdateVisit;
		std::function<void(LayerHandle)> m_DrawVisit;
		std::uint32_t m_DrawArgument = 0;
		bool m_Calling = false;
	};

} // namespace sgre::scene
