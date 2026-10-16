#include "sgre/app/launch_configuration.hpp"

#include "sgre/io/file_reader.hpp"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <system_error>
#include <utility>

namespace sgre::app {

	static constexpr std::size_t kMaximumConfigBytes = 64 * 1024;

	static void Require(bool condition, const std::string& diagnostic) {
		if (!condition) {
			throw std::runtime_error(diagnostic);
		}
	}

	static bool Inside(const std::filesystem::path& child, const std::filesystem::path& parent) {
		auto child_part = child.begin();
		bool lexical = true;
		for (const auto& parent_part : parent) {
			if (child_part == child.end() || *child_part != parent_part) {
				lexical = false;
				break;
			}
			++child_part;
		}
		if (lexical) {
			return true;
		}
		if (std::filesystem::exists(parent)) {
			for (auto ancestor = child; !ancestor.empty(); ancestor = ancestor.parent_path()) {
				if (std::filesystem::exists(ancestor) && std::filesystem::equivalent(ancestor, parent)) {
					return true;
				}
				if (ancestor == ancestor.parent_path()) {
					break;
				}
			}
		}

		return false;
	}

	static std::string ManagedAsciiName(std::string name) {
		for (auto& letter : name) {
			if (letter >= 'A' && letter <= 'Z') {
				letter = static_cast<char>(letter - 'A' + 'a');
			}
		}

		return name;
	}

	static bool IsConfigDirectory(const std::filesystem::path& candidate, const std::filesystem::path& config_directory) {
		if (std::filesystem::exists(candidate) && std::filesystem::exists(config_directory)) {
			return std::filesystem::equivalent(candidate, config_directory);
		}
		// Before first Files/ publication, reserve case variants of this one app-owned directory too.
		return ManagedAsciiName(candidate.filename().string()) == ManagedAsciiName(config_directory.filename().string()) && std::filesystem::exists(candidate.parent_path()) && std::filesystem::exists(config_directory.parent_path()) && std::filesystem::equivalent(candidate.parent_path(), config_directory.parent_path());
	}

	static bool ReservedSaveNamespace(const std::filesystem::path& save_directory, const std::filesystem::path& config_directory) {
		for (auto ancestor = save_directory; !ancestor.empty(); ancestor = ancestor.parent_path()) {
			if (IsConfigDirectory(ancestor, config_directory)) {
				const auto relative = save_directory.lexically_relative(ancestor);
				if (relative.empty() || *relative.begin() == ".") {
					return true;
				}
				const auto first = ManagedAsciiName(relative.begin()->string());
				return first == "config.json" || first == "config.json.last-good" || first == "config.json.corrupt" || first.starts_with(".sgre-config-write-");
			}
			if (ancestor == ancestor.parent_path()) {
				break;
			}
		}

		return false;
	}

