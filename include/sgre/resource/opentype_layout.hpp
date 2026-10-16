#pragma once

#include "sgre/resource/sfnt.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::text {
	class VectorFontShapePlanCache;
}

namespace sgre::resource {

	enum class OpenTypeLayoutKind {
		Substitution,
		Positioning
	};

	enum class OpenTypeGlyphClass : std::uint16_t {
		Unclassified = 0,
		Base = 1,
		Ligature = 2,
		Mark = 3,
		Component = 4
	};

	struct OpenTypeLanguageSystem {
		// Empty tag identifies the script's explicit default language system.
		std::string Tag;
		std::optional<std::uint16_t> RequiredFeature;
		std::vector<std::uint16_t> Features;
	};

	struct OpenTypeScript {
		std::string Tag;
		std::optional<OpenTypeLanguageSystem> DefaultLanguage;
		std::vector<OpenTypeLanguageSystem> Languages;
	};

	struct OpenTypeFeature {
		std::string Tag;
		std::uint16_t ParameterOffset = 0;
		std::vector<std::uint16_t> Lookups;
	};

	struct OpenTypeLookup {
		// A repaired null slot retains its index in the actual admitted LookupList.
		bool Present = true;
		std::uint16_t Type = 0;
		std::uint16_t Flags = 0;
		std::optional<std::uint16_t> MarkFilteringSet;
		// Absolute offsets inside the selected borrowed layout table, not inside the sfnt file.
		std::vector<std::uint32_t> SubtableOffsets;
	};

	// Existing callers retain the substitution metadata name; the serialized lookup header is common.
	using OpenTypeSubstitutionLookup = OpenTypeLookup;

	struct OpenTypeScriptSelection {
		std::optional<std::size_t> Index;
		std::string_view Tag;
		bool MatchedRequested = false;
	};

	struct OpenTypeLanguageSelection {
		// No index is the native 0xffff selection of the script's default language system.
		std::optional<std::size_t> Index;
		std::string_view Tag;
		bool MatchedRequested = false;
	};

	struct OpenTypeLigatureMatch {
		std::uint16_t GlyphIndex = 0;
		std::uint16_t ComponentCount = 0;
	};

	struct OpenTypePairAdjustment {
		std::array<std::int16_t, 4> First = {};
		std::array<std::int16_t, 4> Second = {};
		bool HasSecondValue = false;
	};

	class OpenTypeLayoutFont {
	public:
		class AdmittedTablesKey {
		private:
			friend class text::VectorFontShapePlanCache;
			AdmittedTablesKey() = default;
		};
		// The font's immutable bytes must outlive this reader. Metadata belongs to this reader.
		explicit OpenTypeLayoutFont(const SfntFont& font);
		OpenTypeLayoutFont(const SfntFont& font, OpenTypeLayoutKind kind);
		// Only the same-face cache bridge can provide this key and its actual effective table views.
		// This metadata-only reader does not admit lookup execution or retain the face.
		OpenTypeLayoutFont(AdmittedTablesKey, std::uint16_t glyph_count, OpenTypeLayoutKind kind, std::span<const std::uint8_t> layout, std::span<const std::uint8_t> definition);
		[[nodiscard]] OpenTypeLayoutKind Kind() const;
		[[nodiscard]] std::uint32_t FeatureVariationsOffset() const;
		[[nodiscard]] std::span<const OpenTypeLookup> Lookups() const;
		[[nodiscard]] std::span<const OpenTypeScript> Scripts() const;
		[[nodiscard]] std::span<const OpenTypeFeature> Features() const;
		[[nodiscard]] std::span<const OpenTypeSubstitutionLookup> SubstitutionLookups() const;
		[[nodiscard]] std::optional<std::size_t> FindScript(std::string_view tag) const;
		// Native requested-script order, followed by DFLT, dflt, and latn fallbacks.
		[[nodiscard]] OpenTypeScriptSelection SelectScript(std::span<const std::string_view> requested_tags) const;
		// Native exact language, explicit dflt language, then default-system selection.
		[[nodiscard]] OpenTypeLanguageSelection SelectLanguage(std::size_t script_index, std::string_view requested_tag) const;
		// Exact language lookup; an empty language tag requests the explicit default only.
		[[nodiscard]] std::optional<std::size_t> FindFeature(std::size_t script_index, std::string_view language_tag, std::string_view feature_tag) const;
		[[nodiscard]] OpenTypeGlyphClass GlyphClass(std::uint32_t glyph_index) const;
		[[nodiscard]] bool HasGlyphClasses() const;
		[[nodiscard]] std::uint16_t MarkAttachmentClass(std::uint32_t glyph_index) const;
		// Original packed GDEF substitution properties, distinct from Unicode character properties.
		[[nodiscard]] std::uint16_t GlyphProperties(std::uint32_t glyph_index) const;
		// True only for executable flags-zero type1 or type7 with every actual extension targeting type1.
		[[nodiscard]] bool CanApplySingleSubstitution(std::size_t lookup_index) const;
		// Applies one resolved single-substitution lookup; unsupported run/filter/context behavior fails.
		[[nodiscard]] std::optional<std::uint16_t> ApplySingleSubstitution(std::size_t lookup_index, std::uint32_t glyph_index) const;
		// Exact first-input coverage query using the same type/extension/context layout as lookup accelerators.
		// A miss proves that lookup cannot modify this input; a hit does not execute it.
		[[nodiscard]] bool LookupInputCoversGlyph(std::size_t lookup_index, std::uint32_t glyph_index) const;

