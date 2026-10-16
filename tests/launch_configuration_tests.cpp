#include "sgre/app/launch_configuration.hpp"

#include "sgre/app/launch_arguments.hpp"

#include "sgre/io/file_reader.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <class Callback>
static void Reject(Callback&& callback) {
	bool rejected = false;
	try {
		callback();
	} catch (const std::exception&) {
		rejected = true;
	}
	Require(rejected, "invalid configuration was accepted");
}

static void Write(const std::filesystem::path& path, std::string_view text) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(text.data(), static_cast<std::streamsize>(text.size()));
	output.close();
	Require(static_cast<bool>(output), "fixture output failed");
}

static std::string Read(const std::filesystem::path& path) {
	sgre::io::FileReader input(path);
	Require(input.Size() <= 65537, "test input exceeded its explicit bound");
	const auto bytes = input.Read(0, static_cast<std::size_t>(input.Size()));

	return std::string(bytes.begin(), bytes.end());
}

static std::string Replace(std::string text, std::string_view before, std::string_view after) {
	const auto offset = text.find(before);
	Require(offset != std::string::npos, "fixture replacement source is absent");
	text.replace(offset, before.size(), after);

	return text;
}

class Fixture {
public:
	explicit Fixture(const std::filesystem::path& parent) {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		Root = parent / ("launch-config-fixture-" + std::to_string(stamp));
		Require(std::filesystem::create_directory(Root), "fixture requires a fresh owned directory");
		Game = Root / "game";
		std::filesystem::create_directories(Game / "wind3d11data/movie");
		for (const std::string_view archive : {"config", "font", "image", "map", "motion", "scenario", "script", "sound", "voice"}) {
			Write(Game / "wind3d11data" / (std::string(archive) + "_info.psb.m"), "fixture-index");
			Write(Game / "wind3d11data" / (std::string(archive) + "_body.bin"), archive == "map" ? "" : "fixture-body");
		}
		Write(Game / "sgre_steam.exe", "invalid-executable-for-independent-negative-test");
		Write(Root / "shaders/metal/sprite_pass.metal", "fixture shader");
		Write(Root / "shaders/metal/sprite_presentation.metal", "fixture presentation shader");
		Write(Root / "Files/fixture-seed.txt", "independent-fixture-seed\r\n");
	}

	~Fixture() noexcept {
		std::error_code ignored;
		std::filesystem::remove_all(Root, ignored);
	}

	Fixture(const Fixture&) = delete;
	Fixture& operator=(const Fixture&) = delete;
	std::filesystem::path Root;
	std::filesystem::path Game;

	sgre::app::LaunchConfiguration Configuration() const {
		sgre::app::LaunchConfiguration result;
		result.Installation = Game;
		result.SpriteShader = Root / "shaders/metal/sprite_pass.metal";
		result.PresentationShader = Root / "shaders/metal/sprite_presentation.metal";
		result.SaveDirectory = Root / "Files/saves";
		result.SeedFile = Root / "Files/fixture-seed.txt";

		return result;
	}
};

static void JsonTests(const Fixture& fixture) {
	auto configuration = fixture.Configuration();
	configuration.Installation = fixture.Root / "game \"quoted\" \\ 日本語";
	configuration.DebugStartInGame = true;
	configuration.WindowWidth = 1600;
	configuration.WindowHeight = 900;
	configuration.Fullscreen = true;
	configuration.WindowCentered = false;
	const auto json = sgre::app::SerializeLaunchConfiguration(configuration);
	const auto parsed = sgre::app::ParseLaunchConfiguration(json);
	Require(parsed.Installation == configuration.Installation && parsed.SpriteShader == configuration.SpriteShader && parsed.PresentationShader == configuration.PresentationShader && parsed.SaveDirectory == configuration.SaveDirectory && parsed.SeedFile == configuration.SeedFile, "UTF-8/quote/backslash config path roundtrip differs");
	Require(parsed.DebugStartInGame && parsed.Width == 1280 && parsed.AudioSamplesPerSecond == 48000 && parsed.AudioChannels == 2 && parsed.WindowWidth == 1600 && parsed.WindowHeight == 900 && parsed.Fullscreen && !parsed.WindowCentered, "launch defaults or explicit debug mode changed");
	Require(json.find("\\\"quoted\\\"") != std::string::npos && json.find("\\\\") != std::string::npos, "writer did not independently escape quotes/backslashes");
	const auto unicode = Replace(json, "日本語", "\\u65e5\\u672c\\u8a9e\\ud83d\\ude00");
	Require(sgre::app::ParseLaunchConfiguration(unicode).Installation.filename().generic_string().find("日本語😀") != std::string::npos, "JSON Unicode scalar/surrogate decoding differs");
	for (std::size_t size = 0; size < json.find_last_of('}') + 1; ++size) {
		Reject([&] {
			static_cast<void>(sgre::app::ParseLaunchConfiguration(std::string_view(json).substr(0, size)));
		});
	}
	for (const auto& malformed : {Replace(json, "\"version\": 2", "\"version\": 3"), Replace(json, "\"width\": 1280", "\"width\": 0"), Replace(json, "\"height\": 720", "\"height\": 16385"), Replace(json, "\"audio_rate\": 48000", "\"audio_rate\": 7999"), Replace(json, "\"audio_channels\": 2", "\"audio_channels\": 9"), Replace(json, "\"width\": 1280", "\"width\": -1"), Replace(json, "\"width\": 1280", "\"width\": 01"), Replace(json, "\"width\": 1280", "\"width\": 1.0"), Replace(json, "\"width\": 1280", "\"width\": 1e2"), Replace(json, "\"width\": 1280", "\"width\": 4294967296"), Replace(json, "\"width\": 1280", "\"width\": true"), Replace(json, "\"debug_start_in_game\": true", "\"debug_start_in_game\": 1"), Replace(json, "\"debug_start_in_game\": true", "\"debug_start_in_game\": tru"), Replace(json, "\"height\": 720", "\"width\": 720"), Replace(json, "\"height\"", "\"unknown_field\""), Replace(json, "日本語", "\\ud800"), Replace(json, "日本語", "\\udc00"), Replace(json, "日本語", "\\u0000"), Replace(json, "日本語", "\\n"), Replace(json, "日本語", std::string("\xc0\x80", 2)), json + "x", std::string(65537, ' ')}) {
		Reject([&] {
			static_cast<void>(sgre::app::ParseLaunchConfiguration(malformed));
		});
	}
	configuration.Installation = "relative/game";
	Reject([&] {
		static_cast<void>(sgre::app::SerializeLaunchConfiguration(configuration));
	});
	configuration.Installation.clear();
	Reject([&] {
		static_cast<void>(sgre::app::SerializeLaunchConfiguration(configuration));
	});
}

