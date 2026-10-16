#include "sgre/text/property_settings.hpp"

#include "sgre/text/encoding.hpp"

#include <stdexcept>

namespace sgre::text {

	static bool ContainsTextProperty(const TextPropertyReader& reader, std::string_view name) {
		if (!reader.ContainsRaw) {
			throw std::runtime_error("text properties require the raw presence reader");
		}

		return reader.ContainsRaw(name);
	}

	static std::int32_t ReadTextInteger(const TextPropertyReader& reader, std::string_view name) {
		if (!reader.ReadInteger) {
			throw std::runtime_error("text property " + std::string(name) + " requires the integer reader");
		}

		return reader.ReadInteger(name);
	}

	static float ReadTextNumber(const TextPropertyReader& reader, std::string_view name) {
		if (!reader.ReadNumber) {
			throw std::runtime_error("text property " + std::string(name) + " requires the number reader");
		}

		return reader.ReadNumber(name);
	}

	static bool ReadTextBoolean(const TextPropertyReader& reader, std::string_view name) {
		if (!reader.ReadBoolean) {
			throw std::runtime_error("text property " + std::string(name) + " requires the boolean reader");
		}

		return reader.ReadBoolean(name);
	}

	static std::vector<std::uint16_t> ReadTextString(const TextPropertyReader& reader, std::string_view name) {
		if (!reader.ReadString) {
			throw std::runtime_error("text property " + std::string(name) + " requires the string reader");
		}

		const auto value = reader.ReadString(name);
		const auto terminator = value.find('\0');

		return DecodeTextCodeUnits(std::string_view(value).substr(0, terminator));
	}

	static std::uint32_t ReadTextFace(const TextPropertyReader& reader, std::string_view name) {
		const auto units = ReadTextString(reader, name);
		if (!reader.InternFace) {
			throw std::runtime_error("text property " + std::string(name) + " requires the actual face-name owner");
		}

		return reader.InternFace(units);
	}

	static void ApplyTextNumber(float& field, const TextPropertyReader& reader, std::string_view name) {
		if (ContainsTextProperty(reader, name)) {
			field = ReadTextNumber(reader, name);
		}
	}

	static void ApplyTextInteger(std::int32_t& field, const TextPropertyReader& reader, std::string_view name) {
		if (ContainsTextProperty(reader, name)) {
			field = ReadTextInteger(reader, name);
		}
	}

	static void ApplyTextColor(std::uint32_t& field, const TextPropertyReader& reader, std::string_view name) {
		if (ContainsTextProperty(reader, name)) {
			field = static_cast<std::uint32_t>(ReadTextInteger(reader, name));
		}
	}

	static void ApplyTextBoolean(bool& field, const TextPropertyReader& reader, std::string_view name) {
		if (ContainsTextProperty(reader, name)) {
			field = ReadTextBoolean(reader, name);
		}
	}

	static void ApplyTextString(std::vector<std::uint16_t>& field, const TextPropertyReader& reader, std::string_view name) {
		if (ContainsTextProperty(reader, name)) {
			field = ReadTextString(reader, name);
		}
	}

	void ApplyTextDefaultProperties(TextFormattingDefaults& defaults, const TextPropertyReader& reader) {
		if (ContainsTextProperty(reader, "face")) {
			defaults.NormalFaceIndex = ReadTextFace(reader, "face");
		}
		if (ContainsTextProperty(reader, "rubyface")) {
			defaults.RubyFaceIndex = ReadTextFace(reader, "rubyface");
		}

		ApplyTextBoolean(defaults.Bold, reader, "bold");
		if (ContainsTextProperty(reader, "fontsize")) {
			const auto font_size = static_cast<float>(ReadTextInteger(reader, "fontsize"));
			defaults.FontSize = font_size;
			if (!ContainsTextProperty(reader, "bigfontsize")) {
				defaults.BigFontSize = font_size + font_size;
			}
			if (!ContainsTextProperty(reader, "smallfontsize")) {
				defaults.SmallFontSize = font_size * 0.5f;
			}
			if (!ContainsTextProperty(reader, "rubysize")) {
				defaults.RubySize = font_size / 3.0f;
			}
		}

		ApplyTextNumber(defaults.SmallFontSize, reader, "smallfontsize");
		ApplyTextNumber(defaults.BigFontSize, reader, "bigfontsize");
		ApplyTextNumber(defaults.RubySize, reader, "rubysize");
		ApplyTextNumber(defaults.RubyOffset, reader, "rubyoffset");
		ApplyTextColor(defaults.Color, reader, "color");
		ApplyTextBoolean(defaults.Shadow, reader, "shadow");
		ApplyTextColor(defaults.ShadowColor, reader, "shadowcolor");
		ApplyTextInteger(defaults.ShadowOffset, reader, "shadowdiff");
		ApplyTextBoolean(defaults.Edge, reader, "edge");
		ApplyTextColor(defaults.EdgeColor, reader, "edgecolor");
		ApplyTextInteger(defaults.EdgeSize, reader, "edgesize");
		ApplyTextNumber(defaults.LineSpacing, reader, "linespacing");
		ApplyTextNumber(defaults.Pitch, reader, "pitch");
		if (ContainsTextProperty(reader, "linesize")) {
			defaults.LineSize = ReadTextNumber(reader, "linesize");
		} else if (ContainsTextProperty(reader, "fontsize")) {
			// The native wrapper re-reads fontsize as a float here rather than using its earlier integer conversion.
			defaults.LineSize = ReadTextNumber(reader, "fontsize");
		}

		ApplyTextInteger(defaults.Alignment, reader, "align");
		ApplyTextInteger(defaults.VerticalAlignment, reader, "valign");
	}

