#include "sgre/app/launch_arguments.hpp"

#include <charconv>
#include <stdexcept>
#include <string>
#include <system_error>

namespace sgre::app {
	static constexpr std::size_t kMaximumLaunchArguments = 32;
	static constexpr std::size_t kMaximumLaunchArgumentBytes = 4096;
	static constexpr std::size_t kMaximumLaunchBytes = 16384;

	static void RequireUnique(bool present, std::string_view group) {
		if (present) {
			throw std::runtime_error("select only one launch option for " + std::string(group));
		}
	}

	static std::uint64_t ParseLaunchLimit(std::string_view text, std::uint64_t maximum, std::string_view option) {
		if (text.empty()) {
			throw std::runtime_error(std::string(option) + " requires an integer in1.." + std::to_string(maximum));
		}
		std::uint64_t value = 0;
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
		if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0 || value > maximum) {
			throw std::runtime_error(std::string(option) + " requires an integer in1.." + std::to_string(maximum));
		}
		return value;
	}

	static std::string_view OptionValue(std::span<const std::string_view> arguments, std::size_t& index) {
		const auto option = arguments[index];
		if (index + 1 == arguments.size() || arguments[index + 1].empty() || arguments[index + 1].starts_with('-')) {
			throw std::runtime_error("launch option requires a value before another option: " + std::string(option));
		}
		return arguments[++index];
	}

	static std::filesystem::path LaunchPath(std::string_view text) {
		std::u8string encoded;
		encoded.reserve(text.size());
		for (const auto byte : text) {
			encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
		}
		return std::filesystem::path(encoded);
	}

	LaunchArguments ParseLaunchArguments(std::span<const std::string_view> arguments) {
		if (arguments.size() > kMaximumLaunchArguments) {
			throw std::runtime_error("launch argument count exceeds32");
		}
		std::size_t bytes = 0;
		for (const auto argument : arguments) {
			if (argument.empty() || argument.size() > kMaximumLaunchArgumentBytes || argument.size() > kMaximumLaunchBytes - bytes || argument.find('\0') != std::string_view::npos) {
				throw std::runtime_error("launch argument is empty, contains NUL or exceeds bounded startup input");
			}
			bytes += argument.size();
		}
		LaunchArguments result;
		for (std::size_t index = 0; index != arguments.size(); ++index) {
			const auto option = arguments[index];
			if (option == "--skip-startup" || option == "--debug-start-in-game" || option == "--normal-start") {
				RequireUnique(result.DebugStartInGame.has_value(), "startup sequence");
				result.DebugStartInGame = option != "--normal-start";
			} else if (option == "--fullscreen" || option == "--windowed") {
				RequireUnique(result.Fullscreen.has_value(), "window mode");
				result.Fullscreen = option == "--fullscreen";
			} else if (option == "--center-window" || option == "--no-center-window") {
				RequireUnique(result.WindowCentered.has_value(), "window placement");
				result.WindowCentered = option == "--center-window";
			} else if (option == "--headless") {
				RequireUnique(result.Headless, "headless observation");
				result.Headless = true;
			} else if (option == "--validate-config") {
				RequireUnique(result.ValidateOnly, "config validation");
				result.ValidateOnly = true;
			} else if (option == "--help") {
				RequireUnique(result.Help, "help");
				result.Help = true;
			} else if (option == "--maximum-milliseconds") {
				RequireUnique(result.MaximumMilliseconds.has_value(), "wall observation bound");
				result.MaximumMilliseconds = ParseLaunchLimit(OptionValue(arguments, index), 900000, option);
			} else if (option == "--maximum-iterations") {
				RequireUnique(result.MaximumIterations.has_value(), "iteration observation bound");
				result.MaximumIterations = static_cast<std::size_t>(ParseLaunchLimit(OptionValue(arguments, index), 100000, option));
			} else if (option == "--save-root") {
				RequireUnique(result.SaveDirectory.has_value(), "per-run save directory");
				result.SaveDirectory = LaunchPath(OptionValue(arguments, index));
			} else if (option.starts_with('-')) {
				throw std::runtime_error("unknown launch option: " + std::string(option));
			} else {
				RequireUnique(result.Installation.has_value(), "installation path");
				result.Installation = LaunchPath(option);
			}
		}
		return result;
	}

	LaunchConfiguration ApplyLaunchPreferences(LaunchConfiguration configuration, const LaunchArguments& arguments) {
		if (arguments.Fullscreen) {
			configuration.Fullscreen = *arguments.Fullscreen;
		}
		if (arguments.WindowCentered) {
			configuration.WindowCentered = *arguments.WindowCentered;
		}
		return configuration;
	}
} // namespace sgre::app
