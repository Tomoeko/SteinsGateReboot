#include "sgre/text/vector_font.hpp"

#include "sgre/resource/opentype_layout.hpp"
#include "sgre/text/blob_font_layout_cache.hpp"
#include "sgre/text/recursive_decomposition.hpp"
#include "sgre/text/shaping_font_map.hpp"
#include "sgre/text/shaping_lookup_plan.hpp"
#include "sgre/text/unicode_properties.hpp"
#include "sgre/text/vector_font_normalization_font.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace sgre::text {

	static std::uint32_t UnitScriptTag(VectorFontScript script) {
		switch (script) {
		case VectorFontScript::Latin:
			return 0x4c61746e;
		case VectorFontScript::Cyrillic:
			return 0x4379726c;
		case VectorFontScript::Han:
			return 0x48616e69;
		default:
			throw std::runtime_error("VectorFont unit measurement requires the recovered neutral script");
		}
	}

	static void RequireUnchangedUnitLayout(const resource::SfntFont& font, std::string_view tag, std::span<const std::uint8_t> actual_effective) {
		const auto original = font.FindTable(tag).value_or(std::span<const std::uint8_t>{});
		if (original.size() != actual_effective.size() || (!original.empty() && original.data() != actual_effective.data())) {
			throw std::runtime_error("VectorFont unit measurement requires original clean effective layout tables");
		}
	}

	static void RequireUnitCharacter(const SubstitutionGlyph& glyph) {
		const auto category = glyph.CharacterState & 0x1fU;
		if ((glyph.CharacterState & 0x20U) != 0 || category <= static_cast<std::uint32_t>(UnicodeGeneralCategory::Surrogate) || (category >= static_cast<std::uint32_t>(UnicodeGeneralCategory::SpacingMark) && category <= static_cast<std::uint32_t>(UnicodeGeneralCategory::NonSpacingMark))) {
			throw std::runtime_error("VectorFont unit measurement mark/control/default-ignorable positioning remains unrecovered");
		}
	}

	resource::GlyphSize VectorFont::MeasureCodeUnit(std::uint16_t code_unit) {
		if (m_Configuration.Direction != VectorFontDirection::LeftToRight) {
			throw std::runtime_error("VectorFont unit measurement requires the recovered left-to-right single-base-glyph domain");
		}
		const auto script = UnitScriptTag(m_Configuration.Script);
		static_cast<void>(ResolveVectorFontLayoutLanguageTag(m_Configuration));
		if (!m_MetricServices) {
			BindDefaultMetricServices();
		}
		auto& normalization = *m_NormalizationFont;
		auto& face = normalization.Face();
		if (!face.PrepareLayout() || !face.LayoutCache()) {
			throw std::runtime_error("VectorFont unit measurement requires its actual prepared layout cache");
		}
		const auto& cache = *face.LayoutCache();
		RequireUnchangedUnitLayout(m_Font, "GSUB", cache.Substitution().Bytes());
		RequireUnchangedUnitLayout(m_Font, "GPOS", cache.Positioning().Bytes());
		const auto& definition = cache.Definition();
		RequireUnchangedUnitLayout(m_Font, "GDEF", definition.IsSuppressed() ? std::span<const std::uint8_t>{} : definition.Bytes());
		const resource::OpenTypeLayoutFont substitutions(m_Font, resource::OpenTypeLayoutKind::Substitution);
		const resource::OpenTypeLayoutFont positioning(m_Font, resource::OpenTypeLayoutKind::Positioning);
		const auto plan = PrepareVectorFontLookupPlan(substitutions, positioning, m_Configuration, {});
		auto& buffer = *m_InputBuffer;
		buffer.ClearInput();
		buffer.SetInputDirection(GlyphShapingDirection::LeftToRight);
		buffer.SetInputScript(script);
		const auto unit = static_cast<char16_t>(code_unit);
		buffer.AddUtf16(std::u16string_view(&unit, 1), 0, 1);
		buffer.InitializeInputUnicodeProperties();
		if (buffer.InputGlyphs().size() != 1) {
			throw std::runtime_error("VectorFont unit measurement requires one actual Unicode input record");
		}
		// Native3f3d80 assigns the compiled global mask before cluster/direction/neutral phases.
		buffer.m_Storage[buffer.m_InputStorage].front().Mask = plan.FeatureMap.GlobalMask;
		RequireUnitCharacter(buffer.InputGlyphs().front());
		buffer.InitializeInputClusters(GlyphClusterLevel::MonotoneGraphemes);
		NeutralGlyphPreparationContext context;
		context.BufferDirection = buffer.AdjustInputDirection();
		static_cast<void>(normalization.PrepareNeutralGlyphs(buffer, plan.FeatureMap, context));
		if (buffer.InputGlyphs().size() != 1 || buffer.State().Failed) {
			throw std::runtime_error("VectorFont unit measurement normalization requires one actual resulting base glyph");
		}
		auto glyphs = std::span<SubstitutionGlyph>(buffer.m_Storage[buffer.m_InputStorage]).first(1);
		RequireUnitCharacter(glyphs.front());
		for (const auto& lookup : plan.Substitutions.Lookups) {
			auto& glyph = glyphs.front();
			if ((glyph.Mask & lookup.Mask) == 0) {
				continue;
			}
			if (substitutions.CanApplySingleSubstitution(lookup.Index)) {
				static_cast<void>(ApplyDirectSingleSubstitution(substitutions, lookup, glyphs));
			} else if (substitutions.LookupMayApplyToIsolatedGlyph(lookup.Index, glyph.GlyphIndex)) {
				throw std::runtime_error("VectorFont unit measurement requires unrecovered covered substitution execution");
			}
		}
		const auto& glyph = glyphs.front();
		if (positioning.GlyphClass(glyph.GlyphIndex) == resource::OpenTypeGlyphClass::Mark) {
			throw std::runtime_error("VectorFont unit measurement GDEF mark positioning remains unrecovered");
		}
		for (const auto& lookup : plan.Positioning.Lookups) {
			// Original pair execution cannot find a second input in this actual one-record buffer.
			// Other covered positioning remains gated; this does not admit a multi-unit run.
			if (positioning.Lookups()[lookup.Index].Type == 2) {
				continue;
			}
			if ((glyph.Mask & lookup.Mask) != 0 && positioning.LookupInputCoversGlyph(lookup.Index, glyph.GlyphIndex)) {
				throw std::runtime_error("VectorFont unit measurement requires unrecovered covered positioning execution");
			}
		}
		buffer.ClearPositionRows();
		buffer.EnsurePositionRows();
		GlyphPositionRow row;
		row.XAdvance26Dot6 = m_MetricServices->HorizontalAdvance(glyph.GlyphIndex);
		buffer.SetPositionRow(0, row);
		// Native088320 sums signed 26.6 advances after shaping, separately on each axis.
		resource::GlyphSize result;
		result.Width += static_cast<float>(row.XAdvance26Dot6) * (1.0f / 64.0f);
		result.Height += static_cast<float>(row.YAdvance26Dot6) * (1.0f / 64.0f);
		return result;
	}

} // namespace sgre::text
