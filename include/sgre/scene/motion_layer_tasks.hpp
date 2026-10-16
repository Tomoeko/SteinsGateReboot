#pragma once

#include "sgre/scene/parallel_task_set.hpp"

#include <cstddef>
#include <cstdint>

namespace sgre::script {
	class ScriptLayerBindings;
}

namespace sgre::scene {

	enum class MotionTaskGroup : std::uint8_t {
		CommonFirst,
		CommonSecond,
		First,
		Second
	};

	class MotionLayerTasks {
	public:
		// Actual Layer owns four nested parallel groups. Group identifiers retain constructor ordering.
		explicit MotionLayerTasks(std::size_t max_tasks_per_group = 64);
		~MotionLayerTasks();
		MotionLayerTasks(const MotionLayerTasks&) = delete;
		MotionLayerTasks& operator=(const MotionLayerTasks&) = delete;
		MotionLayerTasks(MotionLayerTasks&&) = delete;
		MotionLayerTasks& operator=(MotionLayerTasks&&) = delete;

		[[nodiscard]] ParallelTaskSet& Group(MotionTaskGroup group);
		[[nodiscard]] const ParallelTaskSet& Group(MotionTaskGroup group) const;
		// Common groups and the concrete second group retire before the secondary source manager.
		void ClearBeforeSourceManagers();
		// Concrete first group retires after the secondary manager and before the primary manager.
		void ClearBetweenSourceManagers();
		void Clear();

	private:
		friend class script::ScriptLayerBindings;
		friend class MotionManagerRuntime;
		// Authored admission: actual installed Layers must expose their runtime/context to owner preflight.
		bool m_RequiresRegisteredRuntime = false;
		ParallelTaskSet m_CommonFirst;
		ParallelTaskSet m_CommonSecond;
		ParallelTaskSet m_First;
		ParallelTaskSet m_Second;
	};

} // namespace sgre::scene
