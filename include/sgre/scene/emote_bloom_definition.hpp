#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/scene/emote_bloom_physics.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sgre::scene {
	struct EmoteBloomCollisionReference {
		std::string Uid;
		std::uint32_t IgnoreMask = 0;
	};
	struct EmoteBloomControlDefinition {
		std::string Uid;
		EmoteBloomDefinition Physics;
		float PixelsPerMeter = 1;
		std::uint32_t PredictionDegree = 0;
		bool InheritShape = true;
		bool ReferenceParentMeshPosture = false;
		std::vector<std::string> SpeedVariables;
		std::vector<EmoteBloomCollisionReference> CollisionReferences;
		bool RequiresEmissions = false;
	};
	// Native2e6440 owns real metadata in serialized order. UID/entity allocation is a separate actual phase.
	[[nodiscard]] std::vector<EmoteBloomControlDefinition> ReadEmoteBloomControlDefinitions(const resource::PsbDocument& document);
} // namespace sgre::scene
