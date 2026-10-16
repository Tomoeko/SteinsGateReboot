#include "sgre/resource/sound.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::resource {

	static std::uint16_t ReadWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
		return static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
	}

	static std::uint32_t ReadDword(std::span<const std::uint8_t> bytes, std::size_t offset) {
		return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
	}

	WaveFormat ReadWaveFormat(std::span<const std::uint8_t> bytes) {
		if (bytes.size() < 16 || bytes.size() > 4114 || bytes.size() == 17) {
			throw std::runtime_error("WAVEFORMATEX has an invalid bounded length");
		}

		WaveFormat format;
		const auto tag = ReadWord(bytes, 0);
		if (tag != 1 && tag != 2 && tag != 0x161 && tag != 0x162) {
			throw std::runtime_error("unsupported recovered WAVE format tag " + std::to_string(tag));
		}

		format.Encoding = static_cast<WaveEncoding>(tag);
		format.Channels = ReadWord(bytes, 2);
		format.SamplesPerSecond = ReadDword(bytes, 4);
		format.AverageBytesPerSecond = ReadDword(bytes, 8);
		format.BlockAlignment = ReadWord(bytes, 12);
		format.BitsPerSample = ReadWord(bytes, 14);
		if (format.Channels == 0 || format.Channels > 8 || format.SamplesPerSecond == 0 || format.SamplesPerSecond > 192000 || format.BlockAlignment == 0) {
			throw std::runtime_error("WAVE format channels/rate/block alignment exceed supported ranges");
		}

		if (bytes.size() > 16) {
			const auto extra_size = ReadWord(bytes, 16);
			if (extra_size != bytes.size() - 18) {
				throw std::runtime_error("WAVEFORMATEX extension length differs from its chunk");
			}

			format.Extra.assign(bytes.begin() + 18, bytes.end());
		} else if (format.Encoding != WaveEncoding::Pcm) {
			throw std::runtime_error("compressed WAVE format requires its cbSize field");
		}

		return format;
	}

	static const PsbValue& RequireField(const PsbValue& value, std::string_view name) {
		const auto* field = value.Find(name);
		if (field == nullptr) {
			throw std::runtime_error("sound metadata is missing " + std::string(name));
		}

		return *field;
	}

	static std::int32_t ReadInteger(const PsbValue& value, std::string_view field) {
		const auto* integer = std::get_if<std::int64_t>(&value.Data);
		if (integer == nullptr || *integer < std::numeric_limits<std::int32_t>::min() || *integer > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("sound integer exceeds its range: " + std::string(field));
		}

		return static_cast<std::int32_t>(*integer);
	}

	static bool ReadOptionalBoolean(const PsbValue& parent, std::string_view name) {
		const auto* value = parent.Find(name);
		if (value == nullptr) {
			return false;
		}

		if (const auto* boolean = std::get_if<bool>(&value->Data)) {
			return *boolean;
		}

		if (const auto* integer = std::get_if<std::int64_t>(&value->Data)) {
			return *integer != 0;
		}

		throw std::runtime_error("sound flag has an unsupported type: " + std::string(name));
	}

	static float ReadNumber(const PsbValue& value, std::string_view name) {
		if (const auto* number = std::get_if<double>(&value.Data)) {
			if (std::isfinite(*number) && std::abs(*number) > std::numeric_limits<float>::max()) {
				throw std::runtime_error("sound number exceeds single-precision range: " + std::string(name));
			}

			return static_cast<float>(*number);
		}

		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return static_cast<float>(*integer);
		}

		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1.0f : 0.0f;
		}

		throw std::runtime_error("sound number has an unsupported type: " + std::string(name));
	}

	static float ReadOptionalNumber(const PsbValue& parent, std::string_view name, float fallback) {
		const auto* value = parent.Find(name);

		return value == nullptr ? fallback : ReadNumber(*value, name);
	}

	static std::string ReadOptionalString(const PsbValue& parent, std::string_view name) {
		const auto* value = parent.Find(name);
		if (value == nullptr) {
			return {};
		}

		const auto* text = std::get_if<std::string>(&value->Data);
		if (text == nullptr) {
			throw std::runtime_error("sound string has an unsupported type: " + std::string(name));
		}

		return *text;
	}

	static PsbChunkRange ReadChunk(const PsbDocument& document, const PsbValue& value) {
		const auto* reference = std::get_if<PsbChunkReference>(&value.Data);
		if (reference == nullptr || reference->Extra || reference->Index >= document.Chunks.size()) {
			throw std::runtime_error("sound metadata contains an invalid chunk reference");
		}

		return document.Chunks[reference->Index];
	}

	static std::span<const std::uint8_t> ChunkBytes(PsbChunkRange range, std::span<const std::uint8_t> bytes) {
		if (range.Offset > bytes.size() || range.Size > bytes.size() - range.Offset) {
			throw std::runtime_error("sound chunk exceeds its PSB storage");
		}

		return bytes.subspan(static_cast<std::size_t>(range.Offset), static_cast<std::size_t>(range.Size));
	}

	static SoundChannel ReadChannel(const PsbDocument& document, const PsbValue& value, std::span<const std::uint8_t> bytes) {
		const auto& data = RequireField(value, "archData");
		SoundChannel channel;
		channel.Format = ReadWaveFormat(ChunkBytes(ReadChunk(document, RequireField(data, "fmt")), bytes));
		channel.EncodedData = ReadChunk(document, RequireField(data, "data"));
		static_cast<void>(ChunkBytes(channel.EncodedData, bytes));
		channel.Background = ReadOptionalBoolean(data, "background");
		channel.DisableSampleRateConversion = ReadOptionalBoolean(data, "nosrc");
		if (const auto* pan_value = value.Find("pan")) {
			const auto* pan = std::get_if<PsbArray>(&pan_value->Data);
			if (pan == nullptr || pan->size() < 2) {
				throw std::runtime_error("sound channel pan requires left and right coefficients");
			}

			// The native archive reader consumes only the first two array elements.
			channel.Pan[0] = ReadNumber((*pan)[0], "pan left");
			channel.Pan[1] = ReadNumber((*pan)[1], "pan right");
		}

		if (const auto* loop_value = data.Find("loop")) {
			const auto* loop = std::get_if<PsbArray>(&loop_value->Data);
			if (loop == nullptr || loop->size() != 2) {
				throw std::runtime_error("sound sample loop must contain begin and length");
			}

			SampleLoop sample_loop;
			const auto begin = ReadInteger((*loop)[0], "loop begin");
			const auto length = ReadInteger((*loop)[1], "loop length");
			if (begin < 0 || length < 0) {
				throw std::runtime_error("sound sample loop cannot be negative");
			}

			sample_loop.Begin = static_cast<std::uint32_t>(begin);
			sample_loop.Length = static_cast<std::uint32_t>(length);
			channel.Loop = sample_loop;
		}

		if (channel.Format.Encoding == WaveEncoding::WmaStandard || channel.Format.Encoding == WaveEncoding::WmaProfessional) {
			const auto table = ChunkBytes(ReadChunk(document, RequireField(data, "dpds")), bytes);
			if (table.empty() || table.size() % 4 != 0 || table.size() > 4 * 1024 * 1024) {
				throw std::runtime_error("WMA decoded-packet table has an invalid bounded length");
			}

			std::uint32_t previous = 0;
			for (std::size_t offset = 0; offset < table.size(); offset += 4) {
				const auto cumulative = ReadDword(table, offset);
				if (cumulative < previous) {
					throw std::runtime_error("WMA decoded-packet table decreases");
				}

				channel.DecodedPacketCumulativeBytes.push_back(cumulative);
				previous = cumulative;
			}

			const auto count = channel.DecodedPacketCumulativeBytes.size();
			if (channel.EncodedData.Size % count != 0 || channel.EncodedData.Size / count != channel.Format.BlockAlignment) {
				throw std::runtime_error("WMA packet table/data/block alignment disagree");
			}
		}

		return channel;
	}

	SoundArchive ReadSoundArchive(const PsbDocument& document, std::span<const std::uint8_t> psb_bytes) {
		const auto* id = std::get_if<std::string>(&RequireField(document.Root, "id").Data);
		const auto* spec = std::get_if<std::string>(&RequireField(document.Root, "spec").Data);
		const auto* version = std::get_if<double>(&RequireField(document.Root, "version").Data);
		if (id == nullptr || *id != "sound_archive" || spec == nullptr || *spec != "win" || version == nullptr || std::abs(*version - 1.02) > 0.00001) {
			throw std::runtime_error("unsupported sound archive identity/spec/version");
		}

		const auto* voices = std::get_if<PsbObject>(&RequireField(document.Root, "voice").Data);
		if (voices == nullptr || voices->size() > 65536) {
			throw std::runtime_error("sound archive entry table exceeds its range");
		}

		SoundArchive archive;
		for (const auto& [name, value] : *voices) {
			SoundEntry entry;
			entry.Name = name;
			entry.DefaultLoopCount = ReadInteger(RequireField(value, "loop"), "loop");
			entry.Device = ReadInteger(RequireField(value, "device"), "device");
			entry.Quality = ReadInteger(RequireField(value, "quality"), "quality");
			entry.Type = ReadInteger(RequireField(value, "type"), "type");
			entry.Volume = ReadOptionalNumber(value, "volume", 1.0f);
			entry.PitchOctaves = ReadOptionalNumber(value, "pitch", 0.0f);
			entry.Priority = ReadOptionalNumber(value, "priority", 0.0f);
			entry.Group = ReadOptionalString(value, "group");
			entry.Uniqueness = ReadOptionalString(value, "uniq");
			const auto* channels = std::get_if<PsbArray>(&RequireField(value, "channelList").Data);
			if (channels == nullptr || channels->empty() || channels->size() > 64) {
				throw std::runtime_error("sound channelList exceeds its supported range");
			}

			for (const auto& channel : *channels) {
				entry.Channels.push_back(ReadChannel(document, channel, psb_bytes));
			}

			archive.Entries.push_back(std::move(entry));
		}

		return archive;
	}

} // namespace sgre::resource
