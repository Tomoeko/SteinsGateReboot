#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::audio {

	// One decoder owns each mutable workspace. Construction prepares all allocation and trigonometric tables.
	class WmaInverseTransform {
	public:
		explicit WmaInverseTransform(std::uint32_t coefficient_count);
		[[nodiscard]] std::uint32_t CoefficientCount() const noexcept;
		// Native Standard compact output is a DCT-IV with scale 2/N. Exact input/output aliasing is supported.
		void ComputeCompact(std::span<const float> coefficients, std::span<float> compact_output);

	private:
		std::uint32_t m_CoefficientCount = 0;
		std::vector<std::complex<double>> m_Spectrum;
		std::vector<std::complex<double>> m_Twiddles;
		std::vector<std::size_t> m_ReversedIndices;
		std::vector<float> m_Output;
	};

	// Expands the compact cosine output into the conventional 2N IMDCT time domain, before windowing/overlap. Storage must be disjoint.
	void ExpandWmaInverseTransform(std::span<const float> compact_output, std::span<float> time_domain);

} // namespace sgre::audio
