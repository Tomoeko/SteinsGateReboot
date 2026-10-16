#include "sgre/text/truetype_glyph_font.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

namespace sgre::text {

	static constexpr std::size_t kMaximumOutlinePoints = 32767;
	static constexpr std::size_t kMaximumOutlineContours = 32767;
	static constexpr std::size_t kMaximumGlyphDepth = 64;
	static constexpr std::size_t kMaximumGlyphVisits = 4096;

	struct TrueTypeLoadContext {
		TrueTypeLoadContext(const resource::SfntFont& sfnt, const resource::TrueTypeFont& font, std::int32_t x_scale, std::int32_t y_scale) : Sfnt(sfnt), Font(font), XScale(x_scale), YScale(y_scale) {
		}

		const resource::SfntFont& Sfnt;
		const resource::TrueTypeFont& Font;
		std::int32_t XScale = 0;
		std::int32_t YScale = 0;
		std::vector<std::uint32_t> Active;
		std::size_t Visits = 0;
	};

	static std::int32_t Coordinate(std::int64_t value) {
		if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("TrueType outline coordinate exceeds its native signed range");
		}

		return static_cast<std::int32_t>(value);
	}

	static std::int32_t MultiplyFixed(std::int32_t value, std::int32_t scale) {
		const auto product = std::int64_t(value) * scale;
		const auto magnitude = product < 0 ? -product : product;
		const auto rounded = (magnitude + 32768) / 65536;

		return Coordinate(product < 0 ? -rounded : rounded);
	}

	static std::int32_t DivideFixed(std::int32_t value, std::int32_t scale) {
		const auto numerator = std::int64_t(value) * 65536;
		const auto magnitude = numerator < 0 ? -numerator : numerator;
		const auto rounded = (magnitude + scale / 2) / scale;

		return Coordinate(numerator < 0 ? -rounded : rounded);
	}

	static std::int32_t RoundOffset(std::int32_t value) {
		const auto shifted = std::int64_t(value) + 32;
		const auto quotient = shifted >= 0 ? shifted / 64 : -((-shifted + 63) / 64);

		return Coordinate(quotient * 64);
	}

	static std::array<std::array<std::int32_t, 2>, 4> PreparePhantoms(const resource::TrueTypeGlyph& glyph, const resource::SfntGlyphMetrics& horizontal, const resource::SfntGlyphMetrics& vertical, const TrueTypeLoadContext& context) {
		// Native empty glyphs zero the header bounds before preparing their metrics.
		const auto minimum_x = glyph.Kind == resource::TrueTypeGlyphKind::Empty ? 0 : glyph.HeaderBounds.MinimumX;
		const auto maximum_y = glyph.Kind == resource::TrueTypeGlyphKind::Empty ? 0 : glyph.HeaderBounds.MaximumY;
		const auto left = Coordinate(std::int64_t(minimum_x) - horizontal.SideBearing);
		const auto right = Coordinate(std::int64_t(left) + horizontal.Advance);
		const auto top = Coordinate(std::int64_t(maximum_y) + vertical.SideBearing);
		const auto bottom = Coordinate(std::int64_t(top) - vertical.Advance);

		return {{{MultiplyFixed(left, context.XScale), 0}, {MultiplyFixed(right, context.XScale), 0}, {0, MultiplyFixed(top, context.YScale)}, {0, MultiplyFixed(bottom, context.YScale)}}};
	}

	static TrueTypeUnhintedGlyph LoadGlyph(TrueTypeLoadContext& context, std::uint32_t glyph_index);

	static void AppendComponent(TrueTypeUnhintedGlyph& destination, TrueTypeUnhintedGlyph child, const resource::TrueTypeComponent& component, const TrueTypeLoadContext& context) {
		if ((component.Flags & 0xc8) != 0) {
			throw std::runtime_error("TrueType unhinted component matrix transforms remain unrecovered");
		}
		if ((component.Flags & 2) == 0) {
			throw std::runtime_error("TrueType unhinted component point attachment remains unrecovered");
		}
		if (child.Points.size() > kMaximumOutlinePoints - destination.Points.size() || child.ContourEnds.size() > kMaximumOutlineContours - destination.ContourEnds.size()) {
			throw std::runtime_error("TrueType composite exceeds native outline counts");
		}

		// Metric ownership is chosen before positioning the child's visible points.
		if ((component.Flags & 0x200) != 0) {
			destination.Phantoms = child.Phantoms;
		}

		auto offset_x = MultiplyFixed(component.Arguments[0], context.XScale);
		auto offset_y = MultiplyFixed(component.Arguments[1], context.YScale);
		if ((component.Flags & 4) != 0) {
			offset_x = RoundOffset(offset_x);
			offset_y = RoundOffset(offset_y);
		}

		const auto first = destination.Points.size();
		for (auto& point : child.Points) {
			point.X = Coordinate(std::int64_t(point.X) + offset_x);
			point.Y = Coordinate(std::int64_t(point.Y) + offset_y);
		}
		for (const auto end : child.ContourEnds) {
			destination.ContourEnds.push_back(static_cast<std::uint16_t>(first + end));
		}

		destination.Points.insert(destination.Points.end(), child.Points.begin(), child.Points.end());
	}

	static TrueTypeUnhintedGlyph LoadGlyph(TrueTypeLoadContext& context, std::uint32_t glyph_index) {
		if (context.Active.size() == kMaximumGlyphDepth || context.Visits == kMaximumGlyphVisits) {
			throw std::runtime_error("TrueType composite recursion or total glyph work exceeds its preparation bound");
		}
		if (std::ranges::find(context.Active, glyph_index) != context.Active.end()) {
			throw std::runtime_error("TrueType composite contains a recursive glyph cycle");
		}

		++context.Visits;
		context.Active.push_back(glyph_index);
		const auto program = context.Font.ReadGlyph(glyph_index);
		const auto horizontal = context.Sfnt.HorizontalMetrics(glyph_index);
		const auto vertical = context.Sfnt.VerticalMetrics(glyph_index);
		if (!vertical) {
			throw std::runtime_error("TrueType unhinted outline requires actual vertical metrics");
		}

		TrueTypeUnhintedGlyph result;
		result.GlyphIndex = static_cast<std::uint16_t>(glyph_index);
		result.Phantoms = PreparePhantoms(program, horizontal, *vertical, context);
		if (program.Kind == resource::TrueTypeGlyphKind::Simple) {
			result.Points.reserve(program.Points.size());
			for (const auto& point : program.Points) {
				result.Points.push_back({MultiplyFixed(point.X, context.XScale), MultiplyFixed(point.Y, context.YScale), point.Kind});
			}

			result.ContourEnds = program.ContourEnds;
		} else if (program.Kind == resource::TrueTypeGlyphKind::Composite) {
			for (const auto& component : program.Components) {
				AppendComponent(result, LoadGlyph(context, component.GlyphIndex), component, context);
			}
		}

		context.Active.pop_back();

		return result;
	}

	static resource::TrueTypeBounds ControlBounds(std::span<const resource::TrueTypePoint> points) {
		resource::TrueTypeBounds bounds;
		if (points.empty()) {
			return bounds;
		}

		bounds.MinimumX = bounds.MaximumX = points.front().X;
		bounds.MinimumY = bounds.MaximumY = points.front().Y;
		for (const auto& point : points) {
			bounds.MinimumX = std::min(bounds.MinimumX, point.X);
			bounds.MinimumY = std::min(bounds.MinimumY, point.Y);
			bounds.MaximumX = std::max(bounds.MaximumX, point.X);
			bounds.MaximumY = std::max(bounds.MaximumY, point.Y);
		}

		return bounds;
	}

	static TrueTypeGlyphMetrics Measure(const TrueTypeUnhintedGlyph& glyph, const resource::SfntGlyphMetrics& horizontal, const TrueTypeLoadContext& context) {
		const auto& bounds = glyph.ControlBounds;
		const auto& pp = glyph.Phantoms;
		const auto horizontal_advance = Coordinate(std::int64_t(pp[1][0]) - pp[0][0]);
		const auto vertical_units = pp[2][1] > pp[3][1] ? DivideFixed(Coordinate(std::int64_t(pp[2][1]) - pp[3][1]), context.YScale) : 0;
		const auto vertical_advance = static_cast<std::uint16_t>(static_cast<std::uint32_t>(vertical_units));
		const auto top_bearing = DivideFixed(Coordinate(std::int64_t(pp[2][1]) - bounds.MaximumY), context.YScale);
		const auto short_bearing = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::uint32_t>(top_bearing)));
		TrueTypeGlyphMetrics metrics;
		metrics.Width26Dot6 = Coordinate(std::int64_t(bounds.MaximumX) - bounds.MinimumX);
		metrics.Height26Dot6 = Coordinate(std::int64_t(bounds.MaximumY) - bounds.MinimumY);
		metrics.HorizontalBearingX26Dot6 = bounds.MinimumX;
		metrics.HorizontalBearingY26Dot6 = bounds.MaximumY;
		metrics.HorizontalAdvance26Dot6 = horizontal_advance;
		metrics.VerticalBearingX26Dot6 = Coordinate(std::int64_t(bounds.MinimumX) - horizontal_advance / 2);
		// Native divides to design units, narrows to signed16, then scales back.
		metrics.VerticalBearingY26Dot6 = MultiplyFixed(short_bearing, context.YScale);
		metrics.VerticalAdvance26Dot6 = MultiplyFixed(vertical_advance, context.YScale);
		metrics.LinearHorizontalAdvance16Dot16 = Coordinate((std::int64_t(horizontal.Advance) * context.XScale + 32) / 64);
		metrics.LinearVerticalAdvance16Dot16 = Coordinate((std::int64_t(vertical_advance) * context.YScale + 32) / 64);
		metrics.VerticalOriginX26Dot6 = Coordinate(std::int64_t(metrics.HorizontalBearingX26Dot6) - metrics.VerticalBearingX26Dot6);
		metrics.VerticalOriginY26Dot6 = Coordinate(std::int64_t(metrics.HorizontalBearingY26Dot6) + metrics.VerticalBearingY26Dot6);

		return metrics;
	}

	TrueTypeGlyphFont::TrueTypeGlyphFont(const resource::SfntFont& font) : m_Sfnt(font), m_Font(font) {
		for (const auto tag : {"EBDT", "EBLC", "CBDT", "CBLC", "sbix", "fvar"}) {
			if (font.FindTable(tag)) {
				throw std::runtime_error("TrueType unhinted outline requires the recovered static outline-only owner");
			}
		}
	}

	TrueTypeUnhintedGlyph TrueTypeGlyphFont::PrepareUnhintedGlyph(std::uint32_t glyph_index, VectorFontScale horizontal_scale, VectorFontScale vertical_scale) const {
		if (horizontal_scale.Scale16Dot16 <= 0 || vertical_scale.Scale16Dot16 <= 0) {
			throw std::runtime_error("TrueType unhinted outline requires positive actual TT axis scales");
		}

		try {
			TrueTypeLoadContext context(m_Sfnt, m_Font, horizontal_scale.Scale16Dot16, vertical_scale.Scale16Dot16);
			auto glyph = LoadGlyph(context, glyph_index);
			// Only the completed root outline is translated; child points remain in their own native plane during assembly.
			for (auto& point : glyph.Points) {
				point.X = Coordinate(std::int64_t(point.X) - glyph.Phantoms[0][0]);
			}

			glyph.ControlBounds = ControlBounds(glyph.Points);
			glyph.Metrics = Measure(glyph, m_Sfnt.HorizontalMetrics(glyph_index), context);

			return glyph;
		} catch (const std::runtime_error& error) {
			throw std::runtime_error("TrueType unhinted glyph " + std::to_string(glyph_index) + ": " + error.what());
		}
	}

} // namespace sgre::text
