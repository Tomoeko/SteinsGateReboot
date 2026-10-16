#include "sgre/text/shaping_font_map.hpp"

#include "sgre/resource/opentype_layout.hpp"

#include <array>
#include <stdexcept>
#include <string_view>

namespace sgre::text {

	std::string_view ResolveVectorFontLayoutLanguageTag(const VectorFontConfiguration& configuration) {
		if (!configuration.HasLanguage) {
			return "dflt";
		}
		if (configuration.Language == "en") {
			return "ENG ";
		}
		if (configuration.Language == "ja") {
			return "JAN ";
		}

		throw std::runtime_error("VectorFont layout language conversion requires the recovered canonical en/ja profile");
	}

	static std::string_view NeutralFontScriptTag(VectorFontScript script) {
		switch (script) {
		case VectorFontScript::Latin:
			return "latn";
		case VectorFontScript::Cyrillic:
			return "cyrl";
		case VectorFontScript::Han:
			return "hani";
		default:
			throw std::runtime_error("VectorFont feature map requires the recovered neutral script");
		}
	}

	static VectorFontFeatureTable PrepareSelectedFontTable(const resource::OpenTypeLayoutFont& layout, std::string_view script_tag, std::string_view language_tag) {
		if (layout.FeatureVariationsOffset() != 0) {
			throw std::runtime_error("VectorFont feature map requires unsupported layout feature variations");
		}

		VectorFontFeatureTable result;
		result.FeatureTags.reserve(layout.Features().size());
		for (const auto& feature : layout.Features()) {
			result.FeatureTags.push_back(feature.Tag);
		}

		const std::array<std::string_view, 2> requested{script_tag, "DFLT"};
		const auto script = layout.SelectScript(requested);
		if (!script.Index) {
			return result;
		}

		const auto language = layout.SelectLanguage(*script.Index, language_tag);
		const auto& selected = layout.Scripts()[*script.Index];
		if (language.Index) {
			const auto& system = selected.Languages[*language.Index];
			result.SelectedFeatures = system.Features;
			result.RequiredFeature = system.RequiredFeature;
		} else if (selected.DefaultLanguage) {
			result.SelectedFeatures = selected.DefaultLanguage->Features;
			result.RequiredFeature = selected.DefaultLanguage->RequiredFeature;
		}

		return result;
	}

	VectorFontFeatureMap PrepareVectorFontFeatureMap(const resource::SfntFont& font, const VectorFontConfiguration& configuration, std::span<const VectorFontFeatureRequest> requests) {
		const auto script_tag = NeutralFontScriptTag(configuration.Script);

		const auto collected = CollectVectorFontFeatures(configuration, requests);
		const resource::OpenTypeLayoutFont substitutions(font, resource::OpenTypeLayoutKind::Substitution);
		const resource::OpenTypeLayoutFont positioning(font, resource::OpenTypeLayoutKind::Positioning);
		const auto language_tag = ResolveVectorFontLayoutLanguageTag(configuration);
		const auto substitution_table = PrepareSelectedFontTable(substitutions, script_tag, language_tag);
		const auto positioning_table = PrepareSelectedFontTable(positioning, script_tag, language_tag);

		return CompileVectorFontFeatureMasks(collected, substitution_table, positioning_table);
	}

	VectorFontFeatureMap PrepareVectorFontFeatureMap(const resource::OpenTypeLayoutFont& substitutions, const resource::OpenTypeLayoutFont& positioning, const VectorFontConfiguration& configuration, std::span<const VectorFontFeatureRequest> requests) {
		if (substitutions.Kind() != resource::OpenTypeLayoutKind::Substitution || positioning.Kind() != resource::OpenTypeLayoutKind::Positioning) {
			throw std::runtime_error("VectorFont feature map requires its actual substitution/positioning readers");
		}

		const auto script_tag = NeutralFontScriptTag(configuration.Script);

		const auto collected = CollectVectorFontFeatures(configuration, requests);
		const auto language_tag = ResolveVectorFontLayoutLanguageTag(configuration);
		const auto substitution_table = PrepareSelectedFontTable(substitutions, script_tag, language_tag);
		const auto positioning_table = PrepareSelectedFontTable(positioning, script_tag, language_tag);

		return CompileVectorFontFeatureMasks(collected, substitution_table, positioning_table);
	}

} // namespace sgre::text
