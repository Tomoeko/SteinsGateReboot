#include "sgre/scene/emote_catenary_curve.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::scene {
	static constexpr double kCatenaryTolerance = 1.0e-9;
	static constexpr std::size_t kCatenaryIterations = 80;

	static double CatenaryHeightDifference(double scale, double offset, const std::array<double, 2>& first, const std::array<double, 2>& second) {
		return (std::cosh((first[0] - offset) / scale) - std::cosh((second[0] - offset) / scale)) * scale - (first[1] - second[1]);
	}

	static std::optional<double> SolveCatenaryOffset(double scale, const std::array<double, 2>& first, const std::array<double, 2>& second) {
		auto extension = std::max(std::abs(second[0] - first[0]), 1.0);
		auto lower = std::min(first[0], second[0]) - extension * 4.0;
		auto upper = std::max(first[0], second[0]) + extension * 4.0;
		auto first_value = CatenaryHeightDifference(scale, lower, first, second);
		auto last_value = CatenaryHeightDifference(scale, upper, first, second);
		for (std::size_t iteration = 0; iteration != 6 && first_value * last_value > 0; ++iteration) {
			extension += extension;
			lower -= extension;
			upper += extension;
			first_value = CatenaryHeightDifference(scale, lower, first, second);
			last_value = CatenaryHeightDifference(scale, upper, first, second);
		}
		if (first_value * last_value > 0) {
			return std::nullopt;
		}
		auto offset = (lower + upper) * 0.5;
		for (std::size_t iteration = 0; iteration != kCatenaryIterations; ++iteration) {
			const auto first_arg = (first[0] - offset) / scale;
			const auto last_arg = (second[0] - offset) / scale;
			const auto derivative = std::sinh(last_arg) - std::sinh(first_arg);
			if (!(std::abs(derivative) <= 1.0e-6)) {
				const auto error = ((std::cosh(first_arg) - std::cosh(last_arg)) * scale) - (first[1] - second[1]);
				const auto next = offset - error / derivative;
				if (!(next <= lower) && !(upper <= next)) {
					offset = next;
				}
			}
			const auto value = CatenaryHeightDifference(scale, offset, first, second);
			if (std::abs(value) < kCatenaryTolerance) {
				return offset;
			}
			first_value = CatenaryHeightDifference(scale, lower, first, second);
			if (first_value * value >= 0) {
				lower = offset;
			} else {
				upper = offset;
			}
			offset = (upper + lower) * 0.5;
		}
		return std::nullopt;
	}

	static double CatenaryArcError(double scale, const std::array<double, 2>& first, const std::array<double, 2>& second, double length) {
		const auto offset = SolveCatenaryOffset(scale, first, second);
		if (!offset) {
			return std::numeric_limits<double>::infinity();
		}
		return (std::sinh((second[0] - *offset) / scale) - std::sinh((first[0] - *offset) / scale)) * scale - length;
	}

	std::optional<EmoteCatenaryCurve> PrepareEmoteCatenaryCurve(const std::array<double, 2>& first, const std::array<double, 2>& second, double length) {
		if (!std::isfinite(first[0]) || !std::isfinite(first[1]) || !std::isfinite(second[0]) || !std::isfinite(second[1]) || !std::isfinite(length) || !(length > 0)) {
			throw std::runtime_error("Emote Catenary requires finite actual endpoints and positive rest length");
		}
		if (first[0] == second[0]) {
			return std::nullopt;
		}
		if (first[0] > second[0]) {
			return PrepareEmoteCatenaryCurve(second, first, length);
		}
		const auto distance = std::abs(second[0] - first[0]);
		auto lower = distance * 0.1;
		auto upper = distance * 10.0;
		auto first_value = CatenaryArcError(lower, first, second, length);
		auto last_value = CatenaryArcError(upper, first, second, length);
		for (std::size_t iteration = 0; iteration != 8 && first_value * last_value > 0; ++iteration) {
			lower *= 0.5;
			upper += upper;
			first_value = CatenaryArcError(lower, first, second, length);
			last_value = CatenaryArcError(upper, first, second, length);
		}
		if (!(first_value * last_value <= 0)) {
			return std::nullopt;
		}
		double scale = 0;
		bool converged = false;
		for (std::size_t iteration = 0; iteration != kCatenaryIterations; ++iteration) {
			scale = (upper + lower) * 0.5;
			const auto value = CatenaryArcError(scale, first, second, length);
			if (std::isfinite(value)) {
				if (std::abs(value) < kCatenaryTolerance || std::abs(upper - lower) < kCatenaryTolerance) {
					converged = true;
					break;
				}
				if (value * first_value >= 0) {
					lower = scale;
					first_value = value;
				} else {
					upper = scale;
				}
			} else {
				lower *= 0.8;
				first_value = CatenaryArcError(lower, first, second, length);
			}
		}
		if (!converged) {
			return std::nullopt;
		}
		const auto offset = SolveCatenaryOffset(scale, first, second);
		if (!offset) {
			return std::nullopt;
		}
		EmoteCatenaryCurve result;
		result.OffsetX = *offset;
		result.OffsetY = first[1] - std::cosh((first[0] - *offset) / scale) * scale;
		result.Scale = scale;
		if (!std::isfinite(result.OffsetY)) {
			return std::nullopt;
		}
		return result;
	}

	std::array<double, 2> EmoteCatenaryCurve::SampleArc(double first_x, double last_x, std::size_t index, std::size_t segments) const {
		if (!std::isfinite(OffsetX) || !std::isfinite(OffsetY) || !std::isfinite(Scale) || !(Scale > 0) || !std::isfinite(first_x) || !std::isfinite(last_x) || segments == 0 || segments > 65536 || index > segments) {
			throw std::runtime_error("Emote Catenary arc sampling requires its actual solved curve and bounded segment");
		}
		const auto first = std::sinh((first_x - OffsetX) / Scale);
		const auto span = (std::sinh((last_x - OffsetX) / Scale) - first) * Scale;
		const auto sample = (((static_cast<double>(index) * span) / static_cast<double>(segments)) / Scale) + first;
		const auto parameter = std::log(std::sqrt(sample * sample + 1.0) + sample);
		const auto x = Scale * parameter + OffsetX;
		const auto y = std::cosh(parameter) * Scale + OffsetY;
		if (!std::isfinite(x) || !std::isfinite(y)) {
			throw std::runtime_error("Emote Catenary arc sampling exceeded finite native output");
		}
		return {x, y};
	}
} // namespace sgre::scene
