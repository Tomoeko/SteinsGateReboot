#include "sgre/text/shaping_lookup_plan.hpp"

#include "sgre/resource/opentype_layout.hpp"
#include "sgre/text/shaping_font_map.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace sgre::text {

	static constexpr std::size_t kMaximumScheduledLookups = 65536;

	static std::string_view LookupPlanScriptTag(VectorFontScript script) {
		switch (script) {
		case VectorFontScript::Latin:
			return "latn";
		case VectorFontScript::Cyrillic:
			return "cyrl";
		case VectorFontScript::Han:
			return "hani";
		default:
			throw std::runtime_error("VectorFont lookup plan requires the recovered neutral script");
		}
	}

	static std::optional<std::uint16_t> SelectRequiredLookupFeature(const resource::OpenTypeLayoutFont& layout, const VectorFontConfiguration& configuration) {
		const std::array<std::string_view, 2> requested{LookupPlanScriptTag(configuration.Script), "DFLT"};
		const auto script = layout.SelectScript(requested);
		if (!script.Index) {
			return std::nullopt;
		}

		const auto language = layout.SelectLanguage(*script.Index, ResolveVectorFontLayoutLanguageTag(configuration));
		const auto& selected = layout.Scripts()[*script.Index];
		if (language.Index) {
			return selected.Languages[*language.Index].RequiredFeature;
		}
		if (selected.DefaultLanguage) {
			return selected.DefaultLanguage->RequiredFeature;
		}

		return std::nullopt;
	}

	static void AppendFeatureLookups(VectorFontLookupSchedule& schedule, const resource::OpenTypeLayoutFont& layout, std::optional<std::uint16_t> feature_index, std::uint32_t mask, std::uint16_t options) {
		if (!feature_index) {
			return;
		}
		if (*feature_index >= layout.Features().size()) {
			throw std::runtime_error("VectorFont lookup plan feature index exceeds the actual layout table");
		}

		for (const auto lookup_index : layout.Features()[*feature_index].Lookups) {
			// The native append helper ignores indices beyond the table's cached lookup count.
			if (lookup_index >= layout.Lookups().size()) {
				continue;
			}
			if (schedule.Lookups.size() >= kMaximumScheduledLookups) {
				throw std::runtime_error("VectorFont lookup plan exceeds its bounded scheduled lookup count");
			}

			VectorFontScheduledLookup lookup;
			lookup.Index = lookup_index;
			lookup.Options = options;
			lookup.Mask = mask;
			schedule.Lookups.push_back(lookup);
		}
	}

	static void FinishLookupStage(VectorFontLookupSchedule& schedule, std::size_t stage_start) {
		auto stage = std::span(schedule.Lookups).subspan(stage_start);
		std::sort(stage.begin(), stage.end(), [](const auto& first, const auto& second) {
			return first.Index < second.Index;
		});

		std::size_t retained = 0;
		for (const auto lookup : stage) {
			if (retained != 0 && stage[retained - 1].Index == lookup.Index) {
				stage[retained - 1].Mask |= lookup.Mask;
				stage[retained - 1].Options &= lookup.Options;
			} else {
				stage[retained++] = lookup;
			}
		}

		schedule.Lookups.resize(stage_start + retained);
		schedule.StageEnds.push_back(schedule.Lookups.size());
	}

	static VectorFontLookupSchedule CompileLookupSchedule(const VectorFontFeatureMap& map, const resource::OpenTypeLayoutFont& layout, std::optional<std::uint16_t> required_feature) {
		const bool substitution = layout.Kind() == resource::OpenTypeLayoutKind::Substitution;
		const std::uint32_t stage_count = substitution ? 2 : 1;
		const auto required_stage = substitution ? map.RequiredSubstitutionStage : map.RequiredPositioningStage;
		VectorFontLookupSchedule result;
		result.StageEnds.reserve(stage_count);

		for (std::uint32_t stage = 0; stage < stage_count; ++stage) {
			const auto stage_start = result.Lookups.size();
			if (required_feature && required_stage == stage) {
				AppendFeatureLookups(result, layout, required_feature, 2, 3);
			}

			for (const auto& feature : map.Features) {
				const auto feature_stage = substitution ? feature.SubstitutionStage : feature.PositioningStage;
				if (feature_stage != stage) {
					continue;
				}

				const auto feature_index = substitution ? feature.SubstitutionFeature : feature.PositioningFeature;
				const auto options = static_cast<std::uint16_t>((feature.LookupFlags >> 1) & 3);
				AppendFeatureLookups(result, layout, feature_index, feature.Mask, options);
			}

			// Earlier stages retain their own occurrence of the same lookup index.
			FinishLookupStage(result, stage_start);
		}

		return result;
	}

	VectorFontLookupPlan PrepareVectorFontLookupPlan(const resource::SfntFont& font, const VectorFontConfiguration& configuration, std::span<const VectorFontFeatureRequest> requests) {
		VectorFontLookupPlan result;
		result.FeatureMap = PrepareVectorFontFeatureMap(font, configuration, requests);
		const resource::OpenTypeLayoutFont substitutions(font, resource::OpenTypeLayoutKind::Substitution);
		const resource::OpenTypeLayoutFont positioning(font, resource::OpenTypeLayoutKind::Positioning);
		result.Substitutions = CompileLookupSchedule(result.FeatureMap, substitutions, SelectRequiredLookupFeature(substitutions, configuration));
		result.Positioning = CompileLookupSchedule(result.FeatureMap, positioning, SelectRequiredLookupFeature(positioning, configuration));

		return result;
	}

	VectorFontLookupPlan PrepareVectorFontLookupPlan(const resource::OpenTypeLayoutFont& substitutions, const resource::OpenTypeLayoutFont& positioning, const VectorFontConfiguration& configuration, std::span<const VectorFontFeatureRequest> requests) {
		VectorFontLookupPlan result;
		result.FeatureMap = PrepareVectorFontFeatureMap(substitutions, positioning, configuration, requests);
		result.Substitutions = CompileLookupSchedule(result.FeatureMap, substitutions, SelectRequiredLookupFeature(substitutions, configuration));
		result.Positioning = CompileLookupSchedule(result.FeatureMap, positioning, SelectRequiredLookupFeature(positioning, configuration));

		return result;
	}

} // namespace sgre::text
