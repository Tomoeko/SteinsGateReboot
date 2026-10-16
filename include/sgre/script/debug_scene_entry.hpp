#pragma once

#include "sgre/script/original_checkpoint.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sgre::script {

	struct ScriptDebugSceneEntry {
		std::string Storage;
		std::int32_t TargetLine = 0;
		OriginalCheckpointLocation CheckpointLocation{};
		std::string CheckpointIdentity{};
		std::vector<std::uint8_t> Checkpoint{};
		// Nonblocking host completion query. It never performs disk I/O or suppresses
		// the containing application's window, rendering, input or audio services.
		std::function<std::optional<std::vector<std::uint8_t>>()> CheckpointPoll{};
	};

	// Explicit rewritten-host debug instrumentation, never installed by normal startup.
	// Install after the original Application class is published and before MyApplication derives
	// or any Application instance locks the class. The VM owns the installed provider and its entry.
	// Original main consumes a fresh complete checkpoint through getMainScene.
	// Bare {storage, target} remains available for isolated scene-only fixtures;
	// System.getDebugBuild and original bootstrap/developer branches remain unchanged.
	void InstallOriginalDebugSceneEntry(SquirrelVm& vm, ScriptDebugSceneEntry entry);

} // namespace sgre::script
