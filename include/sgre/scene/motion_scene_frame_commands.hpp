#pragma once

#include "sgre/scene/motion_frame_submission.hpp"
#include "sgre/scene/motion_scene_evaluation.hpp"

#include <cstddef>
#include <memory>

namespace sgre::scene {

	class MotionSceneFrameCommands {
	public:
		// Frame rows, registered sources and the scene packet generation remain unchanged through Clear.
		// The real scene RequireUnqueued service must reject mutation while IsBorrowing returns true.
		explicit MotionSceneFrameCommands(std::size_t maximum_rows = 65536, std::size_t maximum_candidates = 1048576);
		~MotionSceneFrameCommands();
		MotionSceneFrameCommands(const MotionSceneFrameCommands&) = delete;
		MotionSceneFrameCommands& operator=(const MotionSceneFrameCommands&) = delete;
		// Globals supply the actual camera/scissor/target/texture dimensions, with empty scene-owned spans.
		// Preserves the raw material blend/key and mask descriptor identities; no GPU capability is fabricated.
		[[nodiscard]] MotionPreparedFrame Prepare(const MotionSceneEvaluation& scene, const MotionPreparedFrame& globals);
		// One actual source-manager queue supplies all task scenes before the shared native depth/priority sort.
		[[nodiscard]] MotionPreparedFrame Prepare(std::span<const MotionSceneEvaluation* const> actual_scenes, const MotionPreparedFrame& globals);
		[[nodiscard]] bool IsBorrowing(const MotionSceneEvaluation& scene) const noexcept;
		void Clear();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
