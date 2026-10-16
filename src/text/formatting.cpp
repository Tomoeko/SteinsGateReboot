#include "sgre/text/formatting.hpp"

#include <stdexcept>

namespace sgre::text {

	TextFontSelectionRequest PrepareTextFontSelectionRequest(const TextFormattingState& state, TextFontFamily family, bool filtering) {
		if (family != TextFontFamily::Normal && family != TextFontFamily::Ruby) {
			throw std::runtime_error("text font selection request has an invalid family");
		}

		TextFontSelectionRequest request;
		request.Family = family;
		request.Filtering = filtering;
		if (family == TextFontFamily::Normal) {
			request.Key.RequestedSize = state.FontScale * state.Current.FontSize;
			request.Key.FaceIndex = state.Current.NormalFaceIndex;
			request.Key.Style = state.Current.Bold ? TextFontStyle::Bold : state.Current.Italic ? TextFontStyle::Italic
			                                                                                    : TextFontStyle::Regular;
		} else {
			request.Key.RequestedSize = state.FontScale * state.Current.RubySize;
			request.Key.FaceIndex = state.Current.RubyFaceIndex;
		}

		return request;
	}

	void ResetTextFontDefaults(TextFormattingState& state, const TextFontResetCallbacks& callbacks) {
		auto& current = state.Current;
		const auto& defaults = state.Defaults;
		if (current.NormalFaceIndex != defaults.NormalFaceIndex || current.Bold != defaults.Bold || current.Italic != defaults.Italic || !(current.FontSize == defaults.FontSize)) {
			if (!callbacks.SelectNormal || !callbacks.MeasureNormal) {
				throw std::runtime_error("text font reset requires its actual normal selector and unit measurement");
			}

			current.Bold = defaults.Bold;
			current.Italic = defaults.Italic;
			current.FontSize = defaults.FontSize;
			current.NormalFaceIndex = defaults.NormalFaceIndex;
			callbacks.SelectNormal();
			const auto metrics = callbacks.MeasureNormal(0x3000);
			current.FontBaseWidth = state.Vertical ? state.FontScale * current.FontSize : metrics.Width;
			if (current.FontBaseWidth == 0) {
				current.FontBaseWidth = current.FontSize;
			}
		}

		// Native code reads ruby/default appearance after normal callbacks, preserving their observable changes.
		if (current.RubyFaceIndex != defaults.RubyFaceIndex || !(current.RubySize == defaults.RubySize)) {
			if (!callbacks.SelectRuby) {
				throw std::runtime_error("text font reset requires its actual ruby selector");
			}

			current.RubyFaceIndex = defaults.RubyFaceIndex;
			current.RubySize = defaults.RubySize;
			callbacks.SelectRuby();
		}

		current.RubyOffset = defaults.RubyOffset;
		current.Shadow = defaults.Shadow;
		current.ShadowOffset = defaults.ShadowOffset;
		current.Color = defaults.Color;
		current.ShadowColor = defaults.ShadowColor;
		current.Edge = defaults.Edge;
		current.EdgeColor = defaults.EdgeColor;
		current.EdgeSize = defaults.EdgeSize;
	}

	void ResetTextGeometryDefaults(TextFormattingState& state) {
		auto& current = state.Current;
		const auto& defaults = state.Defaults;
		// SSE MAXSS selects its second operand for equal values and unordered comparisons.
		const auto line_size = defaults.LineSize > current.FontSize ? defaults.LineSize : current.FontSize;
		current.Pitch = defaults.Pitch;
		current.Alignment = defaults.Alignment;
		current.VerticalAlignment = defaults.VerticalAlignment;
		current.LineSize = line_size * state.FontScale;
		current.LineSpacing = state.FontScale * defaults.LineSpacing;
		current.WordBreak = defaults.WordBreak;
	}

} // namespace sgre::text