static void StoreTests(const Fixture& fixture) {
	sgre::app::LaunchConfigurationStore store(fixture.Root);
	Require(store.ConfigurationPath() == fixture.Root / "Files/config.json", "store changed its ignored config location");
	Reject([&] {
		static_cast<void>(store.LoadOrCreate());
	});
	Reject([&] {
		static_cast<void>(store.LoadOrCreate(fixture.Game));
	});
	Require(!std::filesystem::exists(store.ConfigurationPath()), "failed required seed discovery wrote a fake-ready config");
	auto configuration = fixture.Configuration();
	configuration.Width = 1024;
	configuration.Height = 576;
	configuration.AudioSamplesPerSecond = 44100;
	configuration.AudioChannels = 1;
	configuration.DebugStartInGame = true;
	configuration.WindowWidth = 1600;
	configuration.WindowHeight = 1000;
	configuration.Fullscreen = true;
	configuration.WindowCentered = false;
	store.Save(configuration);
	const auto before = Read(store.ConfigurationPath());
	const sgre::app::LaunchConfigurationStore restarted(fixture.Root);
	const auto reused = restarted.LoadOrCreate();
	Require(reused.Disposition == sgre::app::LaunchConfigurationDisposition::Reused && reused.ArchiveSeed == "independent-fixture-seed" && reused.Configuration.Installation == fixture.Game && reused.Configuration.Width == 1024 && reused.Configuration.Height == 576 && reused.Configuration.AudioSamplesPerSecond == 44100 && reused.Configuration.AudioChannels == 1 && reused.Configuration.DebugStartInGame && reused.Configuration.WindowWidth == 1600 && reused.Configuration.WindowHeight == 1000 && reused.Configuration.Fullscreen && !reused.Configuration.WindowCentered, "restart failed to reuse actual stored paths/defaults/debug/seed");
	Require(Read(store.ConfigurationPath()) == before, "restart rewrote a valid config");
	const auto managed = store.ConfigurationPath().string() + ".last-good";
	std::filesystem::rename(managed, managed + ".held");
	std::filesystem::create_directory(managed);
	Reject([&] {
		store.Save(configuration);
	});
	Require(Read(store.ConfigurationPath()) == before, "failed managed write admission damaged the valid primary");
	std::filesystem::remove(managed);
	std::filesystem::rename(managed + ".held", managed);
	Write(store.ConfigurationPath(), "{truncated");
	const auto repaired = store.LoadOrCreate();
	Require(repaired.Disposition == sgre::app::LaunchConfigurationDisposition::Repaired && repaired.Configuration.Width == 1024 && repaired.Configuration.DebugStartInGame && repaired.Configuration.WindowWidth == 1600 && repaired.Configuration.WindowHeight == 1000 && repaired.Configuration.Fullscreen && !repaired.Configuration.WindowCentered && repaired.ArchiveSeed == "independent-fixture-seed" && Read(repaired.CorruptBackup) == "{truncated", "independent last-good repair lost the arbitrary installation path/settings/seed");
	std::filesystem::remove(store.ConfigurationPath());
	Require(store.LoadOrCreate().Configuration.Installation == fixture.Game, "missing config could not restore its remembered arbitrary installation path");
	configuration.SaveDirectory = fixture.Game / "new-saves";
	Reject([&] {
		store.Save(configuration);
	});
	Require(Read(store.ConfigurationPath()) == before, "rejected save path damaged old config");
	configuration = fixture.Configuration();
	configuration.SpriteShader = fixture.Root / "missing.metal";
	Reject([&] {
		store.Save(configuration);
	});
	configuration = fixture.Configuration();
	configuration.SeedFile.clear();
	Reject([&] {
		store.Save(configuration);
	});
	const auto required = fixture.Game / "wind3d11data/script_info.psb.m";
	std::filesystem::rename(required, required.string() + ".held");
	Reject([&] {
		static_cast<void>(store.LoadOrCreate());
	});
	std::filesystem::rename(required.string() + ".held", required);
	Require(Read(store.ConfigurationPath()) == before, "missing required asset silently reset a valid config");
	Write(fixture.Root / "Files/fixture-seed.txt", "bad\nsecond line");
	Reject([&] {
		static_cast<void>(store.LoadOrCreate());
	});
	Write(fixture.Root / "Files/fixture-seed.txt", "independent-fixture-seed\r\n");
	Write(store.ConfigurationPath(), "{truncated");
	std::filesystem::remove(store.ConfigurationPath().string() + ".last-good");
	Reject([&] {
		static_cast<void>(store.LoadOrCreate(fixture.Game));
	});
	Require(Read(store.ConfigurationPath()) == "{truncated", "failed config recreation destroyed corrupt input before asset/seed admission");
	std::filesystem::remove(store.ConfigurationPath());
	std::filesystem::create_symlink(fixture.Root / "Files/fixture-seed.txt", store.ConfigurationPath());
	Reject([&] {
		store.Save(fixture.Configuration());
	});
	std::filesystem::remove(store.ConfigurationPath());
	store.Save(fixture.Configuration());
}

