#pragma once

#include "sgre/resource/motion_mesh.hpp"

#include <optional>
#include <span>

namespace sgre::scene {

	struct MotionMeshSubdivision {
		std::uint32_t X = 1;
		std::uint32_t Y = 1;
	};

	struct MotionDeformationWindow {
		float Left = 0;
		float Top = 0;
		float Right = 1;
		float Bottom = 1;
	};

	struct PreparedMotionDeformation {
		resource::MotionBezierPatch WorldPatch;
		resource::MotionMeshMatrix WorldToPatch;
		// Presence selects the generalized grid; WorldPatch is used exclusively by the original cubic fast path.
		std::optional<resource::MotionBezierLattice> WorldLattice;
	};

	[[nodiscard]] resource::MotionBezierPatch IdentityMotionBezierPatch();
	[[nodiscard]] std::optional<resource::MotionBezierPatch> InterpolateMotionBezierPatch(const std::optional<resource::MotionBezierPatch>& start, const std::optional<resource::MotionBezierPatch>& end, float ratio, const resource::EasingCurve* easing = nullptr);
	[[nodiscard]] resource::MotionPoint EvaluateMotionBezierPatch(const resource::MotionBezierPatch& patch, const resource::MotionPoint& coordinate);
	[[nodiscard]] std::optional<resource::MotionBezierPatch> CombineMotionBezierPatches(const std::optional<resource::MotionBezierPatch>& first, const std::optional<resource::MotionBezierPatch>& second);
	[[nodiscard]] resource::MotionMeshMatrix InverseMotionMeshMatrix(const resource::MotionMeshMatrix& matrix);
	[[nodiscard]] resource::MotionPoint TransformMotionMeshPoint(const resource::MotionMeshMatrix& matrix, const resource::MotionPoint& point);
	[[nodiscard]] resource::MotionBezierPatch TransformMotionBezierPatch(const resource::MotionBezierPatch& patch, const resource::MotionMeshMatrix& matrix);
	[[nodiscard]] PreparedMotionDeformation PrepareMotionDeformation(const resource::MotionBezierPatch& patch, const resource::MotionMeshMatrix& space);
	[[nodiscard]] PreparedMotionDeformation PrepareMotionDeformation(const resource::MotionBezierLattice& lattice, const resource::MotionMeshMatrix& space);
	void ApplyMotionDeformation(const PreparedMotionDeformation& deformation, std::span<resource::MotionPoint> positions);
	void EvaluateMotionDeformationGrid(const PreparedMotionDeformation& deformation, const MotionMeshSubdivision& subdivision, const MotionDeformationWindow& window, std::span<resource::MotionPoint> vertices);
	[[nodiscard]] MotionMeshSubdivision SelectMotionMeshSubdivision(std::uint32_t division, std::uint32_t width, std::uint32_t height);
	[[nodiscard]] std::size_t MotionMeshGridVertexCount(const MotionMeshSubdivision& subdivision);
	void EvaluateMotionBilinearGrid(const std::array<resource::MotionPoint, 4>& corners, const MotionMeshSubdivision& subdivision, std::span<resource::MotionPoint> vertices);
	void EvaluateMotionBezierGrid(const resource::MotionBezierPatch& patch, const MotionMeshSubdivision& subdivision, const MotionDeformationWindow& window, std::span<resource::MotionPoint> vertices);

} // namespace sgre::scene
