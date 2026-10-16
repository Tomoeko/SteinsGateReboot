#include "sgre/scene/emote_clamp_control.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	std::array<float, 2> ApplyEmoteClamp(const EmoteClampTransform& transform, float left_right, float up_down) {
		const auto width = transform.Maximum - transform.Minimum;
		if ((transform.Kind != EmoteClampKind::Smooth && transform.Kind != EmoteClampKind::Radial) || !std::isfinite(width) || width == 0 || !std::isfinite(transform.Minimum) || !std::isfinite(transform.Maximum) || !std::isfinite(left_right) || !std::isfinite(up_down)) {
			throw std::runtime_error("Emote clamp requires recovered finite type0/1 and a nonempty actual range");
		}
		auto x = (left_right - transform.Minimum) / width;
		auto y = (up_down - transform.Minimum) / width;
		x = (x + x) - 1.0f;
		y = (y + y) - 1.0f;
		if (!std::isfinite(x) || !std::isfinite(y)) {
			throw std::runtime_error("Emote clamp normalization exceeds its finite actual arithmetic domain");
		}
		if (x != 0 && y != 0) {
			if (transform.Kind == EmoteClampKind::Smooth) {
				constexpr float kPi = 3.1415927410125732421875f;
				auto ratio = std::abs(x / y);
				if (ratio > 1) {
					ratio = 1.0f / ratio;
				}
				const auto scale = 1.0f / std::sqrt(ratio * ratio + 1.0f);
				x = scale * x;
				y = scale * y;
				const auto radius = std::sqrt(x * x + y * y);
				if (!std::isfinite(radius) || radius == 0) {
					throw std::runtime_error("Emote smooth clamp radius exceeds native finite nonzero domain");
				}
				auto weight = 1.0f - std::cos((ratio * kPi) * 0.5f);
				weight = weight * (std::sin((radius * kPi) * 0.5f) / radius - 1.0f);
				weight = weight + 1.0f;
				x = x * weight;
				y = y * weight;
			} else {
				const auto radius = std::sqrt(y * y + x * x);
				if (radius > 1) {
					const auto angle = std::atan2(y, x);
					x = std::cos(angle);
					y = std::sin(angle);
				}
			}
		}
		x = (x + 1.0f) * 0.5f;
		y = (y + 1.0f) * 0.5f;
		x = x * width + transform.Minimum;
		y = y * width + transform.Minimum;
		if (!std::isfinite(x) || !std::isfinite(y)) {
			throw std::runtime_error("Emote clamp output exceeds finite actual Motion variable domain");
		}
		return {x, y};
	}

} // namespace sgre::scene
