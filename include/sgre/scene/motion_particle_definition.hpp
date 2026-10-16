#pragma once

#include "sgre/resource/motion.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sgre::scene {

	struct MotionParticleTarget {
		std::string Character;
		std::string Timeline;
		bool operator==(const MotionParticleTarget&) const = default;
	};

	struct MotionParticleScreenRectangle {
		float Left = 0;
		float Top = 0;
		float Right = 0;
		float Bottom = 0;

		bool operator==(const MotionParticleScreenRectangle&) const = default;
	};

	struct MotionParticleDefinition {
		std::int32_t Particle = 0;
		std::int32_t MaximumParticles = 0;
		bool InheritAngle = false;
		bool DeleteOutsideScreen = false;
		bool TriangularVolume = false;
		std::int32_t InheritOpacity = 1;
		std::int32_t InheritVelocity = 0;
		std::int32_t FlyDirection = 0;
		std::int32_t ApplyZoomToVelocity = 0;
		float AccelerationRatio = 0;
		std::vector<MotionParticleTarget> MotionList;
		std::optional<MotionParticleScreenRectangle> Screen;
		std::optional<MotionParticleScreenRectangle> ExpandedScreen;
	};

	struct MotionParticleFrameControl {
		// Both constructor-owned native 40-byte slots begin as all zero words.
		std::int32_t Trigger = 0;
		float FrequencyMin = 0;
		float FrequencyMax = 0;
		float VelocityMin = 0;
		float VelocityMax = 0;
		float AngleMin = 0;
		float AngleMax = 0;
		float ZoomMin = 0;
		float ZoomMax = 0;
		float Range = 0;

		bool operator==(const MotionParticleFrameControl&) const = default;
	};

	// Owns target names. Only the outside-screen branch reads the actual document root's screenSize.
	[[nodiscard]] MotionParticleDefinition ReadMotionParticleDefinition(const resource::MotionLayer& layer, const resource::PsbValue& document_root);
	// Hidden frames and frames without mask 0x100000 leave a reused physical slot unchanged.
	[[nodiscard]] std::optional<MotionParticleFrameControl> PrepareMotionParticleFrameControl(const resource::MotionLayer& layer, const resource::MotionFrame& frame);
	void ApplyMotionParticleFrameControl(const std::optional<MotionParticleFrameControl>& update, MotionParticleFrameControl& slot);

} // namespace sgre::scene
