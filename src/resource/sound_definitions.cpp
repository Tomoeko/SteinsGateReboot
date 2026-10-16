#include "sgre/resource/sound_definitions.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::resource {

	SoundArchiveChannelDefinition::SoundArchiveChannelDefinition(const PsbDocument& document, const PsbValue& architecture_data) : Document(document), ArchitectureData(architecture_data) {
	}

	static const PsbValue& RequireDefinitionField(const PsbValue& parent, std::string_view name) {
		const auto* value = parent.Find(name);
		if (!value) {
			throw std::runtime_error("sound archive definition is missing " + std::string(name));
		}

		return *value;
	}

	static float DefinitionNumber(const PsbValue& value, std::string_view name) {
		if (const auto* number = std::get_if<double>(&value.Data)) {
			if (!std::isfinite(*number) || std::abs(*number) > std::numeric_limits<float>::max()) {
				throw std::runtime_error("sound archive definition numeric field is nonfinite or out of range: " + std::string(name));
			}

			return static_cast<float>(*number);
		}
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return static_cast<float>(*integer);
		}
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1.0f : 0.0f;
		}

		throw std::runtime_error("sound archive definition field is not numeric: " + std::string(name));
	}

	static float OptionalDefinitionNumber(const PsbValue& value, std::string_view name, float fallback) {
		const auto* number = value.Find(name);

		return number ? DefinitionNumber(*number, name) : fallback;
	}

	static std::string OptionalDefinitionString(const PsbValue& value, std::string_view name, std::size_t max_bytes) {
		const auto* field = value.Find(name);
		if (!field) {
			return {};
		}

		const auto* text = std::get_if<std::string>(&field->Data);
		if (!text || text->size() > max_bytes || text->find('\0') != std::string::npos) {
			throw std::runtime_error("sound archive definition string exceeds its exact bounded domain: " + std::string(name));
		}

		return *text;
	}

	static std::int32_t DefinitionDevice(const PsbValue& value) {
		const auto* integer = std::get_if<std::int64_t>(&value.Data);
		if (!integer || *integer < std::numeric_limits<std::int32_t>::min() || *integer > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("sound archive definition device requires its bounded native int32 field");
		}

		return static_cast<std::int32_t>(*integer);
	}

	bool ContainsSoundConfigurationArchive(const PsbDocument& document, std::string_view archive_name) {
		const auto* archive = document.Root.Find("archive");
		if (!archive) {
			return false;
		}
		if (!std::holds_alternative<PsbObject>(archive->Data)) {
			throw std::runtime_error("sound configuration archive declaration requires the actual object/key lookup domain");
		}

		return archive->Find(archive_name) != nullptr;
	}

	std::string BuildSoundArchiveFilename(std::string_view directory_prefix, std::string_view archive_name, std::size_t max_bytes) {
		static constexpr std::size_t kInsertedBytes = 5;
		if (max_bytes < kInsertedBytes || directory_prefix.size() > max_bytes - kInsertedBytes || archive_name.size() > max_bytes - kInsertedBytes - directory_prefix.size() || directory_prefix.find('\0') != std::string_view::npos || archive_name.find('\0') != std::string_view::npos) {
			throw std::runtime_error("sound archive filename exceeds its exact bounded prefix/name domain");
		}

		return std::string(directory_prefix) + '/' + std::string(archive_name) + ".psb";
	}

	static SoundArchiveVoiceDefinition ReadVoiceDefinition(const PsbDocument& document, const PsbValue& voice, SoundDefinitionLimits limits) {
		if (!std::holds_alternative<PsbObject>(voice.Data)) {
			throw std::runtime_error("sound archive voice definition requires its actual object fields");
		}

		SoundArchiveVoiceDefinition definition;
		definition.Device = DefinitionDevice(RequireDefinitionField(voice, "device"));
		definition.Volume = OptionalDefinitionNumber(voice, "volume", 1.0f);
		definition.PitchOctaves = OptionalDefinitionNumber(voice, "pitch", 0.0f);
		definition.Priority = OptionalDefinitionNumber(voice, "priority", 0.0f);
		definition.Group = OptionalDefinitionString(voice, "group", limits.MaxNameBytes);
		definition.Uniqueness = OptionalDefinitionString(voice, "uniq", limits.MaxNameBytes);
		const auto* channels = std::get_if<PsbArray>(&RequireDefinitionField(voice, "channelList").Data);
		if (!channels || channels->size() > limits.MaxChannelsPerVoice) {
			throw std::runtime_error("sound archive definition channelList exceeds its bounded array domain");
		}

		definition.Channels.reserve(channels->size());
		for (const auto& channel : *channels) {
			const auto& architecture_data = RequireDefinitionField(channel, "archData");
			if (!std::holds_alternative<PsbObject>(architecture_data.Data)) {
				throw std::runtime_error("sound archive channel archData requires its actual object view");
			}

			SoundArchiveChannelDefinition entry(document, architecture_data);
			if (const auto* pan_value = channel.Find("pan")) {
				const auto* pan = std::get_if<PsbArray>(&pan_value->Data);
				if (!pan || pan->size() < 2) {
					throw std::runtime_error("sound archive channel pan requires the first two coefficients");
				}

				entry.Pan[0] = DefinitionNumber((*pan)[0], "pan left");
				entry.Pan[1] = DefinitionNumber((*pan)[1], "pan right");
			}

			definition.Channels.push_back(std::move(entry));
		}

		return definition;
	}

	SoundArchiveDefinitions ReadSoundArchiveDefinitions(const PsbDocument& document, SoundDefinitionLimits limits) {
		if (limits.MaxVoices == 0 || limits.MaxVoices > 65536 || limits.MaxChannelsPerVoice == 0 || limits.MaxChannelsPerVoice > 64 || limits.MaxNameBytes == 0 || limits.MaxNameBytes > 65536 || limits.MaxTotalChannels == 0 || limits.MaxTotalChannels > 65536 || limits.MaxOwnedFieldBytes == 0 || limits.MaxOwnedFieldBytes > 64 * 1024 * 1024) {
			throw std::runtime_error("sound archive definition reader requires bounded voice/channel/name work");
		}

		const auto* voices = std::get_if<PsbObject>(&RequireDefinitionField(document.Root, "voice").Data);
		if (!voices || voices->size() > limits.MaxVoices) {
			throw std::runtime_error("sound archive voice definition table exceeds its bounded object domain");
		}

		SoundArchiveDefinitions definitions;
		std::size_t remaining_channels = limits.MaxTotalChannels;
		std::size_t remaining_fields = limits.MaxOwnedFieldBytes;
		for (const auto& [name, value] : *voices) {
			if (name.size() > limits.MaxNameBytes || name.find('\0') != std::string::npos) {
				throw std::runtime_error("sound archive voice definition name exceeds its exact bounded identity");
			}
			if (definitions.contains(name)) {
				continue;
			}

			const auto* channels = std::get_if<PsbArray>(&RequireDefinitionField(value, "channelList").Data);
			if (!channels || channels->size() > remaining_channels || channels->size() > remaining_fields / sizeof(SoundArchiveChannelDefinition)) {
				throw std::runtime_error("sound archive definitions exceed their total channel/storage allocation bound");
			}
			remaining_channels -= channels->size();
			remaining_fields -= channels->size() * sizeof(SoundArchiveChannelDefinition);
			for (const auto field_size : {sizeof(SoundArchiveVoiceDefinition) + sizeof(std::string), name.size()}) {
				if (field_size > remaining_fields) {
					throw std::runtime_error("sound archive definition names/records exceed their storage allocation bound");
				}

				remaining_fields -= field_size;
			}
			for (const auto field_name : {"group", "uniq"}) {
				const auto* field = value.Find(field_name);
				if (!field) {
					continue;
				}

				const auto* text = std::get_if<std::string>(&field->Data);
				if (!text || text->size() > remaining_fields) {
					throw std::runtime_error("sound archive definition strings exceed their storage allocation bound");
				}

				remaining_fields -= text->size();
			}

			try {
				definitions.emplace(name, ReadVoiceDefinition(document, value, limits));
			} catch (const std::runtime_error& error) {
				throw std::runtime_error("sound archive voice " + name + ": " + error.what());
			}
		}

		return definitions;
	}

} // namespace sgre::resource
