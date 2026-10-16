#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/scene/emote_jelly_physics.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sgre::scene {

	struct EmoteJellyCollisionReference {
		std::string Uid;
		std::uint32_t IgnoreMask = 0;
	};

	struct EmoteJellyControlDefinition {
		EmoteJellyDefinition Physics;
		float PixelsPerMeter = 1;
		std::uint32_t PredictionDegree = 0;
		bool InheritShape = true;
		bool ReferenceParentMeshPosture = false;
		std::vector<std::string> SpeedVariables;
		std::vector<EmoteJellyCollisionReference> CollisionReferences;
		bool RequiresEmissions = false;
	};

	// Native2ea430: owns serialized definitions in order; actual UID/entity/driver owners remain separate.
	// Raw physical dimensions and normalized tress/fix coordinates are preserved; ppm belongs to UID conversion.
	[[nodiscard]] std::vector<EmoteJellyControlDefinition> ReadEmoteJellyControlDefinitions(const resource::PsbDocument& document);

} // namespace sgre::scene
