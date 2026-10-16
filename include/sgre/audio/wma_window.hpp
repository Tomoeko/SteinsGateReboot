#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::audio {

	class WmaWindowBank {
	public:
		WmaWindowBank();
		// Samples contain an already expanded 2N transform. Previous/next lengths control the two overlap ramps.
		void Apply(std::span<float> time_domain, std::uint32_t previous_coefficients, std::uint32_t next_coefficients) const;

	private:
		std::array<std::vector<float>, 5> m_RisingWindows;
	};

} // namespace sgre::audio
