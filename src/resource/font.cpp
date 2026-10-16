#include "sgre/resource/font.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace sgre::resource {

	static constexpr std::size_t kMaxFontSources = 65536;
	static constexpr std::size_t kMaxFontGlyphs = 65536;
	static constexpr float kMaxFontMetric = 32768;

	static const PsbValue& RequireField(const PsbValue& value, std::string_view name) {
		const auto* field = value.Find(name);
		if (field == nullptr) {
			throw std::runtime_error("bitmap font field is missing: " + std::string(name));
		}

		return *field;
	}

	static std::string ReadText(const PsbValue& value, std::string_view name) {
		const auto* text = std::get_if<std::string>(&RequireField(value, name).Data);
		if (text == nullptr) {
			throw std::runtime_error("bitmap font field is not a string: " + std::string(name));
		}

		return *text;
	}

	static float ReadNumber(const PsbValue& value, std::string_view name, float minimum = 0) {
		const auto& data = RequireField(value, name).Data;
		const auto* integer = std::get_if<std::int64_t>(&data);
		const auto* real = std::get_if<double>(&data);
		const auto number = integer != nullptr ? static_cast<double>(*integer) : real != nullptr ? *real
		                                                                                         : std::numeric_limits<double>::quiet_NaN();
		if (!std::isfinite(number) || number < minimum || number > kMaxFontMetric) {
			throw std::runtime_error("bitmap font numeric field exceeds its metric limit: " + std::string(name));
		}

		return static_cast<float>(number);
	}

	static std::uint32_t ReadCodepoint(std::string_view key) {
		if (key.empty() || key.size() > 4) {
			throw std::runtime_error("bitmap font lookup key is not one UTF-8 scalar");
		}

		const auto first = static_cast<std::uint8_t>(key[0]);
		const auto count = first < 0x80 ? 1u : first >= 0xc2 && first <= 0xdf ? 2u
		    : first >= 0xe0 && first <= 0xef                                  ? 3u
		    : first >= 0xf0 && first <= 0xf4                                  ? 4u
		                                                                      : 0u;
		if (count == 0 || key.size() != count) {
			throw std::runtime_error("bitmap font lookup key is not one UTF-8 scalar");
		}

		const auto mask = count == 1 ? 0x7fu : (1u << (7 - count)) - 1;
		std::uint32_t scalar = first & mask;
		for (std::size_t byte = 1; byte < count; ++byte) {
			const auto next = static_cast<std::uint8_t>(key[byte]);
			if ((next & 0xc0) != 0x80) {
				throw std::runtime_error("bitmap font lookup key has an invalid UTF-8 continuation");
			}

			scalar = (scalar << 6) | (next & 0x3f);
		}

		const auto minimum = count == 1 ? 0u : count == 2 ? 0x80u
		    : count == 3                                  ? 0x800u
		                                                  : 0x10000u;
		if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) {
			throw std::runtime_error("bitmap font lookup key contains an invalid UTF-8 scalar");
		}

		return scalar;
	}

	static BitmapGlyph ReadGlyph(const PsbValue& value, std::string_view key, const std::vector<ImageTexture>& sources) {
		BitmapGlyph glyph;
		glyph.Codepoint = ReadCodepoint(key);
		const auto source_index = ReadNumber(value, "id");
		if (std::trunc(source_index) != source_index || source_index >= sources.size()) {
			throw std::runtime_error("bitmap font glyph source index exceeds its source array");
		}

		glyph.SourceIndex = static_cast<std::uint32_t>(source_index);
		glyph.AtlasX = ReadNumber(value, "x");
		glyph.AtlasY = ReadNumber(value, "y");
		glyph.NominalWidth = ReadNumber(value, "width");
		glyph.NominalHeight = ReadNumber(value, "height");
		glyph.InkWidth = ReadNumber(value, "w");
		glyph.InkHeight = ReadNumber(value, "h");
		glyph.VerticalBearing = ReadNumber(value, "a", -kMaxFontMetric);
		glyph.MetricB = ReadNumber(value, "b", -kMaxFontMetric);
		glyph.MetricD = ReadNumber(value, "d", -kMaxFontMetric);
		const auto& source = sources[glyph.SourceIndex];
		if (glyph.AtlasX > source.Width || glyph.AtlasY > source.Height || glyph.InkWidth > source.Width - glyph.AtlasX || glyph.InkHeight > source.Height - glyph.AtlasY) {
			throw std::runtime_error("bitmap font glyph rectangle exceeds its atlas texture");
		}

		return glyph;
	}

	BitmapFont ReadBitmapFont(const PsbDocument& document) {
		if ((document.Header.Version != 3 && document.Header.Version != 4) || document.Header.Flags != 0) {
			throw std::runtime_error("bitmap font requires an unfiltered PSB version 3 or 4");
		}

		if (ReadText(document.Root, "id") != "font" || ReadText(document.Root, "spec") != "win") {
			throw std::runtime_error("bitmap font requires id=font and spec=win");
		}

		if (ReadNumber(document.Root, "version") != 1.08f) {
			throw std::runtime_error("unsupported bitmap font schema version");
		}

		BitmapFont font;
		font.Label = ReadText(document.Root, "label");
		font.MinWidth = ReadNumber(document.Root, "minWidth");
		font.MinHeight = ReadNumber(document.Root, "minHeight");
		font.MaxWidth = ReadNumber(document.Root, "maxWidth");
		font.MaxHeight = ReadNumber(document.Root, "maxHeight");
		if (font.MinWidth > font.MaxWidth || font.MinHeight > font.MaxHeight) {
			throw std::runtime_error("bitmap font minimum metrics exceed maximum metrics");
		}

		const auto* sources = std::get_if<PsbArray>(&RequireField(document.Root, "source").Data);
		if (sources == nullptr || sources->empty() || sources->size() > kMaxFontSources) {
			throw std::runtime_error("bitmap font source array is invalid or exceeds its entry limit");
		}

		font.Sources.reserve(sources->size());
		for (const auto& source : *sources) {
			font.Sources.push_back(ReadImageTexture(document, source));
		}

		const auto* glyphs = std::get_if<PsbObject>(&RequireField(document.Root, "code").Data);
		if (glyphs == nullptr || glyphs->size() > kMaxFontGlyphs) {
			throw std::runtime_error("bitmap font code table is invalid or exceeds its entry limit");
		}

		for (const auto& [key, value] : *glyphs) {
			try {
				const auto inserted = font.Glyphs.emplace(key, ReadGlyph(value, key, font.Sources));
				if (!inserted.second) {
					throw std::runtime_error("duplicate bitmap font lookup key");
				}
			} catch (const std::exception& error) {
				throw std::runtime_error("bitmap font glyph " + key + ": " + error.what());
			}
		}

		return font;
	}

	const BitmapGlyph* BitmapFont::FindGlyph(std::string_view utf8_key) const {
		const auto glyph = Glyphs.find(utf8_key);
		if (glyph == Glyphs.end()) {
			return nullptr;
		}

		return &glyph->second;
	}

	const BitmapGlyph* BitmapFont::FindGlyphCodeUnit(std::uint16_t code_unit) const {
		// Original 0x1404c0fd0 passes a one-unit, NUL-terminated string to CP_UTF8 conversion.
		const std::uint32_t scalar = code_unit >= 0xd800 && code_unit <= 0xdfff ? 0xfffd : code_unit;
		std::array<char, 3> key{};
		std::size_t length = 0;
		if (scalar != 0 && scalar < 0x80) {
			key[0] = static_cast<char>(scalar);
			length = 1;
		} else if (scalar >= 0x80 && scalar < 0x800) {
			key[0] = static_cast<char>(0xc0 | (scalar >> 6));
			key[1] = static_cast<char>(0x80 | (scalar & 0x3f));
			length = 2;
		} else if (scalar >= 0x800) {
			key[0] = static_cast<char>(0xe0 | (scalar >> 12));
			key[1] = static_cast<char>(0x80 | ((scalar >> 6) & 0x3f));
			key[2] = static_cast<char>(0x80 | (scalar & 0x3f));
			length = 3;
		}

		return FindGlyph(std::string_view(key.data(), length));
	}

	GlyphSize MeasureBitmapGlyph(const BitmapGlyph* glyph, float scale_x, float scale_y) {
		if (!std::isfinite(scale_x) || !std::isfinite(scale_y)) {
			throw std::runtime_error("bitmap glyph measurement scale is nonfinite");
		}

		GlyphSize size;
		if (glyph == nullptr) {
			return size;
		}

		if (!std::isfinite(glyph->NominalWidth) || !std::isfinite(glyph->NominalHeight) || glyph->NominalWidth < 0 || glyph->NominalHeight < 0) {
			throw std::runtime_error("bitmap glyph measurement nominal metrics are invalid");
		}

		size.Width = glyph->NominalWidth * scale_x;
		size.Height = glyph->NominalHeight * scale_y;
		if (!std::isfinite(size.Width) || !std::isfinite(size.Height)) {
			throw std::runtime_error("bitmap glyph measurement overflows finite metrics");
		}

		return size;
	}

	GlyphQuad EvaluateGlyphQuad(const BitmapGlyph& glyph, const ImageTexture& source, float draw_x, float draw_y, float scale_x, float scale_y) {
		if (source.Width == 0 || source.Height == 0 || !std::isfinite(draw_x) || !std::isfinite(draw_y) || !std::isfinite(scale_x) || !std::isfinite(scale_y) || !std::isfinite(glyph.AtlasX) || !std::isfinite(glyph.AtlasY) || !std::isfinite(glyph.InkWidth) || !std::isfinite(glyph.InkHeight) || !std::isfinite(glyph.VerticalBearing) || glyph.AtlasX < 0 || glyph.AtlasY < 0 || glyph.InkWidth < 0 || glyph.InkHeight < 0 || glyph.AtlasX > source.Width || glyph.AtlasY > source.Height || glyph.InkWidth > source.Width - glyph.AtlasX || glyph.InkHeight > source.Height - glyph.AtlasY) {
			throw std::runtime_error("glyph quad input is nonfinite or exceeds its atlas dimensions");
		}

		// Native 0x1404c1970 rounds queued atlas endpoints before 0x1404c1d10 subtracts them.
		const float atlas_right = glyph.AtlasX + glyph.InkWidth;
		const float atlas_bottom = glyph.AtlasY + glyph.InkHeight;
		GlyphQuad quad;
		quad.Left = draw_x;
		quad.Top = draw_y - glyph.VerticalBearing * scale_y;
		quad.Right = (atlas_right - glyph.AtlasX) * scale_x + draw_x;
		quad.Bottom = (atlas_bottom - glyph.AtlasY - glyph.VerticalBearing) * scale_y + draw_y;
		quad.U0 = glyph.AtlasX / source.Width;
		quad.V0 = glyph.AtlasY / source.Height;
		quad.U1 = atlas_right / source.Width;
		quad.V1 = atlas_bottom / source.Height;
		if (!std::isfinite(quad.Top) || !std::isfinite(quad.Right) || !std::isfinite(quad.Bottom)) {
			throw std::runtime_error("glyph quad placement overflows finite coordinates");
		}

		return quad;
	}

} // namespace sgre::resource
