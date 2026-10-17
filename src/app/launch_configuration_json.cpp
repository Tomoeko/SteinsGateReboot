#include "sgre/app/launch_configuration.hpp"

#include <charconv>
#include <cstddef>
#include <map>
#include <system_error>
#include <utility>
#include <variant>

namespace sgre::app {

	static constexpr std::size_t kMaximumConfigBytes = 64 * 1024;
	static constexpr std::size_t kMaximumPathBytes = 4096;
	using ConfigValue = std::variant<std::string, std::uint32_t, bool>;
	using ConfigFields = std::map<std::string, ConfigValue, std::less<>>;

	static void FormatRequire(bool condition, std::string_view message) {
		if (!condition) {
			throw LaunchConfigurationFormatError(std::string(message));
		}
	}

	static void ValidateUtf8(std::string_view text) {
		std::size_t offset = 0;
		while (offset < text.size()) {
			const auto lead = static_cast<unsigned char>(text[offset++]);
			if (lead < 0x80) {
				continue;
			}
			const std::size_t count = lead >= 0xc2 && lead <= 0xdf ? 1 : lead >= 0xe0 && lead <= 0xef ? 2
			    : lead >= 0xf0 && lead <= 0xf4                                                        ? 3
			                                                                                          : 0;
			FormatRequire(count != 0 && count <= text.size() - offset, "config string contains malformed or truncated UTF-8");
			std::uint32_t code = lead & (count == 1 ? 0x1fU : count == 2 ? 0x0fU
			                                                             : 0x07U);
			for (std::size_t index = 0; index < count; ++index) {
				const auto next = static_cast<unsigned char>(text[offset++]);
				FormatRequire((next & 0xc0) == 0x80, "config string contains an invalid UTF-8 continuation");
				code = (code << 6) | (next & 0x3fU);
			}
			FormatRequire(code >= (count == 1 ? 0x80U : count == 2 ? 0x800U
			                                                       : 0x10000U) &&
			        code <= 0x10ffff && (code < 0xd800 || code > 0xdfff),
			    "config string contains overlong UTF-8 or an invalid Unicode scalar");
		}
	}

	static void AppendUtf8(std::string& text, std::uint32_t code) {
		if (code <= 0x7f) {
			text.push_back(static_cast<char>(code));
		} else if (code <= 0x7ff) {
			text.push_back(static_cast<char>(0xc0 | (code >> 6)));
			text.push_back(static_cast<char>(0x80 | (code & 0x3f)));
		} else if (code <= 0xffff) {
			text.push_back(static_cast<char>(0xe0 | (code >> 12)));
			text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
			text.push_back(static_cast<char>(0x80 | (code & 0x3f)));
		} else {
			text.push_back(static_cast<char>(0xf0 | (code >> 18)));
			text.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
			text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
			text.push_back(static_cast<char>(0x80 | (code & 0x3f)));
		}
	}

	class ConfigJsonReader {
	public:
		explicit ConfigJsonReader(std::string_view text) : m_Text(text) {
			FormatRequire(!text.empty() && text.size() <= kMaximumConfigBytes, "config JSON must contain 1 to 65536 bytes");
		}

		ConfigFields Read() {
			Expect('{');
			ConfigFields result;
			if (!Take('}')) {
				do {
					FormatRequire(result.size() < 19, "config contains too many fields");
					auto key = ReadString();
					Expect(':');
					SkipSpace();
					auto value = ReadValue();
					FormatRequire(result.emplace(std::move(key), std::move(value)).second, "config contains a duplicate field");
				} while (Take(','));
				Expect('}');
			}
			SkipSpace();
			FormatRequire(m_Offset == m_Text.size(), "config JSON has trailing bytes");

			return result;
		}

	private:
		std::string_view m_Text;
		std::size_t m_Offset = 0;

		char Peek() const {
			FormatRequire(m_Offset < m_Text.size(), "config JSON is truncated");

			return m_Text[m_Offset];
		}

		void SkipSpace() {
			while (m_Offset < m_Text.size() && (m_Text[m_Offset] == ' ' || m_Text[m_Offset] == '\t' || m_Text[m_Offset] == '\r' || m_Text[m_Offset] == '\n')) {
				++m_Offset;
			}
		}

		bool Take(char expected) {
			SkipSpace();
			if (m_Offset < m_Text.size() && m_Text[m_Offset] == expected) {
				++m_Offset;
				return true;
			}

			return false;
		}

		void Expect(char expected) {
			FormatRequire(Take(expected), "config JSON has an unexpected token or is truncated");
		}

		std::uint32_t HexWord() {
			FormatRequire(m_Text.size() - m_Offset >= 4, "config Unicode escape is truncated");
			std::uint32_t word = 0;
			for (std::size_t index = 0; index < 4; ++index) {
				const auto character = m_Text[m_Offset++];
				const auto digit = character >= '0' && character <= '9' ? character - '0' : character >= 'a' && character <= 'f' ? character - 'a' + 10
				    : character >= 'A' && character <= 'F'                                                                       ? character - 'A' + 10
				                                                                                                                 : -1;
				FormatRequire(digit >= 0, "config Unicode escape contains a nonhex digit");
				word = (word << 4) | static_cast<std::uint32_t>(digit);
			}

			return word;
		}

