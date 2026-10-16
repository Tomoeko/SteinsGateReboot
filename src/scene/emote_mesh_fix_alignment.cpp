#include "sgre/scene/emote_mesh_fix_alignment.hpp"

#include "motion_native_compared_mesh.hpp"
#include "sgre/scene/motion_bezier_lattice.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {
	static resource::MotionMeshMatrix ComposeFixMatrices(const resource::MotionMeshMatrix& outer, const resource::MotionMeshMatrix& inner) {
		resource::MotionMeshMatrix result;
		result.XX = outer.XY * inner.YX + outer.XX * inner.XX;
		result.XY = outer.XY * inner.YY + outer.XX * inner.XY;
		result.YX = outer.YY * inner.YX + outer.YX * inner.XX;
		result.YY = outer.YY * inner.YY + outer.YX * inner.XY;
		result.TranslationX = (inner.TranslationY * outer.XY + inner.TranslationX * outer.XX) + outer.TranslationX;
		result.TranslationY = (inner.TranslationY * outer.YY + inner.TranslationX * outer.YX) + outer.TranslationY;

		return result;
	}

	static resource::MotionPoint FixPoint(const resource::MotionPoint& point, const resource::MotionMeshMatrix& matrix) {
		return {(point.Y * matrix.XY + point.X * matrix.XX) + matrix.TranslationX, (point.Y * matrix.YY + point.X * matrix.YX) + matrix.TranslationY};
	}

	static void TransformFixLattice(resource::MotionBezierLattice& lattice, const resource::MotionMeshMatrix& matrix, bool unordered_comparison) {
		for (auto& point : lattice.Controls) {
			point = FixPoint(point, matrix);
		}
		if (!unordered_comparison) {
			ValidateMotionBezierLattice(lattice);
		}
	}

	static resource::MotionMeshMatrix InvertFixMatrix(const resource::MotionMeshMatrix& matrix) {
		const float determinant = matrix.XX * matrix.YY - matrix.YX * matrix.XY;
		if (!std::isfinite(determinant) || determinant == 0) {
			throw std::runtime_error("Emote mesh fix alignment requires its finite invertible physical normalization");
		}
		const float inverse = 1.0f / determinant;
		resource::MotionMeshMatrix result;
		result.XX = matrix.YY * inverse;
		result.XY = -matrix.XY * inverse;
		result.YX = -matrix.YX * inverse;
		result.YY = matrix.XX * inverse;
		result.TranslationX = (matrix.TranslationY * matrix.XY - matrix.YY * matrix.TranslationX) * inverse;
		result.TranslationY = (matrix.YX * matrix.TranslationX - matrix.XX * matrix.TranslationY) * inverse;

		return result;
	}

	static resource::MotionMeshMatrix FixAxisFrame(const resource::MotionPoint& origin, float cosine, float sine) {
		resource::MotionMeshMatrix result;
		result.XX = cosine;
		result.XY = sine;
		result.YX = -sine;
		result.YY = cosine;
		result.TranslationX = -origin.Y * sine + -origin.X * cosine;
		result.TranslationY = -origin.X * -sine + -origin.Y * cosine;

		return result;
	}

	static float ApproximateFixInverseLength(float squared, bool unordered_comparison) {
		if ((!unordered_comparison && !std::isfinite(squared)) || squared < 0) {
			throw std::runtime_error("Emote mesh fix separation exceeds its finite native domain");
		}
		// Native 2fe220 /2fe690 preserve this seed and both float Newton steps.
		const float seed = std::bit_cast<float>(std::uint32_t{0x5f3759df} - (std::bit_cast<std::uint32_t>(squared) >> 1));
		const float half = squared * 0.5f;
		const float first = (1.5f - (half * seed) * seed) * seed;

		return (1.5f - (first * half) * first) * first;
	}

	EmoteMeshFixAlignment::EmoteMeshFixAlignment(std::span<const resource::MotionPoint> fixes, float width, float height)
	    : m_Count(fixes.size()) {
		if (fixes.size() < 2 || fixes.size() > m_Fixes.size() || !(width > 0) || !(height > 0) || !std::isfinite(width) || !std::isfinite(height)) {
			throw std::runtime_error("Emote multiple-fix alignment requires two/three actual fixes and finite positive physical dimensions");
		}
		for (const auto& fix : fixes) {
			if (!std::isfinite(fix.X) || !std::isfinite(fix.Y)) {
				throw std::runtime_error("Emote mesh fix alignment requires finite authored fixes");
			}
		}
		std::copy(fixes.begin(), fixes.end(), m_Fixes.begin());
		const float x = (fixes[1].X - fixes[0].X) * width;
		const float y = (fixes[1].Y - fixes[0].Y) * height;
		m_ReferenceLength = std::sqrt(y * y + x * x);
		if (!(m_ReferenceLength > 0) || !std::isfinite(m_ReferenceLength)) {
			throw std::runtime_error("Emote multiple-fix alignment requires distinct finite first/second physical fixes");
		}
		const float cosine = x / m_ReferenceLength;
		const float sine = y / m_ReferenceLength;
		m_Reference.XX = sine * 0.0f + cosine;
		m_Reference.XY = cosine * 0.0f + -sine;
		m_Reference.YX = sine + cosine * 0.0f;
		m_Reference.YY = -sine * 0.0f + cosine;
		m_Reference.TranslationX = (fixes[0].X - (fixes[0].X + fixes[1].X) * 0.5f) * width + 0.0f;
		m_Reference.TranslationY = (fixes[0].Y - (fixes[0].Y + fixes[1].Y) * 0.5f) * height + 0.0f;
		if (m_Count == 3) {
			const float third_x = (fixes[2].X - fixes[0].X) * width + (fixes[2].Y - fixes[0].Y) * 0.0f + 0.0f;
			const float third_y = (fixes[2].Y - fixes[0].Y) * height + (fixes[2].X - fixes[0].X) * 0.0f + 0.0f;
			m_ReferenceThird.X = cosine * third_x - -sine * third_y + 0.0f;
			m_ReferenceThird.Y = cosine * third_y + -sine * third_x + 0.0f;
		}
	}

	void EmoteMeshFixAlignment::NormalizeImpl(resource::MotionBezierLattice& lattice, const EmotePhysicalPosture& posture, const resource::MotionMeshMatrix& world_to_normalized, bool unordered_comparison) const {
		const auto evaluate = unordered_comparison ? EvaluateUnorderedMotionBezierLattice : EvaluateMotionBezierLattice;
		const auto first = evaluate(lattice, m_Fixes[0]);
		const auto second = evaluate(lattice, m_Fixes[1]);
		const float x = second.X - first.X;
		const float y = second.Y - first.Y;
		const float inverse_length = ApproximateFixInverseLength(y * y + x * x, unordered_comparison);
		auto alignment = FixAxisFrame(first, x * inverse_length, y * inverse_length);
		resource::MotionMeshMatrix longitudinal_scale;
		longitudinal_scale.XX = inverse_length * m_ReferenceLength;
		alignment = ComposeFixMatrices(longitudinal_scale, alignment);
		if (m_Count == 3) {
			const auto third = FixPoint(evaluate(lattice, m_Fixes[2]), alignment);
			if (m_ReferenceThird.Y != 0 && third.Y != 0) {
				resource::MotionMeshMatrix shear;
				shear.XY = (m_ReferenceThird.X - third.X) * (1.0f / third.Y);
				shear.YY = (1.0f / third.Y) * m_ReferenceThird.Y;
				alignment = ComposeFixMatrices(shear, alignment);
			}
		}
		resource::MotionMeshMatrix physical;
		physical.XX = posture.Cosine;
		physical.XY = -posture.Sine;
		physical.YX = posture.Sine;
		physical.YY = posture.Cosine;
		physical.TranslationX = posture.Coordinate[0];
		physical.TranslationY = posture.Coordinate[1];
		TransformFixLattice(lattice, ComposeFixMatrices(world_to_normalized, ComposeFixMatrices(physical, ComposeFixMatrices(m_Reference, alignment))), unordered_comparison);
	}

	void EmoteMeshFixAlignment::CorrectImpl(resource::MotionBezierLattice& lattice, std::span<const resource::MotionPoint> world_targets, const resource::MotionMeshMatrix& world_to_normalized, bool unordered_comparison) const {
		if (world_targets.size() != m_Count) {
			throw std::runtime_error("Emote multiple-fix correction requires every actual transformed world target");
		}
		const auto normalized_to_world = InvertFixMatrix(world_to_normalized);
		const auto source_first = FixPoint(m_Fixes[0], normalized_to_world);
		const auto source_second = FixPoint(m_Fixes[1], normalized_to_world);
		const float target_x = world_targets[1].X - world_targets[0].X;
		const float target_y = world_targets[1].Y - world_targets[0].Y;
		const float target_length = std::sqrt(target_y * target_y + target_x * target_x);
		if (!(target_length > 0) || !std::isfinite(target_length)) {
			throw std::runtime_error("Emote multiple-fix correction requires distinct finite world targets");
		}
		const float reference_inverse = 1.0f / m_ReferenceLength;
		auto alignment = FixAxisFrame(source_first, (source_second.X - source_first.X) * reference_inverse, (source_second.Y - source_first.Y) * reference_inverse);
		resource::MotionMeshMatrix longitudinal_scale;
		longitudinal_scale.XX = target_length * reference_inverse;
		alignment = ComposeFixMatrices(longitudinal_scale, alignment);
		const float target_inverse = 1.0f / target_length;
		const float cosine = target_x * target_inverse;
		const float sine = target_y * target_inverse;
		alignment = ComposeFixMatrices(alignment, normalized_to_world);
		if (m_Count == 3) {
			// Native2ff550 compares both thirds in the retained source frame with this signed shear.
			const auto source_third = FixPoint(m_Fixes[2], alignment);
			const auto target_third = FixPoint(FixPoint(world_targets[2], world_to_normalized), alignment);
			if (source_third.Y != 0) {
				resource::MotionMeshMatrix shear;
				shear.XY = (source_third.X - target_third.X) * (1.0f / source_third.Y);
				shear.YY = target_third.Y * (1.0f / source_third.Y);
				alignment = ComposeFixMatrices(shear, alignment);
			}
		}
		resource::MotionMeshMatrix target_frame;
		target_frame.XX = cosine;
		target_frame.XY = -sine;
		target_frame.YX = sine;
		target_frame.YY = cosine;
		target_frame.TranslationX = world_targets[0].X;
		target_frame.TranslationY = world_targets[0].Y;
		TransformFixLattice(lattice, ComposeFixMatrices(world_to_normalized, ComposeFixMatrices(target_frame, alignment)), unordered_comparison);
	}

	void EmoteMeshFixAlignment::Normalize(resource::MotionBezierLattice& lattice, const EmotePhysicalPosture& posture, const resource::MotionMeshMatrix& world_to_normalized) const {
		NormalizeImpl(lattice, posture, world_to_normalized, false);
	}

	void EmoteMeshFixAlignment::Correct(resource::MotionBezierLattice& lattice, std::span<const resource::MotionPoint> world_targets, const resource::MotionMeshMatrix& world_to_normalized) const {
		CorrectImpl(lattice, world_targets, world_to_normalized, false);
	}

	void EmoteMeshFixAlignment::NormalizeForUnorderedComparison(resource::MotionBezierLattice& lattice, const EmotePhysicalPosture& posture, const resource::MotionMeshMatrix& world_to_normalized) const {
		NormalizeImpl(lattice, posture, world_to_normalized, true);
	}

	void EmoteMeshFixAlignment::CorrectForUnorderedComparison(resource::MotionBezierLattice& lattice, std::span<const resource::MotionPoint> world_targets, const resource::MotionMeshMatrix& world_to_normalized) const {
		CorrectImpl(lattice, world_targets, world_to_normalized, true);
	}
} // namespace sgre::scene
