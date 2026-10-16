#pragma once

#include "sgre/resource/psb.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::resource {

	struct SoundArchiveChannelDefinition {
		SoundArchiveChannelDefinition(const PsbDocument& document, const PsbValue& architecture_data);
		std::array<float, 8> Pan = {1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
		// The actual document/value storage stays live and unmodified until every definition/cue/source user retires.
		std::reference_wrapper<const PsbDocument> Document;
		std::reference_wrapper<const PsbValue> ArchitectureData;
	};

	struct SoundArchiveVoiceDefinition {
		std::int32_t Device = 0;
		float Volume = 1.0f;
		float PitchOctaves = 0.0f;
		float Priority = 0.0f;
		std::string Group;
		std::string Uniqueness;
		std::vector<SoundArchiveChannelDefinition> Channels;
	};

	struct SoundDefinitionLimits {
		std::size_t MaxVoices = 65536;
		std::size_t MaxChannelsPerVoice = 64;
		std::size_t MaxNameBytes = 4096;
		std::size_t MaxTotalChannels = 65536;
		std::size_t MaxOwnedFieldBytes = 64 * 1024 * 1024;
	};

	using SoundArchiveDefinitions = std::map<std::string, SoundArchiveVoiceDefinition, std::less<>>;

	[[nodiscard]] bool ContainsSoundConfigurationArchive(const PsbDocument& document, std::string_view archive_name);
	[[nodiscard]] std::string BuildSoundArchiveFilename(std::string_view directory_prefix, std::string_view archive_name, std::size_t max_bytes = 4096);
	// Native394c30 interprets these fields without decoding fmt/data/dpds or rejecting identity diagnostics.
	// Returned channel views require the same actual document owner until their last use.
	[[nodiscard]] SoundArchiveDefinitions ReadSoundArchiveDefinitions(const PsbDocument& document, SoundDefinitionLimits limits = {});

} // namespace sgre::resource
