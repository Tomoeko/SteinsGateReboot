#include "sgre/text/recursive_decomposition.hpp"

#include "sgre/text/nominal_glyph.hpp"
#include "sgre/text/unicode_composition.hpp"
#include "sgre/text/unicode_decomposition.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sgre::text {

	static constexpr std::uint32_t kMaximumResidentNormalizationLength = 65536;
	static constexpr std::uint32_t kMaximumCanonicalDecompositionDepth = 16;

	static void SetNormalizationGlyphWord(SubstitutionGlyph& glyph, std::uint32_t glyph_index) {
		// During normalization the four-byte word at +0x0c is the nominal-glyph scratch word.
		glyph.GlyphProperties = static_cast<std::uint16_t>(glyph_index & 0xffffU);
		glyph.PrivateState = static_cast<std::uint16_t>(glyph_index >> 16);
	}

	UnicodeNormalizationBuffer::UnicodeNormalizationBuffer(std::span<const SubstitutionGlyph> glyphs, std::uint32_t maximum_length, UnicodeBufferScratchFlags scratch_flags) {
		if (maximum_length > kMaximumResidentNormalizationLength || glyphs.size() > std::size_t(maximum_length) + 1) {
			throw std::runtime_error("Unicode normalization resident record count exceeds its limit");
		}

		m_State.ResidentCapacity = maximum_length + 1;
		m_State.MaximumLength = maximum_length;
		m_State.InputCount = static_cast<std::uint32_t>(glyphs.size());
		m_State.ScratchFlags = scratch_flags;
		m_InputMetadata.Content = UnicodeBufferContent::Unicode;
		for (auto& storage : m_Storage) {
			storage.resize(m_State.ResidentCapacity);
		}
		std::copy(glyphs.begin(), glyphs.end(), m_Storage[0].begin());
	}

	void UnicodeNormalizationBuffer::RequireCurrentInput() const {
		if (m_State.InputCursor >= m_State.InputCount) {
			throw std::runtime_error("Unicode normalization input cursor exceeds its live records");
		}
	}

	std::size_t UnicodeNormalizationBuffer::OutputStorage() const {
		return m_State.OutputAliasesInput ? m_InputStorage : 1 - m_InputStorage;
	}

	void UnicodeNormalizationBuffer::BeginOutput() {
		m_State.PositionsActive = false;
		m_State.OutputAliasesInput = true;
		m_State.OutputActive = true;
		m_State.OutputCount = 0;
	}

	bool UnicodeNormalizationBuffer::PrepareEmission(std::uint32_t consumed, std::uint32_t produced) {
		const auto requested = m_State.OutputCount + produced;
		// The original resident-capacity check precedes both the sticky error and maximum-length checks.
		if (!EnsureInputCapacity(requested)) {
			return false;
		}

		if (m_State.OutputAliasesInput && requested > m_State.InputCursor + consumed) {
			std::copy_n(m_Storage[m_InputStorage].begin(), m_State.OutputCount, m_Storage[1 - m_InputStorage].begin());
			m_State.OutputAliasesInput = false;
		}

		return true;
	}

	void UnicodeNormalizationBuffer::CopyCurrentInput() {
		RequireCurrentInput();
		if (!m_State.OutputActive) {
			++m_State.InputCursor;

			return;
		}

		if (m_State.OutputAliasesInput && m_State.OutputCount == m_State.InputCursor) {
			++m_State.OutputCount;
		} else {
			if (!PrepareEmission(1, 1)) {
				return;
			}

			m_Storage[OutputStorage()][m_State.OutputCount] = m_Storage[m_InputStorage][m_State.InputCursor];
			++m_State.OutputCount;
		}
		++m_State.InputCursor;
	}

	void UnicodeNormalizationBuffer::EmitDecomposedGlyph(std::uint32_t codepoint, std::uint32_t glyph_index) {
		SetNormalizationGlyphWord(m_Storage[m_InputStorage][m_State.InputCursor], glyph_index);
		if (PrepareEmission(0, 1)) {
			auto& emitted = m_Storage[OutputStorage()][m_State.OutputCount];
			emitted = m_Storage[m_InputStorage][m_State.InputCursor];
			emitted.GlyphIndex = codepoint;
			++m_State.OutputCount;
		}

		// Native 0x14040cef0 refreshes the last output even when append failed or the count is zero.
		const auto last = m_State.OutputCount == 0 ? 0 : m_State.OutputCount - 1;
		auto& glyph = m_Storage[OutputStorage()][last];
		m_State.ScratchFlags = InitializeUnicodeGlyphProperties(std::span<SubstitutionGlyph>(&glyph, 1), m_State.ScratchFlags);
	}

	std::uint32_t UnicodeNormalizationBuffer::EmitCanonicalDecomposition(const resource::SfntFont& font, std::uint32_t codepoint, UnicodeDecompositionPreference preference, std::uint32_t depth) {
		if (depth >= kMaximumCanonicalDecompositionDepth) {
			throw std::runtime_error("Unicode canonical decomposition exceeds its recursion limit");
		}

		const auto decomposition = DecomposeUnicodeCodepoint(codepoint);
		if (!decomposition) {
			return 0;
		}

		std::uint32_t second_glyph = 0;
		if (decomposition->Second != 0) {
			const auto mapping = QueryVectorFontNominalGlyph(font, decomposition->Second);
			if (!mapping) {
				return 0;
			}
			second_glyph = *mapping;
		}
		const auto first_glyph = QueryVectorFontNominalGlyph(font, decomposition->First);
		if (preference != UnicodeDecompositionPreference::MappedFirst || !first_glyph) {
			const auto recursive_count = EmitCanonicalDecomposition(font, decomposition->First, preference, depth + 1);
			if (recursive_count != 0) {
				if (decomposition->Second != 0) {
					EmitDecomposedGlyph(decomposition->Second, second_glyph);

					return recursive_count + 1;
				}

				return recursive_count;
			}
		}
		if (!first_glyph) {
			return 0;
		}

		EmitDecomposedGlyph(decomposition->First, *first_glyph);
		if (decomposition->Second != 0) {
			EmitDecomposedGlyph(decomposition->Second, second_glyph);

			return 2;
		}

		return 1;
	}

	std::uint32_t UnicodeNormalizationBuffer::TryDecomposeCurrentInput(const resource::SfntFont& font, UnicodeDecompositionPreference preference) {
		RequireCurrentInput();
		if (!m_State.OutputActive || (preference != UnicodeDecompositionPreference::RecursiveFirst && preference != UnicodeDecompositionPreference::MappedFirst)) {
			throw std::runtime_error("Unicode canonical emission requires an active output phase and a supported preference");
		}

		const auto count = EmitCanonicalDecomposition(font, m_Storage[m_InputStorage][m_State.InputCursor].GlyphIndex, preference, 0);
		if (count != 0) {
			++m_State.InputCursor;
		}

		return count;
	}

	bool UnicodeNormalizationBuffer::PublishOutput() {
		if (m_State.Failed) {
			return false;
		}

		if (!m_State.OutputAliasesInput) {
			m_InputStorage = 1 - m_InputStorage;
		}
		m_State.OutputActive = false;
		std::swap(m_State.InputCount, m_State.OutputCount);
		m_State.InputCursor = 0;

		return true;
	}

	static constexpr std::size_t kMaximumNeutralNormalizationRecords = 4096;
	static constexpr std::uint32_t kSpaceFallbackScratchBit = 4;
	static constexpr std::uint32_t kUnsafeToBreakMask = 1;

	static bool IsNormalizationMark(const SubstitutionGlyph& glyph) {
		const auto category = glyph.CharacterState & 0x1fU;

		return category >= 10 && category <= 12;
	}

	static bool IsNormalizationSelector(std::uint32_t codepoint) {
		return (codepoint >= 0xfe00 && codepoint <= 0xfe0f) || (codepoint >= 0xe0100 && codepoint <= 0xe01ef);
	}

	static std::uint32_t NormalizationCombiningClass(const SubstitutionGlyph& glyph) {
		return IsNormalizationMark(glyph) ? (glyph.CharacterState & 0xffffU) >> 8 : 0;
	}

	static std::optional<std::uint16_t> SpaceFallbackProperty(std::uint32_t codepoint) {
		switch (codepoint) {
		case 0x20:
		case 0xa0:
			return 0x1200;
		case 0x2000:
		case 0x2002:
			return 0x200;
		case 0x2001:
		case 0x2003:
		case 0x3000:
			return 0x100;
		case 0x2004:
			return 0x300;
		case 0x2005:
			return 0x400;
		case 0x2006:
			return 0x600;
		case 0x2007:
			return 0x1300;
		case 0x2008:
			return 0x1400;
		case 0x2009:
			return 0x500;
		case 0x200a:
			return 0x1000;
		case 0x202f:
			return 0x1500;
		case 0x205f:
			return 0x1100;
		default:
			return std::nullopt;
		}
	}

	static void SetNormalizationCluster(SubstitutionGlyph& glyph, std::uint32_t cluster) {
		if (glyph.Cluster != cluster) {
			glyph.Mask &= ~kUnsafeToBreakMask;
		}
		glyph.Cluster = cluster;
	}

	void UnicodeNormalizationBuffer::MapCurrentInput(const resource::SfntFont& font, UnicodeDecompositionPreference preference) {
		RequireCurrentInput();
		const auto codepoint = m_Storage[m_InputStorage][m_State.InputCursor].GlyphIndex;
		std::optional<std::uint32_t> glyph;
		if (preference == UnicodeDecompositionPreference::MappedFirst) {
			glyph = QueryVectorFontNominalGlyph(font, codepoint);
		}
		if (!glyph) {
			if (TryDecomposeCurrentInput(font, preference) != 0) {
				return;
			}
			if (preference == UnicodeDecompositionPreference::RecursiveFirst) {
				glyph = QueryVectorFontNominalGlyph(font, codepoint);
			}
		}
		if (glyph) {
			SetNormalizationGlyphWord(m_Storage[m_InputStorage][m_State.InputCursor], *glyph);
			CopyCurrentInput();

			return;
		}

		auto& current = m_Storage[m_InputStorage][m_State.InputCursor];
		if ((current.CharacterState & 0x1fU) == 29) {
			if (const auto property = SpaceFallbackProperty(codepoint)) {
				if (const auto space = QueryVectorFontNominalGlyph(font, 0x20)) {
					current.CharacterState = (current.CharacterState & 0xffff00ffU) | *property;
					SetNormalizationGlyphWord(current, *space);
					CopyCurrentInput();
					m_State.ScratchFlags = static_cast<UnicodeBufferScratchFlags>(static_cast<std::uint32_t>(m_State.ScratchFlags) | kSpaceFallbackScratchBit);

					return;
				}
			}
		}
		if (codepoint == 0x2011) {
			glyph = QueryVectorFontNominalGlyph(font, 0x2010);
		}
		SetNormalizationGlyphWord(current, glyph.value_or(0));
		CopyCurrentInput();
	}

	void UnicodeNormalizationBuffer::MapNominalCurrentInput(const resource::SfntFont& font) {
		RequireCurrentInput();
		auto& current = m_Storage[m_InputStorage][m_State.InputCursor];
		SetNormalizationGlyphWord(current, 0);
		const auto glyph = QueryVectorFontNominalGlyph(font, current.GlyphIndex);
		SetNormalizationGlyphWord(current, glyph.value_or(0));
	}

	void UnicodeNormalizationBuffer::MergeInputClusters(std::size_t first, std::size_t last, GlyphClusterLevel cluster_level) {
		auto& input = m_Storage[m_InputStorage];
		auto minimum = input[first].Cluster;
		for (std::size_t index = first + 1; index < last; ++index) {
			minimum = std::min(minimum, input[index].Cluster);
		}
		if (cluster_level == GlyphClusterLevel::Characters) {
			for (std::size_t index = first; index < last; ++index) {
				if (input[index].Cluster != minimum) {
					input[index].Mask |= kUnsafeToBreakMask;
					m_State.ScratchFlags = static_cast<UnicodeBufferScratchFlags>(static_cast<std::uint32_t>(m_State.ScratchFlags) | static_cast<std::uint32_t>(UnicodeBufferScratchFlags::HasUnsafeToBreak));
				}
			}

			return;
		}

		while (last < m_State.InputCount && input[last - 1].Cluster == input[last].Cluster) {
			++last;
		}
		while (first > m_State.InputCursor && input[first - 1].Cluster == input[first].Cluster) {
			--first;
		}
		if (first == m_State.InputCursor) {
			auto& output = m_Storage[OutputStorage()];
			for (std::size_t count = m_State.OutputCount; count > 0; --count) {
				auto& previous = output[count - 1];
				if (previous.Cluster != input[first].Cluster) {
					break;
				}
				SetNormalizationCluster(previous, minimum);
			}
		}
		for (std::size_t index = first; index < last; ++index) {
			SetNormalizationCluster(input[index], minimum);
		}
	}

	void UnicodeNormalizationBuffer::ReplaceVariationPair(GlyphClusterLevel cluster_level) {
		if (!PrepareEmission(2, 1)) {
			return;
		}

		MergeInputClusters(m_State.InputCursor, m_State.InputCursor + 2, cluster_level);
		m_Storage[OutputStorage()][m_State.OutputCount] = m_Storage[m_InputStorage][m_State.InputCursor];
		m_State.InputCursor += 2;
		++m_State.OutputCount;
	}

	void UnicodeNormalizationBuffer::MapSelectorSegment(const resource::SfntFont& font, const VectorFontVariationMap& variations, std::uint32_t end, GlyphClusterLevel cluster_level) {
		while (m_State.InputCursor + 1 < end && !m_State.Failed) {
			const auto next = m_Storage[m_InputStorage][m_State.InputCursor + 1].GlyphIndex;
			if (!IsNormalizationSelector(next)) {
				MapNominalCurrentInput(font);
				CopyCurrentInput();
				continue;
			}

			auto& current = m_Storage[m_InputStorage][m_State.InputCursor];
			SetNormalizationGlyphWord(current, 0);
			if (const auto glyph = variations.Query(current.GlyphIndex, next)) {
				SetNormalizationGlyphWord(current, *glyph);
				ReplaceVariationPair(cluster_level);
			} else {
				MapNominalCurrentInput(font);
				CopyCurrentInput();
				MapNominalCurrentInput(font);
				CopyCurrentInput();
			}

			while (m_State.InputCursor < end && IsNormalizationSelector(m_Storage[m_InputStorage][m_State.InputCursor].GlyphIndex)) {
				MapNominalCurrentInput(font);
				const auto previous_cursor = m_State.InputCursor;
				CopyCurrentInput();
				if (m_State.InputCursor == previous_cursor) {
					throw std::runtime_error("neutral Unicode extra-selector copy cannot advance after resident capacity failure");
				}
			}
		}
		if (m_State.InputCursor < end) {
			MapNominalCurrentInput(font);
			CopyCurrentInput();
		}
	}

	void UnicodeNormalizationBuffer::ReorderCurrentCombiningInput(GlyphClusterLevel cluster_level) {
		auto& input = m_Storage[m_InputStorage];
		std::size_t first = 0;
		while (first < m_State.InputCount) {
			if (NormalizationCombiningClass(input[first]) == 0) {
				++first;
				continue;
			}

			auto last = first + 1;
			while (last < m_State.InputCount && NormalizationCombiningClass(input[last]) != 0) {
				++last;
			}
			if (last - first >= 2 && last - first <= 32) {
				for (std::size_t next = first + 1; next < last; ++next) {
					auto destination = next;
					const auto combining_class = NormalizationCombiningClass(input[next]);
					while (destination > first && NormalizationCombiningClass(input[destination - 1]) > combining_class) {
						--destination;
					}
					if (destination == next) {
						continue;
					}

					MergeInputClusters(destination, next + 1, cluster_level);
					const auto moved = input[next];
					for (std::size_t index = next; index > destination; --index) {
						input[index] = input[index - 1];
					}
					input[destination] = moved;
				}
			}
			first = last + 1;
		}
	}

	bool UnicodeNormalizationBuffer::NormalizeNeutralBeforeRecomposition(const resource::SfntFont& font, GlyphClusterLevel cluster_level) {
		if (cluster_level != GlyphClusterLevel::MonotoneGraphemes && cluster_level != GlyphClusterLevel::MonotoneCharacters && cluster_level != GlyphClusterLevel::Characters) {
			throw std::runtime_error("unsupported neutral Unicode normalization cluster level");
		}
		if (m_State.InputCount == 0) {
			return !m_State.Failed;
		}
		if (m_State.InputCount > kMaximumNeutralNormalizationRecords || (!m_AllocatorGrowth && m_State.MaximumLength > kMaximumNeutralNormalizationRecords)) {
			throw std::runtime_error("neutral Unicode normalization exceeds its resident record limit");
		}

		const VectorFontVariationMap variations(font);
		BeginOutput();
		m_State.InputCursor = 0;
		const auto input_count = m_State.InputCount;
		while (m_State.InputCursor < input_count && !m_State.Failed) {
			auto end = m_State.InputCursor + 1;
			while (end < input_count && IsNormalizationMark(m_Storage[m_InputStorage][end])) {
				++end;
			}
			if (end == m_State.InputCursor + 1) {
				MapCurrentInput(font, UnicodeDecompositionPreference::MappedFirst);
				continue;
			}

			bool has_selector = false;
			for (auto index = m_State.InputCursor; index < end; ++index) {
				if (IsNormalizationSelector(m_Storage[m_InputStorage][index].GlyphIndex)) {
					has_selector = true;
					break;
				}
			}
			if (has_selector) {
				MapSelectorSegment(font, variations, end, cluster_level);
			} else {
				while (m_State.InputCursor < end && !m_State.Failed) {
					MapCurrentInput(font, UnicodeDecompositionPreference::RecursiveFirst);
				}
			}
		}
		static_cast<void>(PublishOutput());
		ReorderCurrentCombiningInput(cluster_level);

		return !m_State.Failed;
	}

	void UnicodeNormalizationBuffer::MergeOutputClusters(std::size_t first, std::size_t last, GlyphClusterLevel cluster_level) {
		if (cluster_level == GlyphClusterLevel::Characters || last - first < 2) {
			return;
		}

		auto& output = m_Storage[OutputStorage()];
		auto minimum = output[first].Cluster;
		for (std::size_t index = first + 1; index < last; ++index) {
			minimum = std::min(minimum, output[index].Cluster);
		}
		while (first > 0 && output[first - 1].Cluster == output[first].Cluster) {
			--first;
		}
		while (last < m_State.OutputCount && output[last - 1].Cluster == output[last].Cluster) {
			++last;
		}
		if (last == m_State.OutputCount) {
			auto& input = m_Storage[m_InputStorage];
			for (std::size_t index = m_State.InputCursor; index < m_State.InputCount; ++index) {
				if (input[index].Cluster != output[last - 1].Cluster) {
					break;
				}
				SetNormalizationCluster(input[index], minimum);
			}
		}
		for (std::size_t index = first; index < last; ++index) {
			SetNormalizationCluster(output[index], minimum);
		}
	}

	void UnicodeNormalizationBuffer::CopyResidentRecompositionInput() {
		// Recomposition unconditionally copies once, including after a failed first publication.
		if (!m_State.OutputActive) {
			++m_State.InputCursor;
			return;
		}
		if (m_State.OutputAliasesInput && m_State.OutputCount == m_State.InputCursor) {
			++m_State.OutputCount;
			++m_State.InputCursor;
			return;
		}
		if (!PrepareEmission(1, 1)) {
			return;
		}
		if (m_State.InputCursor >= m_State.ResidentCapacity) {
			throw std::runtime_error("neutral recomposition copy exceeds resident input records after failed first publication");
		}

		m_Storage[OutputStorage()][m_State.OutputCount] = m_Storage[m_InputStorage][m_State.InputCursor];
		++m_State.OutputCount;
		++m_State.InputCursor;
	}

	void UnicodeNormalizationBuffer::RecomposeNeutralInput(const resource::SfntFont& font, GlyphClusterLevel cluster_level) {
		BeginOutput();
		const auto input_count = m_State.InputCount;
		bool allow_composition = true;
		std::uint32_t starter = 0;
		CopyResidentRecompositionInput();
		while (m_State.InputCursor < input_count && !m_State.Failed) {
			const auto current = m_Storage[m_InputStorage][m_State.InputCursor];
			const auto last = m_State.OutputCount == 0 ? 0 : m_State.OutputCount - 1;
			bool candidate_allowed = allow_composition && IsNormalizationMark(current);
			if (candidate_allowed && starter != m_State.OutputCount - 1) {
				candidate_allowed = NormalizationCombiningClass(m_Storage[OutputStorage()][last]) < NormalizationCombiningClass(current);
			}

			std::optional<std::uint32_t> candidate;
			std::optional<std::uint32_t> candidate_glyph;
			if (candidate_allowed) {
				const auto first = m_Storage[OutputStorage()][starter].GlyphIndex;
				if (first != 0 && current.GlyphIndex != 0) {
					const auto composed = ComposeUnicodeCodepoints(first, current.GlyphIndex);
					if (composed.Status == UnicodeCompositionStatus::Composed) {
						candidate = composed.Codepoint;
						candidate_glyph = QueryVectorFontNominalGlyph(font, *candidate);
					}
				}
			}
			if (candidate_glyph) {
				CopyResidentRecompositionInput();
				if (m_State.Failed) {
					// Native accepted-candidate failure returns before even attempting publication.
					return;
				}
				MergeOutputClusters(starter, m_State.OutputCount, cluster_level);
				--m_State.OutputCount;
				auto& combined = m_Storage[OutputStorage()][starter];
				combined.GlyphIndex = *candidate;
				SetNormalizationGlyphWord(combined, *candidate_glyph);
				m_State.ScratchFlags = InitializeUnicodeGlyphProperties(std::span<SubstitutionGlyph>(&combined, 1), m_State.ScratchFlags);
				continue;
			}

			if (candidate_allowed || (allow_composition && IsNormalizationMark(current))) {
				if (starter < m_State.OutputCount - 1 && NormalizationCombiningClass(m_Storage[OutputStorage()][last]) > NormalizationCombiningClass(current)) {
					allow_composition = false;
				}
			}
			CopyResidentRecompositionInput();
			const auto copied_last = m_State.OutputCount == 0 ? 0 : m_State.OutputCount - 1;
			if (NormalizationCombiningClass(m_Storage[OutputStorage()][copied_last]) == 0) {
				starter = m_State.OutputCount - 1;
				allow_composition = true;
			}
		}
		static_cast<void>(PublishOutput());
	}

	bool UnicodeNormalizationBuffer::NormalizeNeutral(const resource::SfntFont& font, GlyphClusterLevel cluster_level) {
		const auto input_count = m_State.InputCount;
		static_cast<void>(NormalizeNeutralBeforeRecomposition(font, cluster_level));
		if (input_count != 0) {
			RecomposeNeutralInput(font, cluster_level);
		}

		return !m_State.Failed;
	}

	NormalizedGlyphContent UnicodeNormalizationBuffer::PublishNominalGlyphIndices() {
		for (auto& glyph : std::span<SubstitutionGlyph>(m_Storage[m_InputStorage]).first(m_State.InputCount)) {
			glyph.GlyphIndex = glyph.GlyphProperties | (static_cast<std::uint32_t>(glyph.PrivateState) << 16);
		}

		m_InputMetadata.Content = UnicodeBufferContent::GlyphIndices;

		return NormalizedGlyphContent::GlyphIndices;
	}

	UnicodeNormalizationBufferState UnicodeNormalizationBuffer::State() const {
		return m_State;
	}

	std::span<const SubstitutionGlyph> UnicodeNormalizationBuffer::InputGlyphs() const {
		return std::span<const SubstitutionGlyph>(m_Storage[m_InputStorage]).first(m_State.InputCount);
	}

	std::span<const SubstitutionGlyph> UnicodeNormalizationBuffer::OutputGlyphs() const {
		return std::span<const SubstitutionGlyph>(m_Storage[OutputStorage()]).first(m_State.OutputCount);
	}

} // namespace sgre::text
