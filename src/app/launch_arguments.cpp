#include "sgre/app/launch_arguments.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
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

	static LaunchSceneJump ParseSceneJump(std::string_view value) {
		const auto colon = value.find(':');
		if (colon == std::string_view::npos || value.find(':', colon + 1) != std::string_view::npos) {
			throw std::runtime_error("--jump requires an exact scenario basename and decimal source line: SCENE.ks:LINE");
		}
		const auto storage = value.substr(0, colon);
		if (storage.size() <= 3 || !storage.ends_with(".ks")) {
			throw std::runtime_error("--jump scene must be an exact nonempty .ks basename");
		}
		for (const auto character : storage) {
			const auto byte = static_cast<unsigned char>(character);
			if (byte < 32 || byte == 127 || character == '/' || character == '\\') {
				throw std::runtime_error("--jump scene must preserve its exact basename without paths or control characters");
			}
		}
		const auto line = value.substr(colon + 1);
		std::int32_t target = 0;
		const auto parsed = std::from_chars(line.data(), line.data() + line.size(), target);
		if (line.empty() || line.front() == '-' || parsed.ec != std::errc{} || parsed.ptr != line.data() + line.size() || target < 0) {
			throw std::runtime_error("--jump line requires a decimal integer in0.." + std::to_string(std::numeric_limits<std::int32_t>::max()));
		}

		return {std::string(storage), target};
	}

	static DisplayResolution ParseDisplayResolution(std::string_view value) {
		std::string normalized(value);
		for (auto& character : normalized) {
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
		}
		for (const auto& resolution : kDisplayResolutions) {
			if (normalized == resolution.Name || normalized == resolution.Dimensions) {
				return resolution;
			}
		}
		if (normalized == "2160p") {
			return kDisplayResolutions.back();
		}
		std::uint32_t extent = 0;
		const auto parsed = std::from_chars(normalized.data(), normalized.data() + normalized.size(), extent);
		if (parsed.ec == std::errc{} && parsed.ptr == normalized.data() + normalized.size()) {
			for (const auto& resolution : kDisplayResolutions) {
				if (extent == resolution.Width || extent == resolution.Height) {
					return resolution;
				}
			}
		}
		throw std::runtime_error("display size requires 576, 720, 1080, 1440, 2160 or 4k; preset widths, p suffixes and exact WIDTHxHEIGHT dimensions are also accepted");
	}

	static bool IsDisplaySizeArgument(std::string_view value) {
		return !value.empty() && ((value.front() >= '0' && value.front() <= '9') || value == "4k" || value == "4K");
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
			} else if (option == "--jump" || option == "--checkpoint-worker") {
				RequireUnique(result.Jump.has_value(), "scene jump");
				result.Jump = ParseSceneJump(OptionValue(arguments, index));
				result.CheckpointWorker = option == "--checkpoint-worker";
			} else if (option == "--fullscreen" || option == "--windowed") {
				RequireUnique(result.Fullscreen.has_value(), "window mode");
				result.Fullscreen = option == "--fullscreen";
				if (option == "--windowed" && index + 1 < arguments.size() && IsDisplaySizeArgument(arguments[index + 1])) {
					result.WindowSize = ParseDisplayResolution(arguments[++index]);
				}
			} else if (option == "--center-window" || option == "--no-center-window") {
				RequireUnique(result.WindowCentered.has_value(), "window placement");
				result.WindowCentered = option == "--center-window";
			} else if (option == "--resolution" || option == "--res") {
				RequireUnique(result.Resolution.has_value(), "display resolution");
				result.Resolution = ParseDisplayResolution(OptionValue(arguments, index));
			} else if (option == "--headless") {
				RequireUnique(result.Headless, "headless observation");
				result.Headless = true;
			} else if (option == "--test-events") {
				RequireUnique(result.TestEvents, "test event stream");
				result.TestEvents = true;
			} else if (option == "--test-parent") {
				RequireUnique(result.TestParent.has_value(), "test predecessor");
				result.TestParent = OptionValue(arguments, index);
				if (*result.TestParent != "new-game" && (result.TestParent->size() != 64 || !std::all_of(result.TestParent->begin(), result.TestParent->end(), [](char byte) {
					    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
				    }))) {
					throw std::runtime_error("test predecessor requires new-game or an exact lowercase SHA-256");
				}
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
		if (result.TestEvents && (!result.Headless || !result.SaveDirectory || !result.CheckpointWorker)) {
			throw std::runtime_error("--test-events requires explicit --headless, --checkpoint-worker and isolated --save-root");
		}
		if (result.TestParent && !result.TestEvents) {
			throw std::runtime_error("test predecessor selection requires --test-events");
		}
		if (result.CheckpointWorker && !result.Headless) {
			throw std::runtime_error("internal checkpoint worker requires explicit headless policy");
		}
		if (result.Jump) {
			if (result.DebugStartInGame == false) {
				throw std::runtime_error("--jump cannot be combined with --normal-start");
			}
			result.DebugStartInGame = true;
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
		if (arguments.Resolution) {
			configuration.Width = arguments.Resolution->Width;
			configuration.Height = arguments.Resolution->Height;
			configuration.WindowWidth = arguments.Resolution->Width;
			configuration.WindowHeight = arguments.Resolution->Height;
		}
		if (arguments.WindowSize) {
			configuration.WindowWidth = arguments.WindowSize->Width;
			configuration.WindowHeight = arguments.WindowSize->Height;
		}
		return configuration;
	}
} // namespace sgre::app
