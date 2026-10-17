#pragma once

#include "sgre/platform/checkpoint_preparation.hpp"
#include "sgre/scenario/scenario_search.hpp"
#include "sgre/script/original_checkpoint.hpp"

#include <memory>
#include <optional>
#include <string>

namespace sgre::platform {

	struct PreparedSceneJump {
		script::OriginalCheckpointLocation Location;
		save::CachedSceneCheckpoint Checkpoint;
	};

	// Explicit debug actions resolve metadata and disk state off the frame thread.
	// The ordinary original-game player remains active while a missing state is reconstructed.
	class SceneJumpPreparation final {
	public:
		SceneJumpPreparation(scenario::ScenarioSearchLocation location, CheckpointPreparationRequest request, std::shared_ptr<save::SceneCheckpointCache> cache);
		~SceneJumpPreparation();
		SceneJumpPreparation(const SceneJumpPreparation&) = delete;
		SceneJumpPreparation& operator=(const SceneJumpPreparation&) = delete;
		[[nodiscard]] std::optional<PreparedSceneJump> TakeResult();
		[[nodiscard]] std::string Error() const;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::platform
