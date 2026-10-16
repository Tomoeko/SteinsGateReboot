#pragma once

#include "sgre/resource/motion_mesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sgre::scene {

	struct EmoteJellySampleBinding {
		std::int32_t Priority = 0;
		float Order = 0;
		resource::MotionPoint Coordinate;
	};

	class EmoteJellySampleFit {
	public:
		// Native2fd8a0 registers four grid neighbors once;301560 applies samples in stable key order.
		EmoteJellySampleFit(std::uint32_t width, std::uint32_t height, std::span<const EmoteJellySampleBinding> samples);
		// Targets retain registration order. Each residual observes all preceding sample writes.
		void Apply(std::span<resource::MotionPoint> grid, std::span<const resource::MotionPoint> targets) const;

	private:
		friend class EmoteJellyPhysics;
		void ApplyForUnorderedComparison(std::span<resource::MotionPoint> grid, std::span<const resource::MotionPoint> targets) const;
		void ApplyImpl(std::span<resource::MotionPoint> grid, std::span<const resource::MotionPoint> targets, bool unordered_comparison) const;
		struct Coefficient {
			std::size_t Index = 0;
			float Weight = 0;
		};

		struct Binding {
			std::size_t Source = 0;
			std::int32_t Priority = 0;
			float Order = 0;
			std::array<Coefficient, 4> Coefficients;
		};

		std::size_t m_GridCount = 0;
		std::vector<Binding> m_Bindings;
	};

	// Native2fdaf0 resets samples to the actual identity grid before residual scatter.
	void InitializeEmoteJellySampleGrid(std::span<resource::MotionPoint> grid, std::uint32_t width, std::uint32_t height);

} // namespace sgre::scene
