#pragma once

#include "sgre/resource/font.hpp"
#include "sgre/text/font_selection.hpp"

#include <cstdint>
#include <functional>

namespace sgre::text {

	struct TextFormattingDefaults {
		float FontSize = 24;
		float BigFontSize = 48;
		float SmallFontSize = 12;
		float RubySize = 10;
		float RubyOffset = -2;
		float LineSize = 24;
		float LineSpacing = 6;
		float Pitch = 0;
		std::uint32_t NormalFaceIndex = 0;
		std::uint32_t RubyFaceIndex = 1;
		std::int32_t Alignment = -1;
		std::int32_t VerticalAlignment = -1;
		bool Bold = false;
		bool Italic = false;
		bool Shadow = true;
		bool Edge = false;
		bool WordBreak = true;
		std::uint32_t Color = 0xffffffff;
		std::uint32_t ShadowColor = 0xff000000;
		std::int32_t ShadowOffset = 1;
		std::uint32_t EdgeColor = 0xff0080ff;
		std::int32_t EdgeSize = 4;
	};

	struct TextFormattingCurrent {
		float FontSize = -1;
		float RubySize = -1;
		float RubyOffset = 0;
		float FontBaseWidth = 0;
		float LineSize = 0;
		float LineSpacing = 0;
		float Pitch = 0;
		std::uint32_t NormalFaceIndex = 0;
		std::uint32_t RubyFaceIndex = 0;
		std::int32_t Alignment = 0;
		std::int32_t VerticalAlignment = 0;
		bool Bold = false;
		bool Italic = false;
		bool Shadow = false;
		bool Edge = false;
		bool WordBreak = false;
		std::uint32_t Color = 0;
		std::uint32_t ShadowColor = 0;
		std::int32_t ShadowOffset = 0;
		std::uint32_t EdgeColor = 0;
		std::int32_t EdgeSize = 0;
	};

	struct TextFormattingState {
		TextFormattingDefaults Defaults;
		TextFormattingCurrent Current;
		float FontScale = 1;
		bool Vertical = false;
	};

	struct TextFontResetCallbacks {
		// Callbacks borrow the same live state and actual normal/ruby provider owner for this operation.
		std::function<void()> SelectNormal;
		std::function<resource::GlyphSize(std::uint16_t)> MeasureNormal;
		std::function<void()> SelectRuby;
		// Native clearFonts virtual slot+0x78 retires private normal/ruby interfaces before atlas packing reuse.
		std::function<void()> ClearPreparedFonts;
	};

	struct TextFontSelectionRequest {
		TextFontFamily Family = TextFontFamily::Normal;
		TextFontSelectionKey Key;
		bool Filtering = false;
	};

	[[nodiscard]] TextFontSelectionRequest PrepareTextFontSelectionRequest(const TextFormattingState& state, TextFontFamily family, bool filtering);

	// Current storage is safely initialized; only the recovered reset transitions are native fidelity claims.
	void ResetTextFontDefaults(TextFormattingState& state, const TextFontResetCallbacks& callbacks);
	void ResetTextGeometryDefaults(TextFormattingState& state);

} // namespace sgre::text