static void AssetPresenceTests(const std::filesystem::path& fixture_parent) {
	const Fixture fixture(fixture_parent);
	const sgre::app::LaunchConfigurationStore store(fixture.Root);
	const auto configuration = fixture.Configuration();
	Require(std::filesystem::file_size(fixture.Game / "wind3d11data/map_body.bin") == 0, "independent empty archive-body fixture was not created");
	store.Save(configuration);
	Require(store.LoadOrCreate().Configuration.Installation == fixture.Game, "present empty archive body blocked config reuse");
	const auto before = Read(store.ConfigurationPath());
	const auto body = fixture.Game / "wind3d11data/sound_body.bin";
	std::filesystem::rename(body, body.string() + ".held");
	Reject([&] {
		store.Save(configuration);
	});
	std::filesystem::create_directory(body);
	Reject([&] {
		store.Save(configuration);
	});
	Require(Read(store.ConfigurationPath()) == before, "missing/directory body failure changed the remembered config");
	std::filesystem::remove(body);
	// Any archive may be empty; actual index decoding/range admission remains the archive owner's responsibility.
	Write(body, "");
	store.Save(configuration);
	Require(store.LoadOrCreate().Configuration.Installation == fixture.Game, "availability validation imposed a prefix-specific body-size rule");
	std::filesystem::remove(body);
	std::filesystem::rename(body.string() + ".held", body);
	for (const auto& required : {fixture.Game / "wind3d11data/map_info.psb.m", fixture.Game / "sgre_steam.exe", configuration.SpriteShader, configuration.PresentationShader}) {
		const auto content = Read(required);
		Write(required, "");
		Reject([&] {
			store.Save(configuration);
		});
		Require(Read(store.ConfigurationPath()) == before, "empty index/executable/shader failure changed the remembered config");
		Write(required, content);
	}
}

static void ActualInstallationPresenceTests(const std::filesystem::path& fixture_parent, const std::filesystem::path& actual_installation) {
	const Fixture fixture(fixture_parent);
	const sgre::app::LaunchConfigurationStore store(fixture.Root);
	const auto actual = std::filesystem::canonical(actual_installation);
	// Use every actual installation path, not fixture archives with only an executable symlink.
	const auto created = store.LoadOrCreate(actual);
	Require(created.Disposition == sgre::app::LaunchConfigurationDisposition::Created && created.Configuration.Installation == actual && created.Configuration.SeedFile.empty() && !created.Configuration.DebugStartInGame, "full actual installation path-only configuration failed");
	Require(created.ArchiveSeed == sgre::app::ReadOriginalArchiveSeed(actual / "sgre_steam.exe"), "full actual installation seed differs");
	const auto restarted = store.LoadOrCreate();
	Require(restarted.Disposition == sgre::app::LaunchConfigurationDisposition::Reused && restarted.Configuration.Installation == actual && restarted.ArchiveSeed == created.ArchiveSeed, "full actual installation config failed restart reuse");
}

