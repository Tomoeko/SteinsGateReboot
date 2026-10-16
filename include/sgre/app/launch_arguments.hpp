#pragma once

#include "sgre/app/launch_configuration.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace sgre::app {
	struct LaunchArguments {
		std::optional<std::filesystem::path> Installation;
		// Per-launch override; the persisted configuration remains an independent explicit default.
		std::optional<bool> DebugStartInGame;
		std::optional<bool> Fullscreen;
		std::optional<bool> WindowCentered;
		std::optional<std::filesystem::path> SaveDirectory;
		std::optional<std::uint64_t> MaximumMilliseconds;
		std::optional<std::size_t> MaximumIterations;
		bool Headless = false;
		bool ValidateOnly = false;
		bool Help = false;
	};

	// Excludes argv[0]. Parsing performs no filesystem I/O, config write, activation, or service creation.
	[[nodiscard]] LaunchArguments ParseLaunchArguments(std::span<const std::string_view> arguments);
	// Only fullscreen/centering preferences change. Startup, headless, saves and observation limits remain per-run host inputs.
	[[nodiscard]] LaunchConfiguration ApplyLaunchPreferences(LaunchConfiguration configuration, const LaunchArguments& arguments);
} // namespace sgre::app
