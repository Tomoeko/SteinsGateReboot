#include "sgre/resource/zstd_decoder.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::resource {

	// Authored from the format descriptions in RFC 8878, sections 3 and 4.
	static constexpr std::size_t kMaximumBlockSize = 128 * 1024;
	static constexpr std::uint32_t kFrameMagic = 0xfd2fb528;
	static constexpr std::uint64_t kHashPrime1 = 0x9e3779b185ebca87;
	static constexpr std::uint64_t kHashPrime2 = 0xc2b2ae3d27d4eb4f;
	static constexpr std::uint64_t kHashPrime3 = 0x165667b19e3779f9;
	static constexpr std::uint64_t kHashPrime4 = 0x85ebca77c2b2ae63;
	static constexpr std::uint64_t kHashPrime5 = 0x27d4eb2f165667c5;

	[[noreturn]] static void Fail(const std::string& message) {
		throw std::runtime_error("Zstandard: " + message);
	}

	static std::uint64_t ReadLittleEndian(std::span<const std::uint8_t> bytes) {
		if (bytes.size() > 8) {
			Fail("integer field exceeds eight bytes");
		}

		std::uint64_t value = 0;
		for (std::size_t index = 0; index < bytes.size(); ++index) {
			value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8);
		}

		return value;
	}

	class ByteReader {
	public:
		explicit ByteReader(std::span<const std::uint8_t> bytes) : m_Bytes(bytes) {
		}

		std::span<const std::uint8_t> Take(std::size_t count) {
			if (count > Remaining()) {
				Fail("truncated byte field at offset " + std::to_string(m_Position));
			}

			const auto result = m_Bytes.subspan(m_Position, count);
			m_Position += count;

			return result;
		}

		std::uint64_t Integer(std::size_t count) {
			return ReadLittleEndian(Take(count));
		}

		std::size_t Remaining() const {
			return m_Bytes.size() - m_Position;
		}

	private:
		std::span<const std::uint8_t> m_Bytes;
		std::size_t m_Position = 0;
	};

	class ForwardBits {
	public:
		explicit ForwardBits(std::span<const std::uint8_t> bytes) : m_Bytes(bytes) {
			if (bytes.size() > std::numeric_limits<std::size_t>::max() / 8) {
				Fail("FSE probability description exceeds addressable bit extent");
			}
		}

		std::uint32_t Read(unsigned count) {
			const auto result = Peek(count);
			m_Position += count;

			return result;
		}

		std::uint32_t Peek(unsigned count) const {
			if (count > 31 || m_Position > m_Bytes.size() * 8 || count > m_Bytes.size() * 8 - m_Position) {
				Fail("truncated FSE probability description");
			}

			if (count == 0) {
				return 0;
			}
			const auto shift = static_cast<unsigned>(m_Position % 8);
			const auto byte_count = static_cast<std::size_t>((shift + count + 7) / 8);
			const auto word = ReadLittleEndian(m_Bytes.subspan(m_Position / 8, byte_count));

			return static_cast<std::uint32_t>(word >> shift) & ((std::uint32_t{1} << count) - 1);
		}

		std::size_t BytesConsumed() const {
			return (m_Position + 7) / 8;
		}

	private:
		std::span<const std::uint8_t> m_Bytes;
		std::size_t m_Position = 0;
	};

	class BackwardBits {
	public:
		explicit BackwardBits(std::span<const std::uint8_t> bytes) : m_Bytes(bytes) {
			if (bytes.empty() || bytes.back() == 0 || bytes.size() > std::numeric_limits<std::size_t>::max() / 8) {
				Fail("missing entropy stream end marker");
			}

			m_Remaining = (bytes.size() - 1) * 8 + static_cast<std::size_t>(std::bit_width(static_cast<unsigned>(bytes.back())) - 1);
		}

		std::uint32_t Read(unsigned count) {
			if (count > m_Remaining) {
				Fail("entropy stream exhausted");
			}

			const auto result = Peek(count);
			m_Remaining -= count;

			return result;
		}

		std::uint32_t Peek(unsigned count) const {
			if (count > 31) {
				Fail("entropy field exceeds supported width");
			}

			const auto available = static_cast<unsigned>(std::min<std::size_t>(count, m_Remaining));
			const auto first = m_Remaining - available;
			if (available == 0) {
				return 0;
			}
			const auto shift = static_cast<unsigned>(first % 8);
			const auto byte_count = static_cast<std::size_t>((shift + available + 7) / 8);
			// Load only the containing bytes. Preserve little-endian bit order without
			// unaligned host loads or a per-bit loop in every entropy symbol.
			const auto word = ReadLittleEndian(m_Bytes.subspan(first / 8, byte_count));
			const auto result = static_cast<std::uint32_t>(word >> shift) & ((std::uint32_t{1} << available) - 1);

			// Huffman table lookup can look beyond the beginning, with zero low-bit padding.
			return result << (count - available);
		}

		std::size_t Remaining() const {
			return m_Remaining;
		}

	private:
		std::span<const std::uint8_t> m_Bytes;
		std::size_t m_Remaining = 0;
	};

	struct FseEntry {
		std::uint16_t Symbol = 0;
		std::uint16_t Baseline = 0;
		unsigned Bits = 0;
	};

	struct FseTable {
		unsigned Accuracy = 0;
		std::vector<FseEntry> Entries;
	};

	static FseTable BuildFseTable(std::span<const std::int16_t> probabilities, unsigned accuracy) {
		if (accuracy < 5 || accuracy > 9 || probabilities.empty() || probabilities.size() > 256) {
			Fail("invalid FSE table dimensions");
		}

		const std::size_t table_size = std::size_t{1} << accuracy;
		std::size_t total = 0;
		std::size_t present_symbols = 0;
		for (const auto probability : probabilities) {
			if (probability < -1) {
				Fail("invalid normalized FSE probability");
			}

			total += static_cast<std::size_t>(probability == -1 ? 1 : probability);
			present_symbols += probability != 0;
		}

		if (total != table_size || present_symbols < 2) {
			Fail("FSE probabilities do not describe a complete distribution");
		}

		FseTable table;
		table.Accuracy = accuracy;
		table.Entries.resize(table_size);
		std::array<unsigned, 256> next_state{};
		std::size_t regular_limit = table_size;
		for (std::size_t symbol = 0; symbol < probabilities.size(); ++symbol) {
			if (probabilities[symbol] == -1) {
				--regular_limit;
				table.Entries[regular_limit].Symbol = static_cast<std::uint16_t>(symbol);
				next_state[symbol] = 1;
			} else {
				next_state[symbol] = static_cast<unsigned>(probabilities[symbol]);
			}
		}

		const auto step = (table_size >> 1) + (table_size >> 3) + 3;
		std::size_t position = 0;
		for (std::size_t symbol = 0; symbol < probabilities.size(); ++symbol) {
			for (std::int16_t occurrence = 0; occurrence < probabilities[symbol]; ++occurrence) {
				table.Entries[position].Symbol = static_cast<std::uint16_t>(symbol);
				do {
					position = (position + step) & (table_size - 1);
				} while (position >= regular_limit);
			}
		}

		if (position != 0) {
			Fail("FSE symbol spreading did not close");
		}

		for (auto& entry : table.Entries) {
			const auto state = next_state[entry.Symbol]++;
			entry.Bits = accuracy - static_cast<unsigned>(std::bit_width(state) - 1);
			entry.Baseline = static_cast<std::uint16_t>((state << entry.Bits) - table_size);
		}

		return table;
	}

	struct FseDescription {
		FseTable Table;
		std::size_t Bytes = 0;
	};

	static FseDescription ReadFseDescription(std::span<const std::uint8_t> bytes, unsigned maximum_accuracy, std::size_t maximum_symbols) {
		ForwardBits bits(bytes);
		const auto accuracy = bits.Read(4) + 5;
		if (accuracy > maximum_accuracy) {
			Fail("FSE accuracy exceeds the symbol alphabet limit");
		}

		std::vector<std::int16_t> probabilities;
		std::uint32_t remaining = (std::uint32_t{1} << accuracy) + 1;
		while (remaining > 1) {
			if (probabilities.size() >= maximum_symbols) {
				Fail("FSE description exceeds its alphabet");
			}

			const unsigned width = static_cast<unsigned>(std::bit_width(remaining));
			const std::uint32_t threshold = std::uint32_t{1} << (width - 1);
			const std::uint32_t small_limit = (threshold << 1) - 1 - remaining;
			std::uint32_t value = bits.Peek(width - 1);
			if (value < small_limit) {
				bits.Read(width - 1);
			} else {
				value = bits.Read(width);
				if (value >= threshold) {
					value -= small_limit;
				}
			}

			const auto probability = static_cast<std::int16_t>(static_cast<int>(value) - 1);
			const auto allocation = static_cast<std::uint32_t>(probability == -1 ? 1 : probability);
			if (allocation >= remaining) {
				Fail("FSE probability total exceeds table size");
			}

			probabilities.push_back(probability);
			remaining -= allocation;
			if (probability == 0) {
				std::uint32_t repeat = 0;
				do {
					repeat = bits.Read(2);
					if (repeat > maximum_symbols - probabilities.size()) {
						Fail("FSE zero run exceeds its alphabet");
					}

					probabilities.insert(probabilities.end(), repeat, 0);
				} while (repeat == 3);
			}
		}

		FseDescription result;
		result.Table = BuildFseTable(probabilities, accuracy);
		result.Bytes = bits.BytesConsumed();

		return result;
	}

	static const FseEntry& FseSymbol(const FseTable& table, std::uint32_t state) {
		if (state >= table.Entries.size()) {
			Fail("FSE state is outside its table");
		}

		return table.Entries[state];
	}

	static void AdvanceFse(const FseTable& table, std::uint32_t& state, BackwardBits& bits) {
		const auto entry = FseSymbol(table, state);
		state = entry.Baseline + bits.Read(entry.Bits);
	}

	static std::vector<std::uint8_t> DecodeWeights(std::span<const std::uint8_t> bytes) {
		const auto description = ReadFseDescription(bytes, 6, 13);
		BackwardBits bits(bytes.subspan(description.Bytes));
		std::array<std::uint32_t, 2> states{};
		states[0] = bits.Read(description.Table.Accuracy);
		states[1] = bits.Read(description.Table.Accuracy);
		std::vector<std::uint8_t> weights;
		std::size_t turn = 0;
		while (weights.size() < 255) {
			const auto entry = FseSymbol(description.Table, states[turn]);
			weights.push_back(static_cast<std::uint8_t>(entry.Symbol));
			if (entry.Bits > bits.Remaining()) {
				weights.push_back(static_cast<std::uint8_t>(FseSymbol(description.Table, states[1 - turn]).Symbol));
				if (weights.size() > 255) {
					Fail("Huffman weight list exceeds 255 entries");
				}

				return weights;
			}

			AdvanceFse(description.Table, states[turn], bits);
			turn = 1 - turn;
		}

		Fail("Huffman weight stream did not terminate");
	}

	struct HuffmanEntry {
		std::uint8_t Symbol = 0;
		unsigned Bits = 0;
	};

	struct HuffmanTable {
		unsigned Accuracy = 0;
		std::vector<HuffmanEntry> Entries;
	};

	static HuffmanTable BuildHuffmanTable(std::vector<std::uint8_t> weights) {
		std::uint32_t total = 0;
		for (const auto weight : weights) {
			if (weight > 11) {
				Fail("Huffman weight exceeds maximum code depth");
			}

			if (weight != 0) {
				total += std::uint32_t{1} << (weight - 1);
			}
		}

		const unsigned accuracy = static_cast<unsigned>(std::bit_width(total));
		if (accuracy == 0 || accuracy > 11 || weights.size() >= 256) {
			Fail("invalid Huffman tree dimensions");
		}

		const std::uint32_t missing = (std::uint32_t{1} << accuracy) - total;
		if (!std::has_single_bit(missing)) {
			Fail("Huffman final weight is not a power of two");
		}

		weights.push_back(static_cast<std::uint8_t>(std::bit_width(missing)));
		const auto least_weights = std::count(weights.begin(), weights.end(), std::uint8_t{1});
		if (least_weights < 2 || least_weights % 2 != 0) {
			Fail("Huffman tree has an invalid smallest-weight rank");
		}

		HuffmanTable table;
		table.Accuracy = accuracy;
		table.Entries.resize(std::size_t{1} << accuracy);
		std::size_t position = 0;
		for (unsigned weight = 1; weight <= accuracy; ++weight) {
			for (std::size_t symbol = 0; symbol < weights.size(); ++symbol) {
				if (weights[symbol] != weight) {
					continue;
				}

				const auto count = std::size_t{1} << (weight - 1);
				if (count > table.Entries.size() - position) {
					Fail("Huffman tree exceeds its decode table");
				}

				HuffmanEntry entry;
				entry.Symbol = static_cast<std::uint8_t>(symbol);
				entry.Bits = accuracy + 1 - weight;
				std::fill_n(table.Entries.begin() + static_cast<std::ptrdiff_t>(position), count, entry);
				position += count;
			}
		}

		if (position != table.Entries.size()) {
			Fail("Huffman tree leaves undefined codes");
		}

		return table;
	}

	static void ReadHuffmanTable(ByteReader& reader, HuffmanTable& table) {
		const auto header = static_cast<std::uint8_t>(reader.Integer(1));
		std::vector<std::uint8_t> weights;
		if (header < 128) {
			weights = DecodeWeights(reader.Take(header));
		} else {
			const std::size_t count = header - 127;
			const auto encoded = reader.Take((count + 1) / 2);
			for (std::size_t index = 0; index < count; ++index) {
				const auto shift = index % 2 == 0 ? 4 : 0;
				weights.push_back(static_cast<std::uint8_t>((encoded[index / 2] >> shift) & 15));
			}
		}

		table = BuildHuffmanTable(std::move(weights));
	}

	static void DecodeHuffmanStream(std::span<const std::uint8_t> bytes, const HuffmanTable& table, std::size_t count, std::vector<std::uint8_t>& output) {
		BackwardBits bits(bytes);
		for (std::size_t index = 0; index < count; ++index) {
			const auto entry = table.Entries[bits.Peek(table.Accuracy)];
			bits.Read(entry.Bits);
			output.push_back(entry.Symbol);
		}

		if (bits.Remaining() != 0) {
			Fail("Huffman literal stream has unused bits");
		}
	}

	struct FrameState {
		HuffmanTable Huffman;
		FseTable LiteralLengths;
		FseTable Offsets;
		FseTable MatchLengths;
		std::array<std::size_t, 3> RecentOffsets{1, 4, 8};
		std::size_t Start = 0;
		std::size_t Window = 0;
		std::size_t BlockLimit = 0;
	};

	static std::vector<std::uint8_t> ReadLiterals(ByteReader& reader, FrameState& state) {
		const auto first = static_cast<unsigned>(reader.Integer(1));
		const auto type = first & 3;
		const auto format = (first >> 2) & 3;
		std::size_t regenerated_size = 0;
		std::size_t compressed_size = 0;
		if (type < 2) {
			if (format == 0 || format == 2) {
				regenerated_size = first >> 3;
			} else if (format == 1) {
				regenerated_size = (first >> 4) | (reader.Integer(1) << 4);
			} else {
				regenerated_size = (first >> 4) | (reader.Integer(2) << 4);
			}
		} else {
			const unsigned width = format < 2 ? 10 : (format == 2 ? 14 : 18);
			const std::size_t following_bytes = format < 2 ? 2 : (format == 2 ? 3 : 4);
			const auto header = first | (reader.Integer(following_bytes) << 8);
			const auto mask = (std::uint64_t{1} << width) - 1;
			regenerated_size = (header >> 4) & mask;
			compressed_size = (header >> (4 + width)) & mask;
		}

		if (regenerated_size > state.BlockLimit) {
			Fail("literals exceed frame block limit");
		}

		std::vector<std::uint8_t> literals;
		literals.reserve(regenerated_size);
		if (type == 0) {
			const auto bytes = reader.Take(regenerated_size);
			literals.assign(bytes.begin(), bytes.end());
			return literals;
		}

		if (type == 1) {
			literals.assign(regenerated_size, static_cast<std::uint8_t>(reader.Integer(1)));
			return literals;
		}

		ByteReader streams(reader.Take(compressed_size));
		if (type == 2) {
			ReadHuffmanTable(streams, state.Huffman);
		} else if (state.Huffman.Entries.empty()) {
			Fail("treeless literals have no previous Huffman table");
		}

		if (format == 0) {
			DecodeHuffmanStream(streams.Take(streams.Remaining()), state.Huffman, regenerated_size, literals);
			return literals;
		}

		std::array<std::size_t, 4> lengths{};
		for (std::size_t index = 0; index < 3; ++index) {
			lengths[index] = streams.Integer(2);
		}

		const auto first_three = lengths[0] + lengths[1] + lengths[2];
		const auto segment_size = (regenerated_size + 3) / 4;
		if (first_three > streams.Remaining() || segment_size * 3 > regenerated_size) {
			Fail("invalid Huffman four-stream jump table");
		}

		lengths[3] = streams.Remaining() - first_three;
		for (std::size_t index = 0; index < 4; ++index) {
			const auto count = index < 3 ? segment_size : regenerated_size - segment_size * 3;
			DecodeHuffmanStream(streams.Take(lengths[index]), state.Huffman, count, literals);
		}

		return literals;
	}

	// The distributions and length ranges are format constants from RFC 8878 section 3.1.1.3.2.
	static constexpr std::array<std::int16_t, 36> kLiteralDistribution{4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};
	static constexpr std::array<std::int16_t, 53> kMatchDistribution{1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};
	static constexpr std::array<std::int16_t, 29> kOffsetDistribution{1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};
	static constexpr std::array<std::uint32_t, 36> kLiteralBaselines{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 28, 32, 40, 48, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
	static constexpr std::array<unsigned, 36> kLiteralBits{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
	static constexpr std::array<std::uint32_t, 53> kMatchBaselines{3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 131, 259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539};
	static constexpr std::array<unsigned, 53> kMatchBits{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

	static void ReadSequenceTable(ByteReader& reader, unsigned mode, FseTable& table, std::span<const std::int16_t> predefined, unsigned predefined_accuracy, unsigned maximum_accuracy, std::size_t maximum_symbol) {
		if (mode == 0) {
			table = BuildFseTable(predefined, predefined_accuracy);
		} else if (mode == 1) {
			FseEntry entry;
			entry.Symbol = static_cast<std::uint16_t>(reader.Integer(1));
			if (entry.Symbol > maximum_symbol) {
				Fail("sequence RLE symbol exceeds its alphabet");
			}

			table.Accuracy = 0;
			table.Entries.assign(1, entry);
		} else if (mode == 2) {
			const auto remaining = reader.Remaining();
			const auto bytes = reader.Take(remaining);
			const auto description = ReadFseDescription(bytes, maximum_accuracy, maximum_symbol + 1);
			table = description.Table;
			reader = ByteReader(bytes.subspan(description.Bytes));
		} else if (table.Entries.empty()) {
			Fail("sequence repeat mode has no previous table");
		}
	}

	static std::size_t ResolveOffset(std::uint64_t value, std::size_t literal_length, FrameState& state) {
		std::size_t offset = 0;
		if (value > 3) {
			offset = static_cast<std::size_t>(value - 3);
			state.RecentOffsets[2] = state.RecentOffsets[1];
			state.RecentOffsets[1] = state.RecentOffsets[0];
			state.RecentOffsets[0] = offset;
		} else {
			const auto index = static_cast<std::size_t>(value - 1 + (literal_length == 0 ? 1 : 0));
			if (index == 3) {
				if (state.RecentOffsets[0] <= 1) {
					Fail("repeat offset resolves to zero");
				}

				offset = state.RecentOffsets[0] - 1;
				state.RecentOffsets[2] = state.RecentOffsets[1];
				state.RecentOffsets[1] = state.RecentOffsets[0];
			} else {
				offset = state.RecentOffsets[index];
				for (std::size_t recent = index; recent > 0; --recent) {
					state.RecentOffsets[recent] = state.RecentOffsets[recent - 1];
				}
			}

			state.RecentOffsets[0] = offset;
		}

		return offset;
	}

	static void EnsureOutput(std::size_t count, const std::vector<std::uint8_t>& output, std::size_t output_limit) {
		if (output.size() > output_limit || count > output_limit - output.size()) {
			Fail("decoded output exceeds declared size or allocation limit");
		}
	}

	static void DecodeCompressedBlock(std::span<const std::uint8_t> bytes, FrameState& state, std::vector<std::uint8_t>& output, std::size_t output_limit) {
		ByteReader reader(bytes);
		const auto literals = ReadLiterals(reader, state);
		const auto first = static_cast<unsigned>(reader.Integer(1));
		std::size_t sequence_count = first;
		if (first == 255) {
			sequence_count = reader.Integer(2) + 0x7f00;
		} else if (first >= 128) {
			sequence_count = ((first - 128) << 8) + reader.Integer(1);
		}

		const auto block_start = output.size();
		if (sequence_count == 0) {
			if (reader.Remaining() != 0) {
				Fail("zero-sequence block contains trailing data");
			}

			EnsureOutput(literals.size(), output, output_limit);
			output.insert(output.end(), literals.begin(), literals.end());
			return;
		}

		if (sequence_count > state.BlockLimit / 3) {
			Fail("sequence count exceeds maximum block output");
		}

		const auto modes = static_cast<unsigned>(reader.Integer(1));
		if ((modes & 3) != 0) {
			Fail("reserved sequence mode bits are set");
		}

		ReadSequenceTable(reader, modes >> 6, state.LiteralLengths, kLiteralDistribution, 6, 9, 35);
		ReadSequenceTable(reader, (modes >> 4) & 3, state.Offsets, kOffsetDistribution, 5, 8, 31);
		ReadSequenceTable(reader, (modes >> 2) & 3, state.MatchLengths, kMatchDistribution, 6, 9, 52);
		BackwardBits bits(reader.Take(reader.Remaining()));
		std::uint32_t literal_state = bits.Read(state.LiteralLengths.Accuracy);
		std::uint32_t offset_state = bits.Read(state.Offsets.Accuracy);
		std::uint32_t match_state = bits.Read(state.MatchLengths.Accuracy);
		std::size_t literal_position = 0;
		for (std::size_t index = 0; index < sequence_count; ++index) {
			const auto literal_code = FseSymbol(state.LiteralLengths, literal_state).Symbol;
			const auto offset_code = FseSymbol(state.Offsets, offset_state).Symbol;
			const auto match_code = FseSymbol(state.MatchLengths, match_state).Symbol;
			const auto offset_value = (std::uint64_t{1} << offset_code) + bits.Read(offset_code);
			const auto match_length = kMatchBaselines[match_code] + bits.Read(kMatchBits[match_code]);
			const auto literal_length = kLiteralBaselines[literal_code] + bits.Read(kLiteralBits[literal_code]);
			if (literal_length > literals.size() - literal_position) {
				Fail("sequence copies more literals than are available");
			}

			EnsureOutput(literal_length, output, output_limit);
			output.insert(output.end(), literals.begin() + static_cast<std::ptrdiff_t>(literal_position), literals.begin() + static_cast<std::ptrdiff_t>(literal_position + literal_length));
			literal_position += literal_length;
			const auto offset = ResolveOffset(offset_value, literal_length, state);
			if (offset == 0 || offset > output.size() - state.Start || offset > state.Window) {
				Fail("match offset exceeds decoded history or frame window");
			}

			EnsureOutput(match_length, output, output_limit);
			if (output.size() - block_start > state.BlockLimit || match_length > state.BlockLimit - (output.size() - block_start)) {
				Fail("sequence output exceeds block limit");
			}

			for (std::size_t match = 0; match < match_length; ++match) {
				output.push_back(output[output.size() - offset]);
			}

			if (index + 1 != sequence_count) {
				AdvanceFse(state.LiteralLengths, literal_state, bits);
				AdvanceFse(state.MatchLengths, match_state, bits);
				AdvanceFse(state.Offsets, offset_state, bits);
			}
		}

		if (bits.Remaining() != 0) {
			Fail("sequence stream has unused bits");
		}

		const auto trailing_literals = literals.size() - literal_position;
		EnsureOutput(trailing_literals, output, output_limit);
		if (trailing_literals > state.BlockLimit - (output.size() - block_start)) {
			Fail("trailing literals exceed block limit");
		}

		output.insert(output.end(), literals.begin() + static_cast<std::ptrdiff_t>(literal_position), literals.end());
	}

	static std::uint64_t HashRound(std::uint64_t accumulator, std::uint64_t lane) {
		return std::rotl(accumulator + lane * kHashPrime2, 31) * kHashPrime1;
	}

	static std::uint64_t ContentHash(std::span<const std::uint8_t> bytes) {
		ByteReader reader(bytes);
		std::uint64_t hash = kHashPrime5;
		if (bytes.size() >= 32) {
			std::array<std::uint64_t, 4> accumulators{kHashPrime1 + kHashPrime2, kHashPrime2, 0, std::uint64_t{0} - kHashPrime1};
			while (reader.Remaining() >= 32) {
				for (auto& accumulator : accumulators) {
					accumulator = HashRound(accumulator, reader.Integer(8));
				}
			}

			hash = std::rotl(accumulators[0], 1) + std::rotl(accumulators[1], 7) + std::rotl(accumulators[2], 12) + std::rotl(accumulators[3], 18);
			for (const auto accumulator : accumulators) {
				hash = (hash ^ HashRound(0, accumulator)) * kHashPrime1 + kHashPrime4;
			}
		}

		hash += bytes.size();
		while (reader.Remaining() >= 8) {
			hash = std::rotl(hash ^ HashRound(0, reader.Integer(8)), 27) * kHashPrime1 + kHashPrime4;
		}

		if (reader.Remaining() >= 4) {
			hash = std::rotl(hash ^ (reader.Integer(4) * kHashPrime1), 23) * kHashPrime2 + kHashPrime3;
		}

		while (reader.Remaining() != 0) {
			hash = std::rotl(hash ^ (reader.Integer(1) * kHashPrime5), 11) * kHashPrime1;
		}

		hash ^= hash >> 33;
		hash *= kHashPrime2;
		hash ^= hash >> 29;
		hash *= kHashPrime3;
		hash ^= hash >> 32;

		return hash;
	}

	static void DecodeFrame(ByteReader& reader, std::vector<std::uint8_t>& output, std::size_t output_limit, std::size_t allocation_limit) {
		const auto descriptor = static_cast<unsigned>(reader.Integer(1));
		if ((descriptor & 8) != 0) {
			Fail("reserved frame descriptor bit is set");
		}

		const bool single_segment = (descriptor & 32) != 0;
		const bool checksum = (descriptor & 4) != 0;
		const auto content_flag = descriptor >> 6;
		const auto dictionary_flag = descriptor & 3;
		std::uint64_t window = 0;
		if (!single_segment) {
			const auto window_descriptor = static_cast<unsigned>(reader.Integer(1));
			const auto base = std::uint64_t{1} << (10 + (window_descriptor >> 3));
			window = base + (base / 8) * (window_descriptor & 7);
		}

		const std::size_t dictionary_bytes = dictionary_flag == 3 ? 4 : dictionary_flag;
		if (reader.Integer(dictionary_bytes) != 0) {
			Fail("external dictionaries are unsupported");
		}

		const std::size_t content_bytes = content_flag == 0 ? (single_segment ? 1 : 0) : (std::size_t{1} << content_flag);
		std::uint64_t content_size = reader.Integer(content_bytes);
		if (content_bytes == 2) {
			content_size += 256;
		}

		if (single_segment) {
			window = content_size;
		}

		if (window > allocation_limit || window > std::numeric_limits<std::size_t>::max()) {
			Fail("frame window exceeds allocation limit");
		}

		if (content_bytes != 0 && content_size > output_limit - output.size()) {
			Fail("frame content size exceeds expected output");
		}

		FrameState state;
		state.Start = output.size();
		state.Window = static_cast<std::size_t>(window);
		state.BlockLimit = std::min(state.Window, kMaximumBlockSize);
		bool last = false;
		while (!last) {
			const auto header = static_cast<std::uint32_t>(reader.Integer(3));
			last = (header & 1) != 0;
			const auto type = (header >> 1) & 3;
			const std::size_t size = header >> 3;
			if (size > state.BlockLimit) {
				Fail("encoded block exceeds frame block limit");
			}

			if (type == 0) {
				EnsureOutput(size, output, output_limit);
				const auto bytes = reader.Take(size);
				output.insert(output.end(), bytes.begin(), bytes.end());
			} else if (type == 1) {
				EnsureOutput(size, output, output_limit);
				output.insert(output.end(), size, static_cast<std::uint8_t>(reader.Integer(1)));
			} else if (type == 2) {
				DecodeCompressedBlock(reader.Take(size), state, output, output_limit);
			} else {
				Fail("reserved block type is set");
			}
		}

		if (content_bytes != 0 && output.size() - state.Start != content_size) {
			Fail("frame content size does not match decoded bytes");
		}

		if (checksum) {
			const auto stored = static_cast<std::uint32_t>(reader.Integer(4));
			const auto actual = static_cast<std::uint32_t>(ContentHash(std::span<const std::uint8_t>(output).subspan(state.Start)));
			if (stored != actual) {
				Fail("frame content checksum does not match");
			}
		}
	}

	std::vector<std::uint8_t> DecodeZstd(std::span<const std::uint8_t> bytes, std::size_t expected_size, std::size_t max_decoded_size) {
		if (expected_size > max_decoded_size || bytes.size() > std::numeric_limits<std::size_t>::max() / 8) {
			Fail("input or expected output exceeds allocation limits");
		}

		ByteReader reader(bytes);
		std::vector<std::uint8_t> output;
		output.reserve(expected_size);
		std::size_t frame_count = 0;
		while (reader.Remaining() != 0) {
			const auto magic = static_cast<std::uint32_t>(reader.Integer(4));
			if ((magic & 0xfffffff0) == 0x184d2a50) {
				const auto skip_size = static_cast<std::size_t>(reader.Integer(4));
				reader.Take(skip_size);
			} else if (magic == kFrameMagic) {
				DecodeFrame(reader, output, expected_size, max_decoded_size);
				++frame_count;
			} else {
				Fail("frame signature is invalid");
			}
		}

		if (frame_count == 0 || output.size() != expected_size) {
			Fail("decoded size does not match expected output");
		}

		return output;
	}

} // namespace sgre::resource