static void ManagedPathCollisionTests(const std::filesystem::path& fixture_parent) {
	for (const std::string_view relative : {"config.json", "config.json/child", "config.json.last-good", "config.json.last-good/child", "config.json.corrupt", "config.json.corrupt/child", ".sgre-config-write-fixture", ".sgre-config-write-fixture/child", "CONFIG.JSON", "CONFIG.JSON/child", "CONFIG.JSON.LAST-GOOD", "CONFIG.JSON.LAST-GOOD/child", "CoNfIg.JsOn.CoRrUpT", "CoNfIg.JsOn.CoRrUpT/child", ".SGRE-CONFIG-WRITE-fixture", ".SGRE-CONFIG-WRITE-fixture/child"}) {
		const Fixture fixture(fixture_parent);
		const sgre::app::LaunchConfigurationStore store(fixture.Root);
		auto configuration = fixture.Configuration();
		configuration.SaveDirectory = fixture.Root / "Files" / relative;
		Reject([&] {
			store.Save(configuration);
		});
		Require(!std::filesystem::exists(store.ConfigurationPath()) && !std::filesystem::exists(store.ConfigurationPath().string() + ".last-good") && !std::filesystem::exists(store.ConfigurationPath().string() + ".corrupt") && !std::filesystem::exists(configuration.SaveDirectory), "fresh managed-path collision mutated config or save storage before rejection");
	}
	const Fixture fixture(fixture_parent);
	const sgre::app::LaunchConfigurationStore store(fixture.Root);
	if (std::filesystem::exists(fixture.Root / "FILES") && std::filesystem::equivalent(fixture.Root / "Files", fixture.Root / "FILES")) {
		auto configuration = fixture.Configuration();
		configuration.SaveDirectory = fixture.Root / "FILES/CONFIG.JSON.LAST-GOOD/child";
		Reject([&] {
			store.Save(configuration);
		});
		configuration.SaveDirectory = fixture.Root / "GAME/save";
		Reject([&] {
			store.Save(configuration);
		});
		Require(!std::filesystem::exists(store.ConfigurationPath()) && !std::filesystem::exists(store.ConfigurationPath().string() + ".last-good"), "actual case-alias protection mutated fresh config storage");
	}
}

static void AtomicRestartTests(const Fixture& fixture) {
	const sgre::app::LaunchConfigurationStore store(fixture.Root);
	auto configuration = fixture.Configuration();
	store.Save(configuration);
	std::atomic<bool> finished = false;
	std::atomic<bool> reader_failed = false;
	std::exception_ptr failure;
	std::atomic<std::size_t> observations = 0;
	std::thread reader([&] {
		try {
			while (!finished.load(std::memory_order_acquire)) {
				const auto observed = sgre::app::ParseLaunchConfiguration(Read(store.ConfigurationPath()));
				Require(observed.Width >= 1280 && observed.Width <= 1300 && observed.Installation == configuration.Installation, "atomic config reader observed partial/cross-owner data");
				observations.fetch_add(1, std::memory_order_relaxed);
			}
		} catch (...) {
			failure = std::current_exception();
			reader_failed.store(true, std::memory_order_release);
		}
	});
	try {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (observations.load(std::memory_order_acquire) == 0 && !reader_failed.load(std::memory_order_acquire)) {
			Require(std::chrono::steady_clock::now() < deadline, "atomic read fixture did not start within its bound");
			std::this_thread::yield();
		}
		for (std::uint32_t width = 1281; width <= 1300; ++width) {
			configuration.Width = width;
			store.Save(configuration);
		}
	} catch (...) {
		finished.store(true, std::memory_order_release);
		reader.join();
		throw;
	}
	finished.store(true, std::memory_order_release);
	reader.join();
	if (failure) {
		std::rethrow_exception(failure);
	}
	Require(observations.load() != 0 && store.LoadOrCreate().Configuration.Width == 1300, "atomic replacement/restart did not retain the completed value");
	for (const auto& entry : std::filesystem::directory_iterator(fixture.Root / "Files")) {
		Require(!entry.path().filename().string().starts_with(".sgre-config-write-"), "config write left an owned temporary directory");
	}
}

static void OriginalSeedAndRepairTests(const Fixture& fixture, const std::filesystem::path& actual_installation) {
	const auto original_seed = sgre::app::ReadOriginalArchiveSeed(actual_installation / "sgre_steam.exe");
	Require(original_seed.size() == 13, "actual executable seed extent differs");
	std::filesystem::remove(fixture.Game / "sgre_steam.exe");
	std::filesystem::create_symlink(std::filesystem::canonical(actual_installation / "sgre_steam.exe"), fixture.Game / "sgre_steam.exe");
	const sgre::app::LaunchConfigurationStore store(fixture.Root);
	std::filesystem::remove(store.ConfigurationPath());
	std::filesystem::remove(store.ConfigurationPath().string() + ".last-good");
	std::filesystem::remove(store.ConfigurationPath().string() + ".corrupt");
	const auto created = store.LoadOrCreate(fixture.Game);
	Require(created.Disposition == sgre::app::LaunchConfigurationDisposition::Created && created.ArchiveSeed == original_seed && created.Configuration.SeedFile.empty() && !created.Configuration.DebugStartInGame, "path-only first creation failed genuine executable seed/default discovery");
	Require(store.LoadOrCreate().Disposition == sgre::app::LaunchConfigurationDisposition::Reused, "path-only restart did not reuse its first config");
	Write(store.ConfigurationPath(), "first corrupt {config");
	const auto repaired = store.LoadOrCreate(fixture.Game);
	Require(repaired.Disposition == sgre::app::LaunchConfigurationDisposition::Repaired && !repaired.RecoveryDiagnostic.empty() && Read(repaired.CorruptBackup) == "first corrupt {config", "corrupt config recreation/first backup differed");
	Write(store.ConfigurationPath(), "second corrupt");
	static_cast<void>(store.LoadOrCreate(fixture.Game));
	Require(Read(repaired.CorruptBackup) == "first corrupt {config", "second repair overwrote the one preserved corrupt backup");
	std::filesystem::remove(repaired.CorruptBackup);
	Write(store.ConfigurationPath(), std::string(65537, 'x'));
	static_cast<void>(store.LoadOrCreate(fixture.Game));
	Require(std::filesystem::file_size(repaired.CorruptBackup) == 65537 && store.LoadOrCreate().ArchiveSeed == original_seed, "oversized corrupt backup/repair did not preserve input without unbounded copying");
	std::filesystem::remove(repaired.CorruptBackup);
	Write(store.ConfigurationPath(), "bad");
	std::filesystem::create_symlink(fixture.Root / "Files/fixture-seed.txt", repaired.CorruptBackup);
	Reject([&] {
		static_cast<void>(store.LoadOrCreate(fixture.Game));
	});
	Require(Read(store.ConfigurationPath()) == "bad", "unsafe backup failure damaged its original corrupt config");
}

