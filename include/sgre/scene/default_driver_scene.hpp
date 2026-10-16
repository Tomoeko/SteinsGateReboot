#pragma once

#include "sgre/scene/common_process_tasks.hpp"
#include "sgre/scene/layer_manager_frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace sgre::scene {

	enum class DefaultDriverTaskGroup : std::uint16_t {
		Priority = 0x118,
		BeforeLayersFirst = 0x128,
		BeforeLayersSecond = 0x130,
		AfterResourceMeter = 0x120
	};

	struct DefaultDriverOwnedTaskOperations {
		std::function<void()> Update;
		std::function<void(std::uint32_t)> Draw;
		// Real native owner cleanup completes its structural retirement before returning or reporting a failure.
		std::function<void()> Retire;
	};

	struct DefaultDriverSceneServices {
		DefaultDriverOwnedTaskOperations ResourceManager;
		DefaultDriverOwnedTaskOperations ResourceMeter;
		LayerManagerFrameOperations RootLayers;
	};

	class DefaultDriverScene {
	public:
		// Recovered SQDriver setup owns these concrete scene tasks, not a script Layer or Squirrel receiver.
		// Real resource/metric/renderer/Layer services and the controller process/Tree outlive this graph's retirement.
		// Setup may publish during the actual controller onProcess, before embedded parallel traversal starts.
		DefaultDriverScene(CommonProcessTasks& controller, LayerTree& tree, DefaultDriverSceneServices services, std::size_t max_tasks_per_group = 64);
		~DefaultDriverScene() noexcept;
		DefaultDriverScene(const DefaultDriverScene&) = delete;
		DefaultDriverScene& operator=(const DefaultDriverScene&) = delete;
		DefaultDriverScene(DefaultDriverScene&&) = delete;
		DefaultDriverScene& operator=(DefaultDriverScene&&) = delete;

		// The exact +118 group is the genuine supplied global Affine priority task owner.
		[[nodiscard]] ParallelTaskSet& Group(DefaultDriverTaskGroup group);
		void Update(const std::function<void()>& actual_driver_phase);
		void Draw(std::uint32_t argument);
		[[nodiscard]] bool CanClose() const noexcept;
		// Retires the actual embedded parallel graph in native order, including later appended native tasks.
		// VM/scenario/logger shutdown and controller serial destruction remain concrete caller-owned phases.
		void Close();

	private:
		void RegisterRoot(std::size_t index, ParallelTaskCallbacks callbacks, bool draw_paused = false);
		[[nodiscard]] ParallelTaskCallbacks GroupCallbacks(ParallelTaskSet& group);
		[[nodiscard]] ParallelTaskCallbacks OwnedTaskCallbacks(DefaultDriverOwnedTaskOperations& task);
		void RequireOpen() const;
		CommonProcessTasks& m_Controller;
		LayerTree& m_Tree;
		DefaultDriverOwnedTaskOperations m_ResourceManager;
		DefaultDriverOwnedTaskOperations m_ResourceMeter;
		ParallelTaskSet m_Priority;
		ParallelTaskSet m_BeforeLayersFirst;
		ParallelTaskSet m_BeforeLayersSecond;
		ParallelTaskSet m_AfterResourceMeter;
		LayerManagerFrame m_RootLayers;
		std::array<ParallelTaskHandle, 7> m_Roots;
		std::array<bool, 7> m_RootReleased{};
		bool m_Calling = false;
		bool m_Closed = false;
	};

} // namespace sgre::scene
