#pragma once

#include "sgre/scene/motion_player_update.hpp"

#include <functional>
#include <memory>

namespace sgre::scene {
	class MotionManagerRuntime;
	class MotionRootScene;
	class MotionPlayerRandom;
	class MotionSceneEvaluation;
	struct MotionPlayerRenderControls;

	class MotionScenePlayer {
	public:
		// Task Commands/Root/Render and the genuine manager runtime enclose this selected/nested scene owner.
		// Runtime preparation storage encloses selected nodes, and evaluator retirement precedes those nodes.
		MotionScenePlayer(MotionManagerRuntime& runtime, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, std::function<void(const MotionPlayerEvent&)> listener, MotionPlayerRandom* actual_random = nullptr);
		MotionScenePlayer(MotionManagerRuntime& runtime, MotionPlayerCommands& commands, MotionRootScene& root, const MotionPlayerRenderControls& render, std::function<void(const MotionPlayerEvent&)> listener, MotionPlayerRandom* actual_random, const MotionScenePlayer& source);
		~MotionScenePlayer() noexcept;
		MotionScenePlayer(const MotionScenePlayer&) = delete;
		MotionScenePlayer& operator=(const MotionScenePlayer&) = delete;
		void CaptureJoin();
		void Clear();
		void Rebuild(const ResolvedMotionTimeline& actual_selection);
		void Update();
		void Progress(float elapsed_ticks);
		void PostUpdate();
		void Draw();
		[[nodiscard]] bool CanMutate() const noexcept;
		[[nodiscard]] bool ResourcesLive() const;
		[[nodiscard]] const MotionSceneEvaluation* Scene() const noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