static std::string LegacyConfigurationJson(const sgre::app::LaunchConfiguration& configuration) {
	auto json = sgre::app::SerializeLaunchConfiguration(configuration);
	const auto window = json.find(",\n  \"window_width\"");
	Require(window != std::string::npos, "schema2 fixture window suffix is absent");
	json.erase(window);
	json += "\n}\n";
	return Replace(json, "\"version\": 2", "\"version\": 1");
}

static void WindowSchemaAndMigrationTests(const std::filesystem::path& parent) {
	const Fixture fixture(parent);
	sgre::app::LaunchConfigurationStore store(fixture.Root);
	auto configuration = fixture.Configuration();
	configuration.Width = 640;
	configuration.Height = 360;
	configuration.AudioSamplesPerSecond = 44100;
	configuration.DebugStartInGame = true;
	const auto legacy = LegacyConfigurationJson(configuration);
	const auto parsed = sgre::app::ParseLaunchConfigurationDocument(legacy);
	Require(parsed.SchemaVersion == 1 && parsed.Configuration.WindowWidth == 640 && parsed.Configuration.WindowHeight == 360 && parsed.Configuration.WindowCentered && !parsed.Configuration.Fullscreen, "schema1 migration invented window settings or changed the old initial extent");
	Write(store.ConfigurationPath(), legacy);
	const auto migrated = store.LoadOrCreate();
	Require(migrated.Disposition == sgre::app::LaunchConfigurationDisposition::Migrated && migrated.Configuration.Installation == configuration.Installation && migrated.Configuration.Width == 640 && migrated.Configuration.Height == 360 && migrated.Configuration.WindowWidth == 640 && migrated.Configuration.WindowHeight == 360 && migrated.Configuration.DebugStartInGame && migrated.Configuration.AudioSamplesPerSecond == 44100 && migrated.ArchiveSeed == "independent-fixture-seed", "valid legacy config migration lost remembered owners or startup preferences");
	Require(sgre::app::ParseLaunchConfigurationDocument(Read(store.ConfigurationPath())).SchemaVersion == 2 && !std::filesystem::exists(store.ConfigurationPath().string() + ".corrupt"), "legacy migration was not published atomically or was misclassified as corrupt");
	Require(store.LoadOrCreate().Disposition == sgre::app::LaunchConfigurationDisposition::Reused, "migration repeats instead of reusing the new schema");
	configuration.WindowWidth = 1100;
	configuration.WindowHeight = 800;
	configuration.Fullscreen = true;
	configuration.WindowCentered = false;
	store.Save(configuration);
	const auto before = Read(store.ConfigurationPath());
	for (const auto& bad : {Replace(before, "\"window_width\": 1100", "\"window_width\": 0"), Replace(before, "\"window_height\": 800", "\"window_height\": 16385"), Replace(before, "\"window_width\": 1100", "\"window_width\": true"), Replace(before, "\"fullscreen\": true", "\"fullscreen\": 1"), Replace(before, "\"window_centered\": false", "\"window_centered\": \"false\""), Replace(before, "\"window_centered\": false", "\"unknown_window\": false"), Replace(before, "\"version\": 2", "\"version\": 1")}) {
		Reject([&] {
			static_cast<void>(sgre::app::ParseLaunchConfiguration(bad));
		});
	}
	Write(store.ConfigurationPath(), Replace(before, "\"fullscreen\": true", "\"fullscreen\": nul"));
	const auto repaired = store.LoadOrCreate();
	Require(repaired.Disposition == sgre::app::LaunchConfigurationDisposition::Repaired && repaired.Configuration.WindowWidth == 1100 && repaired.Configuration.WindowHeight == 800 && repaired.Configuration.Fullscreen && !repaired.Configuration.WindowCentered && repaired.Configuration.Width == 640 && repaired.Configuration.Height == 360 && repaired.Configuration.DebugStartInGame, "corrupt window config repair lost last-good window, logical extent or debug state");
	std::filesystem::remove(store.ConfigurationPath());
	const auto missing = store.LoadOrCreate();
	Require(missing.Configuration.Fullscreen && !missing.Configuration.WindowCentered && missing.Configuration.WindowWidth == 1100, "missing primary did not restore independently remembered window settings");
}

