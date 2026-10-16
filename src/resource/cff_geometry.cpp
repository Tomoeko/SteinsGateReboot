#include "sgre/resource/cff_geometry.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::resource {

	static constexpr std::size_t kMaxOutlineCommands = 65536;
	static constexpr std::size_t kMaxOutlinePoints = 32767;
	static constexpr double kMinimumDesignCoordinate = -2097152;
	static constexpr double kMaximumDesignCoordinate = 2097151;

	static std::int32_t ReadIntegralCoordinate(double coordinate) {
		if (!std::isfinite(coordinate)) {
			throw std::runtime_error("CFF geometry has a non-finite design coordinate");
		}
		if (coordinate != std::trunc(coordinate)) {
			throw std::runtime_error("CFF geometry fractional design coordinates remain unsupported");
		}
		if (coordinate < kMinimumDesignCoordinate || coordinate > kMaximumDesignCoordinate) {
			throw std::runtime_error("CFF geometry exceeds the native fixed-point coordinate range");
		}

		return static_cast<std::int32_t>(coordinate);
	}

	static CffDesignPoint ReadDesignPoint(const CffOutlinePoint& point, CffPointKind kind) {
		CffDesignPoint result;
		result.X = ReadIntegralCoordinate(point.X);
		result.Y = ReadIntegralCoordinate(point.Y);
		result.Kind = kind;

		return result;
	}

	static void AppendPoint(CffDesignOutline& output, const CffDesignPoint& point) {
		if (output.Points.size() >= kMaxOutlinePoints) {
			throw std::runtime_error("CFF geometry point count exceeds the native signed outline range");
		}

		output.Points.push_back(point);
	}

	static void CloseContour(CffDesignOutline& output, std::size_t start) {
		if (output.Points.size() == start) {
			return;
		}

		const auto& first = output.Points[start];
		const auto& last = output.Points.back();
		if (output.Points.size() > 1 && first.X == last.X && first.Y == last.Y && last.Kind == CffPointKind::OnCurve) {
			output.Points.pop_back();
		}
		if (output.Points.size() == start + 1) {
			output.Points.pop_back();
		} else {
			output.ContourEnds.push_back(static_cast<std::uint16_t>(output.Points.size() - 1));
		}
	}

	CffDesignOutline PrepareCffDesignOutline(const CffOutline& outline) {
		if (outline.Commands.size() > kMaxOutlineCommands) {
			throw std::runtime_error("CFF geometry command count exceeds its work limit");
		}

		CffDesignOutline result;
		CffDesignPoint current;
		std::size_t contour_start = 0;
		bool contour_open = false;
		bool contour_started = false;
		for (const auto& command : outline.Commands) {
			switch (command.Kind) {
			case CffOutlineCommandKind::MoveTo:
				if (contour_open) {
					throw std::runtime_error("CFF geometry contour move lacks its preceding closure");
				}

				current = ReadDesignPoint(command.Points[0], CffPointKind::OnCurve);
				contour_start = result.Points.size();
				contour_open = true;
				contour_started = false;
				break;
			case CffOutlineCommandKind::LineTo:
			case CffOutlineCommandKind::CubicTo:
				if (!contour_open) {
					throw std::runtime_error("CFF geometry drawing command lacks a contour move");
				}
				if (!contour_started) {
					AppendPoint(result, current);
					contour_started = true;
				}

				if (command.Kind == CffOutlineCommandKind::CubicTo) {
					AppendPoint(result, ReadDesignPoint(command.Points[0], CffPointKind::CubicControl));
					AppendPoint(result, ReadDesignPoint(command.Points[1], CffPointKind::CubicControl));
					current = ReadDesignPoint(command.Points[2], CffPointKind::OnCurve);
				} else {
					current = ReadDesignPoint(command.Points[0], CffPointKind::OnCurve);
				}
				AppendPoint(result, current);
				break;
			case CffOutlineCommandKind::ClosePath:
				if (!contour_open) {
					throw std::runtime_error("CFF geometry closure lacks a contour move");
				}

				CloseContour(result, contour_start);
				contour_open = false;
				contour_started = false;
				break;
			default:
				throw std::runtime_error("CFF geometry has an unknown outline command");
			}
		}
		if (contour_open) {
			throw std::runtime_error("CFF geometry has an unclosed contour");
		}

		return result;
	}

} // namespace sgre::resource
