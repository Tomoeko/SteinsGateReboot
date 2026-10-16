#include "sgre/text/recursive_decomposition.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace sgre::text {

	static GlyphPositionRow DecodeAuxiliaryPosition(const SubstitutionGlyph& storage) {
		GlyphPositionRow row;
		row.XAdvance26Dot6 = std::bit_cast<std::int32_t>(storage.GlyphIndex);
		row.YAdvance26Dot6 = std::bit_cast<std::int32_t>(storage.Mask);
		row.XOffset26Dot6 = std::bit_cast<std::int32_t>(storage.Cluster);
		const auto fourth_word = std::uint32_t(storage.GlyphProperties) | (std::uint32_t(storage.PrivateState) << 16);
		row.YOffset26Dot6 = std::bit_cast<std::int32_t>(fourth_word);
		row.PrivateWord = storage.CharacterState;

		return row;
	}

	static SubstitutionGlyph EncodeAuxiliaryPosition(const GlyphPositionRow& row) {
		SubstitutionGlyph storage;
		storage.GlyphIndex = std::bit_cast<std::uint32_t>(row.XAdvance26Dot6);
		storage.Mask = std::bit_cast<std::uint32_t>(row.YAdvance26Dot6);
		storage.Cluster = std::bit_cast<std::uint32_t>(row.XOffset26Dot6);
		const auto fourth_word = std::bit_cast<std::uint32_t>(row.YOffset26Dot6);
		storage.GlyphProperties = static_cast<std::uint16_t>(fourth_word & 0xffffU);
		storage.PrivateState = static_cast<std::uint16_t>(fourth_word >> 16);
		storage.CharacterState = row.PrivateWord;

		return storage;
	}

	void UnicodeNormalizationBuffer::ClearPositionRows() {
		auto& auxiliary = m_Storage[1 - m_InputStorage];
		if (m_State.InputCount > auxiliary.size()) {
			throw std::runtime_error("Unicode buffer live position range exceeds its actual auxiliary allocation");
		}

		m_State.OutputAliasesInput = true;
		m_State.OutputActive = false;
		m_State.PositionsActive = true;
		m_State.OutputCount = 0;
		std::fill_n(auxiliary.begin(), m_State.InputCount, SubstitutionGlyph{});
	}

	void UnicodeNormalizationBuffer::EnsurePositionRows() {
		if (!m_State.PositionsActive) {
			ClearPositionRows();
		}
	}

	GlyphPositionRow UnicodeNormalizationBuffer::PositionRow(std::uint32_t index) {
		const auto& auxiliary = m_Storage[1 - m_InputStorage];
		if (index >= m_State.InputCount || index >= auxiliary.size()) {
			throw std::runtime_error("Unicode buffer position index exceeds its live owned rows");
		}

		EnsurePositionRows();

		return DecodeAuxiliaryPosition(auxiliary[index]);
	}

	void UnicodeNormalizationBuffer::SetPositionRow(std::uint32_t index, const GlyphPositionRow& row) {
		if (!m_State.PositionsActive) {
			throw std::runtime_error("Unicode buffer position storage write requires active positions");
		}

		auto& auxiliary = m_Storage[1 - m_InputStorage];
		if (index >= m_State.InputCount || index >= auxiliary.size()) {
			throw std::runtime_error("Unicode buffer position index exceeds its live owned rows");
		}

		auxiliary[index] = EncodeAuxiliaryPosition(row);
	}

} // namespace sgre::text
