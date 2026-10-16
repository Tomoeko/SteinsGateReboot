#include "sgre/scene/emote_jelly_surface_samples.hpp"

#include "sgre/scene/emote_jelly_section_rings.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	static constexpr std::size_t kMaximumJellySurfaceSamples = 65536;
	static constexpr std::size_t kJellyRingDirections = 16;

	static std::uint8_t RequireJellyDegree(std::uint8_t degree) {
		if (degree != 0x33 && degree != 0x34 && degree != 0x43) {
			throw std::runtime_error("Emote Jelly surface samples require the actual cubic or single-axis quartic constructor degree");
		}

		return degree;
	}

	static std::vector<EmoteJellyRingSample> CopySurfaceSamples(std::span<const EmoteJellyRingSample> samples) {
		if (samples.size() > kMaximumJellySurfaceSamples) {
			throw std::runtime_error("Emote Jelly surface exceeds its bounded prepared sample count");
		}

		return {samples.begin(), samples.end()};
	}

	static EmoteJellySampleFit PrepareSurfaceFit(std::uint32_t width, std::uint32_t height, std::span<const EmoteJellyRingSample> samples) {
		if (samples.size() > kMaximumJellySurfaceSamples) {
			throw std::runtime_error("Emote Jelly surface exceeds its bounded prepared sample count");
		}

		std::vector<EmoteJellySampleBinding> bindings;
		bindings.reserve(samples.size());
		for (const auto& sample : samples) {
			if (sample.Direction >= kJellyRingDirections || !std::isfinite(sample.Circumference) || !std::isfinite(sample.Longitudinal) || sample.Circumference < 0 || sample.Circumference > 1 || sample.Longitudinal < 0 || sample.Longitudinal > 1) {
				throw std::runtime_error("Emote Jelly sample requires its finite actual cyclic ring cell and fractions");
			}

			EmoteJellySampleBinding binding;
			binding.Priority = sample.Priority;
			binding.Order = sample.Order;
			binding.Coordinate = sample.Rest;
			bindings.push_back(binding);
		}

		return EmoteJellySampleFit(width, height, bindings);
	}

	static resource::MotionPoint InterpolateRingPoint(const resource::MotionPoint& first, const resource::MotionPoint& second, float fraction) {
		return {(second.X - first.X) * fraction + first.X, (second.Y - first.Y) * fraction + first.Y};
	}

	EmoteJellySurfaceSamples::EmoteJellySurfaceSamples(const EmoteJellySectionRings& rings, std::uint8_t encoded_degree, std::span<const EmoteJellyRingSample> samples) : m_Rings(rings), m_Width(static_cast<std::uint32_t>(RequireJellyDegree(encoded_degree) & 15) + 1u), m_Height(static_cast<std::uint32_t>(encoded_degree >> 4) + 1u), m_Bindings(CopySurfaceSamples(samples)), m_Targets(m_Bindings.size()), m_Samples(static_cast<std::size_t>(m_Width) * m_Height), m_Fit(PrepareSurfaceFit(m_Width, m_Height, m_Bindings)) {
	}

	void EmoteJellySurfaceSamples::Refresh(const resource::MotionMeshMatrix& physical_to_normalized, float sensitivity) {
		if (!m_Valid || !std::isfinite(sensitivity)) {
			throw std::runtime_error("Emote Jelly surface requires a valid actual sample owner and finite sensitivity");
		}
		for (const float value : {physical_to_normalized.XX, physical_to_normalized.XY, physical_to_normalized.YX, physical_to_normalized.YY, physical_to_normalized.TranslationX, physical_to_normalized.TranslationY}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Emote Jelly surface requires its finite actual physical-to-normalized affine");
			}
		}

		const auto rings = m_Rings.Rings();
		if (rings.size() < kJellyRingDirections * 2 || rings.size() % kJellyRingDirections != 0) {
			throw std::runtime_error("Emote Jelly surface requires the real complete section ring span");
		}
		const auto section_count = rings.size() / kJellyRingDirections;
		for (const auto& sample : m_Bindings) {
			if (sample.Section >= section_count - 1) {
				throw std::runtime_error("Emote Jelly prepared sample cell exceeds its actual adjacent rings");
			}
		}

		try {
			for (std::size_t index = 0; index < m_Bindings.size(); ++index) {
				const auto& binding = m_Bindings[index];
				const auto first = static_cast<std::size_t>(binding.Section) * kJellyRingDirections;
				const auto following = (binding.Direction + 1) % kJellyRingDirections;
				const auto lower = InterpolateRingPoint(rings[first + binding.Direction], rings[first + following], binding.Circumference);
				const auto upper = InterpolateRingPoint(rings[first + kJellyRingDirections + binding.Direction], rings[first + kJellyRingDirections + following], binding.Circumference);
				const auto physical = InterpolateRingPoint(lower, upper, binding.Longitudinal);
				const float normalized_x = (physical.Y * physical_to_normalized.XY + physical.X * physical_to_normalized.XX) + physical_to_normalized.TranslationX;
				const float normalized_y = (physical.Y * physical_to_normalized.YY + physical.X * physical_to_normalized.YX) + physical_to_normalized.TranslationY;
				m_Targets[index].X = (normalized_x - binding.Rest.X) * sensitivity + binding.Rest.X;
				m_Targets[index].Y = (normalized_y - binding.Rest.Y) * sensitivity + binding.Rest.Y;
			}

			InitializeEmoteJellySampleGrid(m_Samples, m_Width, m_Height);
			m_Fit.Apply(m_Samples, m_Targets);
			m_Ready = true;
		} catch (...) {
			m_Valid = false;
			throw;
		}
	}

	std::span<const resource::MotionPoint> EmoteJellySurfaceSamples::Targets() const {
		static_cast<void>(Samples());

		return m_Targets;
	}

	std::span<const resource::MotionPoint> EmoteJellySurfaceSamples::Samples() const {
		static_cast<void>(m_Rings.Rings());
		if (!m_Valid || !m_Ready) {
			throw std::runtime_error("Emote Jelly surface samples require completed valid publication");
		}

		return m_Samples;
	}

} // namespace sgre::scene
