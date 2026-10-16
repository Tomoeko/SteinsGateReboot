#include "sgre/text/cff_contour_outline.hpp"

#include <stdexcept>

namespace sgre::text {

	static constexpr std::size_t kMaximumCallbacks = 65536;
	static constexpr std::size_t kMaximumOutlinePoints = 32767;

	static std::int32_t FloorToOutlineCoordinate(std::int32_t value) {
		return value >= 0 ? value / 1024 : static_cast<std::int32_t>(-((-std::int64_t(value) + 1023) / 1024));
	}

	static CffScaledPoint PreparePoint(CffFixedPoint point, resource::CffPointKind kind) {
		CffScaledPoint result;
		result.X26Dot6 = FloorToOutlineCoordinate(point.X16Dot16);
		result.Y26Dot6 = FloorToOutlineCoordinate(point.Y16Dot16);
		result.Kind = kind;

		return result;
	}

	static void AppendPoint(CffContourOutline& outline, CffFixedPoint point, resource::CffPointKind kind) {
		if (outline.Points.size() == kMaximumOutlinePoints) {
			throw std::runtime_error("CFF callback outline exceeds the native signed point-count range");
		}

		outline.Points.push_back(PreparePoint(point, kind));
	}

	static void CloseContour(CffContourOutline& outline, std::size_t start) {
		if (outline.Points.size() == start) {
			return;
		}

		const auto& first = outline.Points[start];
		const auto& last = outline.Points.back();
		if (outline.Points.size() > 1 && first.X26Dot6 == last.X26Dot6 && first.Y26Dot6 == last.Y26Dot6 && last.Kind == resource::CffPointKind::OnCurve) {
			outline.Points.pop_back();
		}
		if (outline.Points.size() == start + 1) {
			outline.Points.pop_back();
		} else {
			outline.ContourEnds.push_back(static_cast<std::uint16_t>(outline.Points.size() - 1));
		}
	}

	CffContourOutline BuildCffContourOutline(std::span<const CffContourCallback> callbacks) {
		if (callbacks.size() > kMaximumCallbacks) {
			throw std::runtime_error("CFF callback outline exceeds its preparation work limit");
		}

		CffContourOutline result;
		std::size_t contour_start = 0;
		bool started = false;
		for (const auto& callback : callbacks) {
			if (callback.Kind == CffContourCallbackKind::Move) {
				CloseContour(result, contour_start);
				contour_start = result.Points.size();
				started = false;
				continue;
			}
			if (callback.Kind != CffContourCallbackKind::Line && callback.Kind != CffContourCallbackKind::Cubic) {
				throw std::runtime_error("CFF callback outline has an unsupported callback kind");
			}
			if (!started) {
				AppendPoint(result, callback.Points[0], resource::CffPointKind::OnCurve);
				started = true;
			}
			if (callback.Kind == CffContourCallbackKind::Line) {
				AppendPoint(result, callback.Points[1], resource::CffPointKind::OnCurve);
			} else {
				AppendPoint(result, callback.Points[1], resource::CffPointKind::CubicControl);
				AppendPoint(result, callback.Points[2], resource::CffPointKind::CubicControl);
				AppendPoint(result, callback.Points[3], resource::CffPointKind::OnCurve);
			}
		}
		CloseContour(result, contour_start);

		return result;
	}

} // namespace sgre::text
