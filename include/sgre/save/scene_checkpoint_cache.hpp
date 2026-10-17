#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::save {

	struct CachedSceneCheckpoint {
		std::vector<std::uint8_t> Bytes;
		std::string Digest;
		std::size_t Variants = 0;
	};

	struct SceneCheckpointSelection {
		std::size_t Candidate = 0;
		CachedSceneCheckpoint Checkpoint;
	};

	// Content-addressed, append-only per installation. Different route histories
	// survive separately; default lookup selects the most recently recorded state.
	class SceneCheckpointCache {
	public:
		using Digest = std::function<std::string(std::span<const std::uint8_t>)>;
		SceneCheckpointCache(std::filesystem::path directory, std::string identity, Digest digest);
		~SceneCheckpointCache();
		SceneCheckpointCache(const SceneCheckpointCache&) = delete;
		SceneCheckpointCache& operator=(const SceneCheckpointCache&) = delete;
		// Startup-only I/O. An explicit digest selects one retained route variant.
		[[nodiscard]] std::optional<CachedSceneCheckpoint> Find(std::string_view key, std::string_view selected_digest = {}) const;
		// One directory scan selects the first available candidate in caller priority order.
		[[nodiscard]] std::optional<SceneCheckpointSelection> FindFirst(std::span<const std::string> keys) const;
		// No disk I/O. The writer owns all queued bytes and never accesses the VM.
		[[nodiscard]] std::string Submit(std::string_view key, std::vector<std::uint8_t> bytes);
		void Flush();
		[[nodiscard]] std::string Error() const;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::save
