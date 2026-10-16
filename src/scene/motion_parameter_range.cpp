#include "sgre/scene/motion_parameter_range.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>

namespace sgre::scene {

	static const resource::PsbValue& ParameterField(const resource::PsbValue& value, std::string_view name) {
		const auto* field = value.Find(name);
		if (field == nullptr) {
			throw std::runtime_error("motion parameter definition requires field " + std::string(name));
		}

		return *field;
	}

	static bool ParameterBoolean(const resource::PsbValue& value) {
		if (const auto* flag = std::get_if<bool>(&value.Data)) {
			return *flag;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return *integer != 0;
		}
		if (const auto* real = std::get_if<double>(&value.Data)) {
			if (std::isfinite(*real)) {
				return *real != 0;
			}
		}

		throw std::runtime_error("motion parameter discretization requires finite numeric metadata");
	}

	static float ParameterScalar(const resource::PsbValue& value, std::string_view name) {
		if (const auto* flag = std::get_if<bool>(&value.Data)) {
			return *flag ? 1.0f : 0.0f;
		}
		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return static_cast<float>(*integer);
		}
		if (const auto* real = std::get_if<double>(&value.Data)) {
			if (std::isfinite(*real) && *real >= -static_cast<double>(std::numeric_limits<float>::max()) && *real <= static_cast<double>(std::numeric_limits<float>::max())) {
				return static_cast<float>(*real);
			}
		}

		throw std::runtime_error("motion parameter requires finite float metadata for " + std::string(name));
	}

	std::vector<MotionParameterRange> ReadMotionParameterRanges(const resource::MotionTimeline& timeline, std::size_t max_parameters) {
		if (max_parameters == 0 || max_parameters > 65536) {
			throw std::runtime_error("motion parameter preparation requires a bounded positive entry limit");
		}
		std::vector<MotionParameterRange> ranges;
		if (std::holds_alternative<std::monostate>(timeline.Parameters.Data)) {
			return ranges;
		}
		const auto* array = std::get_if<resource::PsbArray>(&timeline.Parameters.Data);
		if (array == nullptr || array->size() > max_parameters) {
			throw std::runtime_error("motion parameter preparation requires its bounded actual definition array");
		}
		ranges.reserve(array->size());
		for (const auto& definition : *array) {
			const auto& identifier_value = ParameterField(definition, "id");
			const auto* identifier = std::get_if<std::string>(&identifier_value.Data);
			if (identifier == nullptr || identifier->size() > 65536) {
				throw std::runtime_error("motion parameter ID requires bounded actual string metadata");
			}
			MotionParameterRange range;
			range.Identifier = std::string_view(*identifier).substr(0, identifier->find('\0'));
			range.Discrete = ParameterBoolean(ParameterField(definition, "discretization"));
			range.Begin = ParameterScalar(ParameterField(definition, "rangeBegin"), "rangeBegin");
			range.End = ParameterScalar(ParameterField(definition, "rangeEnd"), "rangeEnd");
			range.Division = ParameterScalar(ParameterField(definition, "division"), "division");
			ranges.push_back(range);
		}

		return ranges;
	}

	float EvaluateMotionParameterPosition(const MotionParameterRange& range, float value) {
		if (!std::isfinite(range.Begin) || !std::isfinite(range.End) || !std::isfinite(range.Division) || !std::isfinite(value)) {
			throw std::runtime_error("motion parameter position requires finite actual range and resolved variable values");
		}
		if (range.Begin == range.End || range.Division <= 0) {
			return 0;
		}
		if (range.Discrete) {
			// Native CVTTSS2SI returns INT_MIN for finite values outside the signed conversion domain.
			if (value < -2147483648.0f || value >= 2147483648.0f) {
				value = static_cast<float>(std::numeric_limits<std::int32_t>::min());
			} else {
				value = static_cast<float>(static_cast<std::int32_t>(value));
			}
		}
		const auto lower = range.Begin <= range.End ? range.Begin : range.End;
		const auto upper = range.End <= range.Begin ? range.Begin : range.End;
		auto clamped = value > lower ? value : lower;
		clamped = clamped < upper ? clamped : upper;
		const auto offset = clamped - range.Begin;
		const auto scaled = offset * range.Division;
		const auto extent = range.End - range.Begin;
		const auto position = scaled / extent;
		if (!std::isfinite(position)) {
			throw std::runtime_error("motion parameter position arithmetic exceeds its finite supported domain");
		}

		return position;
	}

} // namespace sgre::scene
