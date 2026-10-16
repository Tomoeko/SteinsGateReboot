#include "sgre/text/truetype_hinted_glyph_font.hpp"

#include "sgre/text/truetype_point_program.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace sgre::text {

	static constexpr std::size_t kMaximumGlyphDepth = 64;
	static constexpr std::size_t kMaximumGlyphVisits = 4096;
	static constexpr std::size_t kMaximumGlyphPoints = 32763;
	static constexpr std::size_t kMaximumGlyphContours = 32767;

	static std::span<const std::uint8_t> Table(const resource::SfntFont& font, std::string_view tag) {
		return font.FindTable(tag).value_or(std::span<const std::uint8_t>{});
	}

	static std::uint16_t Big16(std::span<const std::uint8_t> bytes, std::size_t offset) {
		if (offset > bytes.size() || 2 > bytes.size() - offset) {
			throw std::runtime_error("TrueType hint owner table word exceeds its immutable range");
		}

		return static_cast<std::uint16_t>((std::uint32_t(bytes[offset]) << 8) | bytes[offset + 1]);
	}

	static std::int32_t Coordinate(std::int64_t value) {
		if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
			throw std::runtime_error("TrueType hinted coordinate exceeds its checked signed32 domain");
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

	static std::int32_t FloorPixel(std::int64_t value) {
		const auto pixels = value >= 0 ? value / 64 : -((-value + 63) / 64);

		return Coordinate(pixels * 64);
	}

	static std::int32_t RoundPixel(std::int32_t value) {
		return FloorPixel(std::int64_t(value) + 32);
	}

	static std::int32_t CeilingPixel(std::int64_t value) {
		return FloorPixel(value + 63);
	}

	static void RestoreGraphics(TrueTypeInterpreterState& state, const TrueTypeGraphicsState& saved) {
		// These three interpreter cells are outside the native 0x44-byte saved GS.
		const auto period = state.Graphics.RoundPeriod;
		const auto phase = state.Graphics.RoundPhase;
		const auto threshold = state.Graphics.RoundThreshold;
		state.Graphics = saved;
		state.Graphics.RoundPeriod = period;
		state.Graphics.RoundPhase = phase;
		state.Graphics.RoundThreshold = threshold;
	}

	static void ResetPreparationReferences(TrueTypeGraphicsState& graphics) {
		graphics.ReferencePoints = {};
		graphics.DualProjection = {};
		graphics.Projection = {};
		graphics.Freedom = {};
		graphics.Loop = 1;
		graphics.ZonePointers = {1, 1, 1};
	}

	struct LoadedHintedGlyph {
		TrueTypeHintZone Zone;
		std::array<TrueTypeHintPoint, 4> Phantoms{};
	};

	struct HintLoadWork {
		std::vector<std::uint32_t> Active;
		std::size_t Visits = 0;
	};

	struct TrueTypeHintedGlyphFont::Implementation {
		explicit Implementation(const resource::SfntFont& sfnt) : Sfnt(sfnt), Raw(sfnt), Definitions(Table(sfnt, "fpgm"), ReadTrueTypeDefinitionLimits(Table(sfnt, "maxp"))), Preparation(Table(sfnt, "prep")) {
			for (const auto tag : {"EBDT", "EBLC", "CBDT", "CBLC", "sbix", "fvar", "gvar", "cvar"}) {
				if (sfnt.FindTable(tag)) {
					throw std::runtime_error("TrueType hint owner requires the recovered static outline-only font");
				}
			}

			const auto maxp = Table(sfnt, "maxp");
			const auto twilight_points = std::size_t(Big16(maxp, 16)) + 4;
			if (twilight_points > kMaximumGlyphPoints + 4) {
				throw std::runtime_error("TrueType twilight owner exceeds its bounded native point domain");
			}

			State.Storage.resize(Big16(maxp, 18));
			auto& twilight = Points.Zones[0];
			twilight.Original.resize(twilight_points);
			twilight.Current.resize(twilight_points);
			twilight.Unscaled.resize(twilight_points);
			twilight.Tags.resize(twilight_points);
			const auto cvt = Table(sfnt, "cvt ");
			if (cvt.size() % 2 != 0 || cvt.size() / 2 > 65535) {
				throw std::runtime_error("TrueType CVT table exceeds its complete signed16 word domain");
			}

			DesignControlValues.reserve(cvt.size() / 2);
			for (std::size_t offset = 0; offset < cvt.size(); offset += 2) {
				DesignControlValues.push_back(std::bit_cast<std::int16_t>(Big16(cvt, offset)));
			}

			// PUSH/FDEF declarations mutate only their stack and definition owner. The
			// native interpreter-entry prologue supplies this independent policy byte.
			State.BackwardCompatibility = true;
		}

		void ScaleControlValues() {
			State.ControlValues.resize(DesignControlValues.size());
			for (std::size_t index = 0; index < DesignControlValues.size(); ++index) {
				State.ControlValues[index] = MultiplyFixed(DesignControlValues[index], Scale);
			}
		}

		void RunPreparation() {
			Points.Zones[1] = {};
			Points.OriginalXScale16Dot16 = Scale;
			Points.OriginalYScale16Dot16 = Scale;
			Points.Composite = false;
			if (!Preparation.empty()) {
				TrueTypeProgramCode code;
				code.Preparation = Preparation;
				static_cast<void>(ExecuteTrueTypePointProgram(Definitions, code, TrueTypeProgramRange::Preparation, State, Points));
			}

			ResetPreparationReferences(State.Graphics);
			SavedGraphics = State.Graphics;
		}

		void ApplySize(std::int32_t pixel_size) {
			if (pixel_size == PixelSize && !SizeDirty) {
				return;
			}

			Scale = PrepareVectorFontScale(pixel_size, Sfnt.UnitsPerEm()).Scale16Dot16;
			RestoreGraphics(State, TrueTypeGraphicsState{});
			std::fill(State.Storage.begin(), State.Storage.end(), 0);
			std::fill(Points.Zones[0].Original.begin(), Points.Zones[0].Original.end(), TrueTypeHintPoint{});
			std::fill(Points.Zones[0].Current.begin(), Points.Zones[0].Current.end(), TrueTypeHintPoint{});
			State.Metrics.XPpem = static_cast<std::uint16_t>(pixel_size);
			State.Metrics.YPpem = static_cast<std::uint16_t>(pixel_size);
			State.Metrics.CurrentPpem = static_cast<std::uint16_t>(pixel_size);
			State.Metrics.PointSize = pixel_size * 64;
			State.Metrics.Scale16Dot16 = Scale;
			ScaleControlValues();
			RunPreparation();
			if (!State.Metrics.Version40Flag376) {
				// The first actual flags8 load changes this mode after the first prep;
				// only CVT is reloaded. Its storage and twilight effects remain live.
				State.Metrics.Version40Flag376 = true;
				ScaleControlValues();
				RunPreparation();
			}
			if (SavedGraphics.InstructionControl != 0) {
				throw std::runtime_error("TrueType hinted glyph requires recovered nondefault instruction-control loader policy");
			}

			PixelSize = pixel_size;
			SizeDirty = false;
		}

		std::array<TrueTypeHintPoint, 4> DesignPhantoms(const resource::TrueTypeGlyph& glyph, std::uint32_t glyph_index) const {
			const auto horizontal = Sfnt.HorizontalMetrics(glyph_index);
			const auto vertical = Sfnt.VerticalMetrics(glyph_index);
			if (!vertical) {
				throw std::runtime_error("TrueType hinted glyph requires actual vertical metrics");
			}

			const auto minimum_x = glyph.Kind == resource::TrueTypeGlyphKind::Empty ? 0 : glyph.HeaderBounds.MinimumX;
			const auto maximum_y = glyph.Kind == resource::TrueTypeGlyphKind::Empty ? 0 : glyph.HeaderBounds.MaximumY;
			const auto left = Coordinate(std::int64_t(minimum_x) - horizontal.SideBearing);
			const auto top = Coordinate(std::int64_t(maximum_y) + vertical->SideBearing);
			const auto center_x = State.Metrics.Version40Flag376 ? horizontal.Advance / 2 : 0;

			return {{{left, 0}, {Coordinate(std::int64_t(left) + horizontal.Advance), 0}, {center_x, top}, {center_x, Coordinate(std::int64_t(top) - vertical->Advance)}}};
		}

		std::array<TrueTypeHintPoint, 4> ScaledPhantoms(const std::array<TrueTypeHintPoint, 4>& design) const {
			auto result = design;
			for (auto& point : result) {
				point.X = MultiplyFixed(point.X, Scale);
				point.Y = MultiplyFixed(point.Y, Scale);
			}

			return result;
		}

		void Hint(LoadedHintedGlyph& glyph, std::span<const std::uint8_t> instructions, bool composite) {
			auto& zone = glyph.Zone;
			const auto visible_points = zone.Current.size();
			zone.Current.insert(zone.Current.end(), glyph.Phantoms.begin(), glyph.Phantoms.end());
			zone.Tags.resize(zone.Current.size(), 0);
			zone.Original = zone.Current;
			if (composite) {
				zone.Unscaled = zone.Current;
				for (auto& tag : zone.Tags) {
					tag &= 0xe7;
				}
			}

			zone.Current[visible_points].X = RoundPixel(zone.Current[visible_points].X);
			zone.Current[visible_points + 1].X = RoundPixel(zone.Current[visible_points + 1].X);
			zone.Current[visible_points + 2].Y = RoundPixel(zone.Current[visible_points + 2].Y);
			zone.Current[visible_points + 3].Y = RoundPixel(zone.Current[visible_points + 3].Y);
			RestoreGraphics(State, SavedGraphics);
			if (!instructions.empty()) {
				State.Graphics.DualProjection = {};
				State.Graphics.Projection = {};
				State.Graphics.Freedom = {};
				State.Graphics.ZonePointers = {1, 1, 1};
				State.Graphics.Loop = 1;
				State.Graphics.Round = TrueTypeRoundMode::Grid;
				Points.Zones[1] = std::move(zone);
				Points.OriginalXScale16Dot16 = composite ? 65536 : Scale;
				Points.OriginalYScale16Dot16 = composite ? 65536 : Scale;
				Points.Composite = composite;
				TrueTypeProgramCode code;
				code.Preparation = Preparation;
				code.Glyph = instructions;
				static_cast<void>(ExecuteTrueTypePointProgram(Definitions, code, TrueTypeProgramRange::Glyph, State, Points));
				zone = std::move(Points.Zones[1]);
				if (visible_points != 0) {
					zone.Tags[0] |= static_cast<std::uint8_t>((static_cast<std::uint32_t>(State.Graphics.ScanType) << 5) | 4);
				}
			}

			std::copy_n(zone.Current.begin() + static_cast<std::ptrdiff_t>(visible_points), 4, glyph.Phantoms.begin());
			zone.Current.resize(visible_points);
			zone.Original.resize(visible_points);
			zone.Unscaled.resize(visible_points);
			zone.Tags.resize(visible_points);
		}

		void Append(LoadedHintedGlyph& parent, LoadedHintedGlyph child, const resource::TrueTypeComponent& component) const {
			if ((component.Flags & 0xc8) != 0) {
				throw std::runtime_error("TrueType hinted component matrix transforms remain unrecovered");
			}
			if ((component.Flags & 2) == 0) {
				throw std::runtime_error("TrueType hinted component point attachment remains unrecovered");
			}
			if (child.Zone.Current.size() > kMaximumGlyphPoints - parent.Zone.Current.size() || child.Zone.ContourEnds.size() > kMaximumGlyphContours - parent.Zone.ContourEnds.size()) {
				throw std::runtime_error("TrueType hinted composite exceeds its checked outline budget");
			}
			if ((component.Flags & 0x200) != 0) {
				parent.Phantoms = child.Phantoms;
			}

			auto offset_x = MultiplyFixed(component.Arguments[0], Scale);
			auto offset_y = MultiplyFixed(component.Arguments[1], Scale);
			if ((component.Flags & 4) != 0) {
				offset_x = RoundPixel(offset_x);
				offset_y = RoundPixel(offset_y);
			}

			const auto first = parent.Zone.Current.size();
			for (auto& point : child.Zone.Current) {
				point.X = Coordinate(std::int64_t(point.X) + offset_x);
				point.Y = Coordinate(std::int64_t(point.Y) + offset_y);
			}
			for (const auto end : child.Zone.ContourEnds) {
				parent.Zone.ContourEnds.push_back(static_cast<std::uint16_t>(first + end));
			}

			parent.Zone.Current.insert(parent.Zone.Current.end(), child.Zone.Current.begin(), child.Zone.Current.end());
			parent.Zone.Tags.insert(parent.Zone.Tags.end(), child.Zone.Tags.begin(), child.Zone.Tags.end());
		}

		LoadedHintedGlyph Load(std::uint32_t glyph_index, HintLoadWork& work) {
			if (work.Active.size() == kMaximumGlyphDepth || work.Visits == kMaximumGlyphVisits) {
				throw std::runtime_error("TrueType hinted composite exceeds its recursion/work budget");
			}
			if (std::ranges::find(work.Active, glyph_index) != work.Active.end()) {
				throw std::runtime_error("TrueType hinted composite contains a recursive glyph cycle");
			}

			++work.Visits;
			work.Active.push_back(glyph_index);
			const auto raw = Raw.ReadGlyph(glyph_index);
			LoadedHintedGlyph result;
			const auto phantoms = DesignPhantoms(raw, glyph_index);
			result.Phantoms = ScaledPhantoms(phantoms);
			if (raw.Kind == resource::TrueTypeGlyphKind::Simple) {
				if (raw.Points.size() > kMaximumGlyphPoints) {
					throw std::runtime_error("TrueType hinted simple glyph exceeds its point budget");
				}

				result.Zone.Unscaled.reserve(raw.Points.size() + 4);
				result.Zone.Current.reserve(raw.Points.size() + 4);
				result.Zone.Tags.reserve(raw.Points.size() + 4);
				for (const auto& point : raw.Points) {
					result.Zone.Unscaled.push_back({point.X, point.Y});
					result.Zone.Current.push_back({MultiplyFixed(point.X, Scale), MultiplyFixed(point.Y, Scale)});
					result.Zone.Tags.push_back(static_cast<std::uint8_t>(point.Kind));
				}

				result.Zone.Unscaled.insert(result.Zone.Unscaled.end(), phantoms.begin(), phantoms.end());
				result.Zone.ContourEnds = raw.ContourEnds;
				Hint(result, raw.Instructions, false);
			} else if (raw.Kind == resource::TrueTypeGlyphKind::Composite) {
				for (const auto& component : raw.Components) {
					Append(result, Load(component.GlyphIndex, work), component);
				}
				if (!raw.Instructions.empty() && !result.Zone.Current.empty()) {
					Hint(result, raw.Instructions, true);
				}
			}

			work.Active.pop_back();

			return result;
		}

		const resource::SfntFont& Sfnt;
		resource::TrueTypeFont Raw;
		TrueTypeFontProgram Definitions;
		std::span<const std::uint8_t> Preparation;
		std::vector<std::int32_t> DesignControlValues;
		TrueTypeInterpreterState State;
		TrueTypePointState Points;
		TrueTypeGraphicsState SavedGraphics;
		std::int32_t PixelSize = 0;
		std::int32_t RequestedPixelSize = 0;
		bool SizeDirty = true;
		std::int32_t Scale = 0;
	};

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

	static TrueTypeGlyphMetrics Measure(const TrueTypeHintedGlyph& glyph, const resource::SfntGlyphMetrics& horizontal, std::int32_t scale) {
		const auto& bounds = glyph.ControlBounds;
		const auto& pp = glyph.Phantoms;
		const auto horizontal_advance = Coordinate(std::int64_t(pp[1][0]) - pp[0][0]);
		const auto vertical_units = pp[2][1] > pp[3][1] ? DivideFixed(Coordinate(std::int64_t(pp[2][1]) - pp[3][1]), scale) : 0;
		const auto vertical_advance = static_cast<std::uint16_t>(static_cast<std::uint32_t>(vertical_units));
		const auto top_bearing = DivideFixed(Coordinate(std::int64_t(pp[2][1]) - bounds.MaximumY), scale);
		const auto short_bearing = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<std::uint32_t>(top_bearing)));
		TrueTypeGlyphMetrics metrics;
		metrics.HorizontalBearingX26Dot6 = FloorPixel(bounds.MinimumX);
		metrics.HorizontalBearingY26Dot6 = CeilingPixel(bounds.MaximumY);
		metrics.Width26Dot6 = Coordinate(std::int64_t(CeilingPixel(bounds.MaximumX)) - metrics.HorizontalBearingX26Dot6);
		metrics.Height26Dot6 = Coordinate(std::int64_t(metrics.HorizontalBearingY26Dot6) - FloorPixel(bounds.MinimumY));
		metrics.HorizontalAdvance26Dot6 = RoundPixel(horizontal_advance);
		metrics.VerticalBearingX26Dot6 = FloorPixel(std::int64_t(bounds.MinimumX) - horizontal_advance / 2);
		metrics.VerticalBearingY26Dot6 = FloorPixel(MultiplyFixed(short_bearing, scale));
		metrics.VerticalAdvance26Dot6 = RoundPixel(MultiplyFixed(vertical_advance, scale));
		metrics.LinearHorizontalAdvance16Dot16 = Coordinate((std::int64_t(horizontal.Advance) * scale + 32) / 64);
		metrics.LinearVerticalAdvance16Dot16 = Coordinate((std::int64_t(vertical_advance) * scale + 32) / 64);
		metrics.VerticalOriginX26Dot6 = Coordinate(std::int64_t(metrics.HorizontalBearingX26Dot6) - metrics.VerticalBearingX26Dot6);
		metrics.VerticalOriginY26Dot6 = Coordinate(std::int64_t(metrics.HorizontalBearingY26Dot6) + metrics.VerticalBearingY26Dot6);

		return metrics;
	}

	static std::uint32_t OutlineFlags(const TrueTypeGraphicsState& graphics, std::int32_t pixel_size) {
		std::uint32_t flags = pixel_size < 24 ? 0x100u : 0u;
		if (!graphics.ScanControl) {
			return flags | 8;
		}
		switch (graphics.ScanType) {
		case 0:
			return flags | 0x20;
		case 1:
			return flags;
		case 4:
			return flags | 0x30;
		case 5:
			return flags | 0x10;
		default:
			return flags | 8;
		}
	}

	TrueTypeHintedGlyphFont::TrueTypeHintedGlyphFont(const resource::SfntFont& font) : m_Implementation(std::make_unique<Implementation>(font)) {
	}

	TrueTypeHintedGlyphFont::~TrueTypeHintedGlyphFont() = default;

	void TrueTypeHintedGlyphFont::SetPixelSize(std::int32_t pixel_size) {
		if (pixel_size < 1 || pixel_size > 2000) {
			throw std::runtime_error("TrueType hinted glyph requires a positive integral native size at most2000");
		}
		if (pixel_size != m_Implementation->RequestedPixelSize) {
			m_Implementation->RequestedPixelSize = pixel_size;
			m_Implementation->SizeDirty = true;
		}
	}

	void TrueTypeHintedGlyphFont::InvalidateSize() {
		m_Implementation->SizeDirty = true;
	}

	TrueTypeHintedGlyph TrueTypeHintedGlyphFont::PrepareGlyph(std::uint32_t glyph_index, std::int32_t pixel_size) {
		if (pixel_size < 1 || pixel_size > 2000) {
			throw std::runtime_error("TrueType hinted glyph requires a positive integral native size at most2000");
		}
		if (glyph_index >= m_Implementation->Sfnt.GlyphCount()) {
			throw std::runtime_error("TrueType hinted glyph index exceeds its actual immutable font");
		}

		try {
			SetPixelSize(pixel_size);
			m_Implementation->ApplySize(pixel_size);
			RestoreGraphics(m_Implementation->State, m_Implementation->SavedGraphics);
			HintLoadWork work;
			auto loaded = m_Implementation->Load(glyph_index, work);
			TrueTypeHintedGlyph result;
			result.GlyphIndex = static_cast<std::uint16_t>(glyph_index);
			result.PixelSize = static_cast<std::uint16_t>(pixel_size);
			result.ContourEnds = std::move(loaded.Zone.ContourEnds);
			result.Tags = std::move(loaded.Zone.Tags);
			for (std::size_t index = 0; index < loaded.Zone.Current.size(); ++index) {
				const auto& point = loaded.Zone.Current[index];
				const auto kind = result.Tags[index] & 3;
				if (kind > 1) {
					throw std::runtime_error("TrueType hinted glyph contains an unrecovered non-quadratic point tag");
				}

				result.Points.push_back({Coordinate(std::int64_t(point.X) - loaded.Phantoms[0].X), point.Y, static_cast<resource::TrueTypePointKind>(kind)});
			}
			for (std::size_t index = 0; index < 4; ++index) {
				result.Phantoms[index] = {loaded.Phantoms[index].X, loaded.Phantoms[index].Y};
			}

			result.ControlBounds = ControlBounds(result.Points);
			result.Metrics = Measure(result, m_Implementation->Sfnt.HorizontalMetrics(glyph_index), m_Implementation->Scale);
			result.OutlineFlags = OutlineFlags(m_Implementation->State.Graphics, pixel_size);

			return result;
		} catch (const std::runtime_error& error) {
			throw std::runtime_error("TrueType hinted glyph " + std::to_string(glyph_index) + ": " + error.what());
		}
	}

} // namespace sgre::text
