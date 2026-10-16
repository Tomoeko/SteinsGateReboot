#include "sgre/text/element_draw.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::text {

	static void ValidateDrawPosition(const TextCursor& position) {
		if (!std::isfinite(position.X) || !std::isfinite(position.Y)) {
			throw std::runtime_error("text element draw position is not finite");
		}
	}

	static std::uint32_t ConvertStoredColor(std::uint32_t color) {
		return ((color & 0x000000ff) << 24) | ((color & 0x0000ff00) << 8) | ((color & 0x00ff0000) >> 8) | ((color & 0xff000000) >> 24);
	}

	static void AppendDrawPass(TextElementDrawPlan& plan, TextGlyphPassKind kind, const TextCursor& position, std::uint32_t stored_color, std::int32_t edge_width) {
		auto& pass = plan.Passes[plan.PassCount++];
		pass.Kind = kind;
		pass.Position = position;
		pass.ColorRgba = ConvertStoredColor(stored_color);
		pass.EdgeWidth = edge_width;
	}

	TextElementDrawPlan PrepareTextElementDrawPlan(const TextCursor& render_origin, const TextCursor& element_position, const TextElementDrawStyle& style) {
		ValidateDrawPosition(render_origin);
		ValidateDrawPosition(element_position);

		TextCursor position;
		position.X = render_origin.X + element_position.X;
		position.Y = render_origin.Y + element_position.Y;
		ValidateDrawPosition(position);

		TextElementDrawPlan plan;
		plan.FontStyle = style.Bold ? TextFontStyle::Bold : style.Italic ? TextFontStyle::Italic
		                                                                 : TextFontStyle::Regular;
		if (style.EdgeEnabled && style.EdgeWidth > 0) {
			AppendDrawPass(plan, TextGlyphPassKind::Edge, position, style.EdgeColor, style.EdgeWidth);
		}

		if (style.ShadowEnabled) {
			// CVTDQ2PS rounds the signed integer before the separate additions to X and Y.
			const auto offset = static_cast<float>(style.ShadowOffset);
			TextCursor shadow_position;
			shadow_position.X = offset + position.X;
			shadow_position.Y = offset + position.Y;
			ValidateDrawPosition(shadow_position);
			AppendDrawPass(plan, TextGlyphPassKind::Shadow, shadow_position, style.ShadowColor, 0);
		}

		AppendDrawPass(plan, TextGlyphPassKind::Main, position, style.MainColor, 0);

		return plan;
	}

	TextGlyphDrawPass PrepareTextRubyDrawPass(const TextGlyphDrawPass& element_pass, const TextCursor& ruby_offset) {
		ValidateDrawPosition(element_pass.Position);
		ValidateDrawPosition(ruby_offset);
		if (element_pass.EdgeWidth < 0) {
			throw std::runtime_error("text ruby draw pass has a negative edge width");
		}

		auto ruby_pass = element_pass;
		ruby_pass.Position.X = element_pass.Position.X + ruby_offset.X;
		ruby_pass.Position.Y = element_pass.Position.Y + ruby_offset.Y;
		ValidateDrawPosition(ruby_pass.Position);

		return ruby_pass;
	}

} // namespace sgre::text
