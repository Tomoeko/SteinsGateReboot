#pragma once

#include "sgre/resource/psb.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sgre::resource {

	struct EmoteBaseSelection {
		std::string Character;
		std::string Motion;
	};

	struct EmoteSelectorRange {
		std::int32_t Lower = 0;
		std::int32_t Upper = 0;
	};

	struct EmoteSelectorOption {
		std::string Variable;
		float OffValue = 0;
		float OnValue = 0;
		bool Immediate = false;
		std::vector<EmoteSelectorRange> Ranges;
	};

	struct EmoteSelectorDefinition {
		std::string Label;
		std::vector<EmoteSelectorOption> Options;
	};

	struct EmoteSelectorMetadata {
		std::optional<EmoteBaseSelection> Base;
		// Native setup creates selectorControl first, then customSelectorList, preserving each list's order.
		std::vector<EmoteSelectorDefinition> Selectors;
	};

	struct EmoteSelectorLimits {
		std::size_t MaxSelectors = 4096;
		std::size_t MaxOptions = 65536;
		std::size_t MaxRanges = 65536;
		std::size_t MaxStringBytes = 8 * 1024 * 1024;
	};

	// Owns selector strings/ranges independently of the borrowed actual PSB metadata.
	// Other Emote control families are not consumed or initialized by this focused reader.
	[[nodiscard]] EmoteSelectorMetadata ReadEmoteSelectorMetadata(const PsbDocument& document, const EmoteSelectorLimits& limits = {});

} // namespace sgre::resource
