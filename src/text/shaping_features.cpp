#include "sgre/text/shaping_features.hpp"

#include <array>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sgre::text {

	static constexpr std::size_t kMaximumCollectedFeatures = 65536;

	static void ValidateFeatureTag(std::string_view tag) {
		if (tag.size() != 4) {
			throw std::runtime_error("VectorFont feature tag must have four ASCII characters");
		}

		for (const auto character : tag) {
			if (character < ' ' || character > '~') {
				throw std::runtime_error("VectorFont feature tag must have four ASCII characters");
			}
		}
	}

	static void AppendCollectedFeature(std::vector<VectorFontCollectedFeature>& features, std::string_view tag, std::uint32_t value, VectorFontFeatureFlags flags, std::uint32_t substitution_stage) {
		VectorFontCollectedFeature feature;
		feature.Tag = tag;
		feature.Order = static_cast<std::uint32_t>(features.size() + 1);
		feature.Value = value;
		feature.Flags = flags;
		feature.GlobalValue = (static_cast<std::uint32_t>(flags) & 1) != 0 ? value : 0;
		feature.SubstitutionStage = substitution_stage;
		features.push_back(std::move(feature));
	}

	std::vector<VectorFontCollectedFeature> CollectVectorFontFeatures(const VectorFontConfiguration& configuration, std::span<const VectorFontFeatureRequest> requests, std::size_t maximum_features) {
		if (configuration.Script != VectorFontScript::Latin && configuration.Script != VectorFontScript::Cyrillic && configuration.Script != VectorFontScript::Han) {
			throw std::runtime_error("VectorFont feature collection requires the recovered neutral shaper");
		}

		const bool horizontal = configuration.Direction == VectorFontDirection::LeftToRight || configuration.Direction == VectorFontDirection::RightToLeft;
		if (!horizontal && configuration.Direction != VectorFontDirection::TopToBottom) {
			throw std::runtime_error("VectorFont feature direction is outside its recovered range");
		}

		const std::size_t default_count = horizontal ? 17 : 10;
		if (maximum_features > kMaximumCollectedFeatures || default_count > maximum_features || requests.size() > maximum_features - default_count) {
			throw std::runtime_error("VectorFont collected feature limit exceeded");
		}

		for (const auto& request : requests) {
			ValidateFeatureTag(request.Tag);
		}

		std::vector<VectorFontCollectedFeature> features;
		features.reserve(default_count + requests.size());
		AppendCollectedFeature(features, "rvrn", 1, VectorFontFeatureFlags::Global, 0);
		// The original null GSUB pause ends stage zero before direction/default features.
		if (configuration.Direction == VectorFontDirection::LeftToRight) {
			AppendCollectedFeature(features, "ltra", 1, VectorFontFeatureFlags::Global, 1);
			AppendCollectedFeature(features, "ltrm", 1, VectorFontFeatureFlags::Global, 1);
		} else if (configuration.Direction == VectorFontDirection::RightToLeft) {
			AppendCollectedFeature(features, "rtla", 1, VectorFontFeatureFlags::Global, 1);
			AppendCollectedFeature(features, "rtlm", 1, VectorFontFeatureFlags::PerCluster, 1);
		}

		static constexpr std::array<std::string_view, 3> kFractionFeatures{"frac", "numr", "dnom"};
		for (const auto tag : kFractionFeatures) {
			AppendCollectedFeature(features, tag, 1, VectorFontFeatureFlags::PerCluster, 1);
		}

		static constexpr std::array<std::string_view, 5> kCommonFeatures{"ccmp", "locl", "mark", "mkmk", "rlig"};
		for (const auto tag : kCommonFeatures) {
			AppendCollectedFeature(features, tag, 1, VectorFontFeatureFlags::Global, 1);
		}

		if (horizontal) {
			static constexpr std::array<std::string_view, 6> kHorizontalFeatures{"calt", "clig", "curs", "kern", "liga", "rclt"};
			for (const auto tag : kHorizontalFeatures) {
				const auto flags = tag == "kern" ? VectorFontFeatureFlags::GlobalWithFallback : VectorFontFeatureFlags::Global;
				AppendCollectedFeature(features, tag, 1, flags, 1);
			}
		} else {
			AppendCollectedFeature(features, "vert", 1, VectorFontFeatureFlags::GlobalWithTableSearch, 1);
		}

		for (const auto& request : requests) {
			const auto flags = request.StartCluster == 0 && request.EndCluster == 0xffffffff ? VectorFontFeatureFlags::Global : VectorFontFeatureFlags::PerCluster;
			AppendCollectedFeature(features, request.Tag, request.Value, flags, 1);
		}

		return features;
	}

} // namespace sgre::text