		void ReadEscape(std::string& result) {
			const auto escaped = Peek();
			++m_Offset;
			if (escaped == '"' || escaped == '\\' || escaped == '/') {
				result.push_back(escaped);
			} else if (escaped == 'b' || escaped == 'f' || escaped == 'n' || escaped == 'r' || escaped == 't') {
				result.push_back(escaped == 'b' ? '\b' : escaped == 'f' ? '\f'
				        : escaped == 'n'                                ? '\n'
				        : escaped == 'r'                                ? '\r'
				                                                        : '\t');
			} else {
				FormatRequire(escaped == 'u', "config string contains an unsupported escape");
				auto code = HexWord();
				if (code >= 0xd800 && code <= 0xdbff) {
					FormatRequire(Peek() == '\\', "config Unicode high surrogate has no low surrogate");
					++m_Offset;
					FormatRequire(Peek() == 'u', "config Unicode high surrogate has no low surrogate");
					++m_Offset;
					const auto low = HexWord();
					FormatRequire(low >= 0xdc00 && low <= 0xdfff, "config Unicode low surrogate is invalid");
					code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
				} else {
					FormatRequire(code < 0xdc00 || code > 0xdfff, "config contains an unpaired Unicode low surrogate");
				}
				AppendUtf8(result, code);
			}
		}

		std::string ReadString() {
			Expect('"');
			std::string result;
			while (true) {
				const auto character = Peek();
				++m_Offset;
				if (character == '"') {
					break;
				}
				FormatRequire(static_cast<unsigned char>(character) >= 0x20, "config string contains an unescaped control character");
				if (character == '\\') {
					ReadEscape(result);
				} else {
					result.push_back(character);
				}
				FormatRequire(result.size() <= kMaximumPathBytes, "config string exceeds its 4096-byte bound");
			}
			ValidateUtf8(result);

			return result;
		}

		std::uint32_t ReadInteger() {
			const auto begin = m_Offset;
			while (m_Offset < m_Text.size() && m_Text[m_Offset] >= '0' && m_Text[m_Offset] <= '9') {
				++m_Offset;
			}
			FormatRequire(m_Offset != begin && (m_Offset - begin == 1 || m_Text[begin] != '0'), "config value must be a nonnegative integer without leading zeroes");
			std::uint32_t result = 0;
			const auto converted = std::from_chars(m_Text.data() + begin, m_Text.data() + m_Offset, result);
			FormatRequire(converted.ec == std::errc{}, "config integer exceeds uint32 range");

			return result;
		}

		ConfigValue ReadValue() {
			if (Peek() == '"') {
				return ReadString();
			}
			if (Peek() == 't' || Peek() == 'f') {
				const bool value = Peek() == 't';
				const std::string_view expected = value ? "true" : "false";
				FormatRequire(m_Text.substr(m_Offset, expected.size()) == expected, "config boolean is malformed or truncated");
				m_Offset += expected.size();
				return value;
			}

			return ReadInteger();
		}
	};

	static std::string Utf8Path(const std::filesystem::path& path) {
		const auto encoded = path.generic_u8string();
		std::string result;
		for (const auto byte : encoded) {
			result.push_back(static_cast<char>(byte));
		}
		ValidateUtf8(result);
		FormatRequire(result.size() <= kMaximumPathBytes, "config path exceeds its 4096-byte bound");
		for (const auto byte : result) {
			FormatRequire(static_cast<unsigned char>(byte) >= 0x20, "config path contains a control character");
		}

		return result;
	}

	static std::filesystem::path ReadPath(const ConfigFields& fields, std::string_view key, bool allow_empty = false) {
		const auto found = fields.find(key);
		FormatRequire(found != fields.end() && std::holds_alternative<std::string>(found->second), "config path field is missing or not a string");
		const auto& text = std::get<std::string>(found->second);
		std::u8string encoded;
		for (const auto byte : text) {
			encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
		}
		const std::filesystem::path path(encoded);
		FormatRequire((allow_empty && text.empty()) || (!text.empty() && path.is_absolute()), "config paths must be nonempty absolute paths (seed_file may be empty)");
		static_cast<void>(Utf8Path(path));

		return path;
	}

	static std::uint32_t ReadNumber(const ConfigFields& fields, std::string_view key, std::uint32_t minimum, std::uint32_t maximum) {
		const auto found = fields.find(key);
		FormatRequire(found != fields.end() && std::holds_alternative<std::uint32_t>(found->second), "config numeric field is missing or not an integer");
		const auto result = std::get<std::uint32_t>(found->second);
		FormatRequire(result >= minimum && result <= maximum, "config numeric field exceeds its supported launch range");

		return result;
	}

