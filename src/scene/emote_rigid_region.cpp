#include "sgre/scene/emote_rigid_region.hpp"

#include "sgre/scene/motion_bezier_lattice.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {
	static constexpr std::size_t kMaximumRigidConstraints = 16384;

	static std::uint32_t RigidSampleCount(float span, float spacing) {
		const float rounded = std::round(span / spacing) + 1.0f;
		if (!std::isfinite(rounded) || rounded > static_cast<float>(kMaximumRigidConstraints)) {
			throw std::runtime_error("Emote rigid region exceeds bounded native sample work");
		}

		return rounded > 2.0f ? static_cast<std::uint32_t>(rounded) : 2;
	}

	EmoteRigidRegionConstraints::EmoteRigidRegionConstraints(std::uint8_t encoded_degree, const std::array<float, 4>& rigid_range, float width, float height, bool horizontal_bones)
	    : m_Degree(encoded_degree), m_ControlCount(MotionBezierLatticeControlCount(encoded_degree)) {
		const auto horizontal = static_cast<std::uint32_t>(encoded_degree & 15);
		const auto vertical = static_cast<std::uint32_t>(encoded_degree >> 4);
		if ((horizontal < 3 || horizontal > 6) || (vertical < 3 || vertical > 6) || !(width > 0) || !(height > 0) || !std::isfinite(width) || !std::isfinite(height)) {
			throw std::runtime_error("Emote rigid region requires recovered degree3..6 axes and finite positive physical dimensions");
		}
		for (const auto boundary : rigid_range) {
			if (!std::isfinite(boundary)) {
				throw std::runtime_error("Emote rigid region requires finite authored rectangle boundaries");
			}
		}
		const float interval = horizontal_bones ? height / static_cast<float>(vertical * 2) : width / static_cast<float>(horizontal * 2);
		const resource::MotionPoint spacing{interval / width, interval / height};
		if (!(spacing.X > 0) || !(spacing.Y > 0) || !std::isfinite(spacing.X) || !std::isfinite(spacing.Y)) {
			throw std::runtime_error("Emote rigid region physical sampling spacing exceeds its finite domain");
		}

		if (rigid_range[1] > 0) {
			AddRegion({0, 0, 1, rigid_range[1]}, {0.5f, rigid_range[1]}, spacing);
		}
		if (rigid_range[3] < 1) {
			AddRegion({0, rigid_range[3], 1, 1}, {0.5f, rigid_range[3]}, spacing);
		}
		if (rigid_range[0] > 0) {
			AddRegion({0, 0, rigid_range[0], 1}, {rigid_range[0], 0.5f}, spacing);
		}
		if (rigid_range[2] < 1) {
			AddRegion({rigid_range[2], 0, 1, 1}, {rigid_range[2], 0.5f}, spacing);
		}
		// Original records have priority zero and compare nonnegative distance words; ties retain insertion order.
		std::stable_sort(m_Constraints.begin(), m_Constraints.end(), [](const Constraint& left, const Constraint& right) {
			return std::bit_cast<std::uint32_t>(left.DistanceSquared) < std::bit_cast<std::uint32_t>(right.DistanceSquared);
		});
	}

	void EmoteRigidRegionConstraints::AddRegion(const std::array<float, 4>& region, const resource::MotionPoint& anchor, const resource::MotionPoint& spacing) {
		const float width = region[2] - region[0];
		const float height = region[3] - region[1];
		const auto columns = RigidSampleCount(width, spacing.X);
		const auto rows = RigidSampleCount(height, spacing.Y);
		if (columns > (kMaximumRigidConstraints - m_Constraints.size()) / rows) {
			throw std::runtime_error("Emote rigid region exceeds bounded total constraint work");
		}
		const auto horizontal = static_cast<std::uint32_t>(m_Degree & 15);
		const auto vertical = static_cast<std::uint32_t>(m_Degree >> 4);
		for (std::uint32_t row = 0; row != rows; ++row) {
			const float y = height * (static_cast<float>(row) / static_cast<float>(rows - 1)) + region[1];
			const auto vertical_weights = MotionBezierBasisWeights(vertical, y);
			for (std::uint32_t column = 0; column != columns; ++column) {
				const float x = (static_cast<float>(column) / static_cast<float>(columns - 1)) * width + region[0];
				const auto horizontal_weights = MotionBezierBasisWeights(horizontal, x);
				Constraint constraint;
				constraint.Target = {x, y};
				constraint.DistanceSquared = (x - anchor.X) * (x - anchor.X) + (y - anchor.Y) * (y - anchor.Y);
				for (std::uint32_t control_row = 0; control_row <= vertical; ++control_row) {
					for (std::uint32_t control_column = 0; control_column <= horizontal; ++control_column) {
						constraint.Weights[static_cast<std::size_t>(control_row) * (horizontal + 1) + control_column] = horizontal_weights[control_column] * vertical_weights[control_row];
					}
				}
				m_Constraints.push_back(constraint);
			}
		}
	}

	bool EmoteRigidRegionConstraints::Empty() const noexcept {
		return m_Constraints.empty();
	}

	std::size_t EmoteRigidRegionConstraints::Count() const noexcept {
		return m_Constraints.size();
	}

	void EmoteRigidRegionConstraints::Apply(resource::MotionBezierLattice& actual_lattice, float relaxation) const {
		ApplyImpl(actual_lattice, relaxation, false);
	}

	void EmoteRigidRegionConstraints::ApplyForUnorderedComparison(resource::MotionBezierLattice& actual_lattice, float relaxation) const {
		ApplyImpl(actual_lattice, relaxation, true);
	}

	void EmoteRigidRegionConstraints::ApplyImpl(resource::MotionBezierLattice& actual_lattice, float relaxation, bool unordered_comparison) const {
		if (!unordered_comparison) {
			ValidateMotionBezierLattice(actual_lattice);
		}
		if (actual_lattice.EncodedDegree != m_Degree || actual_lattice.Controls.size() != m_ControlCount || !std::isfinite(relaxation)) {
			throw std::runtime_error("Emote rigid projection requires its actual degree and finite relaxation");
		}
		for (const auto& constraint : m_Constraints) {
			float x = 0;
			float y = 0;
			for (std::size_t control = 0; control != m_ControlCount; ++control) {
				x = x + constraint.Weights[control] * actual_lattice.Controls[control].X;
				y = y + constraint.Weights[control] * actual_lattice.Controls[control].Y;
			}
			const float correction_x = (constraint.Target.X - x) * relaxation;
			const float correction_y = (constraint.Target.Y - y) * relaxation;
			for (std::size_t control = 0; control != m_ControlCount; ++control) {
				actual_lattice.Controls[control].X = constraint.Weights[control] * correction_x + actual_lattice.Controls[control].X;
				actual_lattice.Controls[control].Y = constraint.Weights[control] * correction_y + actual_lattice.Controls[control].Y;
			}
		}
		if (!unordered_comparison) {
			ValidateMotionBezierLattice(actual_lattice);
		}
	}
} // namespace sgre::scene
