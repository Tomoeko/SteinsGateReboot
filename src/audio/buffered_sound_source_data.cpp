#include "sgre/audio/buffered_sound_source_data.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace sgre::audio {

	static constexpr std::size_t kMaximumDecodedPacketBytes = 4 * 1024 * 1024;

	static const resource::PsbValue& RequireSourceField(const resource::PsbValue& architecture, std::string_view field) {
		const auto* value = architecture.Find(field);
		if (!value) {
			throw std::runtime_error("buffered sound architecture is missing " + std::string(field));
		}

		return *value;
	}

	static resource::PsbChunkRange SourceChunk(const resource::PsbDocument& document, const resource::PsbValue& value, std::string_view field) {
		const auto* reference = std::get_if<resource::PsbChunkReference>(&value.Data);
		if (!reference) {
			throw std::runtime_error("buffered sound architecture requires a chunk for " + std::string(field));
		}

		const auto& ranges = reference->Extra ? document.ExtraChunks : document.Chunks;
		if (reference->Index >= ranges.size()) {
			throw std::runtime_error("buffered sound chunk index exceeds its actual document table for " + std::string(field));
		}

		return ranges[reference->Index];
	}

	static std::span<const std::uint8_t> SourceBytes(resource::PsbChunkRange range, std::span<const std::uint8_t> bytes, std::string_view field) {
		if (range.Offset > bytes.size() || range.Size > bytes.size() - range.Offset) {
			throw std::runtime_error("buffered sound chunk exceeds its actual archive backing for " + std::string(field));
		}

		return bytes.subspan(static_cast<std::size_t>(range.Offset), static_cast<std::size_t>(range.Size));
	}

	static bool SourceFlag(const resource::PsbValue& architecture, std::string_view field) {
		const auto* value = architecture.Find(field);
		if (!value) {
			return false;
		}

		if (const auto* boolean = std::get_if<bool>(&value->Data)) {
			return *boolean;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&value->Data)) {
			// Native4ca6d0 normalizes the full 32/64-bit getter result through TEST and SETNE.
			return *integer != 0;
		}
		if (const auto* number = std::get_if<double>(&value->Data)) {
			return *number != 0.0;
		}

		throw std::runtime_error("buffered sound architecture flag has an unsupported type for " + std::string(field));
	}

	static std::uint32_t SourceLoopInteger(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			if (*integer < 0 || *integer > std::numeric_limits<std::int32_t>::max()) {
				throw std::runtime_error("buffered sound sample loop exceeds its nonnegative signed native range");
			}

			return static_cast<std::uint32_t>(*integer);
		}
		if (const auto* number = std::get_if<double>(&value.Data)) {
			if (!std::isfinite(*number) || *number < 0 || *number > std::numeric_limits<std::int32_t>::max()) {
				throw std::runtime_error("buffered sound sample loop exceeds its finite nonnegative signed native range");
			}

			return static_cast<std::uint32_t>(std::trunc(*number));
		}

		throw std::runtime_error("buffered sound sample loop has an unsupported numeric type");
	}

	static void ReadSourceLoop(const resource::PsbValue& architecture, resource::SoundChannel& channel) {
		const auto* value = architecture.Find("loop");
		if (!value) {
			return;
		}

		const auto* values = std::get_if<resource::PsbArray>(&value->Data);
		if (!values || values->size() < 2) {
			throw std::runtime_error("buffered sound sample loop requires its first two elements");
		}

		resource::SampleLoop loop;
		loop.Begin = SourceLoopInteger((*values)[0]);
		loop.Length = SourceLoopInteger((*values)[1]);
		channel.Loop = loop;
	}

	static void ReadSourcePackets(BufferedSoundSourceData& source) {
		const auto bytes = source.DecodedPacketBytes;
		if (bytes.empty() || bytes.size() % 4 != 0 || bytes.size() > kMaximumDecodedPacketBytes) {
			throw std::runtime_error("buffered WMA decoded-packet table has an invalid bounded length");
		}

		auto& packets = source.Description.DecodedPacketCumulativeBytes;
		packets.reserve(bytes.size() / 4);
		std::uint32_t previous = 0;
		for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
			const auto cumulative = static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
			if (cumulative < previous) {
				throw std::runtime_error("buffered WMA decoded-packet table decreases");
			}

			packets.push_back(cumulative);
			previous = cumulative;
		}

		if (source.EncodedBytes.size() % packets.size() != 0 || source.EncodedBytes.size() / packets.size() != source.Description.Format.BlockAlignment) {
			throw std::runtime_error("buffered WMA packet table/data/block alignment disagree");
		}
	}

	BufferedSoundSourceData ReadBufferedSoundSourceData(const io::PublishedPsbFile& archive_file, const resource::SoundArchiveVoiceDefinition& voice, std::size_t channel_index, std::int32_t requested_play_count) {
		if ((voice.Device != 0 && voice.Device != 1) || channel_index >= voice.Channels.size()) {
			throw std::runtime_error("buffered sound data requires a real pool0/1 channel; streaming type2 remains unimplemented");
		}

		const auto& definition = voice.Channels[channel_index];
		const auto& document = archive_file.Document();
		if (&definition.Document.get() != &document) {
			throw std::runtime_error("buffered sound definition must borrow this exact archive document owner");
		}

		const auto bytes = archive_file.Bytes();
		if ((document.Header.Version != 3 && document.Header.Version != 4) || document.Header.Flags != 0 || document.SerializedSize != bytes.size()) {
			throw std::runtime_error("buffered sound data requires its complete actual unfiltered PSB backing");
		}

		const auto& architecture = definition.ArchitectureData.get();
		if (!std::holds_alternative<resource::PsbObject>(architecture.Data)) {
			throw std::runtime_error("buffered sound architecture requires its actual object fields");
		}

		BufferedSoundSourceData source;
		source.Pool = static_cast<SoundVoiceType>(voice.Device);
		source.FormatBytes = SourceBytes(SourceChunk(document, RequireSourceField(architecture, "fmt"), "fmt"), bytes, "fmt");
		source.Description.Format = resource::ReadWaveFormat(source.FormatBytes);
		source.Description.EncodedData = SourceChunk(document, RequireSourceField(architecture, "data"), "data");
		source.EncodedBytes = SourceBytes(source.Description.EncodedData, bytes, "data");
		source.Description.Pan = definition.Pan;
		source.Description.Background = SourceFlag(architecture, "background");
		source.Description.DisableSampleRateConversion = SourceFlag(architecture, "nosrc");
		ReadSourceLoop(architecture, source.Description);

		if (source.Description.Format.Encoding == resource::WaveEncoding::WmaStandard || source.Description.Format.Encoding == resource::WaveEncoding::WmaProfessional) {
			source.DecodedPacketBytes = SourceBytes(SourceChunk(document, RequireSourceField(architecture, "dpds"), "dpds"), bytes, "dpds");
			ReadSourcePackets(source);
		}

		source.Submission = PlanSoundSubmission(source.Description, requested_play_count);

		return source;
	}

} // namespace sgre::audio
