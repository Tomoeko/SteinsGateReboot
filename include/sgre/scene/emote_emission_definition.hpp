#pragma once

#include "sgre/resource/psb.hpp"
#include "sgre/scene/emote_emission_emitter.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scene {
	struct EmoteEmissionEmitterMetadata {
		EmoteEmissionEmitterDefinition Definition;
		bool FollowMesh = false;
		bool IgnoreCollision = false;
		bool IgnoreSourceAngle = false;
		std::uint32_t Trigger = 0;
		std::int32_t TriggerEmitter = 0;
		std::vector<std::string> TriggerVariables;
	};

	struct EmoteEmissionGroupDefinition {
		std::string Uid;
		EmoteEmissionPoolDefinition Pool;
		std::vector<EmoteEmissionEmitterMetadata> Emitters;
		bool StartWithParticles = false;
		bool GenerateParticlesAtBack = false;
		std::uint32_t CollisionType = 0;
		float ReductionRatioAtReflection = 0;
		std::vector<std::string> SpeedVariables;
		std::vector<std::string> CollisionReferences;
	};

	// Actual document encloses this catalogue. Acquired definitions own their names/ranges/cel records.
	class EmoteEmissionCatalog {
	public:
		explicit EmoteEmissionCatalog(const resource::PsbDocument& document);
		~EmoteEmissionCatalog() noexcept;
		EmoteEmissionCatalog(const EmoteEmissionCatalog&) = delete;
		EmoteEmissionCatalog& operator=(const EmoteEmissionCatalog&) = delete;
		[[nodiscard]] std::optional<EmoteEmissionGroupDefinition> Read(std::string_view uid) const;
		[[nodiscard]] std::span<const std::string> AutomaticGroups() const noexcept;
		[[nodiscard]] float PixelsPerMeter() const noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
