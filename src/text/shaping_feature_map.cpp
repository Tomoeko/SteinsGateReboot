#include "sgre/text/shaping_feature_map.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sgre::text {

	struct ConsolidatedVectorFontFeature {
		std::string Tag;
		std::uint32_t Order = 0;
		std::uint32_t Value = 0;
		std::uint32_t Flags = 0;
		std::uint32_t GlobalValue = 0;
		std::uint32_t SubstitutionStage = 0;
		std::uint32_t PositioningStage = 0;
	};

	static void ValidateMapTag(std::string_view tag) {
		if (tag.size() != 4) {
			throw std::runtime_error("VectorFont feature-map tag must have four ASCII characters");
		}

		for (const auto character : tag) {
			if (character < ' ' || character > '~') {
				throw std::runtime_error("VectorFont feature-map tag must have four ASCII characters");
			}
		}
	}

	struct PreparedVectorFontFeatureTable {
		std::map<std::string_view, std::uint16_t> SelectedFeatures;
		std::map<std::string_view, std::uint16_t> AllFeatures;
		std::optional<std::string_view> RequiredTag;
	};

	static PreparedVectorFontFeatureTable PrepareFeatureTable(const VectorFontFeatureTable& table) {
		if (table.FeatureTags.size() > 65535 || table.SelectedFeatures.size() > 65535) {
			throw std::runtime_error("VectorFont feature table exceeds its 16-bit index limit");
		}

		PreparedVectorFontFeatureTable result;
		for (std::size_t index = 0; index < table.FeatureTags.size(); ++index) {
			ValidateMapTag(table.FeatureTags[index]);
			result.AllFeatures.try_emplace(table.FeatureTags[index], static_cast<std::uint16_t>(index));
		}

		for (const auto index : table.SelectedFeatures) {
			if (index >= table.FeatureTags.size()) {
				throw std::runtime_error("VectorFont selected feature index exceeds its table");
			}

			result.SelectedFeatures.try_emplace(table.FeatureTags[index], index);
		}

		if (table.RequiredFeature) {
			if (*table.RequiredFeature >= table.FeatureTags.size()) {
				throw std::runtime_error("VectorFont required feature index exceeds its table");
			}

			result.RequiredTag = table.FeatureTags[*table.RequiredFeature];
		}

		return result;
	}

	static std::vector<ConsolidatedVectorFontFeature> ConsolidateFeatureRecords(std::span<const VectorFontCollectedFeature> features) {
		if (features.size() > 65536) {
			throw std::runtime_error("VectorFont feature-map record limit exceeded");
		}

		std::vector<ConsolidatedVectorFontFeature> ordered;
		ordered.reserve(features.size());
		for (std::size_t index = 0; index < features.size(); ++index) {
			const auto& source = features[index];
			ValidateMapTag(source.Tag);
			const auto flags = static_cast<std::uint32_t>(source.Flags);
			if ((flags & ~std::uint32_t{0x13}) != 0 || source.Order != index + 1 || source.GlobalValue != ((flags & 1) != 0 ? source.Value : 0)) {
				throw std::runtime_error("VectorFont feature-map input violates collected-record invariants");
			}

			ConsolidatedVectorFontFeature target;
			target.Tag = source.Tag;
			target.Order = source.Order;
			target.Value = source.Value;
			target.Flags = flags;
			target.GlobalValue = source.GlobalValue;
			target.SubstitutionStage = source.SubstitutionStage;
			target.PositioningStage = source.PositioningStage;
			ordered.push_back(std::move(target));
		}

		std::sort(ordered.begin(), ordered.end(), [](const auto& first, const auto& second) {
			return first.Tag < second.Tag || (first.Tag == second.Tag && first.Order < second.Order);
		});
		std::size_t count = 0;
		for (const auto& source : ordered) {
			if (count == 0 || ordered[count - 1].Tag != source.Tag) {
				ordered[count] = source;
				++count;

				continue;
			}

			auto& target = ordered[count - 1];
			if ((source.Flags & 1) != 0) {
				target.Flags |= 1;
				target.Value = source.Value;
				target.GlobalValue = source.GlobalValue;
			} else {
				target.Flags &= ~std::uint32_t{1};
				target.Value = std::max(target.Value, source.Value);
			}

			target.Flags |= source.Flags & 2;
			target.SubstitutionStage = std::min(target.SubstitutionStage, source.SubstitutionStage);
			target.PositioningStage = std::min(target.PositioningStage, source.PositioningStage);
		}

		ordered.resize(count);

		return ordered;
	}

	static std::optional<std::uint16_t> FindTableFeature(const PreparedVectorFontFeatureTable& table, std::string_view tag, bool whole_table) {
		const auto& features = whole_table ? table.AllFeatures : table.SelectedFeatures;
		const auto position = features.find(tag);
		if (position != features.end()) {
			return position->second;
		}

		return std::nullopt;
	}

	static std::uint32_t ShiftNativeFeatureWord(std::uint32_t value, std::uint32_t shift) {
		return value << (shift & 31);
	}

	VectorFontFeatureMap CompileVectorFontFeatureMasks(std::span<const VectorFontCollectedFeature> features, const VectorFontFeatureTable& substitutions, const VectorFontFeatureTable& positioning) {
		const auto prepared_substitutions = PrepareFeatureTable(substitutions);
		const auto prepared_positioning = PrepareFeatureTable(positioning);
		const auto ordered = ConsolidateFeatureRecords(features);
		VectorFontFeatureMap result;
		result.Features.reserve(ordered.size());
		std::uint32_t next_bit = 2;
		for (const auto& source : ordered) {
			if (source.Value == 0) {
				continue;
			}

			const bool common_mask = (source.Flags & 1) != 0 && source.Value == 1;
			const auto width = common_mask ? 0 : std::min(static_cast<std::uint32_t>(std::bit_width(source.Value)), std::uint32_t{8});
			if (width > 32 - next_bit) {
				continue;
			}

			if (prepared_substitutions.RequiredTag && *prepared_substitutions.RequiredTag == source.Tag) {
				result.RequiredSubstitutionStage = source.SubstitutionStage;
			}
			if (prepared_positioning.RequiredTag && *prepared_positioning.RequiredTag == source.Tag) {
				result.RequiredPositioningStage = source.PositioningStage;
			}

			VectorFontFeatureMask feature;
			feature.Tag = source.Tag;
			feature.SubstitutionFeature = FindTableFeature(prepared_substitutions, source.Tag, false);
			feature.PositioningFeature = FindTableFeature(prepared_positioning, source.Tag, false);
			if (!feature.SubstitutionFeature && !feature.PositioningFeature && (source.Flags & 0x10) != 0) {
				feature.SubstitutionFeature = FindTableFeature(prepared_substitutions, source.Tag, true);
				feature.PositioningFeature = FindTableFeature(prepared_positioning, source.Tag, true);
			}

			const bool found = feature.SubstitutionFeature || feature.PositioningFeature;
			if (!found && (source.Flags & 2) == 0) {
				continue;
			}

			feature.SubstitutionStage = source.SubstitutionStage;
			feature.PositioningStage = source.PositioningStage;
			feature.LookupFlags = ((~(source.Flags >> 1)) & 6) | (found ? 0 : 1);
			if (common_mask) {
				feature.Shift = 1;
				feature.Mask = 2;
			} else {
				feature.Shift = next_bit;
				// x86 shifts wrap their count, including the original end-at-bit-32 case.
				feature.Mask = ShiftNativeFeatureWord(1, next_bit + width) - ShiftNativeFeatureWord(1, next_bit);
				result.GlobalMask |= ShiftNativeFeatureWord(source.GlobalValue, next_bit) & feature.Mask;
				next_bit += width;
			}

			feature.OneMask = ShiftNativeFeatureWord(1, feature.Shift) & feature.Mask;
			result.Features.push_back(std::move(feature));
		}

		return result;
	}

} // namespace sgre::text
