#pragma once

#include "sgre/resource/motion.hpp"
#include "sgre/scene/motion_clip_bounds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sgre::scene {
	struct ScreenProjectionVertex {
		float X = 0;
		float Y = 0;
		float Z = 0;
		resource::MotionPoint Uv;
	};

	struct ScreenProjectionRegion {
		// Native4fa080 starts with one normalized whole-source/whole-destination pair.
		MotionBounds Source{0, 0, 1, 1};
		MotionBounds Destination{0, 0, 1, 1};
	};

	struct ScreenProjectionPacket {
		std::span<const ScreenProjectionVertex> Vertices;
		std::span<const std::uint32_t> Indices;
		std::uint32_t Color = 0xffffffff;
		std::uint32_t TextureAttributes = 4;
		std::uint32_t BlendMode = 0;
		bool Smoothing = false;
	};

	// Native061800 replaces one channel view with four fixed source quadrants and displaced/zoomed destination quadrants.
	[[nodiscard]] std::array<ScreenProjectionRegion, 4> PrepareChromaticProjectionRegions(resource::MotionPoint logical_size, resource::MotionPoint displacement, float zoom);

	// Native4fa480 uses actual logical Screen dimensions, physical texture dimensions and the view bounds.
	// The caller owns bounded scratch through the containing actual draw/host-input copy.
	[[nodiscard]] ScreenProjectionPacket PrepareScreenProjectionPacket(const MotionBounds& bounds, resource::MotionPoint logical_size, resource::MotionPoint texture_size, float depth, std::uint32_t packed_argb, std::span<const ScreenProjectionRegion> regions, std::span<ScreenProjectionVertex> vertices, std::span<std::uint32_t> indices);
} // namespace sgre::scene
