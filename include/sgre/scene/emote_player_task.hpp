#pragma once

#include "sgre/scene/emote_scene_player.hpp"
#include "sgre/scene/motion_player_task.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace sgre::scene {
	class EmotePlayerTask {
	public:
		// The real Layer Second group owns MEmote's distinct player through its owning callbacks.
		// This weak view borrows that group/runtime and expires at actual physical retirement.
		[[nodiscard]] static EmotePlayerTask Attach(MotionManagerRuntime& actual_runtime, MotionLayerTasks& actual_tasks, MotionProjectId actual_project, bool asynchronous, std::function<std::uint32_t()> read_entropy_word, MotionTaskInsertion insertion = MotionTaskInsertion::Append, EmoteWindRandom* actual_wind_random = nullptr);
		[[nodiscard]] bool IsLive() const noexcept;
		[[nodiscard]] bool CanRetire() const noexcept;
		[[nodiscard]] ParallelTaskHandle Handle() const noexcept;
		// Immediate use only: selected scene storage remains owned by the actual task group.
		[[nodiscard]] EmoteScenePlayer& Player() const;
		void SetVisible(bool visible);
		// Native2aef80 writes actual status0/1; status1 remains owned and drawable.
		void Pause(bool paused);
		[[nodiscard]] bool Visible() const;
		void RequestRetire();

	private:
		class Implementation;
		EmotePlayerTask(std::weak_ptr<Implementation> implementation, ParallelTaskSet& tasks, ParallelTaskHandle handle);
		[[nodiscard]] std::shared_ptr<Implementation> Require() const;
		std::weak_ptr<Implementation> m_Implementation;
		ParallelTaskSet* m_Tasks = nullptr;
		ParallelTaskHandle m_Handle;
	};
} // namespace sgre::scene