	static void RefreshNormalTextFont(TextFormattingState& formatting, const TextFontResetCallbacks& callbacks) {
		if (!callbacks.SelectNormal || !callbacks.MeasureNormal) {
			throw std::runtime_error("text font properties require their actual normal selector and unit measurement");
		}

		callbacks.SelectNormal();
		const auto metrics = callbacks.MeasureNormal(0x3000);
		auto& current = formatting.Current;
		current.FontBaseWidth = formatting.Vertical ? formatting.FontScale * current.FontSize : metrics.Width;
		if (current.FontBaseWidth == 0) {
			current.FontBaseWidth = current.FontSize;
		}
	}

	static void RefreshRubyTextFont(const TextFontResetCallbacks& callbacks) {
		if (!callbacks.SelectRuby) {
			throw std::runtime_error("text font properties require their actual ruby selector");
		}

		callbacks.SelectRuby();
	}

	void ApplyTextFontProperties(TextFormattingState& formatting, const TextPropertyReader& reader, const TextFontResetCallbacks& callbacks) {
		auto& current = formatting.Current;
		if (ContainsTextProperty(reader, "face")) {
			const auto face = ReadTextFace(reader, "face");
			if (current.NormalFaceIndex != face) {
				current.NormalFaceIndex = face;
				RefreshNormalTextFont(formatting, callbacks);
			}
		}
		if (ContainsTextProperty(reader, "rubyface")) {
			const auto face = ReadTextFace(reader, "rubyface");
			if (current.RubyFaceIndex != face) {
				current.RubyFaceIndex = face;
				RefreshRubyTextFont(callbacks);
			}
		}
		if (ContainsTextProperty(reader, "bold")) {
			const auto bold = ReadTextBoolean(reader, "bold");
			if (current.Bold != bold) {
				current.Bold = bold;
				RefreshNormalTextFont(formatting, callbacks);
			}
		}
		if (ContainsTextProperty(reader, "fontsize")) {
			const auto size = ReadTextNumber(reader, "fontsize");
			if (current.FontSize < 0 || !(current.FontSize == size)) {
				current.FontSize = size;
				RefreshNormalTextFont(formatting, callbacks);
			}
		}

		ApplyTextColor(current.Color, reader, "color");
		if (ContainsTextProperty(reader, "rubysize")) {
			const auto size = ReadTextNumber(reader, "rubysize");
			if (current.RubySize < 0 || !(current.RubySize == size)) {
				current.RubySize = size;
				RefreshRubyTextFont(callbacks);
			}
		}

		ApplyTextNumber(current.RubyOffset, reader, "rubyoffset");
		ApplyTextBoolean(current.Shadow, reader, "shadow");
		ApplyTextColor(current.ShadowColor, reader, "shadowcolor");
		ApplyTextInteger(current.ShadowOffset, reader, "shadowdiff");
		ApplyTextBoolean(current.Edge, reader, "edge");
		ApplyTextColor(current.EdgeColor, reader, "edgecolor");
		ApplyTextInteger(current.EdgeSize, reader, "edgesize");
	}

	void ApplyTextOptionProperties(TextFormattingState& formatting, TextProcessingOptions& options, const TextPropertyReader& reader) {
		ApplyTextString(options.Following, reader, "following");
		ApplyTextString(options.Leading, reader, "leading");
		ApplyTextString(options.Begin, reader, "begin");
		ApplyTextString(options.End, reader, "end");
		ApplyTextBoolean(formatting.Vertical, reader, "vertical");
		ApplyTextInteger(options.MaximumProhibitionCount, reader, "kinsoku_max");
		if (ContainsTextProperty(reader, "word_break")) {
			const auto enabled = ReadTextBoolean(reader, "word_break");
			formatting.Current.WordBreak = enabled;
			formatting.Defaults.WordBreak = enabled;
		}

		ApplyTextBoolean(options.IgnoreColor, reader, "ignore_color");
		ApplyTextBoolean(options.IgnoreSize, reader, "ignore_size");
		ApplyTextBoolean(options.IgnoreDelay, reader, "ignore_delay");
		ApplyTextBoolean(options.IgnoreVerticalOverflow, reader, "ignore_over");
		ApplyTextBoolean(options.IgnoreVerticalOverflow, reader, "ignore_overy");
		ApplyTextBoolean(options.IgnoreHorizontalOverflow, reader, "ignore_overx");
		ApplyTextBoolean(options.WidthTimeScale, reader, "width_time_scale");
		ApplyTextBoolean(options.IgnoreRuby, reader, "ignore_ruby");
		ApplyTextBoolean(options.IgnoreType, reader, "ignore_type");
		ApplyTextBoolean(options.IgnoreFace, reader, "ignore_face");
		ApplyTextBoolean(options.IgnoreStyle, reader, "ignore_style");
		ApplyTextInteger(options.RequestedTextureSize, reader, "texture_size");
	}

} // namespace sgre::text