static sgre::app::LaunchArguments LaunchArguments(std::initializer_list<std::string_view> arguments) {
	return sgre::app::ParseLaunchArguments(std::span<const std::string_view>(arguments.begin(), arguments.size()));
}

static void LaunchArgumentTests(const Fixture& fixture) {
	const auto normal = LaunchArguments({});
	Require(!normal.Installation && !normal.DebugStartInGame && !normal.Fullscreen && !normal.WindowCentered && !normal.Headless && !normal.ValidateOnly && !normal.MaximumMilliseconds && !normal.MaximumIterations && !normal.SaveDirectory, "no-argument startup invented persisted or research policy overrides");
	const auto selected = LaunchArguments({"/actual game path/日本語", "--skip-startup", "--fullscreen", "--no-center-window", "--headless", "--maximum-milliseconds", "7000", "--maximum-iterations", "2000", "--save-root", "relative-saves", "--validate-config"});
	Require(selected.Installation && selected.Installation->generic_u8string() == u8"/actual game path/日本語" && selected.DebugStartInGame == true && selected.Fullscreen == true && selected.WindowCentered == false && selected.Headless && selected.ValidateOnly && selected.MaximumMilliseconds == 7000 && selected.MaximumIterations == 2000 && selected.SaveDirectory == std::filesystem::path("relative-saves"), "explicit portable launch flags/UTF-8 path changed or diagnostic limits were ignored");
	const auto legacy_debug = LaunchArguments({"--debug-start-in-game"});
	Require(legacy_debug.DebugStartInGame == selected.DebugStartInGame, "existing direct-game argument and readable alias differ");
	auto configuration = fixture.Configuration();
	const auto changed = sgre::app::ApplyLaunchPreferences(configuration, selected);
	Require(!changed.DebugStartInGame && changed.Fullscreen && !changed.WindowCentered && changed.Installation == configuration.Installation && changed.SaveDirectory == configuration.SaveDirectory && changed.Width == configuration.Width && changed.Height == configuration.Height && changed.WindowWidth == configuration.WindowWidth && changed.WindowHeight == configuration.WindowHeight && changed.AudioSamplesPerSecond == configuration.AudioSamplesPerSecond, "window preference application changed saved startup, assets, logical extent, audio or per-run save policy");
	const auto ordinary = sgre::app::ApplyLaunchPreferences(changed, LaunchArguments({"--normal-start", "--windowed", "--center-window"}));
	Require(!ordinary.DebugStartInGame && !ordinary.Fullscreen && ordinary.WindowCentered, "explicit false preferences were lost as optional<bool> values");
	const auto serialized = sgre::app::SerializeLaunchConfiguration(changed);
	Require(serialized.find("headless") == std::string::npos && serialized.find("maximum_") == std::string::npos && serialized.find("relative-saves") == std::string::npos && serialized.find("validate") == std::string::npos, "research/validation options escaped into remembered configuration");
	Require(LaunchArguments({"--help"}).Help, "help cannot be selected before application creation");
	const std::vector<std::vector<std::string_view>> invalid_cases{{"--skip-startup", "--normal-start"}, {"--debug-start-in-game", "--skip-startup"}, {"--windowed", "--fullscreen"}, {"--center-window", "--no-center-window"}, {"--headless", "--headless"}, {"--validate-config", "--validate-config"}, {"--save-root", "--validate-config"}, {"--save-root", ""}, {"--maximum-milliseconds"}, {"--maximum-milliseconds", "0"}, {"--maximum-milliseconds", "900001"}, {"--maximum-milliseconds", "-1"}, {"--maximum-milliseconds", "1x"}, {"--maximum-iterations", "100001"}, {"--maximum-iterations", "18446744073709551616"}, {"--save-root", "first", "--save-root", "second"}, {"--maximum-iterations", "1", "--maximum-iterations", "2"}, {"one-installation", "second-installation"}, {"--unknown"}, {"-unknown"}, {"--skip-startup=true"}, {""}};
	for (const auto& invalid : invalid_cases) {
		Reject([&] {
			static_cast<void>(sgre::app::ParseLaunchArguments(invalid));
		});
	}
	std::vector<std::string_view> too_many(33, "path");
	Reject([&] {
		static_cast<void>(sgre::app::ParseLaunchArguments(too_many));
	});
	const std::string oversized(4097, 'x');
	Reject([&] {
		static_cast<void>(LaunchArguments({oversized}));
	});
	const std::string nul("path\0suffix", 11);
	Reject([&] {
		static_cast<void>(LaunchArguments({nul}));
	});
	const std::string maximum_size(4096, 'x');
	Reject([&] {
		static_cast<void>(LaunchArguments({maximum_size, maximum_size, maximum_size, maximum_size, "over-total-bound"}));
	});
}

