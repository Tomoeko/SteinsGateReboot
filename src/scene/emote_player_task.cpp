#include "sgre/scene/emote_player_task.hpp"

#include <stdexcept>
#include <utility>

namespace sgre::scene {
	static void ConsumeEmoteEventWithoutRegisteredListeners(const MotionPlayerEvent&) {
		// Native506e50 starts with an empty listener list. The sole51d630 caller is SQMotion273100,
		// while actual MEmote2b85b0 never installs one. The genuine plain event queue is still consumed.
	}

	class EmotePlayerTask::Implementation {
	public:
		Implementation(MotionManagerRuntime& runtime, MotionProjectId project, std::function<std::uint32_t()> entropy, EmoteWindRandom* wind_random)
		    : Player(std::make_unique<EmoteScenePlayer>(runtime, project, std::move(entropy), ConsumeEmoteEventWithoutRegisteredListeners, wind_random)) {
		}
		std::unique_ptr<EmoteScenePlayer> Player;
	};

	EmotePlayerTask::EmotePlayerTask(std::weak_ptr<Implementation> implementation, ParallelTaskSet& tasks, ParallelTaskHandle handle)
	    : m_Implementation(std::move(implementation)), m_Tasks(&tasks), m_Handle(handle) {
	}
	EmotePlayerTask EmotePlayerTask::Attach(MotionManagerRuntime& runtime, MotionLayerTasks& tasks, MotionProjectId project, bool asynchronous, std::function<std::uint32_t()> entropy, MotionTaskInsertion insertion, EmoteWindRandom* wind_random) {
		if (asynchronous || !entropy || (insertion != MotionTaskInsertion::Append && insertion != MotionTaskInsertion::Prepend)) {
			throw std::runtime_error("Emote task requires actual synchronous mode, entropy and supported native insertion");
		}
		runtime.RequireLayerTaskOwner(tasks);
		auto implementation = std::make_shared<Implementation>(runtime, project, std::move(entropy), wind_random);
		auto& second = tasks.Group(MotionTaskGroup::Second);
		ParallelTaskCallbacks callbacks;
		callbacks.Update = [implementation](ParallelTaskHandle) {
			// Native2d4670 uses actual Looper delta times the task's constructor-default multiplier1.
			implementation->Player->Update();
		};
		callbacks.Draw = [implementation](ParallelTaskHandle, std::uint32_t) {
			// Native2d4660/2bf270 draw the real plain owner with0, then the actual Velvety visual tail.
			implementation->Player->Draw();
		};
		callbacks.CanRelease = [implementation](ParallelTaskHandle) {
			return !implementation->Player || implementation->Player->CanClose();
		};
		callbacks.Release = [implementation](ParallelTaskHandle) {
			if (implementation->Player) {
				implementation->Player->Close();
				implementation->Player.reset();
			}
		};
		const auto handle = insertion == MotionTaskInsertion::Prepend ? second.Prepend(std::move(callbacks)) : second.Append(std::move(callbacks));
		return EmotePlayerTask(implementation, second, handle);
	}
	std::shared_ptr<EmotePlayerTask::Implementation> EmotePlayerTask::Require() const {
		auto owner = m_Implementation.lock();
		if (!owner || !owner->Player || !m_Tasks->Contains(m_Handle)) {
			throw std::runtime_error("Emote task view expired after actual physical retirement");
		}
		return owner;
	}
	bool EmotePlayerTask::IsLive() const noexcept {
		const auto owner = m_Implementation.lock();
		return owner && owner->Player && m_Tasks->Contains(m_Handle);
	}
	bool EmotePlayerTask::CanRetire() const noexcept {
		const auto owner = m_Implementation.lock();
		return !owner || !owner->Player || owner->Player->CanClose();
	}
	ParallelTaskHandle EmotePlayerTask::Handle() const noexcept {
		return m_Handle;
	}
	EmoteScenePlayer& EmotePlayerTask::Player() const {
		return *Require()->Player;
	}
	void EmotePlayerTask::SetVisible(bool visible) {
		const auto owner = Require();
		if (!owner->Player->CanClose()) {
			throw std::runtime_error("Emote visibility requires actual published frame inputs to retire");
		}
		const auto paused = m_Tasks->State(m_Handle).DrawPaused;
		m_Tasks->SetDrawPaused(m_Handle, !visible);
		try {
			// Native2d4640/2d46c0 changes the task pause byte before its real contained roots.
			owner->Player->SetVisible(visible);
		} catch (...) {
			m_Tasks->SetDrawPaused(m_Handle, paused);
			throw;
		}
	}
	void EmotePlayerTask::Pause(bool paused) {
		const auto owner = Require();
		if (!owner->Player->CanClose()) {
			throw std::runtime_error("Emote task pause requires actual published frame inputs to retire");
		}
		m_Tasks->SetStatus(m_Handle, paused ? TaskStatus::Complete : TaskStatus::Active);
	}
	bool EmotePlayerTask::Visible() const {
		static_cast<void>(Require());
		return !m_Tasks->State(m_Handle).DrawPaused;
	}
	void EmotePlayerTask::RequestRetire() {
		const auto owner = Require();
		if (!owner->Player->CanClose()) {
			throw std::runtime_error("Emote task retirement requires actual published frame inputs to retire");
		}
		m_Tasks->SetStatus(m_Handle, TaskStatus::Retire);
	}
} // namespace sgre::scene
