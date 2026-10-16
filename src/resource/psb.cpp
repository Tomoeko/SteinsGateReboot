#include "sgre/resource/psb.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>
#include <unordered_set>

namespace sgre::resource {

	static constexpr std::uint32_t kPsbHeaderSize = 44;
	static constexpr std::uint32_t kMaximumMetadataBytes = 64 * 1024 * 1024;
	static constexpr std::size_t kMaximumTableElements = 1'000'000;
	static constexpr std::size_t kMaximumAggregateTableElements = 4'000'000;
	static constexpr std::size_t kMaximumValueNodes = 1'000'000;
	static constexpr std::size_t kMaximumStringBytes = 64 * 1024 * 1024;
	static constexpr std::size_t kMaximumNameBytes = 4096;
	static constexpr std::size_t kMaximumValueDepth = 128;

	static std::runtime_error PsbError(std::string_view message, std::uint64_t offset) {
		return std::runtime_error("PSB " + std::string(message) + " at offset " + std::to_string(offset));
	}

	static std::uint64_t ReadLittleEndian(std::span<const std::uint8_t> bytes, std::uint32_t offset, std::uint32_t width, std::uint32_t limit) {
		if (width == 0 || width > 8 || limit > bytes.size() || offset > limit || width > limit - offset) {
			throw PsbError("integer exceeds its section", offset);
		}

		std::uint64_t value = 0;
		for (std::uint32_t index = 0; index < width; ++index) {
			value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8);
		}

