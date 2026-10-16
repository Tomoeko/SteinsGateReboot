#include "sgre/scene/emote_jelly_profile.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {
	static constexpr float kJellyPi = std::bit_cast<float>(std::uint32_t{0x40490fdb});
	static constexpr float kJellyTwoThirds = std::bit_cast<float>(std::uint32_t{0x3f2aaaaa});

	EmoteJellyProfile BuildEmoteJellyProfile(const EmoteJellyProfileDefinition& definition) {
		if (definition.ParticleCount < 2 || definition.ParticleCount > 256 || !(definition.Radius > 0) || !(definition.HeightRate > 0) || !(definition.Density > 0) || !std::isfinite(definition.Radius) || !std::isfinite(definition.HeightRate) || !std::isfinite(definition.Density)) {
			throw std::runtime_error("Emote Jelly profile requires bounded particles and finite positive actual radius/height/density");
		}
		const float radius = definition.Radius;
		const float height_rate = definition.HeightRate;
		const float sphere_factor = (height_rate * height_rate + 1.0f) / (height_rate + height_rate);
		const float start = (sphere_factor - height_rate) / sphere_factor;
		const float radius_cube_pi = ((radius * radius) * radius) * kJellyPi;
		const float start_integral = start - ((start * start) * start) / 3.0f;
		const float total_volume = (kJellyTwoThirds - start_integral) * radius_cube_pi;
		EmoteJellyProfile result;
		result.Length = height_rate * radius;
		result.Mass = (((radius * 3.0f - result.Length) * (result.Length * result.Length)) * (definition.Density * kJellyPi)) / 3.0f;
		if (!(total_volume > 0) || !std::isfinite(total_volume) || !(result.Length > 0) || !std::isfinite(result.Length) || !(result.Mass > 0) || !std::isfinite(result.Mass)) {
			throw std::runtime_error("Emote Jelly profile exceeds its finite positive native cap domain");
		}
		result.MassWeights.resize(definition.ParticleCount);
		result.Radii.resize(definition.ParticleCount);
		// Native31120d keeps the fixed root's volume and mass weight exactly zero.
		result.MassWeights[0] = 0;
		result.Radii[0] = std::sqrt(1.0f - start * start) * (radius * sphere_factor);
		const float step = (1.0f - start) / static_cast<float>(definition.ParticleCount - 1);
		for (std::size_t index = 0; index != definition.ParticleCount - 1; ++index) {
			const float left = static_cast<float>(index) * step + start;
			const float right = step * static_cast<float>(index + 1) + start;
			const float right_integral = right - ((right * right) * right) / 3.0f;
			const float left_integral = left - ((left * left) * left) / 3.0f;
			const float volume = (right_integral - left_integral) * radius_cube_pi;
			const float radial_sample = (right * 9.0f + left) / 10.0f;
			result.MassWeights[index + 1] = volume / total_volume;
			result.Radii[index + 1] = std::sqrt(1.0f - radial_sample * radial_sample) * (sphere_factor * radius);
		}
		for (std::size_t index = 0; index != result.Radii.size(); ++index) {
			if (!std::isfinite(result.Radii[index]) || result.Radii[index] < 0 || !std::isfinite(result.MassWeights[index]) || (index != 0 && !(result.MassWeights[index] > 0))) {
				throw std::runtime_error("Emote Jelly profile sample exceeds its finite actual radius/mass domain");
			}
		}

		return result;
	}
} // namespace sgre::scene
