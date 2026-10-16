#include "sgre/scene/screen_projection_packet.hpp"

#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace sgre::scene {
	static void RequireProjectionBounds(const MotionBounds& bounds) {
		for (const auto value : {bounds.MinimumX, bounds.MinimumY, bounds.MaximumX, bounds.MaximumY}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("ScreenProjection bounds require finite native coordinate words");
			}
		}
	}

	std::array<ScreenProjectionRegion, 4> PrepareChromaticProjectionRegions(resource::MotionPoint logical_size, resource::MotionPoint displacement, float zoom) {
		if (!std::isfinite(logical_size.X) || !std::isfinite(logical_size.Y) || logical_size.X <= 0 || logical_size.Y <= 0 || !std::isfinite(displacement.X) || !std::isfinite(displacement.Y) || !std::isfinite(zoom)) {
			throw std::runtime_error("ScreenProjectionCA mesh requires finite arguments and actual positive view dimensions");
		}

		const float offset_x = displacement.X / logical_size.X;
		const float offset_y = displacement.Y / logical_size.Y;
		// Original061800 computes the zoom margin in double before narrowing, then uses separate float additions.
		const float margin = static_cast<float>((static_cast<double>(zoom) - 1.0) * 0.5);
		const float minimum_x = offset_x + 0.0f - margin;
		const float minimum_y = offset_y + 0.0f - margin;
		const float middle_x = offset_x + 0.5f;
		const float middle_y = offset_y + 0.5f;
		const float maximum_x = offset_x + 1.0f + margin;
		const float maximum_y = offset_y + 1.0f + margin;
		std::array<ScreenProjectionRegion, 4> regions;
		regions[0].Source = {0, 0, .5f, .5f};
		regions[0].Destination = {minimum_x, minimum_y, middle_x, middle_y};
		regions[1].Source = {.5f, 0, 1, .5f};
		regions[1].Destination = {middle_x, minimum_y, maximum_x, middle_y};
		regions[2].Source = {0, .5f, .5f, 1};
		regions[2].Destination = {minimum_x, middle_y, middle_x, maximum_y};
		regions[3].Source = {.5f, .5f, 1, 1};
		regions[3].Destination = {middle_x, middle_y, maximum_x, maximum_y};
		for (const auto& region : regions) {
			RequireProjectionBounds(region.Destination);
		}

		return regions;
	}

	ScreenProjectionPacket PrepareScreenProjectionPacket(const MotionBounds& bounds, resource::MotionPoint logical_size, resource::MotionPoint texture_size, float depth, std::uint32_t packed_argb, std::span<const ScreenProjectionRegion> regions, std::span<ScreenProjectionVertex> vertices, std::span<std::uint32_t> indices) {
		RequireProjectionBounds(bounds);
		if (!std::isfinite(depth) || !std::isfinite(logical_size.X) || !std::isfinite(logical_size.Y) || !std::isfinite(texture_size.X) || !std::isfinite(texture_size.Y) || logical_size.X <= 0 || logical_size.Y <= 0 || texture_size.X <= 0 || texture_size.Y <= 0 || regions.size() > 0x7fffu / 6 || regions.size() > vertices.size() / 4 || regions.size() > indices.size() / 6) {
			throw std::runtime_error("ScreenProjection requires actual positive extents and bounded complete region scratch");
		}

		const float maximum_u = logical_size.X / texture_size.X;
		const float maximum_v = logical_size.Y / texture_size.Y;
		static constexpr std::array<std::uint32_t, 6> kIndices{0, 1, 2, 1, 3, 2};
		for (std::size_t region_index = 0; region_index < regions.size(); ++region_index) {
			const auto& region = regions[region_index];
			RequireProjectionBounds(region.Source);
			RequireProjectionBounds(region.Destination);
			const std::array<float, 4> destination_x{region.Destination.MaximumX, region.Destination.MinimumX, region.Destination.MaximumX, region.Destination.MinimumX};
			const std::array<float, 4> destination_y{region.Destination.MaximumY, region.Destination.MaximumY, region.Destination.MinimumY, region.Destination.MinimumY};
			const std::array<float, 4> source_x{region.Source.MaximumX, region.Source.MinimumX, region.Source.MaximumX, region.Source.MinimumX};
			const std::array<float, 4> source_y{region.Source.MaximumY, region.Source.MaximumY, region.Source.MinimumY, region.Source.MinimumY};
			for (std::size_t vertex_index = 0; vertex_index < 4; ++vertex_index) {
				auto& vertex = vertices[region_index * 4 + vertex_index];
				vertex.X = (bounds.MaximumX - bounds.MinimumX) * destination_x[vertex_index] + bounds.MinimumX;
				vertex.Y = (bounds.MaximumY - bounds.MinimumY) * destination_y[vertex_index] + bounds.MinimumY;
				vertex.Z = depth;
				vertex.Uv.X = (maximum_u - 0.0f) * source_x[vertex_index] + 0.0f;
				vertex.Uv.Y = (maximum_v - 0.0f) * source_y[vertex_index] + 0.0f;
				for (const auto value : {vertex.X, vertex.Y, vertex.Uv.X, vertex.Uv.Y}) {
					if (!std::isfinite(value)) {
						throw std::runtime_error("ScreenProjection native interpolation exceeds the admitted finite geometry domain");
					}
				}
			}
			for (std::size_t index = 0; index < kIndices.size(); ++index) {
				indices[region_index * 6 + index] = static_cast<std::uint32_t>(region_index * 4) + kIndices[index];
			}
		}

		ScreenProjectionPacket packet;
		packet.Vertices = vertices.first(regions.size() * 4);
		packet.Indices = indices.first(regions.size() * 6);
		packet.Color = packed_argb;
		packet.TextureAttributes = packed_argb == 0xffffffff ? 4u : 0u;

		return packet;
	}
} // namespace sgre::scene
