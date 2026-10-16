#pragma once

#include "sgre/scene/motion_deformation.hpp"
#include "sgre/scene/motion_deformation_chain.hpp"
#include "sgre/scene/motion_evaluator.hpp"
#include "sgre/scene/motion_tessellation.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace sgre::scene {

	struct MotionGeometryFrame {
		std::optional<resource::MotionBezierPatch> Patch;
		resource::MotionMeshMatrix Space;
		std::array<resource::MotionPoint, 4> Quad{};
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		resource::MotionPoint Origin;
		resource::MotionPoint Anchor;
	};

	struct MotionGeometryCache {
		std::optional<resource::MotionBezierPatch> CombinedPatch;
		std::optional<PreparedMotionDeformation> Warp;
	};

	[[nodiscard]] MotionGeometryFrame PrepareMotionGeometryFrame(const PreparedMotionLayer& layer, const MotionHierarchyState& state, const resource::MotionIcon& icon, float depth_projection);

	// All spans borrow the scene owner's storage. Keep it alive and unchanged until this view is discarded.
	class PreparedMotionGeometry {
	public:
		PreparedMotionGeometry(std::span<const MotionDeformationNode> nodes, std::span<const MotionGeometryFrame> frames, std::span<MotionDeformationLink> links, std::span<MotionGeometryCache> cache);
		[[nodiscard]] MotionMeshSubdivision SelectSubdivision(std::uint32_t index, float ratio, std::span<MotionMeshDivisionCache> division_cache) const;
		void EvaluateVertices(std::uint32_t index, const MotionMeshSubdivision& subdivision, std::span<resource::MotionPoint> positions) const;

	private:
		[[nodiscard]] std::optional<std::uint32_t> NextAncestor(std::uint32_t index) const;
		[[nodiscard]] bool CombinedChainReaches(std::uint32_t index, std::uint32_t ancestor) const;
		[[nodiscard]] resource::MotionPoint BoundaryOffset(std::uint32_t index) const;
		void AddBoundaryOffsets(std::uint32_t index, std::span<resource::MotionPoint> positions) const;
		void ApplyWarp(std::uint32_t index, std::span<resource::MotionPoint> positions) const;

		std::span<const MotionDeformationNode> m_Nodes;
		std::span<const MotionGeometryFrame> m_Frames;
		std::span<const MotionDeformationLink> m_Links;
		std::span<const MotionGeometryCache> m_Cache;
	};

} // namespace sgre::scene