		// False proves a flags-zero GSUB lookup cannot change one glyph with no preceding/following context.
		// This only excludes covered multi-component ligatures and format3 chains needing missing records.
		// True still requires real lookup execution; it never admits an unsupported substitution.
		[[nodiscard]] bool LookupMayApplyToIsolatedGlyph(std::size_t lookup_index, std::uint32_t glyph_index) const;

		// Consecutive base glyphs only: flags-zero format1 ligatures, original rule order, at most64 components.
		[[nodiscard]] bool CanApplyLigatureSubstitution(std::size_t lookup_index) const;
		[[nodiscard]] std::optional<OpenTypeLigatureMatch> MatchLigatureSubstitution(std::size_t lookup_index, std::span<const std::uint32_t> glyphs, std::size_t& work) const;
		// Flags-zero PairPos formats1/2 without device/variation values; design-unit values retain field order.
		[[nodiscard]] bool CanApplyPairPositioning(std::size_t lookup_index) const;
		[[nodiscard]] std::optional<OpenTypePairAdjustment> MatchPairPositioning(std::size_t lookup_index, std::uint32_t first, std::uint32_t second, std::size_t& work) const;

	private:
		void ReadLayoutTable();
		void ReadDefinitionTable();
		void ReadFeatures(std::size_t offset);
		void ReadScripts(std::size_t offset);
		void ReadLookups(std::size_t offset);
		[[nodiscard]] OpenTypeLanguageSystem ReadLanguage(std::size_t offset, std::string tag);
		void ValidateSingleSubtable(std::size_t offset);
		[[nodiscard]] std::optional<std::uint16_t> ApplySingleSubtable(std::size_t offset, std::uint16_t glyph_index) const;

		OpenTypeLayoutKind m_Kind = OpenTypeLayoutKind::Substitution;
		bool m_AdmittedMetadata = false;
		std::span<const std::uint8_t> m_LayoutBytes;
		std::span<const std::uint8_t> m_DefinitionBytes;
		std::span<const std::uint8_t> m_GlyphClasses;
		std::span<const std::uint8_t> m_MarkAttachmentClasses;
		std::uint16_t m_GlyphCount = 0;
		std::size_t m_RecordCount = 0;
		std::uint32_t m_FeatureVariationsOffset = 0;
		std::vector<OpenTypeScript> m_Scripts;
		std::vector<OpenTypeFeature> m_Features;
		std::vector<OpenTypeLookup> m_Lookups;
	};

} // namespace sgre::resource
