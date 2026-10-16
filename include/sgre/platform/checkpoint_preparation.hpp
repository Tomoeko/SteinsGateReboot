#pragma once

#include "sgre/save/scene_checkpoint_cache.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace sgre::platform {

	struct CheckpointPreparationRequest {
		std::filesystem::path Executable;
		std::filesystem::path Installation;
		std::filesystem::path JobRoot;
		std::string Target;
		std::string Key;
	};

	// The visible application retains its normal window/render/audio owners.
	// This independent bounded worker only reconstructs an absent original state.
	class CheckpointPreparation {
	public:
		CheckpointPreparation(CheckpointPreparationRequest request, std::shared_ptr<save::SceneCheckpointCache> cache);
		~CheckpointPreparation();
		CheckpointPreparation(const CheckpointPreparation&) = delete;
		CheckpointPreparation& operator=(const CheckpointPreparation&) = delete;
		[[nodiscard]] std::optional<save::CachedSceneCheckpoint> Result() const;
		[[nodiscard]] std::string Error() const;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::platform
