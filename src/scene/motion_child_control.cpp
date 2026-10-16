#include "sgre/scene/motion_child_control.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace sgre::scene {

	static const resource::PsbValue& RequireChildField(const resource::PsbValue& value, std::string_view name) {
		const auto* field = value.Find(name);
		if (field == nullptr) {
			throw std::runtime_error("motion child control requires field " + std::string(name));
		}

		return *field;
	}

	static std::uint32_t ReadChildWord(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean ? 1u : 0u;
		}

		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			if (*integer >= std::numeric_limits<std::int32_t>::min() && *integer <= std::numeric_limits<std::uint32_t>::max()) {
				return static_cast<std::uint32_t>(*integer);
			}
		}

		if (const auto* real = std::get_if<double>(&value.Data)) {
			if (std::isfinite(*real) && *real >= std::numeric_limits<std::int32_t>::min() && *real <= std::numeric_limits<std::int32_t>::max()) {
				return static_cast<std::uint32_t>(static_cast<std::int32_t>(*real));
			}
		}

		throw std::runtime_error("motion child integer control is outside its bounded native conversion domain");
	}

	static float ReadChildNumber(const resource::PsbValue& value) {
		const auto* integer = std::get_if<std::int64_t>(&value.Data);
		const auto* real = std::get_if<double>(&value.Data);
		const auto number = integer != nullptr ? static_cast<double>(*integer) : real != nullptr ? *real
		                                                                                         : std::numeric_limits<double>::quiet_NaN();
		if (!std::isfinite(number) || std::abs(number) > 1000000000) {
			throw std::runtime_error("motion child numeric control is outside its finite bounded domain");
		}

		return static_cast<float>(number);
	}

	static bool ReadChildBoolean(const resource::PsbValue& value) {
		if (const auto* boolean = std::get_if<bool>(&value.Data)) {
			return *boolean;
		}

		if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
			return *integer != 0;
		}

		if (const auto* real = std::get_if<double>(&value.Data)) {
			return *real != 0;
		}

		throw std::runtime_error("motion child boolean control requires actual boolean or numeric metadata");
	}

	static std::string_view ReadChildTarget(const resource::PsbValue& value) {
		const auto* text = std::get_if<std::string>(&value.Data);
		if (text == nullptr || text->size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("motion child direction target requires a bounded native string");
		}

		return std::string_view(*text).substr(0, text->find('\0'));
	}

	std::optional<MotionChildFrameControl> PrepareMotionChildFrameControl(const resource::MotionLayer& layer, const resource::MotionFrame& frame) {
		if (layer.Type != 3 || frame.Type > 3) {
			throw std::runtime_error("motion child control requires its genuine kind3 layer and bounded frame type");
		}

		if (frame.Type == 0) {
			return std::nullopt;
		}

		MotionChildFrameControl control;
		if ((ReadChildWord(RequireChildField(frame.Content, "mask")) & 0x80000) == 0) {
			return std::nullopt;
		}

		const auto& motion = RequireChildField(frame.Content, "motion");
		const auto mask = ReadChildWord(RequireChildField(motion, "mask")) & 255;
		control.Specified = true;
		control.DirectionMode = 1;
		if ((mask & 1) != 0) {
			control.Flags = static_cast<MotionPlayFlags>(ReadChildWord(RequireChildField(motion, "flags")));
		}

		if ((mask & 2) != 0) {
			control.DirectionMode = ReadChildWord(RequireChildField(motion, "dt"));
		}

		if ((mask & 8) != 0) {
			control.DirectionOffset = ReadChildNumber(RequireChildField(motion, "dofst"));
		}

		if ((mask & 4) != 0) {
			control.CompleteDirection = ReadChildBoolean(RequireChildField(motion, "docmpl"));
		}

		if ((mask & 16) != 0) {
			control.DirectionTarget = ReadChildTarget(RequireChildField(motion, "dtgt"));
		}

		control.TimeOffset = ReadChildNumber(RequireChildField(motion, "timeOffset"));

		return control;
	}

	void ApplyMotionChildFrameControl(const std::optional<MotionChildFrameControl>& update, MotionChildFrameControl& slot) {
		if (update.has_value()) {
			slot = *update;
		}
	}

} // namespace sgre::scene
