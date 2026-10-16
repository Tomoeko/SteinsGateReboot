#pragma once

#include <filesystem>
#include <optional>

namespace sgre::platform {

	struct InspectGuiOptions {
		std::optional<std::filesystem::path> Installation;
		std::optional<std::filesystem::path> GameExecutable;
	};

	// Explicit macOS inspector mode. Runs on the main thread; no game is launched until requested.
	int RunInspectGui(const InspectGuiOptions& options);

} // namespace sgre::platform
