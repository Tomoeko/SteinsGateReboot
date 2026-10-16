#pragma once

#include <array>
#include <span>
#include <vector>

namespace sgre::scene {
	class EmoteBobbleModel;

	struct EmoteBobbleSectionDefinition {
		std::array<float, 3> Normal{0, 0, 1};
		std::array<float, 3> Tangent{1, 0, 0};
		float OrientationInheritance = 0;
		float RadiusInheritance = 0;
		std::vector<float> Radii;
	};

	struct EmoteBobbleSection {
		std::array<float, 2> Center{};
		std::array<float, 2> Radius{};
		float Angle = 0;
		float RelativeAngle = 0;
		float LengthScale = 1;
	};

	class EmoteBobbleSections {
	public:
		// Borrows the actual numerical owner, which must outlive this publication owner.
		EmoteBobbleSections(const EmoteBobbleModel& model, const EmoteBobbleSectionDefinition& definition);
		// Native325fc0 runs after Initialize/Step, without an additional numerical update.
		// Native assignment copies mutable values while retaining this owner's resources and bindings.
		void RequireCanAssignState(const EmoteBobbleSections& source) const;
		void AssignState(const EmoteBobbleSections& source);
		void Refresh();
		[[nodiscard]] std::span<const EmoteBobbleSection> Sections() const;

	private:
		const EmoteBobbleModel& m_Model;
		EmoteBobbleSectionDefinition m_Definition;
		std::vector<EmoteBobbleSection> m_Sections;
		bool m_Ready = false;
		bool m_Valid = true;
	};
} // namespace sgre::scene
