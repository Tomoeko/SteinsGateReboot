#include "sgre/text/vector_font.hpp"

#include "sgre/resource/opentype_layout.hpp"
#include "sgre/text/blob_font_layout_cache.hpp"
#include "sgre/text/recursive_decomposition.hpp"
#include "sgre/text/shaping_font_map.hpp"
#include "sgre/text/shaping_lookup_plan.hpp"
#include "sgre/text/unicode_properties.hpp"
#include "sgre/text/vector_font_normalization_font.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumRunInputUnits = 4096;
	static constexpr std::size_t kMaximumRunLayoutWork = 1024 * 1024;

	static void ChargeRunLayoutWork(std::size_t& work, std::size_t added) {
		if (work > kMaximumRunLayoutWork || added > kMaximumRunLayoutWork - work) {
			throw std::runtime_error("VectorFont whole-run layout work exceeds its preparation limit");
		}
		work += added;
	}

	static std::uint32_t RunScriptTag(VectorFontScript script) {
		switch (script) {
		case VectorFontScript::Latin:
			return 0x4c61746e;
		case VectorFontScript::Cyrillic:
			return 0x4379726c;
		case VectorFontScript::Han:
			return 0x48616e69;
		default:
			throw std::runtime_error("VectorFont whole-run shaping requires the recovered neutral script");
		}
	}

	static void RequireWholeRunBase(const SubstitutionGlyph& glyph) {
		const auto category = glyph.CharacterState & 0x1fU;
		if ((glyph.CharacterState & 0x20U) != 0 || category <= static_cast<std::uint32_t>(UnicodeGeneralCategory::Surrogate) || (category >= static_cast<std::uint32_t>(UnicodeGeneralCategory::SpacingMark) && category <= static_cast<std::uint32_t>(UnicodeGeneralCategory::NonSpacingMark)) || (glyph.GlyphProperties & 8U) != 0) {
			throw std::runtime_error("VectorFont whole-run retained mark/control/default-ignorable positioning remains unrecovered");
		}
	}

	static void RequireOriginalRunLayout(const resource::SfntFont& font, std::string_view tag, std::span<const std::uint8_t> effective) {
		const auto original = font.FindTable(tag).value_or(std::span<const std::uint8_t>{});
		if (original.size() != effective.size() || (!original.empty() && original.data() != effective.data())) {
			throw std::runtime_error("VectorFont whole-run shaping requires its original clean effective layout tables");
		}
	}

	static void ApplyRunLigatures(const resource::OpenTypeLayoutFont& layout, const VectorFontScheduledLookup& lookup, std::vector<SubstitutionGlyph>& glyphs, std::size_t& work, std::uint32_t& ligature_serial) {
		std::vector<std::uint32_t> indices;
		indices.reserve(glyphs.size());
		for (const auto& glyph : glyphs) {
			indices.push_back(glyph.GlyphIndex);
		}
		for (std::size_t index = 0; index < glyphs.size(); ++index) {
			ChargeRunLayoutWork(work, 1);
			if ((glyphs[index].Mask & lookup.Mask) == 0) {
				continue;
			}
			const auto match = layout.MatchLigatureSubstitution(lookup.Index, std::span<const std::uint32_t>(indices).subspan(index), work);
			if (!match) {
				continue;
			}
			for (std::size_t component = 0; component < match->ComponentCount; ++component) {
				if ((glyphs[index + component].Mask & lookup.Mask) == 0 || (match->ComponentCount > 1 && (glyphs[index + component].PrivateState & 0xffU) != 0)) {
					throw std::runtime_error("VectorFont whole-run ligature requires consecutive enabled base components without prior ligature attachment state");
				}
			}
			auto& first = glyphs[index];
			for (std::size_t component = 1; component < match->ComponentCount; ++component) {
				first.Cluster = std::min(first.Cluster, glyphs[index + component].Cluster);
			}
			first.GlyphIndex = match->GlyphIndex;
			if (match->ComponentCount == 1) {
				if (layout.HasGlyphClasses()) {
					first.GlyphProperties = static_cast<std::uint16_t>((first.GlyphProperties & 0x60U) | layout.GlyphProperties(match->GlyphIndex) | 0x10U);
				}
			} else {
				const auto properties = layout.HasGlyphClasses() ? layout.GlyphProperties(match->GlyphIndex) : 4U;
				first.GlyphProperties = static_cast<std::uint16_t>(properties | 0x30U);
				auto identifier = ligature_serial++ & 7U;
				while (identifier == 0) {
					identifier = ligature_serial++ & 7U;
				}
				first.PrivateState = static_cast<std::uint16_t>((first.PrivateState & 0xff00U) | (identifier << 5) | (match->ComponentCount & 0xfU) | 0x10U);
			}
			const auto last = index + match->ComponentCount;
			glyphs.erase(glyphs.begin() + static_cast<std::ptrdiff_t>(index + 1), glyphs.begin() + static_cast<std::ptrdiff_t>(last));
			indices[index] = match->GlyphIndex;
			indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(index + 1), indices.begin() + static_cast<std::ptrdiff_t>(last));
		}
	}

	static std::int32_t ScaleRunLayoutValue(std::int16_t value, std::int32_t scale, std::uint16_t units_per_em) {
		if (units_per_em == 0) {
			throw std::runtime_error("VectorFont whole-run layout requires actual nonzero units per em");
		}
		// Native4027C0/402820 round the signed product before truncating division.
		const auto product = std::int64_t(value) * scale;
		const auto half = std::int64_t(units_per_em / 2);
		return static_cast<std::int32_t>((product + (product < 0 ? -half : half)) / units_per_em);
	}

	static void ApplyRunPairValues(GlyphPositionRow& row, const std::array<std::int16_t, 4>& values, std::uint16_t units_per_em) {
		// Fresh native CFF and TrueType construction publishes HB scales0/0; FT SetSize never changes them.
		// Live flags-2 advance callbacks remain independently sized. Device/variation values are not admitted.
		row.XOffset26Dot6 += ScaleRunLayoutValue(values[0], 0, units_per_em);
		row.YOffset26Dot6 += ScaleRunLayoutValue(values[1], 0, units_per_em);
		row.XAdvance26Dot6 += ScaleRunLayoutValue(values[2], 0, units_per_em);
	}

	OwnedTextGlyphRun VectorFont::PrepareRun(std::u16string_view text) {
		if (text.size() > kMaximumRunInputUnits || m_Configuration.Direction != VectorFontDirection::LeftToRight) {
			throw std::runtime_error("VectorFont whole-run shaping requires bounded left-to-right input");
		}
		const auto script = RunScriptTag(m_Configuration.Script);
		static_cast<void>(ResolveVectorFontLayoutLanguageTag(m_Configuration));
		if (!m_MetricServices) {
			BindDefaultMetricServices();
		}
		if (!m_DefaultLayoutScale) {
			throw std::runtime_error("VectorFont whole-run layout requires actual fresh-owner zero HB scale provenance");
		}
		auto& normalization = *m_NormalizationFont;
		auto& face = normalization.Face();
		if (!face.PrepareLayout() || !face.LayoutCache()) {
			throw std::runtime_error("VectorFont whole-run shaping requires its actual prepared layout cache");
		}
		const auto& cache = *face.LayoutCache();
		RequireOriginalRunLayout(m_Font, "GSUB", cache.Substitution().Bytes());
		RequireOriginalRunLayout(m_Font, "GPOS", cache.Positioning().Bytes());
		const auto& definition = cache.Definition();
		RequireOriginalRunLayout(m_Font, "GDEF", definition.IsSuppressed() ? std::span<const std::uint8_t>{} : definition.Bytes());
		const resource::OpenTypeLayoutFont substitutions(m_Font, resource::OpenTypeLayoutKind::Substitution);
		const resource::OpenTypeLayoutFont positioning(m_Font, resource::OpenTypeLayoutKind::Positioning);
		const auto plan = PrepareVectorFontLookupPlan(substitutions, positioning, m_Configuration, {});
		auto& buffer = *m_InputBuffer;
		buffer.ClearInput();
		buffer.SetInputDirection(GlyphShapingDirection::LeftToRight);
		buffer.SetInputScript(script);
		buffer.AddUtf16(text, 0, static_cast<std::uint32_t>(text.size()));
		buffer.InitializeInputUnicodeProperties();
		for (auto& glyph : std::span<SubstitutionGlyph>(buffer.m_Storage[buffer.m_InputStorage]).first(buffer.m_State.InputCount)) {
			glyph.Mask = plan.FeatureMap.GlobalMask;
		}
		buffer.InitializeInputClusters(GlyphClusterLevel::MonotoneGraphemes);
		NeutralGlyphPreparationContext context;
		context.BufferDirection = buffer.AdjustInputDirection();
		static_cast<void>(normalization.PrepareNeutralGlyphs(buffer, plan.FeatureMap, context));
		if (buffer.State().Failed) {
			throw std::runtime_error("VectorFont whole-run Unicode normalization failed");
		}
		std::vector<SubstitutionGlyph> glyphs(buffer.InputGlyphs().begin(), buffer.InputGlyphs().end());
		for (const auto& glyph : glyphs) {
			RequireWholeRunBase(glyph);
		}
		std::size_t work = 0;
		std::uint32_t ligature_serial = 0;
		for (const auto& lookup : plan.Substitutions.Lookups) {
			ChargeRunLayoutWork(work, glyphs.size());
			if (substitutions.CanApplySingleSubstitution(lookup.Index)) {
				static_cast<void>(ApplyDirectSingleSubstitution(substitutions, lookup, glyphs));
			} else if (substitutions.CanApplyLigatureSubstitution(lookup.Index)) {
				ApplyRunLigatures(substitutions, lookup, glyphs, work, ligature_serial);
			} else {
				for (const auto& glyph : glyphs) {
					if ((glyph.Mask & lookup.Mask) != 0 && substitutions.LookupInputCoversGlyph(lookup.Index, glyph.GlyphIndex)) {
						throw std::runtime_error("VectorFont whole-run covered substitution/filter/context execution remains unrecovered");
					}
				}
			}
		}
		std::vector<GlyphPositionRow> rows(glyphs.size());
		for (std::size_t index = 0; index < glyphs.size(); ++index) {
			RequireWholeRunBase(glyphs[index]);
			rows[index].XAdvance26Dot6 = m_MetricServices->HorizontalAdvance(glyphs[index].GlyphIndex);
		}
		for (const auto& lookup : plan.Positioning.Lookups) {
			for (std::size_t index = 0; index < glyphs.size(); ++index) {
				ChargeRunLayoutWork(work, 1);
				if ((glyphs[index].Mask & lookup.Mask) == 0) {
					continue;
				}
				if (positioning.CanApplyPairPositioning(lookup.Index)) {
					if (index + 1 < glyphs.size() && (glyphs[index + 1].Mask & lookup.Mask) != 0) {
						const auto pair = positioning.MatchPairPositioning(lookup.Index, glyphs[index].GlyphIndex, glyphs[index + 1].GlyphIndex, work);
						if (pair) {
							ApplyRunPairValues(rows[index], pair->First, m_Font.UnitsPerEm());
							ApplyRunPairValues(rows[index + 1], pair->Second, m_Font.UnitsPerEm());
							if (pair->HasSecondValue) {
								++index;
							}
						}
					}
				} else if (positioning.LookupInputCoversGlyph(lookup.Index, glyphs[index].GlyphIndex)) {
					throw std::runtime_error("VectorFont whole-run covered positioning/filter/context execution remains unrecovered");
				}
			}
		}
		std::copy(glyphs.begin(), glyphs.end(), buffer.m_Storage[buffer.m_InputStorage].begin());
		buffer.m_State.InputCount = static_cast<std::uint32_t>(glyphs.size());
		buffer.ClearPositionRows();
		std::vector<ShapedTextGlyph> shaped;
		shaped.reserve(glyphs.size());
		for (std::size_t index = 0; index < glyphs.size(); ++index) {
			buffer.SetPositionRow(static_cast<std::uint32_t>(index), rows[index]);
			const auto& row = rows[index];
			shaped.push_back({glyphs[index].GlyphIndex, row.XAdvance26Dot6, row.YAdvance26Dot6, row.XOffset26Dot6, row.YOffset26Dot6});
		}
		OwnedTextGlyphRun result;
		result.Glyphs.resize(shaped.size());
		result.Advance = PrepareTextGlyphRun(shaped, result.Glyphs);
		return result;
	}

} // namespace sgre::text
