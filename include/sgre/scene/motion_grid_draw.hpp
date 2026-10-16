#pragma once

#include "sgre/scene/motion_blank_source.hpp"
#include "sgre/scene/motion_deformation.hpp"
#include "sgre/scene/motion_draw.hpp"

#include <optional>
#include <span>

namespace sgre::scene {

	struct MotionGridBuffers {
		std::span<resource::MotionPoint> Positions;
		std::span<MotionSpriteVertex> Vertices;
		std::span<std::uint32_t> Indices;
	};

	struct MotionGridCommand {
		MotionSpriteMaterial Material;
		// Geometry borrows the caller's preallocated buffers.
		std::span<const MotionSpriteVertex> Vertices;
		std::span<const std::uint32_t> Indices;
		MotionDrawTopology Topology = MotionDrawTopology::TriangleList;
		float Depth = 0;
	};

	// Native512a30/528400 interpolate each byte with an integer 8-bit fraction.
	[[nodiscard]] std::uint32_t InterpolateMotionGridColor(std::uint32_t first, std::uint32_t last, std::uint32_t index, std::uint32_t subdivision);
	[[nodiscard]] std::size_t MotionMeshGridIndexCount(const MotionMeshSubdivision& subdivision);
	[[nodiscard]] resource::MotionMeshMatrix MotionBlankDeformationSpace(const MotionHierarchyState& state, const MotionBlankSourceRectangle& rectangle, float depth_projection);
	[[nodiscard]] resource::MotionMeshMatrix MotionSpriteDeformationSpace(const MotionHierarchyState& state, const resource::MotionIcon& icon, float depth_projection);
	// Positions must already contain the recovered evaluated geometry; vertex/index outputs borrow these buffers.
	[[nodiscard]] std::optional<MotionGridCommand> PackMotionGridCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const resource::MotionSource& source, const resource::MotionIcon& icon, const MotionMeshSubdivision& subdivision, const MotionGridBuffers& buffers);
	[[nodiscard]] std::optional<MotionGridCommand> PackMotionGridCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const ResolvedMotionIcon& binding, const MotionMeshSubdivision& subdivision, const MotionGridBuffers& buffers);
	// The scene owner must resolve inherited/reference warps before requesting this supported local-patch path.
	[[nodiscard]] std::optional<MotionGridCommand> BuildOwnMotionGridCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const resource::MotionSource& source, const resource::MotionIcon& icon, const MotionMeshSubdivision& subdivision, const MotionGridBuffers& buffers, float depth_projection, bool inherited_deformation);
	[[nodiscard]] std::optional<MotionGridCommand> BuildOwnMotionGridCommand(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const ResolvedMotionIcon& binding, const MotionMeshSubdivision& subdivision, const MotionGridBuffers& buffers, float depth_projection, bool inherited_deformation);

} // namespace sgre::scene
