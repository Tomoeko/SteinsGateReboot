#pragma once

#include "sgre/scene/motion_child_construction.hpp"
#include "sgre/scene/parallel_task_set.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace sgre::scene {
	class MotionManagerRuntime;
	class MotionPlayerTask;

	class MotionVariableAnimations {
	public:
		// A weak association with the genuine nested task set owned by this Layer's CommonFirst group.
		// That group retires before the Motion task and its source manager; no VM or player root is retained.
		MotionVariableAnimations(MotionVariableAnimations&&) = default;
		MotionVariableAnimations& operator=(MotionVariableAnimations&&) = default;
		~MotionVariableAnimations() = default;
		MotionVariableAnimations(const MotionVariableAnimations&) = delete;
		MotionVariableAnimations& operator=(const MotionVariableAnimations&) = delete;
		[[nodiscard]] bool IsLive() const noexcept;

	private:
		friend class MotionPlayerTask;
		class Implementation;
		MotionVariableAnimations(std::weak_ptr<Implementation> implementation, ParallelTaskSet& parent, ParallelTaskHandle handle);
		static void Validate(std::string_view name, float initial, float target, std::int32_t duration, std::int32_t curve);
		[[nodiscard]] static MotionVariableAnimations Attach(const MotionPlayerTask& actual_task, MotionManagerRuntime& actual_runtime, ParallelTaskSet& actual_common_first, std::optional<MotionChildBorrow> child = {});
		void Append(std::string_view name, float initial, float target, std::int32_t duration, std::int32_t curve, std::optional<MotionChildBorrow> child = {});
		void RequestRetire();
		std::weak_ptr<Implementation> m_Implementation;
		ParallelTaskSet* m_Parent = nullptr;
		ParallelTaskHandle m_Handle;
	};
} // namespace sgre::scene