static void StartupOverridesDoNotPersistTests(const std::filesystem::path& parent) {
	const Fixture fixture(parent);
	sgre::app::LaunchConfigurationStore store(fixture.Root);
	const auto configuration = fixture.Configuration();
	Require(!configuration.DebugStartInGame, "new normal launch configuration must default to the original startup sequence");
	store.Save(configuration);
	const auto initial = Read(store.ConfigurationPath());
	for (const auto flag : {"--skip-startup", "--debug-start-in-game"}) {
		const auto options = LaunchArguments({flag});
		const auto temporary = sgre::app::ApplyLaunchPreferences(configuration, options);
		Require(options.DebugStartInGame.value_or(temporary.DebugStartInGame) && !temporary.DebugStartInGame, "direct-game flag must affect only the current launch");
		Require(sgre::app::SerializeLaunchConfiguration(temporary) == initial, "startup-only flag changed the serialized remembered configuration");
	}
	const auto options = LaunchArguments({"--skip-startup", "--fullscreen", "--no-center-window"});
	const auto window_preferences = sgre::app::ApplyLaunchPreferences(configuration, options);
	store.Save(window_preferences);
	const sgre::app::LaunchConfigurationStore restarted(fixture.Root);
	const auto next = restarted.LoadOrCreate();
	Require(next.Disposition == sgre::app::LaunchConfigurationDisposition::Reused && next.Configuration.Fullscreen && !next.Configuration.WindowCentered && !next.Configuration.DebugStartInGame, "saving window preferences also persisted a temporary skip-startup flag");
	Require(!LaunchArguments({}).DebugStartInGame.value_or(next.Configuration.DebugStartInGame), "the next launch without flags must return to its saved normal startup");

	auto manual = next.Configuration;
	manual.DebugStartInGame = true;
	store.Save(manual);
	const auto remembered = restarted.LoadOrCreate();
	Require(LaunchArguments({}).DebugStartInGame.value_or(remembered.Configuration.DebugStartInGame), "explicit stored debug choice remains a compatible manual configuration input");
	const auto normal_options = LaunchArguments({"--normal-start", "--windowed", "--center-window"});
	const auto normal_window_preferences = sgre::app::ApplyLaunchPreferences(remembered.Configuration, normal_options);
	Require(!normal_options.DebugStartInGame.value_or(normal_window_preferences.DebugStartInGame) && normal_window_preferences.DebugStartInGame, "normal-start is an engaged false override, not a request to rewrite the saved manual choice");
	store.Save(normal_window_preferences);
	const auto manual_restart = restarted.LoadOrCreate();
	Require(manual_restart.Configuration.DebugStartInGame && !manual_restart.Configuration.Fullscreen && manual_restart.Configuration.WindowCentered, "temporary normal startup must not erase a compatible manual saved default while saving window settings");
	Require(LaunchArguments({}).DebugStartInGame.value_or(manual_restart.Configuration.DebugStartInGame), "manual saved startup default must resume after the temporary normal launch");
}

static void SceneJumpArgumentTests() {
	Require(!LaunchArguments({}).Jump && !LaunchArguments({"--skip-startup"}).Jump && !LaunchArguments({"--normal-start"}).Jump, "ordinary and existing startup options must not invent a custom scene request");
	const auto selected = LaunchArguments({"--jump", "reSG00_01.ks:1355"});
	Require(selected.Jump && selected.Jump->Storage == "reSG00_01.ks" && selected.Jump->TargetLine == 1355 && selected.DebugStartInGame == true, "jump must own its exact original storage/source line and imply per-launch direct entry");
	const auto zero = LaunchArguments({"--jump", "ExactCase.ks:0"});
	const auto maximum = LaunchArguments({"--jump", "chapter.ks:2147483647"});
	const auto unicode = LaunchArguments({"--headless", "--jump", "章名.ks:0007", "--help"});
	Require(zero.Jump->Storage == "ExactCase.ks" && zero.Jump->TargetLine == 0 && maximum.Jump->TargetLine == 2147483647 && unicode.Jump->Storage == "章名.ks" && unicode.Jump->TargetLine == 7 && unicode.Headless && unicode.Help, "jump parsing must preserve exact case/UTF-8, native nonnegative source lines and independent help/quiet flags");
	for (const auto flag : {"--skip-startup", "--debug-start-in-game"}) {
		const auto before = LaunchArguments({flag, "--jump", "chapter.ks:12"});
		const auto after = LaunchArguments({"--jump", "chapter.ks:12", flag});
		Require(before.Jump && after.Jump && before.DebugStartInGame == true && after.DebugStartInGame == true, "explicit direct-entry aliases must remain compatible with one jump in either order");
	}
	std::string request = "OwnedCase.ks:42";
	const auto owned = LaunchArguments({"--jump", request});
	request.assign("replaced.ks:9");
	Require(owned.Jump->Storage == "OwnedCase.ks" && owned.Jump->TargetLine == 42, "parsed scene request must own its bytes after argv storage changes");
	std::string maximum_bytes(4091, 'a');
	maximum_bytes += ".ks:0";
	Require(LaunchArguments({"--jump", maximum_bytes}).Jump->Storage.size() == 4094, "scene parsing must retain the existing bounded argument byte domain");
	const std::vector<std::vector<std::string_view>> invalid{{"--jump"}, {"--jump", "--help"}, {"--jump", "scene.ks"}, {"--jump", ":1"}, {"--jump", ".ks:1"}, {"--jump", "scene:1"}, {"--jump", "scene.KS:1"}, {"--jump", "scene.ks:"}, {"--jump", "scene.ks:-1"}, {"--jump", "scene.ks:-0"}, {"--jump", "scene.ks:+1"}, {"--jump", "scene.ks:1.0"}, {"--jump", "scene.ks:0x10"}, {"--jump", "scene.ks: 1"}, {"--jump", "scene.ks:1x"}, {"--jump", "scene.ks:2147483648"}, {"--jump", "scene.ks:99999999999999999999999999"}, {"--jump", "scene.ks:1:2"}, {"--jump", "../scene.ks:1"}, {"--jump", "/scene.ks:1"}, {"--jump", "folder/scene.ks:1"}, {"--jump", "folder\\scene.ks:1"}, {"--jump", "C:scene.ks:1"}, {"--jump", "scene\n.ks:1"}, {"--jump", "scene\x01.ks:1"}, {"--jump", "first.ks:1", "--jump", "second.ks:2"}, {"--jump", "scene.ks:1", "--normal-start"}, {"--normal-start", "--jump", "scene.ks:1"}, {"--jump=scene.ks:1"}};
	for (const auto& arguments : invalid) {
		Reject([&] {
			static_cast<void>(sgre::app::ParseLaunchArguments(arguments));
		});
	}
}

