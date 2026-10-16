#pragma once

#include "sgre/text/formatting.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::text {

	struct TextPropertyReader {
		// Presence is a raw lookup; each later typed read performs a separate general lookup.
		std::function<bool(std::string_view)> ContainsRaw;
		std::function<std::int32_t(std::string_view)> ReadInteger;
		std::function<float(std::string_view)> ReadNumber;
		std::function<bool(std::string_view)> ReadBoolean;
		std::function<std::string(std::string_view)> ReadString;
		std::function<std::uint32_t(std::span<const std::uint16_t>)> InternFace;
	};

	struct TextProcessingOptions {
		TextProcessingOptions();
		// Native02c040 initializes these owned punctuation sequences in the original order.
		std::vector<std::uint16_t> Following;
		std::vector<std::uint16_t> Leading;
		std::vector<std::uint16_t> Begin;
		std::vector<std::uint16_t> End;
		std::int32_t MaximumProhibitionCount = 1;
		std::int32_t RequestedTextureSize = 1024;
		bool IgnoreColor = false;
		bool IgnoreSize = false;
		bool IgnoreDelay = false;
		bool IgnoreHorizontalOverflow = false;
		bool IgnoreVerticalOverflow = false;
		bool WidthTimeScale = false;
		bool IgnoreRuby = false;
		bool IgnoreType = false;
		bool IgnoreFace = false;
		bool IgnoreStyle = false;
	};

	// Typed readers supply the original converter's zero/empty fallback after a failed general get or conversion.
	void ApplyTextDefaultProperties(TextFormattingDefaults& defaults, const TextPropertyReader& reader);
	void ApplyTextFontProperties(TextFormattingState& formatting, const TextPropertyReader& reader, const TextFontResetCallbacks& callbacks);
	void ApplyTextOptionProperties(TextFormattingState& formatting, TextProcessingOptions& options, const TextPropertyReader& reader);

} // namespace sgre::text
