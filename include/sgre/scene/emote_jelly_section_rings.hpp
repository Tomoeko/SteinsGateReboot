#pragma once

#include "sgre/resource/motion_mesh.hpp"
#include "sgre/scene/emote_bobble_sections.hpp"

#include <array>
#include <span>
#include <vector>

namespace sgre::scene {

	class EmoteBobbleModel;

	class EmoteJellySectionRings {
	public:
		// Borrows the real numerical owner. Refresh follows Initialize/Step, without another physics update.
		EmoteJellySectionRings(const EmoteBobbleModel& model, const EmoteBobbleSectionDefinition& definition);
		void Refresh();
		// Native316c00 keeps16 points per particle plus one additional collapsed ring at the actual tip center.
		[[nodiscard]] std::span<const resource::MotionPoint> Rings() const;

	private:
		const EmoteBobbleModel& m_Model;
		EmoteBobbleSections m_Sections;
		std::array<resource::MotionPoint, 16> m_Directions;
		std::vector<resource::MotionPoint> m_Rings;
		bool m_Ready = false;
		bool m_Valid = true;
	};

} // namespace sgre::scene
