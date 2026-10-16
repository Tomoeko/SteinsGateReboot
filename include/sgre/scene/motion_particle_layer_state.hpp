#pragma once

#include "sgre/scene/motion_particle_emission.hpp"
#include "sgre/scene/motion_particle_frame_slots.hpp"
#include "sgre/scene/motion_particle_player_state.hpp"
#include "sgre/scene/motion_particle_retirement.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace sgre::scene {
	class MotionSelectedLayerNodes;
	class MotionSceneEvaluation;

	struct MotionParticleInstance {
		// Selected descendants retire before their actual player/preparation owner.
		std::unique_ptr<MotionParticlePlayerState> Player;
		std::unique_ptr<MotionSelectedLayerNodes> Selected;
		MotionParticleScreenState Screen;
		MotionParticleInstance();
		~MotionParticleInstance() noexcept;
		MotionParticleInstance(const MotionParticleInstance&) = delete;
		MotionParticleInstance& operator=(const MotionParticleInstance&) = delete;
	};

	class MotionParticleLayerState {
	public:
		MotionParticleLayerState(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const resource::PsbValue& document_root);
		// Copies physical slots/cadence only; the enclosing selected clone reconstructs each owned particle in birth order.
		MotionParticleLayerState(const MotionParticleLayerState& source, const PreparedMotionLayer& prepared);
		~MotionParticleLayerState() noexcept;
		MotionParticleLayerState(const MotionParticleLayerState&) = delete;
		MotionParticleLayerState& operator=(const MotionParticleLayerState&) = delete;
		[[nodiscard]] const MotionParticleDefinition& Definition() const noexcept;
		[[nodiscard]] MotionParticleFrameSlots& Frames() noexcept;
		[[nodiscard]] const MotionParticleFrameSlots& Frames() const noexcept;
		[[nodiscard]] std::size_t Size() const noexcept;

	private:
		friend class MotionSelectedLayerNodes;
		friend class MotionSceneEvaluation;
		friend class MotionLocalFrames;
		MotionParticleDefinition m_Definition;
		MotionParticleFrameSlots m_Frames;
		MotionParticleEmissionState m_Emission;
		std::vector<std::unique_ptr<MotionParticleInstance>> m_Particles;
	};

} // namespace sgre::scene
