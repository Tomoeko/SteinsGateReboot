#include "sgre/resource/emote_selectors.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::resource {

	struct EmoteMetadataBudget {
		const EmoteSelectorLimits& Limits;
		std::size_t Selectors = 0;
		std::size_t Options = 0;
		std::size_t Ranges = 0;
		std::size_t Strings = 0;
	};

	static const PsbValue& RequireField(const PsbValue& value, std::string_view field) {
		if (const auto* result = value.Find(field)) {
			return *result;
		}
		throw std::runtime_error("Emote selector metadata is missing " + std::string(field));
	}

	static const PsbArray& Array(const PsbValue& value, std::string_view field) {
		if (const auto* result = std::get_if<PsbArray>(&value.Data)) {
			return *result;
		}
		throw std::runtime_error("Emote selector " + std::string(field) + " requires an array");
	}

	static std::string Name(const PsbValue& value, EmoteMetadataBudget& budget) {
		const auto* name = std::get_if<std::string>(&value.Data);
		if (name == nullptr) {
			throw std::runtime_error("Emote selector name requires the actual string metadata");
		}
		const auto view = std::string_view(*name).substr(0, name->find('\0'));
		if (view.size() >= budget.Limits.MaxStringBytes - budget.Strings) {
			throw std::runtime_error("Emote selector strings exceed their bounded owned storage");
		}
		budget.Strings += view.size() + 1;
		return std::string(view);
	}

	static bool Boolean(const PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return static_cast<std::uint8_t>(*integer) != 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data)) {
			return *real != 0;
		}
		throw std::runtime_error("Emote selector boolean requires native numeric metadata");
	}

	static std::int32_t Integer(const PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1 : 0;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(*integer));
		}
		if (const auto* real = std::get_if<double>(&value.Data); real != nullptr && std::isfinite(*real) && *real >= -2147483648.0 && *real < 2147483648.0) {
			return static_cast<std::int32_t>(*real);
		}
		throw std::runtime_error("Emote selector range exceeds the recovered signed numeric domain");
	}

	static float Number(const PsbValue& value) {
		float result = 0;
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			result = *boolean ? 1 : 0;
		} else if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			result = static_cast<float>(*integer);
		} else if (const auto* real = std::get_if<double>(&value.Data)) {
			result = static_cast<float>(*real);
		} else {
			throw std::runtime_error("Emote selector on/off value requires numeric metadata");
		}
		if (!std::isfinite(result)) {
			throw std::runtime_error("Emote selector on/off value exceeds the finite recovered domain");
		}
		return result;
	}

	static void ReadSelectors(const PsbValue& list, EmoteSelectorMetadata& result, EmoteMetadataBudget& budget) {
		for (const auto& entry : Array(list, "control list")) {
			if (!Boolean(RequireField(entry, "enabled"))) {
				continue;
			}
			if (budget.Selectors == budget.Limits.MaxSelectors) {
				throw std::runtime_error("Emote selector count exceeds its bounded initialization");
			}
			++budget.Selectors;
			EmoteSelectorDefinition definition;
			definition.Label = Name(RequireField(entry, "label"), budget);
			std::map<std::string, std::size_t, std::less<>> grouped;
			const auto& options = Array(RequireField(entry, "optionList"), "optionList");
			if (options.size() > budget.Limits.MaxRanges - budget.Ranges || options.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
				throw std::runtime_error("Emote selector ranges exceed their bounded initialization");
			}
			for (std::size_t index = 0; index != options.size(); ++index) {
				const auto& entry_option = options[index];
				auto name = Name(RequireField(entry_option, "label"), budget);
				EmoteSelectorRange range;
				const auto* lower = entry_option.Find("lower");
				const auto* upper = entry_option.Find("upper");
				range.Lower = lower == nullptr ? static_cast<std::int32_t>(index) : Integer(*lower);
				range.Upper = upper == nullptr ? static_cast<std::int32_t>(index) : Integer(*upper);
				++budget.Ranges;
				if (const auto old = grouped.find(name); old != grouped.end()) {
					// Native duplicate labels append only a range, retaining the first option's values and imm.
					definition.Options[old->second].Ranges.push_back(range);
					continue;
				}
				if (budget.Options == budget.Limits.MaxOptions) {
					throw std::runtime_error("Emote selector options exceed their bounded initialization");
				}
				++budget.Options;
				EmoteSelectorOption option;
				option.Variable = std::move(name);
				option.OffValue = Number(RequireField(entry_option, "offValue"));
				option.OnValue = Number(RequireField(entry_option, "onValue"));
				if (const auto* immediate = entry_option.Find("imm")) {
					option.Immediate = Boolean(*immediate);
				}
				option.Ranges.push_back(range);
				grouped.emplace(option.Variable, definition.Options.size());
				definition.Options.push_back(std::move(option));
			}
			result.Selectors.push_back(std::move(definition));
		}
	}

	EmoteSelectorMetadata ReadEmoteSelectorMetadata(const PsbDocument& document, const EmoteSelectorLimits& limits) {
		EmoteSelectorMetadata result;
		EmoteMetadataBudget budget{limits};
		const auto* metadata = document.Root.Find("metadata");
		if (metadata == nullptr || !std::holds_alternative<PsbObject>(metadata->Data)) {
			throw std::runtime_error("Emote selector setup requires the actual metadata object");
		}
		if (const auto* base = metadata->Find("base")) {
			EmoteBaseSelection selection;
			selection.Character = Name(RequireField(*base, "chara"), budget);
			selection.Motion = Name(RequireField(*base, "motion"), budget);
			result.Base = std::move(selection);
		}
		for (const auto field : {"selectorControl", "customSelectorList"}) {
			if (const auto* list = metadata->Find(field)) {
				ReadSelectors(*list, result, budget);
			}
		}
		return result;
	}

} // namespace sgre::resource
