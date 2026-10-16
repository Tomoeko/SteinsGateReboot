#include "sgre/resource/cff.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::resource {

	static constexpr std::size_t kMaximumCffRecords = 1024 * 1024;
	static constexpr std::size_t kMaximumDictionaryOperands = 48;

	struct CffIndex {
		std::vector<std::span<const std::uint8_t>> Items;
		std::size_t EndOffset = 0;
	};

	struct CffDictionaryEntry {
		std::uint16_t Operator = 0;
		std::vector<double> Operands;
	};

	static std::span<const std::uint8_t> CffRange(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count) {
		if (offset > bytes.size() || count > bytes.size() - offset) {
			throw std::runtime_error("CFF range exceeds its resource");
		}

		return bytes.subspan(offset, count);
	}

	static std::uint8_t CffByte(std::span<const std::uint8_t> bytes, std::size_t& offset) {
		const auto value = CffRange(bytes, offset, 1)[0];
		++offset;

		return value;
	}

	static std::uint32_t CffUnsigned(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count) {
		const auto source = CffRange(bytes, offset, count);
		std::uint32_t value = 0;
		for (const auto byte : source) {
			value = (value << 8) | byte;
		}

		return value;
	}

	static void AddCffRecords(std::size_t& records, std::size_t added) {
		if (added > kMaximumCffRecords - records) {
			throw std::runtime_error("CFF record count exceeds its preparation limit");
		}

		records += added;
	}

	static CffIndex ReadCffIndex(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t& records) {
		const auto count = CffUnsigned(bytes, offset, 2);
		AddCffRecords(records, count);
		CffIndex index;
		index.EndOffset = offset + 2;
		if (count == 0) {
			return index;
		}

		const auto offset_size = CffByte(bytes, index.EndOffset);
		if (offset_size < 1 || offset_size > 4) {
			throw std::runtime_error("CFF INDEX offset size is invalid");
		}
		const auto offsets_begin = index.EndOffset;
		static_cast<void>(CffRange(bytes, offsets_begin, (std::size_t(count) + 1) * offset_size));
		const auto data_begin = offsets_begin + (std::size_t(count) + 1) * offset_size;
		auto previous = CffUnsigned(bytes, offsets_begin, offset_size);
		if (previous != 1) {
			throw std::runtime_error("CFF INDEX first offset is not one");
		}

		index.Items.reserve(count);
		for (std::size_t item = 0; item < count; ++item) {
			const auto next = CffUnsigned(bytes, offsets_begin + (item + 1) * offset_size, offset_size);
			if (next < previous || previous == 0 || previous - 1 > bytes.size() - data_begin) {
				throw std::runtime_error("CFF INDEX offsets are unordered or outside their data");
			}
			index.Items.push_back(CffRange(bytes, data_begin + previous - 1, next - previous));
			previous = next;
		}
		index.EndOffset = data_begin + previous - 1;

		return index;
	}

	static double ReadDictionaryReal(std::span<const std::uint8_t> bytes, std::size_t& offset) {
		std::string number;
		bool finished = false;
		while (!finished) {
			const auto byte = CffByte(bytes, offset);
			for (const auto nibble : {std::uint8_t(byte >> 4), std::uint8_t(byte & 15)}) {
				if (nibble <= 9) {
					number.push_back(static_cast<char>('0' + nibble));
				} else if (nibble == 10) {
					number.push_back('.');
				} else if (nibble == 11) {
					number.push_back('E');
				} else if (nibble == 12) {
					number.append("E-");
				} else if (nibble == 14) {
					number.push_back('-');
				} else if (nibble == 15) {
					finished = true;
					break;
				} else {
					throw std::runtime_error("CFF real operand has a reserved nibble");
				}
				if (number.size() > 64) {
					throw std::runtime_error("CFF real operand exceeds its digit limit");
				}
			}
		}

		double value = 0;
		std::istringstream input(number);
		input.imbue(std::locale::classic());
		input >> value;
		if (!input || input.peek() != std::char_traits<char>::eof() || !std::isfinite(value)) {
			throw std::runtime_error("CFF real operand is malformed or nonfinite");
		}

		return value;
	}

	static double ReadDictionaryNumber(std::span<const std::uint8_t> bytes, std::size_t& offset, std::uint8_t first) {
		if (first >= 32 && first <= 246) {
			return std::int32_t(first) - 139;
		}
		if (first >= 247 && first <= 250) {
			return (std::int32_t(first) - 247) * 256 + CffByte(bytes, offset) + 108;
		}
		if (first >= 251 && first <= 254) {
			return -(std::int32_t(first) - 251) * 256 - CffByte(bytes, offset) - 108;
		}
		if (first == 28) {
			const auto word = static_cast<std::uint16_t>(CffUnsigned(bytes, offset, 2));
			offset += 2;

			return std::bit_cast<std::int16_t>(word);
		}
		if (first == 29) {
			const auto word = CffUnsigned(bytes, offset, 4);
			offset += 4;

			return std::bit_cast<std::int32_t>(word);
		}
		if (first == 30) {
			return ReadDictionaryReal(bytes, offset);
		}

		throw std::runtime_error("CFF dictionary operand encoding is unsupported");
	}

	static std::vector<CffDictionaryEntry> ReadCffDictionary(std::span<const std::uint8_t> bytes, std::size_t& records) {
		std::vector<CffDictionaryEntry> dictionary;
		std::vector<double> operands;
		std::size_t offset = 0;
		while (offset < bytes.size()) {
			const auto first = CffByte(bytes, offset);
			if (first >= 28) {
				if (operands.size() >= kMaximumDictionaryOperands) {
					throw std::runtime_error("CFF dictionary operand count exceeds its limit");
				}
				operands.push_back(ReadDictionaryNumber(bytes, offset, first));
				continue;
			}
			if (first > 21) {
				throw std::runtime_error("CFF dictionary operator is reserved");
			}

			CffDictionaryEntry entry;
			entry.Operator = first == 12 ? std::uint16_t(0x0c00 | CffByte(bytes, offset)) : first;
			if (std::any_of(dictionary.begin(), dictionary.end(), [&](const auto& previous) {
				    return previous.Operator == entry.Operator;
			    })) {
				throw std::runtime_error("CFF dictionary contains a duplicate operator");
			}
			AddCffRecords(records, 1);
			entry.Operands = std::move(operands);
			dictionary.push_back(std::move(entry));
			operands.clear();
		}
		if (!operands.empty()) {
			throw std::runtime_error("CFF dictionary ends with unterminated operands");
		}

		return dictionary;
	}

	static const CffDictionaryEntry* FindCffEntry(const std::vector<CffDictionaryEntry>& dictionary, std::uint16_t operation, std::size_t count) {
		const auto found = std::find_if(dictionary.begin(), dictionary.end(), [&](const auto& entry) {
			return entry.Operator == operation;
		});
		if (found == dictionary.end()) {
			return nullptr;
		}
		if (found->Operands.size() != count) {
			throw std::runtime_error("CFF dictionary field has an invalid operand count");
		}

		return &*found;
	}

	static std::uint32_t CffInteger(double value) {
		if (value < 0 || value > std::numeric_limits<std::uint32_t>::max() || value != std::floor(value)) {
			throw std::runtime_error("CFF dictionary offset or integer is out of range");
		}

		return static_cast<std::uint32_t>(value);
	}

	static std::uint32_t RequiredCffInteger(const std::vector<CffDictionaryEntry>& dictionary, std::uint16_t operation) {
		const auto entry = FindCffEntry(dictionary, operation, 1);
		if (entry == nullptr) {
			throw std::runtime_error("CFF dictionary is missing a required offset");
		}

		return CffInteger(entry->Operands[0]);
	}

	static std::optional<std::array<double, 6>> ReadCffMatrix(const std::vector<CffDictionaryEntry>& dictionary) {
		const auto entry = FindCffEntry(dictionary, 0x0c07, 6);
		if (entry == nullptr) {
			return std::nullopt;
		}

		std::array<double, 6> matrix{};
		std::copy(entry->Operands.begin(), entry->Operands.end(), matrix.begin());

		return matrix;
	}

	static std::vector<double> ReadCffHintArray(const std::vector<CffDictionaryEntry>& dictionary, std::uint16_t operation, std::size_t maximum_count, bool paired) {
		const auto found = std::find_if(dictionary.begin(), dictionary.end(), [&](const auto& entry) {
			return entry.Operator == operation;
		});
		if (found == dictionary.end()) {
			return {};
		}
		if (found->Operands.size() > maximum_count || (paired && found->Operands.size() % 2 != 0)) {
			throw std::runtime_error("CFF private hint array has an excessive or unpaired count");
		}

		std::vector<double> values;
		values.reserve(found->Operands.size());
		double current = 0;
		for (const auto delta : found->Operands) {
			current += delta;
			if (!std::isfinite(current)) {
				throw std::runtime_error("CFF private hint array delta accumulation is nonfinite");
			}

			values.push_back(current);
		}

		return values;
	}

	static std::optional<double> ReadCffHintScalar(const std::vector<CffDictionaryEntry>& dictionary, std::uint16_t operation) {
		const auto entry = FindCffEntry(dictionary, operation, 1);
		if (entry == nullptr) {
			return std::nullopt;
		}

		return entry->Operands[0];
	}

	static std::int32_t CffHintInteger(double value) {
		if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max() || value != std::floor(value)) {
			throw std::runtime_error("CFF private hint integer is fractional or outside signed storage");
		}

		return static_cast<std::int32_t>(value);
	}

	static CffHintParameters ReadCffHintParameters(const std::vector<CffDictionaryEntry>& dictionary) {
		CffHintParameters hints;
		hints.BlueValuesFontUnits = ReadCffHintArray(dictionary, 6, 14, true);
		hints.OtherBluesFontUnits = ReadCffHintArray(dictionary, 7, 10, true);
		hints.FamilyBluesFontUnits = ReadCffHintArray(dictionary, 8, 14, true);
		hints.FamilyOtherBluesFontUnits = ReadCffHintArray(dictionary, 9, 10, true);
		hints.HorizontalStemSnapFontUnits = ReadCffHintArray(dictionary, 0x0c0c, 12, false);
		hints.VerticalStemSnapFontUnits = ReadCffHintArray(dictionary, 0x0c0d, 12, false);
		hints.StandardHorizontalStemWidthFontUnits = ReadCffHintScalar(dictionary, 10);
		hints.StandardVerticalStemWidthFontUnits = ReadCffHintScalar(dictionary, 11);
		hints.BlueScale = ReadCffHintScalar(dictionary, 0x0c09).value_or(hints.BlueScale);
		hints.BlueShiftFontUnits = ReadCffHintScalar(dictionary, 0x0c0a).value_or(hints.BlueShiftFontUnits);
		hints.BlueFuzzFontUnits = ReadCffHintScalar(dictionary, 0x0c0b).value_or(hints.BlueFuzzFontUnits);
		hints.ExpansionFactor = ReadCffHintScalar(dictionary, 0x0c12).value_or(hints.ExpansionFactor);
		hints.LanguageGroup = CffHintInteger(ReadCffHintScalar(dictionary, 0x0c11).value_or(0));
		hints.InitialRandomSeed = CffHintInteger(ReadCffHintScalar(dictionary, 0x0c13).value_or(0));
		const auto force_bold = ReadCffHintScalar(dictionary, 0x0c0e).value_or(0);
		if (force_bold != 0 && force_bold != 1) {
			throw std::runtime_error("CFF private ForceBold is not a boolean");
		}

		hints.ForceBold = force_bold == 1;

		return hints;
	}

	static CffFontDictionary ReadFontDictionary(std::span<const std::uint8_t> bytes, const std::vector<CffDictionaryEntry>& dictionary, std::size_t& records) {
		CffFontDictionary result;
		result.FontMatrix = ReadCffMatrix(dictionary);
		const auto private_entry = FindCffEntry(dictionary, 18, 2);
		if (private_entry == nullptr) {
			return result;
		}

		const auto private_size = CffInteger(private_entry->Operands[0]);
		const auto private_offset = CffInteger(private_entry->Operands[1]);
		result.PrivateDictionaryBytes = CffRange(bytes, private_offset, private_size);
		const auto private_dictionary = ReadCffDictionary(result.PrivateDictionaryBytes, records);
		result.Hinting = ReadCffHintParameters(private_dictionary);
		const auto default_width = FindCffEntry(private_dictionary, 20, 1);
		const auto nominal_width = FindCffEntry(private_dictionary, 21, 1);
		result.DefaultWidth = default_width == nullptr ? 0 : default_width->Operands[0];
		result.NominalWidth = nominal_width == nullptr ? 0 : nominal_width->Operands[0];
		const auto subroutines = FindCffEntry(private_dictionary, 19, 1);
		if (subroutines != nullptr) {
			const auto relative = CffInteger(subroutines->Operands[0]);
			if (private_offset > bytes.size() || relative > bytes.size() - private_offset) {
				throw std::runtime_error("CFF local subroutine offset exceeds its resource");
			}
			result.LocalSubroutines = ReadCffIndex(bytes, private_offset + relative, records).Items;
		}

		return result;
	}

	static std::vector<std::uint8_t> ReadFontDictionarySelection(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t glyph_count, std::size_t dictionary_count, std::size_t& records) {
		std::vector<std::uint8_t> selection(glyph_count);
		const auto format = CffByte(bytes, offset);
		if (format == 0) {
			const auto indices = CffRange(bytes, offset, glyph_count);
			std::copy(indices.begin(), indices.end(), selection.begin());
		} else if (format == 3) {
			const auto ranges = CffUnsigned(bytes, offset, 2);
			offset += 2;
			if (ranges == 0) {
				throw std::runtime_error("CFF FDSelect has no glyph ranges");
			}
			AddCffRecords(records, ranges);
			static_cast<void>(CffRange(bytes, offset, std::size_t(ranges) * 3 + 2));
			const auto sentinel = CffUnsigned(bytes, offset + std::size_t(ranges) * 3, 2);
			if (sentinel != glyph_count || CffUnsigned(bytes, offset, 2) != 0) {
				throw std::runtime_error("CFF FDSelect does not cover the font's glyph range");
			}
			for (std::size_t range = 0; range < ranges; ++range) {
				const auto record = offset + range * 3;
				const auto first = CffUnsigned(bytes, record, 2);
				const auto last = range + 1 == ranges ? sentinel : CffUnsigned(bytes, record + 3, 2);
				if (first >= last || last > glyph_count) {
					throw std::runtime_error("CFF FDSelect ranges are unordered or outside the font");
				}
				std::fill(selection.begin() + static_cast<std::ptrdiff_t>(first), selection.begin() + static_cast<std::ptrdiff_t>(last), bytes[record + 2]);
			}
		} else {
			throw std::runtime_error("CFF FDSelect format is unsupported");
		}

		for (const auto index : selection) {
			if (index >= dictionary_count) {
				throw std::runtime_error("CFF selected font dictionary exceeds its array");
			}
		}

		return selection;
	}

	CffFont::CffFont(const SfntFont& font) {
		if (font.OutlineKind() != SfntOutlineKind::Cff) {
			throw std::runtime_error("CFF reader requires a CFF outline font");
		}
		const auto table = font.FindTable("CFF ");
		if (!table) {
			throw std::runtime_error("CFF table is missing from its font");
		}
		m_Bytes = *table;
		const auto header = CffRange(m_Bytes, 0, 4);
		if (header[0] != 1 || header[1] != 0 || header[2] < 4 || header[3] < 1 || header[3] > 4) {
			throw std::runtime_error("CFF header version or offset size is unsupported");
		}
		static_cast<void>(CffRange(m_Bytes, 0, header[2]));

		std::size_t records = 0;
		const auto names = ReadCffIndex(m_Bytes, header[2], records);
		const auto top = ReadCffIndex(m_Bytes, names.EndOffset, records);
		const auto strings = ReadCffIndex(m_Bytes, top.EndOffset, records);
		m_GlobalSubroutines = ReadCffIndex(m_Bytes, strings.EndOffset, records).Items;
		if (names.Items.size() != 1 || top.Items.size() != 1) {
			throw std::runtime_error("OpenType CFF font set does not contain one font");
		}
		const auto dictionary = ReadCffDictionary(top.Items[0], records);
		const auto charstring_type = FindCffEntry(dictionary, 0x0c06, 1);
		if (charstring_type != nullptr && charstring_type->Operands[0] != 2) {
			throw std::runtime_error("CFF charstring type is unsupported");
		}
		const auto matrix = ReadCffMatrix(dictionary);
		if (matrix) {
			m_FontMatrix = *matrix;
		}
		m_GlyphPrograms = ReadCffIndex(m_Bytes, RequiredCffInteger(dictionary, 17), records).Items;
		if (m_GlyphPrograms.size() != font.GlyphCount()) {
			throw std::runtime_error("CFF charstring count differs from its sfnt glyph count");
		}

		const auto cid = FindCffEntry(dictionary, 0x0c1e, 3);
		m_IsCid = cid != nullptr;
		if (m_IsCid) {
			if (CffInteger(cid->Operands[0]) >= 391 + strings.Items.size() || CffInteger(cid->Operands[1]) >= 391 + strings.Items.size()) {
				throw std::runtime_error("CFF CID registry or ordering SID exceeds its strings");
			}
			static_cast<void>(CffInteger(cid->Operands[2]));
			const auto dictionaries = ReadCffIndex(m_Bytes, RequiredCffInteger(dictionary, 0x0c24), records);
			if (dictionaries.Items.empty() || dictionaries.Items.size() > 256) {
				throw std::runtime_error("CFF CID font dictionary count is invalid");
			}
			m_FontDictionaries.reserve(dictionaries.Items.size());
			for (const auto entry : dictionaries.Items) {
				m_FontDictionaries.push_back(ReadFontDictionary(m_Bytes, ReadCffDictionary(entry, records), records));
			}
			m_GlyphDictionaries = ReadFontDictionarySelection(m_Bytes, RequiredCffInteger(dictionary, 0x0c25), m_GlyphPrograms.size(), m_FontDictionaries.size(), records);
		} else {
			m_FontDictionaries.push_back(ReadFontDictionary(m_Bytes, dictionary, records));
			m_GlyphDictionaries.resize(m_GlyphPrograms.size());
		}
	}

	bool CffFont::IsCid() const {
		return m_IsCid;
	}

	const std::array<double, 6>& CffFont::FontMatrix() const {
		return m_FontMatrix;
	}

	std::span<const std::span<const std::uint8_t>> CffFont::GlobalSubroutines() const {
		return m_GlobalSubroutines;
	}

	std::span<const CffFontDictionary> CffFont::FontDictionaries() const {
		return m_FontDictionaries;
	}

	std::span<const std::uint8_t> CffFont::GlyphProgram(std::uint32_t glyph_index) const {
		if (glyph_index >= m_GlyphPrograms.size()) {
			throw std::runtime_error("CFF glyph program index exceeds its font");
		}

		return m_GlyphPrograms[glyph_index];
	}

	std::size_t CffFont::GlyphFontDictionary(std::uint32_t glyph_index) const {
		if (glyph_index >= m_GlyphDictionaries.size()) {
			throw std::runtime_error("CFF glyph dictionary index exceeds its font");
		}

		return m_GlyphDictionaries[glyph_index];
	}

} // namespace sgre::resource
