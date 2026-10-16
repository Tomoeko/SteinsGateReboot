#include "sgre/scene/emote_jelly_section_rings.hpp"

#include "sgre/scene/emote_bobble_model.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace sgre::scene {

	static constexpr float kJellyRingPi = std::bit_cast<float>(std::uint32_t{0x40490fdb});

	static resource::MotionMeshMatrix ComposeRingMatrices(const resource::MotionMeshMatrix& outer, const resource::MotionMeshMatrix& inner) {
		resource::MotionMeshMatrix result;
		result.XX = outer.XY * inner.YX + outer.XX * inner.XX;
		result.XY = outer.XY * inner.YY + outer.XX * inner.XY;
		result.YX = outer.YY * inner.YX + outer.YX * inner.XX;
		result.YY = outer.YY * inner.YY + outer.YX * inner.XY;
		result.TranslationX = (inner.TranslationY * outer.XY + inner.TranslationX * outer.XX) + outer.TranslationX;
		result.TranslationY = (inner.TranslationY * outer.YY + inner.TranslationX * outer.YX) + outer.TranslationY;

		return result;
	}

	static resource::MotionMeshMatrix SectionRingMatrix(const EmoteBobbleSection& section, const std::array<float, 2>& offset, bool unordered_comparison) {
		if (section.Radius[0] == 0 || section.Radius[1] == 0) {
			throw std::runtime_error("Emote Jelly ring requires a finite invertible actual cross-section");
		}

		const float sine = std::sin(-section.Angle);
		const float cosine = std::cos(-section.Angle);
		resource::MotionMeshMatrix translation;
		translation.TranslationX = -section.Center[0];
		translation.TranslationY = -section.Center[1];
		resource::MotionMeshMatrix rotation;
		rotation.XX = cosine;
		rotation.XY = -sine;
		rotation.YX = sine;
		rotation.YY = cosine;
		resource::MotionMeshMatrix scale;
		scale.XX = 1.0f / section.Radius[0];
		scale.YY = 1.0f / section.Radius[1];
		const auto normalization = ComposeRingMatrices(scale, ComposeRingMatrices(rotation, translation));
		const float determinant = normalization.YY * normalization.XX - normalization.YX * normalization.XY;
		if (determinant == 0 || (!unordered_comparison && !std::isfinite(determinant))) {
			throw std::runtime_error("Emote Jelly section normalization exceeded its finite native inverse");
		}

		const float inverse_determinant = 1.0f / determinant;
		resource::MotionMeshMatrix inverse;
		inverse.XX = inverse_determinant * normalization.YY;
		inverse.XY = -normalization.XY * inverse_determinant;
		inverse.YX = -normalization.YX * inverse_determinant;
		inverse.YY = inverse_determinant * normalization.XX;
		inverse.TranslationX = (normalization.XY * normalization.TranslationY - normalization.YY * normalization.TranslationX) * inverse_determinant;
		inverse.TranslationY = (normalization.YX * normalization.TranslationX - normalization.XX * normalization.TranslationY) * inverse_determinant;
		resource::MotionMeshMatrix relative;
		relative.XX = std::cos(section.RelativeAngle);
		relative.XY = -std::sin(section.RelativeAngle);
		relative.YX = -relative.XY;
		relative.YY = relative.XX;
		auto output = ComposeRingMatrices(inverse, relative);
		output.TranslationX = output.TranslationX - offset[0];
		output.TranslationY = output.TranslationY - offset[1];

		return output;
	}

	EmoteJellySectionRings::EmoteJellySectionRings(const EmoteBobbleModel& model, const EmoteBobbleSectionDefinition& definition)
	    : m_Model(model), m_Sections(model, definition), m_Rings((model.Particles().size() + 1) * m_Directions.size()) {
		for (std::size_t index = 0; index < m_Directions.size(); ++index) {
			const float angle = (static_cast<float>(index) * kJellyRingPi) / 8.0f;
			const float sine = std::sin(angle);
			const float cosine = std::cos(angle);
			m_Directions[index] = {cosine * 0.0f - sine, sine * 0.0f + cosine};
		}
	}

	void EmoteJellySectionRings::Refresh() {
		if (!m_Valid) {
			throw std::runtime_error("Emote Jelly section rings require a valid actual publication owner");
		}

		try {
			m_Sections.Refresh();
			const auto sections = m_Sections.Sections();
			const auto& offset = m_Model.m_CoordinateOffset;
			for (std::size_t section = 0; section < sections.size(); ++section) {
				const auto matrix = SectionRingMatrix(sections[section], offset, m_Model.m_UnorderedJellyComparison);
				for (std::size_t index = 0; index < m_Directions.size(); ++index) {
					const auto& direction = m_Directions[index];
					auto& point = m_Rings[section * m_Directions.size() + index];
					point.X = (direction.Y * matrix.XY + direction.X * matrix.XX) + matrix.TranslationX;
					point.Y = (direction.Y * matrix.YY + direction.X * matrix.YX) + matrix.TranslationY;
					if (!m_Model.m_UnorderedJellyComparison && (!std::isfinite(point.X) || !std::isfinite(point.Y))) {
						throw std::runtime_error("Emote Jelly ring publication exceeded finite actual section geometry");
					}
				}
			}

			const auto& tip = sections.back().Center;
			const resource::MotionPoint collapsed{tip[0] - offset[0], tip[1] - offset[1]};
			for (auto& point : std::span(m_Rings).last(m_Directions.size())) {
				point = collapsed;
			}
			m_Ready = true;
		} catch (...) {
			m_Valid = false;
			throw;
		}
	}

	std::span<const resource::MotionPoint> EmoteJellySectionRings::Rings() const {
		m_Model.RequireIdle();
		if (!m_Valid || !m_Ready) {
			throw std::runtime_error("Emote Jelly rings require completed actual section publication");
		}

		return m_Rings;
	}

} // namespace sgre::scene
