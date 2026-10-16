#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/emote_jelly_sample_fit.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sgre::scene {

	class EmoteJellySectionRings;

	struct EmoteJellyRingSample {
		std::uint32_t Section = 0;
		std::uint32_t Direction = 0;
		float Circumference = 0;
		float Longitudinal = 0;
		resource::MotionPoint Rest;
		std::int32_t Priority = 0;
		float Order = 0;
	};

	class EmoteJellySurfaceSamples {
	public:
		// Borrows completed real section rings. Prepared cell bindings come from the separate native surface constructor.
		EmoteJellySurfaceSamples(const EmoteJellySectionRings& rings, std::uint8_t encoded_degree, std::span<const EmoteJellyRingSample> samples);
		void Refresh(const resource::MotionMeshMatrix& physical_to_normalized, float sensitivity);
		[[nodiscard]] std::span<const resource::MotionPoint> Targets() const;
		// These are samples before the native2fd6e0 horizontal/vertical basis inverse.
		[[nodiscard]] std::span<const resource::MotionPoint> Samples() const;

	private:
		const EmoteJellySectionRings& m_Rings;
		std::uint32_t m_Width = 0;
		std::uint32_t m_Height = 0;
		std::vector<EmoteJellyRingSample> m_Bindings;
		std::vector<resource::MotionPoint> m_Targets;
		std::vector<resource::MotionPoint> m_Samples;
		EmoteJellySampleFit m_Fit;
		bool m_Ready = false;
		bool m_Valid = true;
	};

} // namespace sgre::scene
