#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace sgre::resource {

	struct PsbHeader {
		std::uint16_t Version = 0;
		std::uint16_t Flags = 0;
		std::uint32_t HeaderSize = 0;
		std::uint32_t NamesOffset = 0;
		std::uint32_t StringOffsetsOffset = 0;
		std::uint32_t StringsOffset = 0;
		std::uint32_t ChunkOffsetsOffset = 0;
		std::uint32_t ChunkSizesOffset = 0;
		std::uint32_t DataOffset = 0;
		std::uint32_t RootOffset = 0;
		std::uint32_t Checksum = 0;
		std::uint32_t ExtraChunkOffsetsOffset = 0;
		std::uint32_t ExtraChunkSizesOffset = 0;
		std::uint32_t ExtraDataOffset = 0;
	};

	struct PsbChunkRange {
		std::uint64_t Offset = 0;
		std::uint64_t Size = 0;
	};

	struct PsbChunkReference {
		std::uint32_t Index = 0;
		bool Extra = false;
	};

	struct PsbValue;
	using PsbArray = std::vector<PsbValue>;
	using PsbObject = std::vector<std::pair<std::string, PsbValue>>;

	struct PsbValue {
		// Preserve atom identity when native script conversion distinguishes normalized values.
		std::uint8_t SerializedTag = 0;
		// Texture sharing compares document/value identity; equal contents are not interchangeable.
		std::optional<std::uint32_t> SerializedOffset;
		std::variant<std::monostate, bool, std::int64_t, double, std::string, PsbChunkReference, PsbArray, PsbObject> Data;

		[[nodiscard]] const PsbValue* Find(std::string_view name) const;
	};

	struct PsbDocument {
		PsbHeader Header;
		std::vector<std::string> Names;
		std::vector<std::string> Strings;
		std::vector<PsbChunkRange> Chunks;
		std::vector<PsbChunkRange> ExtraChunks;
		PsbValue Root;
		std::uint64_t SerializedSize = 0;
	};

	[[nodiscard]] PsbHeader ReadPsbHeader(std::span<const std::uint8_t> bytes);

	// Only bytes through Header.DataOffset are required. Chunk ranges refer to the PSB start.
	[[nodiscard]] PsbDocument ReadPsbMetadata(std::span<const std::uint8_t> metadata, std::uint64_t available_file_size);

} // namespace sgre::resource
