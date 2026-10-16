#include "sgre/audio/wma_transform.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <numbers>
#include <stdexcept>

namespace sgre::audio {

	static constexpr std::uint32_t kMinimumCoefficients = 128;
	static constexpr std::uint32_t kMaximumCoefficients = 2048;

	static bool ValidCoefficientCount(std::size_t count) noexcept {
		return count >= kMinimumCoefficients && count <= kMaximumCoefficients && std::has_single_bit(count);
	}

	static void RequireFinite(std::span<const float> values) {
		if (!std::ranges::all_of(values, [](float value) {
			    return std::isfinite(value);
		    })) {
			throw std::runtime_error("WMA inverse transform input contains a nonfinite coefficient");
		}
	}

	static void ComputeFourier(std::span<std::complex<double>> spectrum, std::span<const std::complex<double>> twiddles, std::span<const std::size_t> reversed_indices) {
		for (std::size_t index = 0; index < spectrum.size(); ++index) {
			if (reversed_indices[index] > index) {
				std::swap(spectrum[index], spectrum[reversed_indices[index]]);
			}
		}

		for (std::size_t length = 2; length <= spectrum.size(); length *= 2) {
			const auto half_length = length / 2;
			const auto twiddle_stride = spectrum.size() / length;
			for (std::size_t begin = 0; begin < spectrum.size(); begin += length) {
				for (std::size_t index = 0; index < half_length; ++index) {
					const auto even = spectrum[begin + index];
					const auto odd = spectrum[begin + index + half_length] * twiddles[index * twiddle_stride];
					spectrum[begin + index] = even + odd;
					spectrum[begin + index + half_length] = even - odd;
				}
			}
		}
	}

	WmaInverseTransform::WmaInverseTransform(std::uint32_t coefficient_count) : m_CoefficientCount(coefficient_count) {
		if (!ValidCoefficientCount(coefficient_count)) {
			throw std::runtime_error("WMA inverse transform requires 128–2048 power-of-two coefficients");
		}

		const auto spectrum_count = static_cast<std::size_t>(coefficient_count) * 8;
		m_Spectrum.resize(spectrum_count);
		m_Twiddles.resize(spectrum_count / 2);
		m_ReversedIndices.resize(spectrum_count);
		m_Output.resize(coefficient_count);
		const auto index_bits = static_cast<std::size_t>(std::bit_width(spectrum_count) - 1);
		for (std::size_t index = 0; index < spectrum_count; ++index) {
			std::size_t reversed = 0;
			for (std::size_t bit = 0; bit < index_bits; ++bit) {
				reversed = (reversed << 1) | ((index >> bit) & 1);
			}

			m_ReversedIndices[index] = reversed;
		}

		for (std::size_t index = 0; index < m_Twiddles.size(); ++index) {
			const double angle = -2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(spectrum_count);
			m_Twiddles[index] = {std::cos(angle), std::sin(angle)};
		}
	}

	std::uint32_t WmaInverseTransform::CoefficientCount() const noexcept {
		return m_CoefficientCount;
	}

	void WmaInverseTransform::ComputeCompact(std::span<const float> coefficients, std::span<float> compact_output) {
		if (coefficients.size() != m_CoefficientCount || compact_output.size() != m_CoefficientCount) {
			throw std::runtime_error("WMA inverse transform coefficient/output extents differ");
		}

		RequireFinite(coefficients);
		std::ranges::fill(m_Spectrum, std::complex<double>{});
		// Odd spatial indices and odd Fourier bins of the 8N transform evaluate cos(pi/N * (k+.5) * (n+.5)).
		for (std::size_t index = 0; index < coefficients.size(); ++index) {
			m_Spectrum[index * 2 + 1] = static_cast<double>(coefficients[index]);
		}

		ComputeFourier(m_Spectrum, m_Twiddles, m_ReversedIndices);
		const double scale = 2.0 / static_cast<double>(m_CoefficientCount);
		for (std::size_t index = 0; index < m_Output.size(); ++index) {
			m_Output[index] = static_cast<float>(m_Spectrum[index * 2 + 1].real() * scale);
			if (!std::isfinite(m_Output[index])) {
				throw std::runtime_error("WMA inverse transform exceeds finite sample storage");
			}
		}

		std::ranges::copy(m_Output, compact_output.begin());
	}

	void ExpandWmaInverseTransform(std::span<const float> compact_output, std::span<float> time_domain) {
		if (!ValidCoefficientCount(compact_output.size()) || time_domain.size() != compact_output.size() * 2) {
			throw std::runtime_error("WMA expanded inverse transform extents differ");
		}

		const std::less<const float*> before;
		if (before(compact_output.data(), time_domain.data() + time_domain.size()) && before(time_domain.data(), compact_output.data() + compact_output.size())) {
			throw std::runtime_error("WMA expanded inverse transform requires disjoint input/output storage");
		}

		RequireFinite(compact_output);
		const auto half = compact_output.size() / 2;
		for (std::size_t index = 0; index < half; ++index) {
			time_domain[index] = compact_output[half + index];
		}

		for (std::size_t index = 0; index < compact_output.size(); ++index) {
			time_domain[half + index] = -compact_output[compact_output.size() - index - 1];
		}

		for (std::size_t index = 0; index < half; ++index) {
			time_domain[half + compact_output.size() + index] = -compact_output[index];
		}
	}

} // namespace sgre::audio
