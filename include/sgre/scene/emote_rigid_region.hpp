#pragma once

#include "sgre/resource/motion_mesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sgre::scene {
	class EmoteRigidRegionConstraints {
	public:
		// Native 3005e0 samples the four regions outside the authored rigid rectangle.
		EmoteRigidRegionConstraints(std::uint8_t encoded_degree, const std::array<float, 4>& rigid_range, float width, float height, bool horizontal_bones);
		[[nodiscard]] bool Empty() const noexcept;
		[[nodiscard]] std::size_t Count() const noexcept;
		// One native 3012b0 projection pass in normalized coordinates, with no frame-time allocation.
		void Apply(resource::MotionBezierLattice& actual_lattice, float relaxation) const;

	private:
		friend class EmoteStringMeshOutput;
		void ApplyForUnorderedComparison(resource::MotionBezierLattice& actual_lattice, float relaxation) const;
		void ApplyImpl(resource::MotionBezierLattice& actual_lattice, float relaxation, bool unordered_comparison) const;
		struct Constraint {
			resource::MotionPoint Target;
			float DistanceSquared = 0;
			std::array<float, 49> Weights{};
		};
		void AddRegion(const std::array<float, 4>& region, const resource::MotionPoint& anchor, const resource::MotionPoint& spacing);
		std::uint8_t m_Degree = 0;
		std::size_t m_ControlCount = 0;
		std::vector<Constraint> m_Constraints;
	};
} // namespace sgre::scene