	static void RequireFile(const std::filesystem::path& path) {
		Require(std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) != 0, "required launch asset is missing or empty: " + path.string());
	}

	static LaunchConfiguration Defaults(const std::filesystem::path& workspace, const std::filesystem::path& installation) {
		LaunchConfiguration result;
		result.Installation = std::filesystem::weakly_canonical(std::filesystem::absolute(installation));
		result.SpriteShader = workspace / "shaders/metal/sprite_pass.metal";
		result.PresentationShader = workspace / "shaders/metal/sprite_presentation.metal";
		result.SaveDirectory = workspace / "Files/saves";

		return result;
	}

	static LaunchConfiguration ValidateAssets(LaunchConfiguration configuration, const std::filesystem::path& config_path) {
		static_cast<void>(SerializeLaunchConfiguration(configuration));
		configuration.Installation = std::filesystem::canonical(configuration.Installation);
		Require(std::filesystem::is_directory(configuration.Installation), "game installation path is not a directory");
		RequireFile(configuration.Installation / "sgre_steam.exe");
		for (const std::string_view archive : {"config", "font", "image", "map", "motion", "scenario", "script", "sound", "voice"}) {
			const auto directory = configuration.Installation / "wind3d11data";
			RequireFile(directory / (std::string(archive) + "_info.psb.m"));
			const auto body = directory / (std::string(archive) + "_body.bin");
			// Availability only: the actual archive reader validates every index range against this body, including zero bytes.
			Require(std::filesystem::is_regular_file(body), "required launch archive body is missing or not a regular file: " + body.string());
		}
		Require(std::filesystem::is_directory(configuration.Installation / "wind3d11data/movie"), "required game movie directory is missing");
		configuration.SpriteShader = std::filesystem::canonical(configuration.SpriteShader);
		configuration.PresentationShader = std::filesystem::canonical(configuration.PresentationShader);
		RequireFile(configuration.SpriteShader);
		RequireFile(configuration.PresentationShader);
		configuration.SaveDirectory = std::filesystem::weakly_canonical(configuration.SaveDirectory);
		Require(!Inside(configuration.SaveDirectory, configuration.Installation), "rewrite saves must stay outside the preserved game installation");
		Require(!std::filesystem::exists(configuration.SaveDirectory) || std::filesystem::is_directory(configuration.SaveDirectory), "rewrite save path exists but is not a directory");
		for (const std::string_view suffix : {"", ".last-good", ".corrupt"}) {
			auto managed = config_path;
			managed += std::string(suffix);
			managed = std::filesystem::weakly_canonical(managed);
			Require(!Inside(managed, configuration.Installation), "managed launch config must stay outside the preserved game installation");
			Require(!Inside(managed, configuration.SaveDirectory) && !Inside(configuration.SaveDirectory, managed), "rewrite save directory must not overlap any managed launch config path");
		}
		// Only these authored app-owned names are reserved case-insensitively; actual asset paths stay unchanged.
		Require(!ReservedSaveNamespace(configuration.SaveDirectory, config_path.parent_path()), "rewrite save directory must not use a reserved managed config or publication namespace");
		if (!configuration.SeedFile.empty()) {
			configuration.SeedFile = std::filesystem::canonical(configuration.SeedFile);
			RequireFile(configuration.SeedFile);
		}

		return configuration;
	}

	static std::string SeedFromFile(const std::filesystem::path& path) {
		io::FileReader reader(path);
		Require(reader.Size() != 0 && reader.Size() <= 1024, "archive seed file must contain 1 to 1024 bytes");
		const auto bytes = reader.Read(0, static_cast<std::size_t>(reader.Size()));
		std::string seed(bytes.begin(), bytes.end());
		while (!seed.empty() && (seed.back() == '\r' || seed.back() == '\n')) {
			seed.pop_back();
		}
		Require(!seed.empty() && seed.find_first_of(std::string_view("\0\r\n", 3)) == std::string::npos, "archive seed file must be a single nonempty line without NUL");

		return seed;
	}

	static std::string ReadConfig(const std::filesystem::path& path) {
		io::FileReader reader(path);
		if (reader.Size() == 0 || reader.Size() > kMaximumConfigBytes) {
			throw LaunchConfigurationFormatError("config JSON must contain 1 to 65536 bytes");
		}
		const auto bytes = reader.Read(0, static_cast<std::size_t>(reader.Size()));

		return std::string(bytes.begin(), bytes.end());
	}

	static bool HasConfig(const std::filesystem::path& path) {
		const auto status = std::filesystem::symlink_status(path);
		Require(!std::filesystem::is_symlink(status), "launch config must be a regular file, not a symbolic link");
		Require(!std::filesystem::exists(status) || std::filesystem::is_regular_file(status), "launch config path exists but is not a regular file");

		return std::filesystem::exists(status);
	}

	class ConfigWriteDirectory {
	public:
		explicit ConfigWriteDirectory(const std::filesystem::path& parent) {
			const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
			for (std::size_t attempt = 0; attempt != 64; ++attempt) {
				auto candidate = parent / (".sgre-config-write-" + std::to_string(stamp) + "-" + std::to_string(attempt));
				if (std::filesystem::create_directory(candidate)) {
					m_Path = std::move(candidate);
					return;
				}
			}
			throw std::runtime_error("could not reserve a unique same-filesystem config write directory");
		}

		~ConfigWriteDirectory() noexcept {
			std::error_code ignored;
			std::filesystem::remove_all(m_Path, ignored);
		}

		ConfigWriteDirectory(const ConfigWriteDirectory&) = delete;
		ConfigWriteDirectory& operator=(const ConfigWriteDirectory&) = delete;

		std::filesystem::path File() const {
			return m_Path / "config.json";
		}

	private:
		std::filesystem::path m_Path;
	};

	static void AtomicSave(const std::filesystem::path& path, const LaunchConfiguration& configuration) {
		const auto json = SerializeLaunchConfiguration(configuration);
		static_cast<void>(HasConfig(path));
		std::filesystem::create_directories(path.parent_path());
		const ConfigWriteDirectory temporary(path.parent_path());
		{
			std::ofstream output(temporary.File(), std::ios::binary | std::ios::trunc);
			Require(static_cast<bool>(output), "cannot create temporary launch config");
			output.write(json.data(), static_cast<std::streamsize>(json.size()));
			output.flush();
			Require(static_cast<bool>(output), "cannot flush complete temporary launch config");
			output.close();
			Require(static_cast<bool>(output), "cannot close complete temporary launch config");
		}
		// Rename publishes one fully written file. A failed replacement leaves the prior config intact.
		// No remove-then-rename fallback or power-loss durability claim is made.
		std::filesystem::rename(temporary.File(), path);
	}

	static std::filesystem::path LastGoodPath(std::filesystem::path path) {
		path += ".last-good";

		return path;
	}

	static void Persist(const std::filesystem::path& path, const LaunchConfiguration& configuration) {
		static_cast<void>(HasConfig(path));
		static_cast<void>(HasConfig(LastGoodPath(path)));
		// Each file is atomic. A failed primary publication still leaves the old primary and a complete validated fallback.
		AtomicSave(LastGoodPath(path), configuration);
		AtomicSave(path, configuration);
	}

	LaunchConfigurationStore::LaunchConfigurationStore(std::filesystem::path workspace) : m_Workspace(std::filesystem::canonical(std::move(workspace))) {
		Require(std::filesystem::is_directory(m_Workspace), "launch workspace must be an existing directory");
	}

	std::filesystem::path LaunchConfigurationStore::ConfigurationPath() const {
		return m_Workspace / "Files/config.json";
	}

	LoadedLaunchConfiguration LaunchConfigurationStore::LoadOrCreate(std::optional<std::filesystem::path> installation) const {
		LoadedLaunchConfiguration result;
		const auto config_path = ConfigurationPath();
		const bool existed = HasConfig(config_path);
		bool valid = false;
		if (existed) {
			try {
				const auto parsed = ParseLaunchConfigurationDocument(ReadConfig(config_path));
				result.Configuration = parsed.Configuration;
				if (parsed.SchemaVersion == 1) {
					result.Disposition = LaunchConfigurationDisposition::Migrated;
				}
				valid = true;
			} catch (const LaunchConfigurationFormatError& error) {
				result.RecoveryDiagnostic = error.what();
				result.Disposition = LaunchConfigurationDisposition::Repaired;
			}
		}
		const bool corrupt = existed && !valid;
		if (!valid && HasConfig(LastGoodPath(config_path))) {
			try {
				result.Configuration = ParseLaunchConfiguration(ReadConfig(LastGoodPath(config_path)));
				valid = true;
				result.Disposition = LaunchConfigurationDisposition::Repaired;
				result.RecoveryDiagnostic += result.RecoveryDiagnostic.empty() ? "missing config; restored managed last-good configuration" : "; restored managed last-good configuration";
			} catch (const LaunchConfigurationFormatError& error) {
				result.RecoveryDiagnostic += "; managed last-good config invalid: ";
				result.RecoveryDiagnostic += error.what();
			}
		}
		if (!valid) {
			const auto selected = installation.value_or(m_Workspace / "Files/STEINS.GATE.REBOOT");
			Require(!selected.empty() && std::filesystem::is_directory(selected), "first launch or config repair requires the game installation path");
			result.Configuration = Defaults(m_Workspace, selected);
			if (!existed) {
				result.Disposition = LaunchConfigurationDisposition::Created;
			}
		} else if (installation) {
			Require(!installation->empty(), "game installation argument cannot be empty");
			const auto selected = std::filesystem::canonical(*installation);
			if (selected != result.Configuration.Installation) {
				result.Configuration.Installation = selected;
				result.Disposition = LaunchConfigurationDisposition::InstallationChanged;
			}
		}
		result.Configuration = ValidateAssets(std::move(result.Configuration), config_path);
		result.ArchiveSeed = result.Configuration.SeedFile.empty() ? ReadOriginalArchiveSeed(result.Configuration.Installation / "sgre_steam.exe") : SeedFromFile(result.Configuration.SeedFile);
		if (corrupt) {
			result.CorruptBackup = config_path;
			result.CorruptBackup += ".corrupt";
			const auto backup_status = std::filesystem::symlink_status(result.CorruptBackup);
			Require(!std::filesystem::is_symlink(backup_status) && (!std::filesystem::exists(backup_status) || std::filesystem::is_regular_file(backup_status)), "corrupt config backup must be a regular file, not a symbolic link or directory");
			if (!std::filesystem::exists(backup_status)) {
				if (std::filesystem::file_size(config_path) <= kMaximumConfigBytes) {
					std::filesystem::copy_file(config_path, result.CorruptBackup, std::filesystem::copy_options::none);
				} else {
					// Preserve an oversized corrupt input without copying an unbounded file.
					std::filesystem::create_hard_link(config_path, result.CorruptBackup);
				}
			}
		}
		if (result.Disposition != LaunchConfigurationDisposition::Reused) {
			Persist(config_path, result.Configuration);
		}

		return result;
	}

	void LaunchConfigurationStore::Save(const LaunchConfiguration& configuration) const {
		const auto validated = ValidateAssets(configuration, ConfigurationPath());
		static_cast<void>(validated.SeedFile.empty() ? ReadOriginalArchiveSeed(validated.Installation / "sgre_steam.exe") : SeedFromFile(validated.SeedFile));
		static_cast<void>(HasConfig(LastGoodPath(ConfigurationPath())));
		if (HasConfig(ConfigurationPath())) {
			try {
				const auto existing = ParseLaunchConfigurationDocument(ReadConfig(ConfigurationPath()));
				if (existing.SchemaVersion == 2 && SerializeLaunchConfiguration(existing.Configuration) == SerializeLaunchConfiguration(validated)) {
					return;
				}
			} catch (const LaunchConfigurationFormatError&) {
				// Explicit Save still replaces malformed managed state after asset validation.
			}
		}
		Persist(ConfigurationPath(), validated);
	}

} // namespace sgre::app
