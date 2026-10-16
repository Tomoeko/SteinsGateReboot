#include "sgre/text/recursive_decomposition.hpp"

#include "sgre/text/nominal_glyph.hpp"

#include <stdexcept>

namespace sgre::text {

	void UnicodeNormalizationBuffer::InsertInitialDottedCircle(const resource::SfntFont& font, const InitialDottedCircleContext& context) {
		if ((context.TextFlags & 1U) == 0 || context.PrecedingContextLength != 0) {
			return;
		}
		if (m_State.InputCount == 0) {
			throw std::runtime_error("initial dotted circle requires a live first Unicode record");
		}
		if ((m_Storage[m_InputStorage][0].CharacterState & 0x1fU) != 12 || !QueryVectorFontNominalGlyph(font, 0x25cc)) {
			return;
		}

		SubstitutionGlyph circle;
		circle.GlyphIndex = 0x25cc;
		m_State.ScratchFlags = InitializeUnicodeGlyphProperties(std::span<SubstitutionGlyph>(&circle, 1), m_State.ScratchFlags);
		BeginOutput();
		m_State.InputCursor = 0;
		circle.Cluster = m_Storage[m_InputStorage][0].Cluster;
		circle.Mask = m_Storage[m_InputStorage][0].Mask;
		if (PrepareEmission(0, 1)) {
			m_Storage[OutputStorage()][m_State.OutputCount] = circle;
			++m_State.OutputCount;
		}
		while (m_State.InputCursor < m_State.InputCount && !m_State.Failed) {
			CopyCurrentInput();
		}
		static_cast<void>(PublishOutput());
	}

} // namespace sgre::text
