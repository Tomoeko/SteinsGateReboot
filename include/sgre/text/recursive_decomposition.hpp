#pragma once

#include "sgre/resource/sfnt.hpp"
#include "sgre/text/direction_adjustment.hpp"
#include "sgre/text/initial_clusters.hpp"
#include "sgre/text/single_substitution.hpp"
#include "sgre/text/unicode_preprocess.hpp"
#include "sgre/text/variation_glyph.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace sgre::text {

	class GlyphDefinitionBlob;

	struct VectorFontFeatureMap;
	struct VectorFontFeatureRequest;
	struct NeutralGlyphPreparationContext;

	enum class NormalizedGlyphContent : std::uint32_t {
		GlyphIndices = 2,
	};

	enum class UnicodeDecompositionPreference : std::uint8_t {
		RecursiveFirst = 0,
		MappedFirst = 1,
	};

	struct GlyphPositionRow {
		std::int32_t XAdvance26Dot6 = 0;
		std::int32_t YAdvance26Dot6 = 0;
		std::int32_t XOffset26Dot6 = 0;
		std::int32_t YOffset26Dot6 = 0;
		// Original fifth DWORD belongs to positioning/attachment state, not padding.
		std::uint32_t PrivateWord = 0;
		bool operator==(const GlyphPositionRow&) const = default;
	};

	struct UnicodeNormalizationBufferState {
		std::uint32_t InputCursor = 0;
		std::uint32_t InputCount = 0;
		std::uint32_t OutputCount = 0;
		std::uint32_t ResidentCapacity = 0;
		std::uint32_t MaximumLength = 0;
		UnicodeBufferScratchFlags ScratchFlags = UnicodeBufferScratchFlags::None;
		bool OutputActive = false;
		bool PositionsActive = false;
		bool Failed = false;
		bool OutputAliasesInput = true;
	};

	enum class UnicodeBufferContent : std::uint32_t {
		Unset = 0,
		Unicode = 1,
		GlyphIndices = 2,
	};

	struct UnicodeBufferInputMetadata {
		UnicodeBufferContent Content = UnicodeBufferContent::Unset;
		std::uint32_t TextFlags = 0;
		std::uint32_t ReplacementCodepoint = 0xfffd;
		std::uint32_t MaximumOperations = 0x1fffffff;
		std::uint32_t DirectionWord = 0;
		std::uint32_t ScriptTag = 0;
		std::array<std::uint32_t, 5> PrecedingContext = {};
		std::array<std::uint32_t, 5> FollowingContext = {};
		std::uint32_t PrecedingContextCount = 0;
		std::uint32_t FollowingContextCount = 0;
		GlyphClusterLevel ClusterLevel = GlyphClusterLevel::MonotoneGraphemes;
		std::uint32_t InputAllocationSlots = 0;
		std::uint32_t AuxiliaryAllocationSlots = 0;
	};

	struct InitialDottedCircleContext {
		std::uint32_t TextFlags = 0;
		std::uint32_t PrecedingContextLength = 0;
	};

	// Owns both actual record arrays. The explicit resident constructor reserves MaximumLength + 1 slots; the default owner uses recovered paired growth.
	// The font and immutable resource bytes passed to a query must remain alive throughout that call.
	class UnicodeNormalizationBuffer {
	public:
		UnicodeNormalizationBuffer();
		// Engine-default UTF16 ingestion retains original code-unit cluster offsets and five scalar contexts.
		// The checked subrange is explicit; native negative-length/string scanning variants are outside this API.
		void AddUtf16(std::u16string_view text, std::uint32_t item_offset, std::uint32_t item_length);
		// Clear preserves limits, allocated arrays, text flags and replacement policy, as the real engine does.
		// Clear/reset retain storage but empty the live ranges; reacquire borrowed spans.
		void ClearInput();
		void ResetInput();
		void SetInputDirection(GlyphShapingDirection direction);
		void SetInputScript(std::uint32_t script_tag);
		void SetInputGrowthLimits(std::uint32_t maximum_length, std::uint32_t maximum_operations);
		void InitializeInputUnicodeProperties();
		void InitializeInputClusters(GlyphClusterLevel level);
		[[nodiscard]] GlyphShapingDirection AdjustInputDirection();
		// Each adopted array invalidates its spans, even when the paired allocation fails.
		[[nodiscard]] bool EnsureInputCapacity(std::uint32_t requested);
		[[nodiscard]] bool UsesAllocatorGrowth() const;
		[[nodiscard]] UnicodeBufferInputMetadata InputMetadata() const;
		explicit UnicodeNormalizationBuffer(std::span<const SubstitutionGlyph> glyphs, std::uint32_t maximum_length, UnicodeBufferScratchFlags scratch_flags = UnicodeBufferScratchFlags::None);
		UnicodeNormalizationBuffer(const UnicodeNormalizationBuffer&) = delete;
		UnicodeNormalizationBuffer& operator=(const UnicodeNormalizationBuffer&) = delete;
		UnicodeNormalizationBuffer(UnicodeNormalizationBuffer&&) = delete;
		UnicodeNormalizationBuffer& operator=(UnicodeNormalizationBuffer&&) = delete;
		// Actual pre-cluster phase; requires a live first record when its text/context gates permit insertion.
		void InsertInitialDottedCircle(const resource::SfntFont& font, const InitialDottedCircleContext& context);
		// Uses the same auxiliary allocation; clear preserves input records/cursor/failure/tail/capacity.
		// These storage operations do not publish glyph content or prove any shaping phase complete.
		void ClearPositionRows();
		// Actual unindexed lazy-access transition, including an empty live range; exposes no pointer or span.
		void EnsurePositionRows();
		// Checked value access lazily clears live positions once. Values add no owner or borrowed alias.
		[[nodiscard]] GlyphPositionRow PositionRow(std::uint32_t index);
		// Explicit storage write requires active positions; callers supply their actual positioning result.
		void SetPositionRow(std::uint32_t index, const GlyphPositionRow& row);
		void BeginOutput();
		void CopyCurrentInput();
		// Runs the recovered canonical helper and consumes one input iff its logical emission count is nonzero.
		[[nodiscard]] std::uint32_t TryDecomposeCurrentInput(const resource::SfntFont& font, UnicodeDecompositionPreference preference);
		// Failure suppresses pointer/count publication, preserving the partially written arrays for inspection.
		[[nodiscard]] bool PublishOutput();
		// Requires the caller's existing Unicode character-state words. Stops before recomposition.
		[[nodiscard]] bool NormalizeNeutralBeforeRecomposition(const resource::SfntFont& font, GlyphClusterLevel cluster_level = GlyphClusterLevel::MonotoneGraphemes);

		// Actual neutral mode2 through recomposition. GlyphIndex still contains codepoints on return.
		[[nodiscard]] bool NormalizeNeutral(const resource::SfntFont& font, GlyphClusterLevel cluster_level = GlyphClusterLevel::MonotoneGraphemes);
		// Call only after the original intervening fraction/feature/mark-class phases; returns their final content tag.
		[[nodiscard]] NormalizedGlyphContent PublishNominalGlyphIndices();
		// Actual GDEF entry and its missing-class continuation after final glyph-index publication.
		void InitializeGlyphDefinitionProperties(const GlyphDefinitionBlob& definition, NormalizedGlyphContent content);
		[[nodiscard]] UnicodeNormalizationBufferState State() const;
		// Views borrow this scoped owner; emission changes their contents and growth may invalidate them.
		// Reacquire them after growth, clear/reset or publication.
		[[nodiscard]] std::span<const SubstitutionGlyph> InputGlyphs() const;
		[[nodiscard]] std::span<const SubstitutionGlyph> OutputGlyphs() const;

	private:
		friend class VectorFont;
		friend struct UnicodeBufferPositionTestAccess;
		friend NormalizedGlyphContent PrepareNeutralGlyphIndices(UnicodeNormalizationBuffer& buffer, const resource::SfntFont& font, const VectorFontFeatureMap& features, const NeutralGlyphPreparationContext& context, std::span<const VectorFontFeatureRequest> requests);

		void RequireCurrentInput() const;
		[[nodiscard]] std::size_t OutputStorage() const;
		[[nodiscard]] bool PrepareEmission(std::uint32_t consumed, std::uint32_t produced);
		void EmitDecomposedGlyph(std::uint32_t codepoint, std::uint32_t glyph_index);
		[[nodiscard]] std::uint32_t EmitCanonicalDecomposition(const resource::SfntFont& font, std::uint32_t codepoint, UnicodeDecompositionPreference preference, std::uint32_t depth);

		void MapCurrentInput(const resource::SfntFont& font, UnicodeDecompositionPreference preference);
		void MapNominalCurrentInput(const resource::SfntFont& font);
		void MapSelectorSegment(const resource::SfntFont& font, const VectorFontVariationMap& variations, std::uint32_t end, GlyphClusterLevel cluster_level);
		void MergeInputClusters(std::size_t first, std::size_t last, GlyphClusterLevel cluster_level);
		void ReplaceVariationPair(GlyphClusterLevel cluster_level);
		void ReorderCurrentCombiningInput(GlyphClusterLevel cluster_level);
		void MergeOutputClusters(std::size_t first, std::size_t last, GlyphClusterLevel cluster_level);
		void CopyResidentRecompositionInput();
		void RecomposeNeutralInput(const resource::SfntFont& font, GlyphClusterLevel cluster_level);

		std::array<std::vector<SubstitutionGlyph>, 2> m_Storage;
		std::size_t m_InputStorage = 0;
		UnicodeNormalizationBufferState m_State;
		UnicodeBufferInputMetadata m_InputMetadata;
		bool m_AllocatorGrowth = false;
	};

} // namespace sgre::text