	static bool ReadBoolean(const ConfigFields& fields, std::string_view key) {
		const auto found = fields.find(key);
		FormatRequire(found != fields.end() && std::holds_alternative<bool>(found->second), "config field must be a boolean: " + std::string(key));
		return std::get<bool>(found->second);
	}

	ParsedLaunchConfiguration ParseLaunchConfigurationDocument(std::string_view json) {
		const auto fields = ConfigJsonReader(json).Read();
		const auto version = ReadNumber(fields, "version", 1, 2);
		auto expected_fields = version == 1 ? 11U : 15U;
		for (const auto key : {"master_volume", "music_volume", "voice_volume", "effects_volume"}) {
			expected_fields += fields.contains(key) ? 1U : 0U;
		}
		FormatRequire(fields.size() == expected_fields, "config schema has missing or unknown fields");
		ParsedLaunchConfiguration parsed;
		parsed.SchemaVersion = version;
		auto& result = parsed.Configuration;
		result.AudioSettingsStored = expected_fields != (version == 1 ? 11U : 15U);
		result.Installation = ReadPath(fields, "installation");
		result.SpriteShader = ReadPath(fields, "sprite_shader");
		result.PresentationShader = ReadPath(fields, "presentation_shader");
		result.SaveDirectory = ReadPath(fields, "save_directory");
		result.SeedFile = ReadPath(fields, "seed_file", true);
		result.Width = ReadNumber(fields, "width", 1, 16384);
		result.Height = ReadNumber(fields, "height", 1, 16384);
		result.AudioSamplesPerSecond = ReadNumber(fields, "audio_rate", 8000, 192000);
		result.AudioChannels = ReadNumber(fields, "audio_channels", 1, 8);
		result.DebugStartInGame = ReadBoolean(fields, "debug_start_in_game");
		if (version == 1) {
			result.WindowWidth = result.Width;
			result.WindowHeight = result.Height;
		} else {
			result.WindowWidth = ReadNumber(fields, "window_width", 1, 16384);
			result.WindowHeight = ReadNumber(fields, "window_height", 1, 16384);
			result.Fullscreen = ReadBoolean(fields, "fullscreen");
			result.WindowCentered = ReadBoolean(fields, "window_centered");
		}
		if (fields.contains("master_volume")) {
			result.Audio.Master = ReadNumber(fields, "master_volume", 0, 100);
		}
		if (fields.contains("music_volume")) {
			result.Audio.Music = ReadNumber(fields, "music_volume", 0, 100);
		}
		if (fields.contains("voice_volume")) {
			result.Audio.Voice = ReadNumber(fields, "voice_volume", 0, 100);
		}
		if (fields.contains("effects_volume")) {
			result.Audio.Effects = ReadNumber(fields, "effects_volume", 0, 100);
		}

		return parsed;
	}

	LaunchConfiguration ParseLaunchConfiguration(std::string_view json) {
		return ParseLaunchConfigurationDocument(json).Configuration;
	}

	static std::string QuotePath(const std::filesystem::path& path) {
		const auto text = Utf8Path(path);
		std::string result = "\"";
		for (const auto character : text) {
			if (character == '"' || character == '\\') {
				result.push_back('\\');
			}
			result.push_back(character);
		}
		result.push_back('"');

		return result;
	}

	std::string SerializeLaunchConfiguration(const LaunchConfiguration& configuration) {
		auto json = std::string("{\n  \"version\": 2,\n  \"installation\": ") + QuotePath(configuration.Installation) + ",\n  \"sprite_shader\": " + QuotePath(configuration.SpriteShader) + ",\n  \"presentation_shader\": " + QuotePath(configuration.PresentationShader) + ",\n  \"save_directory\": " + QuotePath(configuration.SaveDirectory) + ",\n  \"seed_file\": " + QuotePath(configuration.SeedFile) + ",\n  \"width\": " + std::to_string(configuration.Width) + ",\n  \"height\": " + std::to_string(configuration.Height) + ",\n  \"audio_rate\": " + std::to_string(configuration.AudioSamplesPerSecond) + ",\n  \"audio_channels\": " + std::to_string(configuration.AudioChannels) + ",\n  \"debug_start_in_game\": " + (configuration.DebugStartInGame ? "true" : "false") + ",\n  \"window_width\": " + std::to_string(configuration.WindowWidth) + ",\n  \"window_height\": " + std::to_string(configuration.WindowHeight) + ",\n  \"fullscreen\": " + (configuration.Fullscreen ? "true" : "false") + ",\n  \"window_centered\": " + (configuration.WindowCentered ? "true" : "false");
		if (configuration.AudioSettingsStored || configuration.Audio != audio::UserAudioSettings{}) {
			json += ",\n  \"master_volume\": " + std::to_string(configuration.Audio.Master) + ",\n  \"music_volume\": " + std::to_string(configuration.Audio.Music) + ",\n  \"voice_volume\": " + std::to_string(configuration.Audio.Voice) + ",\n  \"effects_volume\": " + std::to_string(configuration.Audio.Effects);
		}
		json += "\n}\n";
		static_cast<void>(ParseLaunchConfiguration(json));

		return json;
	}

} // namespace sgre::app
