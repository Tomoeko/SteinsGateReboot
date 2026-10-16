#include "sgre/audio/wma_window.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace sgre::audio {

	static constexpr std::uint32_t kMinimumCoefficients = 128;
	static constexpr std::uint32_t kMaximumCoefficients = 2048;

	static bool ValidCoefficientCount(std::size_t count) noexcept {
		return count >= kMinimumCoefficients && count <= kMaximumCoefficients && std::has_single_bit(count);
	}

	static std::size_t WindowIndex(std::uint32_t count) noexcept {
		return static_cast<std::size_t>(std::bit_width(count) - std::bit_width(kMinimumCoefficients));
	}

	WmaWindowBank::WmaWindowBank() {
		for (std::uint32_t count = kMinimumCoefficients; count <= kMaximumCoefficients; count *= 2) {
			auto& window = m_RisingWindows[WindowIndex(count)];
			window.resize(count);
			for (std::size_t index = 0; index < window.size(); ++index) {
				const double phase = std::numbers::pi / (2.0 * count) * (static_cast<double>(index) + 0.5);
				window[index] = static_cast<float>(std::sin(phase));
			}
		}
	}

	void WmaWindowBank::Apply(std::span<float> time_domain, std::uint32_t previous_coefficients, std::uint32_t next_coefficients) const {
		const auto coefficients = time_domain.size() / 2;
		if (time_domain.size() % 2 != 0 || !ValidCoefficientCount(coefficients) || !ValidCoefficientCount(previous_coefficients) || !ValidCoefficientCount(next_coefficients)) {
			throw std::runtime_error("WMA overlap window requires valid previous/current/next coefficient extents");
		}

		if (!std::ranges::all_of(time_domain, [](float value) {
			    return std::isfinite(value);
		    })) {
			throw std::runtime_error("WMA overlap window input contains a nonfinite sample");
		}

		const auto current_coefficients = static_cast<std::uint32_t>(coefficients);
		const auto left_count = std::min(previous_coefficients, current_coefficients);
		const auto right_count = std::min(next_coefficients, current_coefficients);
		const auto left_begin = (coefficients - left_count) / 2;
		const auto right_begin = (coefficients * 3 - right_count) / 2;
		const auto& left_window = m_RisingWindows[WindowIndex(left_count)];
		const auto& right_window = m_RisingWindows[WindowIndex(right_count)];
		std::fill_n(time_domain.begin(), left_begin, 0.0f);
		for (std::size_t index = 0; index < left_window.size(); ++index) {
			time_domain[left_begin + index] *= left_window[index];
		}

		for (std::size_t index = 0; index < right_window.size(); ++index) {
			time_domain[right_begin + index] *= right_window[right_window.size() - index - 1];
		}

		std::ranges::fill(time_domain.subspan(right_begin + right_count), 0.0f);
	}

} // namespace sgre::audio
