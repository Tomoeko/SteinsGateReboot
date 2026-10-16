#pragma once

#include "sgre/scene/motion_frame_submission.hpp"

#include <cstddef>
#include <memory>

namespace sgre::scene {
	class MotionSceneEvaluation;
	class MotionManagerRuntime;

	class MotionSceneDrawContext {
	public:
		// Real task Draw appends its actual scene once per source-manager draw occurrence.
		// Scene, selected/prepared/resource owners and all publication spans remain live until Clear after End/Abort.
		explicit MotionSceneDrawContext(const MotionManagerRuntime& actual_runtime, std::size_t maximum_scenes = 1024, std::size_t maximum_rows = 65536);
		~MotionSceneDrawContext();
		MotionSceneDrawContext(const MotionSceneDrawContext&) = delete;
		MotionSceneDrawContext& operator=(const MotionSceneDrawContext&) = delete;
		void Append(const MotionSceneEvaluation& actual_scene);
		[[nodiscard]] bool IsBorrowing(const MotionSceneEvaluation& actual_scene) const noexcept;
		[[nodiscard]] std::size_t Size() const noexcept;
		[[nodiscard]] bool CanClear() const noexcept;
		// One global depth/priority sort includes every actual task occurrence in this manager queue.
		[[nodiscard]] MotionPreparedFrame Prepare(const MotionPreparedFrame& actual_globals);
		void Clear();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
