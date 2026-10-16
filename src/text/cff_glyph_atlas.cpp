#include "sgre/text/cff_glyph_atlas.hpp"

#include "sgre/text/cff_glyph_path.hpp"
#include "sgre/text/glyph_raster_solid_copy.hpp"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::text {

	static constexpr std::int32_t kMaximumGlyphExtentPixels = 65534;
	static constexpr std::size_t kMaximumGlyphPixels = 1024 * 1024;

	class AtlasSurfaceLock {
	public:
		explicit AtlasSurfaceLock(GlyphAtlasCpuSurface& surface)
		    : m_Surface(surface) {
		}
		AtlasSurfaceLock(const AtlasSurfaceLock&) = delete;
		AtlasSurfaceLock& operator=(const AtlasSurfaceLock&) = delete;
		~AtlasSurfaceLock() noexcept {
			UnlockGlyphAtlasSurface(m_Surface);
		}

	private:
		GlyphAtlasCpuSurface& m_Surface;
	};

	static void ValidateBitmapPlan(const CffGlyphBitmapPlan& plan) {
		if (plan.WidthPixels <= 0 || plan.HeightPixels <= 0 || plan.WidthPixels > kMaximumGlyphExtentPixels || plan.HeightPixels > kMaximumGlyphExtentPixels || std::size_t(plan.WidthPixels) > kMaximumGlyphPixels / std::size_t(plan.HeightPixels)) {
			throw std::runtime_error("CFF glyph atlas bitmap exceeds the bounded raster dimensions");
		}
		if (plan.TopPixel == std::numeric_limits<std::int32_t>::min()) {
			throw std::runtime_error("CFF glyph atlas top translation exceeds signed pixel range");
		}
	}

	static void TranslateGlyphPath(CffGlyphPath& path, const CffGlyphPathPoint& translation) {
		for (auto& command : path.Commands) {
			auto point_count = std::size_t(1);
			if (command.Kind == CffGlyphPathCommandKind::Close) {
				point_count = 0;
			} else if (command.Kind == CffGlyphPathCommandKind::QuadraticTo) {
				point_count = 2;
			} else if (command.Kind == CffGlyphPathCommandKind::CubicTo) {
				point_count = 3;
			}
			for (std::size_t index = 0; index < point_count; ++index) {
				command.Points[index].X += translation.X;
				command.Points[index].Y += translation.Y;
			}
		}
	}

	std::optional<CffGlyphStrokeInput> CffGlyphStrokeInput::Prepare(const CffPreparedGlyph& glyph, std::int32_t edge_width_pixels) {
		if (edge_width_pixels <= 0) {
			throw std::runtime_error("CFF glyph stroke input requires a positive actual edge width");
		}

		auto path = glyph.PreparedPath ? *glyph.PreparedPath : PrepareCffGlyphPath(glyph.Outline.Points, glyph.Outline.ContourEnds);
		const auto plan = PlanCffGlyphBitmap(path.Bounds, edge_width_pixels);
		if (!plan) {
			return std::nullopt;
		}

		ValidateBitmapPlan(*plan);
		TranslateGlyphPath(path, plan->PathTranslationPixels);

		return CffGlyphStrokeInput(*plan, std::move(path), edge_width_pixels);
	}

	CffGlyphStrokeInput::CffGlyphStrokeInput(CffGlyphBitmapPlan plan, CffGlyphPath path, std::int32_t edge_width_pixels)
	    : m_Plan(plan), m_Path(std::move(path)), m_EdgeWidthPixels(edge_width_pixels) {
	}

	const CffGlyphBitmapPlan& CffGlyphStrokeInput::BitmapPlan() const {
		return m_Plan;
	}

	std::span<const CffGlyphPathCommand> CffGlyphStrokeInput::Commands() const {
		return m_Path.Commands;
	}

	double CffGlyphStrokeInput::WidthPixels() const {
		return static_cast<double>(m_EdgeWidthPixels);
	}

	std::optional<PreparedCffGlyphRaster> PreparedCffGlyphRaster::Prepare(const CffPreparedGlyph& glyph) {
		auto path = glyph.PreparedPath ? *glyph.PreparedPath : PrepareCffGlyphPath(glyph.Outline.Points, glyph.Outline.ContourEnds);
		const auto plan = PlanCffGlyphBitmap(path.Bounds);
		if (!plan) {
			return std::nullopt;
		}

		ValidateBitmapPlan(*plan);
		TranslateGlyphPath(path, plan->PathTranslationPixels);

		return PrepareContours(*plan, path.Commands);
	}

	PreparedCffGlyphRaster PreparedCffGlyphRaster::PrepareStroke(const CffGlyphStrokeInput& input, std::vector<CffGlyphPathCommand> contours) {
		if (input.Commands().empty() || input.WidthPixels() <= 0) {
			throw std::runtime_error("CFF glyph stroke input no longer owns its translated source path");
		}

		// Native453340 keeps the padded, cleared atlas region even when actual expansion produces no edges.
		return PrepareContours(input.BitmapPlan(), contours);
	}

	PreparedCffGlyphRaster PreparedCffGlyphRaster::PrepareContours(CffGlyphBitmapPlan plan, std::span<const CffGlyphPathCommand> contours) {
		ValidateBitmapPlan(plan);
		GlyphRasterClip clip;
		clip.WidthPixels = static_cast<std::uint16_t>(plan.WidthPixels);
		clip.HeightPixels = static_cast<std::uint16_t>(plan.HeightPixels);
		while (std::uint32_t(clip.WidthPixels) * clip.BandHeightPixels * 4 > 0x40000 && clip.BandHeightPixels > 8) {
			clip.BandHeightPixels /= 2;
		}
		const auto edges = BuildGlyphRasterPathEdges(contours, clip);
		auto cells = AccumulateGlyphRasterCells(edges, clip);

		return PreparedCffGlyphRaster(plan, clip, std::move(cells));
	}

	PreparedCffGlyphRaster::PreparedCffGlyphRaster(CffGlyphBitmapPlan plan, GlyphRasterClip clip, GlyphRasterAreaCells cells)
	    : m_Plan(plan), m_Clip(clip), m_Cells(std::move(cells)) {
	}

	const CffGlyphBitmapPlan& PreparedCffGlyphRaster::BitmapPlan() const {
		return m_Plan;
	}

	const GlyphRasterClip& PreparedCffGlyphRaster::Clip() const {
		return m_Clip;
	}

	const GlyphRasterAreaCells& PreparedCffGlyphRaster::Cells() const {
		return m_Cells;
	}

	void PreparedCffGlyphRaster::RequireValid() const {
		ValidateBitmapPlan(m_Plan);
		if (m_Clip.WidthPixels != m_Plan.WidthPixels || m_Clip.HeightPixels != m_Plan.HeightPixels || m_Cells.WidthPixels != m_Clip.WidthPixels || m_Cells.FirstRowPixel > m_Clip.HeightPixels || m_Cells.RowCount > m_Clip.HeightPixels - m_Cells.FirstRowPixel || m_Cells.Deltas.size() != std::size_t(m_Cells.RowCount) * (std::size_t(m_Cells.WidthPixels) + 1) || m_Cells.ActiveGroups.size() != std::size_t(m_Cells.RowCount) * ((std::size_t(m_Cells.WidthPixels) + 4) / 4)) {
			throw std::runtime_error("prepared CFF glyph raster storage no longer covers its immutable geometry");
		}
	}

	std::optional<CffAtlasGlyph> RasterizePreparedCffGlyphInAtlas(const PreparedCffGlyphRaster& raster, GlyphAtlasPacking& packing, GlyphAtlasCpuSurface& surface) {
		const auto& plan = raster.BitmapPlan();
		const auto& clip = raster.Clip();
		const auto& cells = raster.Cells();
		raster.RequireValid();

		// Validate all supported CPU geometry before mutating the borrowed atlas owner.
		const auto allocation = AllocateGlyphAtlasRegion(packing, surface, clip.WidthPixels, clip.HeightPixels);
		if (!allocation) {
			return std::nullopt;
		}

		const AtlasSurfaceLock lock(surface);
		CompositeGlyphRasterSolidCopyBytes(cells, surface.Pixels.subspan(allocation->DataOffsetBytes), clip.HeightPixels, allocation->RowPitchBytes);
		CffAtlasGlyph result;
		result.Allocation = *allocation;
		result.LeftPixel = plan.LeftPixel;
		result.TopTranslationPixel = -plan.TopPixel;

		return result;
	}

	std::optional<CffAtlasGlyph> RasterizeDefaultCffGlyphInAtlas(const CffGlyphFont& font, std::uint32_t glyph_index, std::int32_t pixel_size, GlyphAtlasPacking& packing, GlyphAtlasCpuSurface& surface) {
		const auto raster = PreparedCffGlyphRaster::Prepare(font.PrepareGlyph(glyph_index, pixel_size));
		if (!raster) {
			return std::nullopt;
		}

		return RasterizePreparedCffGlyphInAtlas(*raster, packing, surface);
	}

} // namespace sgre::text
