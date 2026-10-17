#pragma once

#include "sgre/audio/user_audio_settings.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sgre::app {

	struct LaunchConfiguration {
		std::filesystem::path Installation;
		std::filesystem::path SpriteShader;
		std::filesystem::path PresentationShader;
		std::filesystem::path SaveDirectory;
		// Empty selects the verified supplied executable's archive-seed initializer, never a guessed key.
		std::filesystem::path SeedFile;
		std::uint32_t Width = 1280;
		std::uint32_t Height = 720;
		std::uint32_t AudioSamplesPerSecond = 48000;
		std::uint32_t AudioChannels = 2;
		bool DebugStartInGame = false;
		// Content points are independent of the fixed logical render target above.
		std::uint32_t WindowWidth = 1280;
		std::uint32_t WindowHeight = 720;
		bool Fullscreen = false;
		bool WindowCentered = true;
		audio::UserAudioSettings Audio;
		// Optional volume keys are published only after an explicit audio edit.
		bool AudioSettingsStored = false;
	};

	class LaunchConfigurationFormatError final : public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};

	enum class LaunchConfigurationDisposition {
		Reused,
		Created,
		Repaired,
		InstallationChanged,
		Migrated
	};

	struct LoadedLaunchConfiguration {
		LaunchConfiguration Configuration;
		std::string ArchiveSeed;
		LaunchConfigurationDisposition Disposition = LaunchConfigurationDisposition::Reused;
		// A repaired config reports the actual parse/schema error; no game-resource error is swallowed.
		std::string RecoveryDiagnostic;
		std::filesystem::path CorruptBackup;
	};

	struct ParsedLaunchConfiguration {
		LaunchConfiguration Configuration;
		std::uint32_t SchemaVersion = 0;
	};

	[[nodiscard]] ParsedLaunchConfiguration ParseLaunchConfigurationDocument(std::string_view json);
	[[nodiscard]] LaunchConfiguration ParseLaunchConfiguration(std::string_view json);
	[[nodiscard]] std::string SerializeLaunchConfiguration(const LaunchConfiguration& configuration);
	[[nodiscard]] std::string ReadOriginalArchiveSeed(const std::filesystem::path& executable);

	// Startup-only serialized owner. It never writes to the preserved installation or launches an OS service.
	class LaunchConfigurationStore {
	public:
		explicit LaunchConfigurationStore(std::filesystem::path workspace);
		[[nodiscard]] std::filesystem::path ConfigurationPath() const;
		// First launch accepts only the installation path. Later launches need no arguments.
		// Missing/corrupt config restores one managed last-good file before rediscovering the supplied workspace installation.
		[[nodiscard]] LoadedLaunchConfiguration LoadOrCreate(std::optional<std::filesystem::path> installation = {}) const;
		void Save(const LaunchConfiguration& configuration) const;

	private:
		std::filesystem::path m_Workspace;
	};

} // namespace sgre::app
