#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/scene/emote_catenary_physics.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sgre::scene {
	struct EmoteCatenaryCollisionReference {
		std::string Uid;
		std::uint32_t IgnoreMask = 0;
	};
	struct EmoteCatenaryControlDefinition {
		std::string Uid;
		EmoteCatenaryDefinition Physics;
		float PixelsPerMeter = 1;
		std::uint32_t PredictionDegree = 0;
		bool InheritShape = true;
		bool ReferenceParentMeshPosture = false;
		std::vector<std::string> SpeedVariables;
		std::vector<EmoteCatenaryCollisionReference> CollisionReferences;
		bool RequiresEmissions = false;
	};
	// Native2e7340 owns real metadata in serialized order. UID/entity allocation is a separate actual phase.
	[[nodiscard]] std::vector<EmoteCatenaryControlDefinition> ReadEmoteCatenaryControlDefinitions(const resource::PsbDocument& document);
} // namespace sgre::scene
