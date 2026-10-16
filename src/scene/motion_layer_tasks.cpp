#include "sgre/scene/motion_layer_tasks.hpp"

#include <array>
#include <exception>
#include <span>
#include <stdexcept>

namespace sgre::scene {

	static void ClearTaskGroups(std::span<ParallelTaskSet*> groups) {
		// Preflight every concrete group before any sibling cleanup; this adds no native owner retain.
		for (const auto* group : groups) {
			if (!group->CanClear()) {
				throw std::runtime_error("motion layer task retirement requires all actual group borrows to be consumed");
			}
		}

		std::exception_ptr failure;
		for (auto* group : groups) {
			try {
				group->Clear();
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}
		}

		if (failure) {
			std::rethrow_exception(failure);
		}
	}

	MotionLayerTasks::MotionLayerTasks(std::size_t max_tasks_per_group) : m_CommonFirst(max_tasks_per_group), m_CommonSecond(max_tasks_per_group), m_First(max_tasks_per_group), m_Second(max_tasks_per_group) {
	}

	MotionLayerTasks::~MotionLayerTasks() {
		try {
			Clear();
		} catch (...) {
			// Explicit retirement reports failures after clearing every actual task group.
		}
	}

	ParallelTaskSet& MotionLayerTasks::Group(MotionTaskGroup group) {
		switch (group) {
		case MotionTaskGroup::CommonFirst:
			return m_CommonFirst;
		case MotionTaskGroup::CommonSecond:
			return m_CommonSecond;
		case MotionTaskGroup::First:
			return m_First;
		case MotionTaskGroup::Second:
			return m_Second;
		}

		throw std::runtime_error("motion task group is outside the recovered four-group layout");
	}

	const ParallelTaskSet& MotionLayerTasks::Group(MotionTaskGroup group) const {
		switch (group) {
		case MotionTaskGroup::CommonFirst:
			return m_CommonFirst;
		case MotionTaskGroup::CommonSecond:
			return m_CommonSecond;
		case MotionTaskGroup::First:
			return m_First;
		case MotionTaskGroup::Second:
			return m_Second;
		}

		throw std::runtime_error("motion task group is outside the recovered four-group layout");
	}

	void MotionLayerTasks::ClearBeforeSourceManagers() {
		std::array<ParallelTaskSet*, 3> groups{&m_CommonFirst, &m_CommonSecond, &m_Second};
		ClearTaskGroups(groups);
	}

	void MotionLayerTasks::ClearBetweenSourceManagers() {
		m_First.Clear();
	}

	void MotionLayerTasks::Clear() {
		std::array<ParallelTaskSet*, 4> groups{&m_CommonFirst, &m_CommonSecond, &m_Second, &m_First};
		ClearTaskGroups(groups);
	}

} // namespace sgre::scene
