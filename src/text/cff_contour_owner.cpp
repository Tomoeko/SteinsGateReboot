#include "sgre/text/cff_contour_owner.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace sgre::text {

	static bool SamePoint(CffFixedPoint first, CffFixedPoint second) {
		return first.X16Dot16 == second.X16Dot16 && first.Y16Dot16 == second.Y16Dot16;
	}

	static std::int32_t AddFixed(std::int32_t first, std::int32_t second) {
		return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(first) + std::bit_cast<std::uint32_t>(second));
	}

	static CffFixedPoint AddPoint(CffFixedPoint point, CffFixedPoint offset) {
		CffFixedPoint result;
		result.X16Dot16 = AddFixed(point.X16Dot16, offset.X16Dot16);
		result.Y16Dot16 = AddFixed(point.Y16Dot16, offset.Y16Dot16);

		return result;
	}

	static std::int32_t FloorFontUnits(std::int32_t value) {
		return value >= 0 ? value / 65536 : static_cast<std::int32_t>(-((-std::int64_t(value) + 65535) / 65536));
	}

	static void AddMiddleCubicWinding(CffContourDarkeningState& state, CffFixedPoint first_control, CffFixedPoint second_control) {
		const auto dx = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(second_control.X16Dot16) - std::bit_cast<std::uint32_t>(first_control.X16Dot16));
		const auto dy = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(second_control.Y16Dot16) - std::bit_cast<std::uint32_t>(first_control.Y16Dot16));
		const auto first_product = std::bit_cast<std::uint32_t>(FloorFontUnits(dy)) * std::bit_cast<std::uint32_t>(FloorFontUnits(first_control.X16Dot16));
		const auto second_product = std::bit_cast<std::uint32_t>(FloorFontUnits(dx)) * std::bit_cast<std::uint32_t>(FloorFontUnits(first_control.Y16Dot16));
		state.WindingArea = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(state.WindingArea) + first_product - second_product);
	}

	static void AppendCallback(CffContourOwnerStep& step, CffContourCallback callback) {
		if (step.CallbackCount == step.Callbacks.size()) {
			throw std::runtime_error("CFF contour owner exceeds its per-operation callback bound");
		}

		step.Callbacks[step.CallbackCount++] = callback;
	}

	static void AppendStep(CffContourOwnerStep& destination, const CffContourOwnerStep& source) {
		for (std::size_t index = 0; index < source.CallbackCount; ++index) {
			AppendCallback(destination, source.Callbacks[index]);
		}
	}

	CffContourOwner::CffContourOwner(const CffContourConfiguration& configuration, std::span<const CffStemHint> horizontal_stems, std::size_t vertical_stem_count, CffHintMask mask) {
		if (horizontal_stems.size() > kCffStemHintLimit || vertical_stem_count > kCffStemHintLimit - horizontal_stems.size()) {
			throw std::runtime_error("CFF contour owner exceeds its declared hint bound");
		}
		if (configuration.BaseScale16Dot16 <= 0 || configuration.BlueZones.VerticalScale16Dot16 <= 0 || configuration.BlueZones.ZoneCount > configuration.BlueZones.Zones.size() || configuration.BlueZones.BlueFuzz16Dot16 < 0 || configuration.BlueZones.BlueShift16Dot16 < 0 || configuration.Darkening.HorizontalDarkening16Dot16 < 0 || configuration.Darkening.VerticalDarkening16Dot16 < 0 || configuration.JunctionLimits.MaximumMidpointDistance16Dot16 < 0 || configuration.JunctionLimits.AxisSnapTolerance16Dot16 < 0) {
			throw std::runtime_error("CFF contour owner prepared configuration is invalid");
		}
		if (mask.Valid && mask.HintCount != horizontal_stems.size() + vertical_stem_count) {
			throw std::runtime_error("CFF contour owner mask count differs from declared hints");
		}

		m_State.Configuration = configuration;
		m_State.HorizontalStems.Count = horizontal_stems.size();
		std::copy(horizontal_stems.begin(), horizontal_stems.end(), m_State.HorizontalStems.Stems.begin());
		m_State.VerticalStemCount = vertical_stem_count;
		m_State.Mask = mask;
		if (!m_State.Mask.Valid) {
			m_State.Mask.New = false;
		}
		for (auto* map : {&m_State.InitialMap, &m_State.CurrentMap, &m_State.FirstContourMap}) {
			map->BaseScale16Dot16 = configuration.BaseScale16Dot16;
			map->Enabled = configuration.HintingEnabled;
		}
		m_State.Pending.Kind = CffPendingSegmentKind::None;
	}

	void CffContourOwner::RebuildCurrentMap() {
		CffStemMapCoordinates coordinates;
		coordinates.VerticalOrigin16Dot16 = m_State.Configuration.VerticalOrigin16Dot16;
		coordinates.VerticalDarkening16Dot16 = m_State.Configuration.Darkening.VerticalDarkening16Dot16;
		BuildCffGlyphHintMap(m_State.CurrentMap, m_State.InitialMap, m_State.Configuration.BlueZones, std::span<CffStemHint>(m_State.HorizontalStems.Stems).first(m_State.HorizontalStems.Count), m_State.VerticalStemCount, m_State.Mask, coordinates);
	}

	void CffContourOwner::BeginContour(CffFixedPoint start, CffContourOwnerStep& step) {
		if (!m_State.CurrentMap.Initialized) {
			AppendStep(step, MoveTo(m_State.FirstLogicalPoint));
		}

		CffContourCallback callback;
		callback.Kind = CffContourCallbackKind::Move;
		callback.Points[0] = m_State.Pending.CurrentDevicePoint;
		callback.Points[1] = TransformCffHintedPoint(m_State.CurrentMap, m_State.Configuration.Transform, start);
		AppendCallback(step, callback);
		m_State.Pending.CurrentDevicePoint = callback.Points[1];
		m_State.FirstDarkenedStart = start;
	}

	CffFixedPoint CffContourOwner::FlushPending(CffFixedPoint next_start, CffFixedPoint next_end, bool closing, CffContourOwnerStep& step) {
		CffContourJoinRequest request;
		request.NextStart = next_start;
		request.NextEnd = next_end;
		request.ClosingContour = closing;
		const auto flushed = FlushCffPendingContour(m_State.Pending, m_State.CurrentMap, m_State.FirstContourMap, request, m_State.Configuration.Transform, m_State.Configuration.JunctionLimits);
		for (std::size_t index = 0; index < flushed.SegmentCount; ++index) {
			CffContourCallback callback;
			callback.Kind = static_cast<CffContourCallbackKind>(flushed.Segments[index].Kind);
			callback.Points = flushed.Segments[index].Points;
			AppendCallback(step, callback);
		}

		return flushed.NextStart;
	}

	CffContourOwnerStep CffContourOwner::MoveTo(CffFixedPoint point) {
		auto step = CloseContour();
		m_State.FirstLogicalPoint = point;
		m_State.CurrentLogicalPoint = point;
		m_State.FirstSegment = true;
		if (!m_State.CurrentMap.Initialized || m_State.Mask.New) {
			RebuildCurrentMap();
		}
		m_State.FirstContourMap = m_State.CurrentMap;

		return step;
	}

	CffContourOwnerStep CffContourOwner::LineTo(CffFixedPoint point) {
		CffContourOwnerStep step;
		const bool dirty_mask = m_State.Mask.New && !m_State.ClosingContour;
		if (SamePoint(point, m_State.CurrentLogicalPoint) && !dirty_mask) {
			return step;
		}

		const auto offset = ComputeCffContourDarkeningOffset(m_State.Configuration.Darkening, m_State.CurrentLogicalPoint, point);
		auto start = AddPoint(m_State.CurrentLogicalPoint, offset);
		const auto end = AddPoint(point, offset);
		if (m_State.FirstSegment) {
			BeginContour(start, step);
			m_State.FirstSegment = false;
			m_State.ContourOpen = true;
			m_State.FirstDarkenedDirectionPoint = end;
		}
		if (m_State.HasPendingSegment) {
			start = FlushPending(start, end, false, step);
		}

		m_State.Pending.Kind = CffPendingSegmentKind::Line;
		m_State.Pending.LogicalPoints[0] = start;
		m_State.Pending.LogicalPoints[1] = end;
		m_State.HasPendingSegment = true;
		if (dirty_mask) {
			RebuildCurrentMap();
		}
		m_State.CurrentLogicalPoint = point;

		return step;
	}

	CffContourOwnerStep CffContourOwner::CubicTo(CffFixedPoint first_control, CffFixedPoint second_control, CffFixedPoint endpoint) {
		CffContourOwnerStep step;
		const auto first_offset = ComputeCffContourDarkeningOffset(m_State.Configuration.Darkening, m_State.CurrentLogicalPoint, first_control);
		const auto last_offset = ComputeCffContourDarkeningOffset(m_State.Configuration.Darkening, second_control, endpoint);
		// Native cubic preparation accumulates the middle control edge even when darkening is disabled.
		AddMiddleCubicWinding(m_State.Configuration.Darkening, first_control, second_control);
		auto start = AddPoint(m_State.CurrentLogicalPoint, first_offset);
		const auto first = AddPoint(first_control, first_offset);
		const auto second = AddPoint(second_control, last_offset);
		const auto end = AddPoint(endpoint, last_offset);
		if (m_State.FirstSegment) {
			BeginContour(start, step);
			m_State.FirstSegment = false;
			m_State.ContourOpen = true;
			m_State.FirstDarkenedDirectionPoint = first;
		}
		if (m_State.HasPendingSegment) {
			start = FlushPending(start, first, false, step);
		}

		m_State.Pending.Kind = CffPendingSegmentKind::Cubic;
		m_State.Pending.LogicalPoints[0] = start;
		m_State.Pending.LogicalPoints[1] = first;
		m_State.Pending.LogicalPoints[2] = second;
		m_State.Pending.LogicalPoints[3] = end;
		m_State.HasPendingSegment = true;
		if (m_State.Mask.New) {
			RebuildCurrentMap();
		}
		m_State.CurrentLogicalPoint = endpoint;

		return step;
	}

	CffContourOwnerStep CffContourOwner::CloseContour() {
		CffContourOwnerStep step;
		if (!m_State.ContourOpen) {
			return step;
		}

		m_State.ClosingContour = true;
		AppendStep(step, LineTo(m_State.FirstLogicalPoint));
		if (m_State.HasPendingSegment) {
			m_State.FirstDarkenedStart = FlushPending(m_State.FirstDarkenedStart, m_State.FirstDarkenedDirectionPoint, true, step);
		}
		m_State.FirstSegment = true;
		m_State.ContourOpen = false;
		m_State.ClosingContour = false;
		m_State.HasPendingSegment = false;

		return step;
	}

	void CffContourOwner::AppendHorizontalStems(std::span<const CffStemHint> stems) {
		if (m_State.Mask.Valid || stems.size() > kCffStemHintLimit - m_State.HorizontalStems.Count - m_State.VerticalStemCount) {
			throw std::runtime_error("CFF contour owner cannot append these horizontal declarations");
		}

		std::copy(stems.begin(), stems.end(), m_State.HorizontalStems.Stems.data() + m_State.HorizontalStems.Count);
		m_State.HorizontalStems.Count += stems.size();
	}

	void CffContourOwner::AppendVerticalStemCount(std::size_t count) {
		if (m_State.Mask.Valid || count > kCffStemHintLimit - m_State.HorizontalStems.Count - m_State.VerticalStemCount) {
			throw std::runtime_error("CFF contour owner cannot append these vertical declarations");
		}

		m_State.VerticalStemCount += count;
	}

	std::size_t CffContourOwner::UpdateHintMask(std::span<const std::uint8_t> program_bytes) {
		return UpdateCffCharstringHintMask(m_State.Mask, program_bytes, m_State.HorizontalStems.Count, m_State.VerticalStemCount);
	}

	CffCounterMaskPreparation CffContourOwner::ApplyCounterMask(std::span<const std::uint8_t> program_bytes) {
		CffCounterMaskConfiguration configuration;
		configuration.BaseScale16Dot16 = m_State.Configuration.BaseScale16Dot16;
		configuration.HintingEnabled = m_State.Configuration.HintingEnabled;
		configuration.VerticalDarkening16Dot16 = m_State.Configuration.Darkening.VerticalDarkening16Dot16;

		return PrepareCffCounterHintMap(m_State.InitialMap, m_State.Configuration.BlueZones, std::span<CffStemHint>(m_State.HorizontalStems.Stems).first(m_State.HorizontalStems.Count), m_State.VerticalStemCount, program_bytes, configuration);
	}

	const CffContourOwnerState& CffContourOwner::State() const {
		return m_State;
	}

} // namespace sgre::text