		return value;
	}

	static std::uint32_t HeaderChecksum(std::span<const std::uint8_t> bytes, std::uint16_t version) {
		std::uint32_t first = 1;
		std::uint32_t second = 0;
		for (std::size_t index = 8; index < 40; ++index) {
			first = (first + bytes[index]) % 65521;
			second = (second + first) % 65521;
		}

		if (version == 4) {
			for (std::size_t index = 44; index < 56; ++index) {
				first = (first + bytes[index]) % 65521;
				second = (second + first) % 65521;
			}
		}

		return (second << 16) | first;
	}

	static void ValidateUtf8(std::string_view text, std::uint32_t offset) {
		std::size_t index = 0;
		while (index < text.size()) {
			const auto leading = static_cast<std::uint8_t>(text[index++]);
			if (leading < 0x80) {
				continue;
			}

			std::uint32_t codepoint = 0;
			std::size_t continuation_count = 0;
			std::uint32_t minimum = 0;
			if (leading >= 0xc2 && leading <= 0xdf) {
				codepoint = leading & 0x1f;
				continuation_count = 1;
				minimum = 0x80;
			} else if (leading >= 0xe0 && leading <= 0xef) {
				codepoint = leading & 0x0f;
				continuation_count = 2;
				minimum = 0x800;
			} else if (leading >= 0xf0 && leading <= 0xf4) {
				codepoint = leading & 0x07;
				continuation_count = 3;
				minimum = 0x10000;
			} else {
				throw PsbError("invalid UTF-8 leading byte", offset);
			}

			if (continuation_count > text.size() - index) {
				throw PsbError("truncated UTF-8 sequence", offset);
			}

			for (std::size_t continuation = 0; continuation < continuation_count; ++continuation) {
				const auto byte = static_cast<std::uint8_t>(text[index++]);
				if ((byte & 0xc0) != 0x80) {
					throw PsbError("invalid UTF-8 continuation byte", offset);
				}

				codepoint = (codepoint << 6) | (byte & 0x3f);
			}

			if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
				throw PsbError("invalid UTF-8 codepoint", offset);
			}
		}
	}

	PsbHeader ReadPsbHeader(std::span<const std::uint8_t> bytes) {
		if (bytes.size() < kPsbHeaderSize) {
			throw PsbError("truncated version 3 header", 0);
		}

		if (bytes[0] != 'P' || bytes[1] != 'S' || bytes[2] != 'B' || bytes[3] != 0) {
			throw PsbError("invalid signature", 0);
		}

		PsbHeader header;
		header.Version = static_cast<std::uint16_t>(ReadLittleEndian(bytes, 4, 2, kPsbHeaderSize));
		header.Flags = static_cast<std::uint16_t>(ReadLittleEndian(bytes, 6, 2, kPsbHeaderSize));
		if ((header.Version != 3 && header.Version != 4) || header.Flags != 0) {
			throw PsbError("unsupported version or filtered header/body", 4);
		}

		const std::uint32_t expected_header_size = header.Version == 4 ? 56 : kPsbHeaderSize;
		if (bytes.size() < expected_header_size) {
			throw PsbError("truncated extended header", 44);
		}

		header.HeaderSize = static_cast<std::uint32_t>(ReadLittleEndian(bytes, 8, 4, kPsbHeaderSize));
		header.NamesOffset = static_cast<std::uint32_t>(ReadLittleEndian(bytes, 12, 4, kPsbHeaderSize));
		header.StringOffsetsOffset = static_cast<std::uint32_t>(ReadLittleEndian(bytes, 16, 4, kPsbHeaderSize));
		header.StringsOffset = static_cast<std::uint32_t>(ReadLittleEndian(bytes, 20, 4, kPsbHeaderSize));
		header.ChunkOffsetsOffset = static_cast<std::uint32_t>(ReadLittleEndian(bytes, 24, 4, kPsbHeaderSize));
		header.ChunkSizesOffset = static_cast<std::uint32_t>(ReadLittleEndian(bytes, 28, 4, kPsbHeaderSize));
		header.DataOffset = static_cast<std::uint32_t>(ReadLittleEndian(bytes, 32, 4, kPsbHeaderSize));
		header.RootOffset = static_cast<std::uint32_t>(ReadLittleEndian(bytes, 36, 4, kPsbHeaderSize));
		header.Checksum = static_cast<std::uint32_t>(ReadLittleEndian(bytes, 40, 4, kPsbHeaderSize));
		if (header.HeaderSize != expected_header_size || header.DataOffset < expected_header_size || header.DataOffset > kMaximumMetadataBytes) {
			throw PsbError("invalid header size or metadata limit", 8);
		}

		if (header.Checksum != HeaderChecksum(bytes, header.Version)) {
			throw PsbError("header checksum mismatch", 40);
		}

		if (header.Version == 4) {
			header.ExtraChunkOffsetsOffset = static_cast<std::uint32_t>(ReadLittleEndian(bytes, 44, 4, expected_header_size));
			header.ExtraChunkSizesOffset = static_cast<std::uint32_t>(ReadLittleEndian(bytes, 48, 4, expected_header_size));
			header.ExtraDataOffset = static_cast<std::uint32_t>(ReadLittleEndian(bytes, 52, 4, expected_header_size));
		}

		return header;
	}

	class PsbMetadataReader {
	public:
		PsbMetadataReader(std::span<const std::uint8_t> bytes, std::uint64_t available_file_size) : m_Bytes(bytes), m_AvailableSize(available_file_size) {
			m_Document.Header = ReadPsbHeader(bytes);
			const PsbHeader& header = m_Document.Header;
			if (header.DataOffset > bytes.size() || header.DataOffset > available_file_size) {
				throw PsbError("metadata exceeds available input", header.DataOffset);
			}

			m_Bytes = bytes.first(header.DataOffset);
			if (header.NamesOffset < header.HeaderSize || header.NamesOffset > header.RootOffset || header.RootOffset >= header.StringOffsetsOffset || header.StringOffsetsOffset > header.StringsOffset || header.StringsOffset > header.ChunkOffsetsOffset || header.ChunkOffsetsOffset > header.ChunkSizesOffset || header.ChunkSizesOffset > header.DataOffset) {
				throw PsbError("invalid section ordering", 12);
			}
			if (header.Version == 4 && (header.StringsOffset > header.ExtraChunkOffsetsOffset || header.ExtraChunkOffsetsOffset > header.ExtraChunkSizesOffset || header.ExtraChunkSizesOffset > header.ExtraDataOffset || header.ExtraDataOffset > header.ChunkOffsetsOffset)) {
				throw PsbError("invalid extended section ordering", 44);
			}
		}

		PsbDocument Read() {
			ReadNames();
			ReadStrings();
			ReadChunks();
			m_Document.Root = ReadValue(m_Document.Header.RootOffset, 0);

			return std::move(m_Document);
		}

	private:
		std::uint64_t ReadInteger(std::uint32_t& position, std::uint32_t width, std::uint32_t limit) const {
			const std::uint64_t value = ReadLittleEndian(m_Bytes, position, width, limit);
			position += width;

			return value;
		}

		std::vector<std::uint32_t> ReadArray(std::uint32_t& position, std::uint32_t limit) {
			const auto count_tag = ReadInteger(position, 1, limit);
			if (count_tag < 13 || count_tag > 16) {
				throw PsbError("invalid compact-array count tag", position - 1);
			}

			const auto count = static_cast<std::uint32_t>(ReadInteger(position, static_cast<std::uint32_t>(count_tag - 12), limit));
			const auto item_tag = ReadInteger(position, 1, limit);
			if (item_tag < 13 || item_tag > 16) {
				throw PsbError("invalid compact-array element tag", position - 1);
			}

			const auto width = static_cast<std::uint32_t>(item_tag - 12);
			if (count > kMaximumTableElements || count > m_TableElementsRemaining || position > limit || count > (limit - position) / width) {
				throw PsbError("compact-array count exceeds section or work limit", position);
			}

			m_TableElementsRemaining -= count;
			std::vector<std::uint32_t> values;
			values.reserve(count);
			for (std::uint32_t index = 0; index < count; ++index) {
				values.push_back(static_cast<std::uint32_t>(ReadInteger(position, width, limit)));
			}

			return values;
		}

		void ConsumeStringBytes(std::size_t byte_count, std::uint32_t offset) {
			if (byte_count > m_StringBytesRemaining) {
				throw PsbError("aggregate string allocation limit exceeded", offset);
			}

			m_StringBytesRemaining -= byte_count;
		}

		void ReadNames() {
			const PsbHeader& header = m_Document.Header;
			std::uint32_t position = header.NamesOffset;
			const auto bases = ReadArray(position, header.RootOffset);
			const auto parents = ReadArray(position, header.RootOffset);
			const auto leaves = ReadArray(position, header.RootOffset);
			if (bases.empty() || bases.size() != parents.size()) {
				throw PsbError("inconsistent key trie tables", header.NamesOffset);
			}

			m_ValueStart = position;
			std::vector<std::uint32_t> visited(bases.size(), 0);
			m_Document.Names.reserve(leaves.size());
			for (std::size_t name_index = 0; name_index < leaves.size(); ++name_index) {
				const std::uint32_t leaf = leaves[name_index];
				if (leaf >= parents.size() || parents[leaf] >= bases.size() || leaf != bases[parents[leaf]]) {
					throw PsbError("invalid key trie leaf", header.NamesOffset);
				}

				std::string name;
				std::uint32_t node = parents[leaf];
				const auto visit_marker = static_cast<std::uint32_t>(name_index + 1);
				while (node != 0) {
					if (node >= parents.size() || visited[node] == visit_marker || name.size() >= kMaximumNameBytes) {
						throw PsbError("cyclic or overlong key trie path", header.NamesOffset);
					}

					visited[node] = visit_marker;
					const std::uint32_t parent = parents[node];
					if (parent >= bases.size() || node <= bases[parent] || node - bases[parent] > 255) {
						throw PsbError("invalid key trie character", header.NamesOffset);
					}

					ConsumeStringBytes(1, header.NamesOffset);
					name.push_back(std::bit_cast<char>(static_cast<std::uint8_t>(node - bases[parent])));
					node = parent;
				}

				std::reverse(name.begin(), name.end());
				ValidateUtf8(name, header.NamesOffset);
				m_Document.Names.push_back(std::move(name));
			}
		}

		void ReadStrings() {
			const PsbHeader& header = m_Document.Header;
			const std::uint32_t string_limit = header.Version == 4 ? header.ExtraChunkOffsetsOffset : header.ChunkOffsetsOffset;
			std::uint32_t position = header.StringOffsetsOffset;
			const auto offsets = ReadArray(position, header.StringsOffset);
			m_Document.Strings.reserve(offsets.size());
			for (const std::uint32_t relative_offset : offsets) {
				if (relative_offset >= string_limit - header.StringsOffset) {
					throw PsbError("string offset exceeds string entity", relative_offset);
				}

				const std::uint32_t start = header.StringsOffset + relative_offset;
				std::uint32_t end = start;
				while (end < string_limit && m_Bytes[end] != 0) {
					ConsumeStringBytes(1, start);
					++end;
				}

				if (end == string_limit) {
					throw PsbError("unterminated string", start);
				}

				std::string value;
				value.reserve(end - start);
				for (std::uint32_t index = start; index < end; ++index) {
					value.push_back(std::bit_cast<char>(m_Bytes[index]));
				}

				ValidateUtf8(value, start);
				m_Document.Strings.push_back(std::move(value));
			}
		}

		std::vector<PsbChunkRange> ReadChunkTable(std::uint32_t offsets_offset, std::uint32_t sizes_offset, std::uint32_t data_offset, std::uint64_t available_size) {
			std::uint32_t position = offsets_offset;
			const auto offsets = ReadArray(position, sizes_offset);
			position = sizes_offset;
			const auto sizes = ReadArray(position, data_offset);
			if (offsets.size() != sizes.size()) {
				throw PsbError("chunk table counts disagree", offsets_offset);
			}

			std::vector<PsbChunkRange> ranges;
			ranges.reserve(offsets.size());
			for (std::size_t index = 0; index < offsets.size(); ++index) {
				PsbChunkRange range;
				range.Offset = static_cast<std::uint64_t>(data_offset) + offsets[index];
				range.Size = sizes[index];
				if (range.Offset > available_size || range.Size > available_size - range.Offset) {
					throw PsbError("chunk exceeds available section size", range.Offset);
				}

				m_Document.SerializedSize = std::max(m_Document.SerializedSize, range.Offset + range.Size);
				ranges.push_back(range);
			}

			return ranges;
		}

		void ReadChunks() {
			const PsbHeader& header = m_Document.Header;
			m_Document.SerializedSize = header.DataOffset;
			m_Document.Chunks = ReadChunkTable(header.ChunkOffsetsOffset, header.ChunkSizesOffset, header.DataOffset, m_AvailableSize);
			if (header.Version == 4) {
				// Version 4 stores its extra resource entity before the regular chunk tables.
				m_Document.ExtraChunks = ReadChunkTable(header.ExtraChunkOffsetsOffset, header.ExtraChunkSizesOffset, header.ExtraDataOffset, header.ChunkOffsetsOffset);
			}
		}

		std::uint32_t ValueOffset(std::uint32_t base, std::uint32_t relative_offset) const {
			const std::uint32_t limit = m_Document.Header.StringOffsetsOffset;
			if (base >= limit || relative_offset >= limit - base) {
				throw PsbError("value offset exceeds value tree", base);
			}

			return base + relative_offset;
		}

		void ConsumeValueSlots(std::size_t count, std::uint32_t offset) {
			if (count > m_ValueSlotsRemaining) {
				throw PsbError("aggregate value allocation limit exceeded", offset);
			}

			m_ValueSlotsRemaining -= count;
		}

		PsbValue ReadArrayValue(std::uint32_t position, std::size_t depth) {
			const auto offsets = ReadArray(position, m_Document.Header.StringOffsetsOffset);
			if (offsets.size() > m_ValueNodesRemaining) {
				throw PsbError("array exceeds value-node work limit", position);
			}

			ConsumeValueSlots(offsets.size(), position);
			PsbArray array;
			array.reserve(offsets.size());
			for (const std::uint32_t offset : offsets) {
				array.push_back(ReadValue(ValueOffset(position, offset), depth + 1));
			}

			PsbValue value;
			value.SerializedTag = 32;
			value.Data = std::move(array);

			return value;
		}

		PsbValue ReadObjectValue(std::uint32_t position, std::size_t depth) {
			const auto name_indices = ReadArray(position, m_Document.Header.StringOffsetsOffset);
			const auto offsets = ReadArray(position, m_Document.Header.StringOffsetsOffset);
			if (name_indices.size() != offsets.size() || offsets.size() > m_ValueNodesRemaining) {
				throw PsbError("object tables disagree or exceed work limit", position);
			}

			ConsumeValueSlots(name_indices.size(), position);
			PsbObject object;
			object.reserve(name_indices.size());
			std::unordered_set<std::string_view> used_names;
			for (std::size_t index = 0; index < name_indices.size(); ++index) {
				if (name_indices[index] >= m_Document.Names.size()) {
					throw PsbError("object key index exceeds key table", position);
				}

				const std::string& name = m_Document.Names[name_indices[index]];
				if (!used_names.insert(name).second) {
					throw PsbError("duplicate object key", position);
				}

				ConsumeStringBytes(name.size(), position);
				object.emplace_back(name, ReadValue(ValueOffset(position, offsets[index]), depth + 1));
			}

			PsbValue value;
			value.SerializedTag = 33;
			value.Data = std::move(object);

			return value;
		}

		PsbValue ReadValue(std::uint32_t position, std::size_t depth) {
			if (depth >= kMaximumValueDepth || m_ValueNodesRemaining == 0 || position < m_ValueStart || position >= m_Document.Header.StringOffsetsOffset) {
				throw PsbError("value exceeds tree, depth, or work limit", position);
			}

			for (std::size_t ancestor = 0; ancestor < depth; ++ancestor) {
				if (m_ActiveOffsets[ancestor] == position) {
					throw PsbError("cyclic value reference", position);
				}
			}

			m_ActiveOffsets[depth] = position;
			--m_ValueNodesRemaining;
			const std::uint32_t limit = m_Document.Header.StringOffsetsOffset;
			const auto value_offset = position;
			const auto tag = ReadInteger(position, 1, limit);
			PsbValue value;
			value.SerializedTag = static_cast<std::uint8_t>(tag);
			value.SerializedOffset = value_offset;
			if (tag == 1) {
				return value;
			}

			if (tag == 2 || tag == 3) {
				value.Data = tag == 2;
			} else if (tag >= 4 && tag <= 12) {
				std::int64_t integer = 0;
				if (tag != 4) {
					const auto width = static_cast<std::uint32_t>(tag - 4);
					const std::uint64_t raw = ReadInteger(position, width, limit);
					const std::uint64_t sign_bit = std::uint64_t{1} << (width * 8 - 1);
					if ((raw & sign_bit) == 0) {
						integer = static_cast<std::int64_t>(raw);
					} else if (width == 8) {
						integer = -1 - static_cast<std::int64_t>(~raw);
					} else {
						integer = -static_cast<std::int64_t>((std::uint64_t{1} << (width * 8)) - raw);
					}
				}

				value.Data = integer;
			} else if (tag >= 13 && tag <= 16) {
				value.Data = static_cast<std::int64_t>(ReadInteger(position, static_cast<std::uint32_t>(tag - 12), limit));
			} else if (tag >= 21 && tag <= 24) {
				const std::uint64_t index = ReadInteger(position, static_cast<std::uint32_t>(tag - 20), limit);
				if (index >= m_Document.Strings.size()) {
					throw PsbError("string reference exceeds string table", position);
				}

				const std::string& text = m_Document.Strings[static_cast<std::size_t>(index)];
				ConsumeStringBytes(text.size(), position);
				value.Data = text;
			} else if (tag >= 25 && tag <= 28) {
				PsbChunkReference reference;
				reference.Index = static_cast<std::uint32_t>(ReadInteger(position, static_cast<std::uint32_t>(tag - 24), limit));
				if (reference.Index >= m_Document.Chunks.size()) {
					throw PsbError("chunk reference exceeds chunk table", position);
				}

				value.Data = reference;
			} else if (tag >= 34 && tag <= 37 && m_Document.Header.Version == 4) {
				PsbChunkReference reference;
				reference.Index = static_cast<std::uint32_t>(ReadInteger(position, static_cast<std::uint32_t>(tag - 33), limit));
				reference.Extra = true;
				if (reference.Index >= m_Document.ExtraChunks.size()) {
					throw PsbError("extra chunk reference exceeds extra chunk table", position);
				}

				value.Data = reference;
			} else if (tag == 29) {
				value.Data = 0.0;
			} else if (tag == 30) {
				value.Data = static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(ReadInteger(position, 4, limit))));
			} else if (tag == 31) {
				value.Data = std::bit_cast<double>(ReadInteger(position, 8, limit));
			} else if (tag == 32) {
				value.Data = ReadArrayValue(position, depth).Data;
			} else if (tag == 33) {
				value.Data = ReadObjectValue(position, depth).Data;
			} else {
				throw PsbError("unsupported value tag " + std::to_string(tag), position - 1);
			}

			return value;
		}

		std::span<const std::uint8_t> m_Bytes;
		std::uint64_t m_AvailableSize = 0;
		PsbDocument m_Document;
		std::uint32_t m_ValueStart = 0;
		std::size_t m_TableElementsRemaining = kMaximumAggregateTableElements;
		std::size_t m_ValueNodesRemaining = kMaximumValueNodes;
		std::size_t m_ValueSlotsRemaining = kMaximumValueNodes;
		std::size_t m_StringBytesRemaining = kMaximumStringBytes;
		std::array<std::uint32_t, kMaximumValueDepth> m_ActiveOffsets{};
	};

	const PsbValue* PsbValue::Find(std::string_view name) const {
		const auto* object = std::get_if<PsbObject>(&Data);
		if (object == nullptr) {
			return nullptr;
		}

		for (const auto& entry : *object) {
			if (entry.first == name) {
				return &entry.second;
			}
		}

		return nullptr;
	}

	PsbDocument ReadPsbMetadata(std::span<const std::uint8_t> metadata, std::uint64_t available_file_size) {
		return PsbMetadataReader(metadata, available_file_size).Read();
	}

} // namespace sgre::resource