static void SceneJumpDoesNotPersistTests(const std::filesystem::path& parent) {
	const Fixture fixture(parent);
	sgre::app::LaunchConfigurationStore store(fixture.Root);
	const auto original = fixture.Configuration();
	store.Save(original);
	const auto before = Read(store.ConfigurationPath());
	const auto options = LaunchArguments({"--jump", "chapter.ks:42"});
	const auto temporary = sgre::app::ApplyLaunchPreferences(original, options);
	Require(options.DebugStartInGame == true && !temporary.DebugStartInGame && sgre::app::SerializeLaunchConfiguration(temporary) == before, "jump must not rewrite the persisted startup default or add scenario state to config");
	const auto window_options = LaunchArguments({"--jump", "chapter.ks:42", "--fullscreen", "--no-center-window"});
	store.Save(sgre::app::ApplyLaunchPreferences(original, window_options));
	const sgre::app::LaunchConfigurationStore restarted(fixture.Root);
	const auto next = restarted.LoadOrCreate();
	const auto next_options = LaunchArguments({});
	Require(next.Configuration.Fullscreen && !next.Configuration.WindowCentered && !next.Configuration.DebugStartInGame && !next_options.Jump && !next_options.DebugStartInGame.value_or(next.Configuration.DebugStartInGame), "saving window preferences alongside a jump must leave the next ordinary launch normal");
	const auto serialized = Read(store.ConfigurationPath());
	Require(serialized.find("chapter.ks") == std::string::npos && serialized.find("jump") == std::string::npos && serialized.find("target_line") == std::string::npos, "per-run scene identity escaped into remembered configuration");
	auto manual = next.Configuration;
	manual.DebugStartInGame = true;
	const auto retained = sgre::app::ApplyLaunchPreferences(manual, options);
	Require(retained.DebugStartInGame && retained.Installation == manual.Installation && retained.SaveDirectory == manual.SaveDirectory && retained.AudioSamplesPerSecond == manual.AudioSamplesPerSecond, "jump must preserve an explicit manual startup default and resource/audio/save preferences");
}

int main(int argument_count, char** arguments) {
	try {
		Require(argument_count == 2 || argument_count == 3, "supply ignored fixture parent and optional original installation for real seed/automatic repair checks");
		Fixture fixture(std::filesystem::canonical(arguments[1]));
		JsonTests(fixture);
		WindowSchemaAndMigrationTests(std::filesystem::canonical(arguments[1]));
		LaunchArgumentTests(fixture);
		SceneJumpArgumentTests();
		SceneJumpDoesNotPersistTests(std::filesystem::canonical(arguments[1]));
		StartupOverridesDoNotPersistTests(std::filesystem::canonical(arguments[1]));
		StoreTests(fixture);
		AssetPresenceTests(std::filesystem::canonical(arguments[1]));
		ManagedPathCollisionTests(std::filesystem::canonical(arguments[1]));
		AtomicRestartTests(fixture);
		std::cout << "PASS independent JSON/schema/Unicode/path/required-asset/seed-file/atomic-restart groups\n";
		if (argument_count == 3) {
			ActualInstallationPresenceTests(std::filesystem::canonical(arguments[1]), arguments[2]);
			OriginalSeedAndRepairTests(fixture, arguments[2]);
			std::cout << "PASS full actual installation availability/path-only restart and bounded original seed/corrupt-repair/backup groups (no game execution)\n";
		}

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
