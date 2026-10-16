#include "sgre/resource/truetype_outline.hpp"

#include <bit>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sgre::resource {

	static constexpr std::size_t kMaximumPoints = 32767;
	static constexpr std::size_t kMaximumContours = 4094;
	static constexpr std::size_t kMaximumComponents = 1024;

	static std::span<const std::uint8_t> Range(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count, std::string_view context) {
		if (offset > bytes.size() || count > bytes.size() - offset) {
			throw std::runtime_error("TrueType " + std::string(context) + " exceeds its glyph/table range");
		}

		return bytes.subspan(offset, count);
	}

	static std::uint16_t Big16(std::span<const std::uint8_t> bytes, std::size_t offset) {
		const auto value = Range(bytes, offset, 2, "word");

		return static_cast<std::uint16_t>((std::uint32_t(value[0]) << 8) | value[1]);
	}

	static std::int16_t Signed16(std::span<const std::uint8_t> bytes, std::size_t offset) {
		return std::bit_cast<std::int16_t>(Big16(bytes, offset));
	}

	static std::uint32_t Big32(std::span<const std::uint8_t> bytes, std::size_t offset) {
		return (std::uint32_t(Big16(bytes, offset)) << 16) | Big16(bytes, offset + 2);
	}

	static std::span<const std::uint8_t> Table(const SfntFont& font, std::string_view tag) {
		const auto table = font.FindTable(tag);
		if (!table) {
			throw std::runtime_error("TrueType font lacks table " + std::string(tag));
		}

		return *table;
	}

	static std::span<const std::uint8_t> ReadInstructions(std::span<const std::uint8_t> bytes, std::size_t& cursor) {
		const auto count = Big16(bytes, cursor);
		cursor += 2;
		const auto instructions = Range(bytes, cursor, count, "instructions");
		cursor += count;

		return instructions;
	}

	static std::int32_t Delta(std::span<const std::uint8_t> bytes, std::size_t& cursor, std::uint8_t flag, std::uint8_t short_mask, std::uint8_t same_mask) {
		if ((flag & short_mask) != 0) {
			const auto magnitude = Range(bytes, cursor++, 1, "short coordinate")[0];

			return (flag & same_mask) != 0 ? std::int32_t(magnitude) : -std::int32_t(magnitude);
		}
		if ((flag & same_mask) != 0) {
			return 0;
		}

		const auto value = Signed16(bytes, cursor);
		cursor += 2;

		return value;
	}

	static void ReadSimple(std::span<const std::uint8_t> bytes, std::uint16_t contour_count, TrueTypeGlyph& glyph) {
		if (contour_count > kMaximumContours) {
			throw std::runtime_error("TrueType contour count exceeds the native simple-glyph range");
		}

		std::size_t cursor = 10;
		glyph.ContourEnds.reserve(contour_count);
		for (std::size_t index = 0; index < contour_count; ++index) {
			const auto end = Big16(bytes, cursor);
			cursor += 2;
			if (end >= kMaximumPoints || (!glyph.ContourEnds.empty() && end <= glyph.ContourEnds.back())) {
				throw std::runtime_error("TrueType contour endpoint is out of order or range");
			}

			glyph.ContourEnds.push_back(end);
		}

		glyph.Instructions = ReadInstructions(bytes, cursor);
		const auto point_count = std::size_t(glyph.ContourEnds.back()) + 1;
		std::vector<std::uint8_t> flags;
		flags.reserve(point_count);
		while (flags.size() < point_count) {
			const auto flag = Range(bytes, cursor++, 1, "point flag")[0];
			std::size_t count = 1;
			if ((flag & 8) != 0) {
				count += Range(bytes, cursor++, 1, "point flag repeat")[0];
			}
			if (count > point_count - flags.size()) {
				throw std::runtime_error("TrueType point flag repeat exceeds its contour points");
			}

			flags.insert(flags.end(), count, flag);
		}

		glyph.Points.resize(point_count);
		std::int32_t x = 0;
		for (std::size_t index = 0; index < point_count; ++index) {
			x += Delta(bytes, cursor, flags[index], 2, 0x10);
			glyph.Points[index].X = x;
			glyph.Points[index].Kind = (flags[index] & 1) != 0 ? TrueTypePointKind::OnCurve : TrueTypePointKind::ConicControl;
		}

		std::int32_t y = 0;
		for (std::size_t index = 0; index < point_count; ++index) {
			y += Delta(bytes, cursor, flags[index], 4, 0x20);
			glyph.Points[index].Y = y;
		}
	}

	static void ReadComposite(std::span<const std::uint8_t> bytes, std::uint16_t glyph_count, TrueTypeGlyph& glyph) {
		std::size_t cursor = 10;
		std::uint16_t flags = 0;
		do {
			if (glyph.Components.size() == kMaximumComponents) {
				throw std::runtime_error("TrueType component count exceeds its preparation bound");
			}

			TrueTypeComponent component;
			component.Flags = Big16(bytes, cursor);
			component.GlyphIndex = Big16(bytes, cursor + 2);
			flags = component.Flags;
			cursor += 4;
			if (component.GlyphIndex >= glyph_count) {
				throw std::runtime_error("TrueType component glyph index exceeds the font owner");
			}

			for (auto& argument : component.Arguments) {
				if ((flags & 1) != 0) {
					argument = (flags & 2) != 0 ? std::int32_t(Signed16(bytes, cursor)) : std::int32_t(Big16(bytes, cursor));
					cursor += 2;
				} else {
					const auto byte = Range(bytes, cursor++, 1, "component argument")[0];
					argument = (flags & 2) != 0 ? std::int32_t(std::bit_cast<std::int8_t>(byte)) : std::int32_t(byte);
				}
			}

			if ((flags & 8) != 0) {
				component.Matrix[0] = component.Matrix[3] = std::int32_t(Signed16(bytes, cursor)) * 4;
				cursor += 2;
			} else if ((flags & 0x40) != 0) {
				component.Matrix[0] = std::int32_t(Signed16(bytes, cursor)) * 4;
				component.Matrix[3] = std::int32_t(Signed16(bytes, cursor + 2)) * 4;
				cursor += 4;
			} else if ((flags & 0x80) != 0) {
				component.Matrix[0] = std::int32_t(Signed16(bytes, cursor)) * 4;
				component.Matrix[2] = std::int32_t(Signed16(bytes, cursor + 2)) * 4;
				component.Matrix[1] = std::int32_t(Signed16(bytes, cursor + 4)) * 4;
				component.Matrix[3] = std::int32_t(Signed16(bytes, cursor + 6)) * 4;
				cursor += 8;
			}

			glyph.Components.push_back(component);
		} while ((flags & 0x20) != 0);
		if ((flags & 0x100) != 0) {
			glyph.Instructions = ReadInstructions(bytes, cursor);
		}
	}

	TrueTypeFont::TrueTypeFont(const SfntFont& font) : m_Sfnt(font) {
		if (font.OutlineKind() != SfntOutlineKind::TrueType) {
			throw std::runtime_error("TrueType outline requires its actual SFNT glyf owner");
		}

		m_Glyphs = Table(font, "glyf");
		const auto loca = Table(font, "loca");
		const auto format = Signed16(Table(font, "head"), 50);
		if (format != 0 && format != 1) {
			throw std::runtime_error("TrueType loca format is neither short nor long");
		}

		const auto width = format == 0 ? std::size_t{2} : std::size_t{4};
		const auto count = std::size_t(font.GlyphCount()) + 1;
		static_cast<void>(Range(loca, 0, count * width, "loca entries"));
		m_Offsets.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			const auto offset = format == 0 ? std::uint32_t(Big16(loca, index * width)) * 2 : Big32(loca, index * width);
			if (offset > m_Glyphs.size() || (!m_Offsets.empty() && offset < m_Offsets.back())) {
				throw std::runtime_error("TrueType loca entries are decreasing or exceed glyf storage");
			}

			m_Offsets.push_back(offset);
		}
	}

	TrueTypeGlyph TrueTypeFont::ReadGlyph(std::uint32_t glyph_index) const {
		if (glyph_index >= m_Sfnt.GlyphCount()) {
			throw std::runtime_error("TrueType glyph index exceeds its font owner");
		}

		try {
			const auto begin = m_Offsets[glyph_index];
			const auto end = m_Offsets[std::size_t(glyph_index) + 1];
			const auto bytes = Range(m_Glyphs, begin, std::size_t(end) - begin, "glyph");
			TrueTypeGlyph glyph;
			if (bytes.empty()) {
				return glyph;
			}

			const auto contours = Signed16(bytes, 0);
			glyph.HeaderBounds = {Signed16(bytes, 2), Signed16(bytes, 4), Signed16(bytes, 6), Signed16(bytes, 8)};
			if (contours > 0) {
				glyph.Kind = TrueTypeGlyphKind::Simple;
				ReadSimple(bytes, static_cast<std::uint16_t>(contours), glyph);
			} else if (contours == -1) {
				glyph.Kind = TrueTypeGlyphKind::Composite;
				ReadComposite(bytes, m_Sfnt.GlyphCount(), glyph);
			} else if (contours < -1) {
				throw std::runtime_error("TrueType glyph has an invalid negative contour count");
			}

			return glyph;
		} catch (const std::runtime_error& error) {
			throw std::runtime_error("TrueType glyph " + std::to_string(glyph_index) + ": " + error.what());
		}
	}

} // namespace sgre::resource
