#include "sgre/scene/motion_particle_layer_state.hpp"

#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {
	static constexpr std::int32_t kMaximumSelectedParticles = 65536;
	MotionParticleInstance::MotionParticleInstance() = default;
	MotionParticleInstance::~MotionParticleInstance() noexcept = default;

	MotionParticleLayerState::MotionParticleLayerState(const resource::MotionLayer& layer, const PreparedMotionLayer& prepared, const resource::PsbValue& document_root)
	    : m_Definition(ReadMotionParticleDefinition(layer, document_root)), m_Frames(layer, prepared) {
		if (m_Definition.Particle < 0 || m_Definition.Particle > 2 || m_Definition.InheritAngle || m_Definition.TriangularVolume || m_Definition.InheritOpacity != 1 || m_Definition.InheritVelocity != 0 || (m_Definition.FlyDirection != 0 && m_Definition.FlyDirection != 1) || m_Definition.ApplyZoomToVelocity < 0 || m_Definition.ApplyZoomToVelocity > 2 || !std::isfinite(m_Definition.AccelerationRatio) || m_Definition.AccelerationRatio < 0) {
			throw std::runtime_error("motion selected particles require their recovered planar birth and inheritance domain");
		}
		if (m_Definition.MaximumParticles < 1 || m_Definition.MaximumParticles > kMaximumSelectedParticles) {
			throw std::runtime_error("motion selected particle vector exceeds its bounded actual maximum");
		}
		m_Particles.reserve(static_cast<std::size_t>(m_Definition.MaximumParticles) + 1);
	}

	MotionParticleLayerState::MotionParticleLayerState(const MotionParticleLayerState& source, const PreparedMotionLayer& prepared)
	    : m_Definition(source.m_Definition), m_Frames(source.m_Frames, prepared), m_Emission(source.m_Emission) {
		if (source.m_Particles.size() > static_cast<std::size_t>(m_Definition.MaximumParticles) + 1) {
			throw std::runtime_error("motion particle clone exceeds its actual bounded retained vector");
		}
		m_Particles.reserve(static_cast<std::size_t>(m_Definition.MaximumParticles) + 1);
	}

	MotionParticleLayerState::~MotionParticleLayerState() noexcept {
		// Native50fd90 retires this real vector in forward birth order.
		for (auto& particle : m_Particles) {
			particle.reset();
		}
	}

	const MotionParticleDefinition& MotionParticleLayerState::Definition() const noexcept {
		return m_Definition;
	}

	MotionParticleFrameSlots& MotionParticleLayerState::Frames() noexcept {
		return m_Frames;
	}

	const MotionParticleFrameSlots& MotionParticleLayerState::Frames() const noexcept {
		return m_Frames;
	}

	std::size_t MotionParticleLayerState::Size() const noexcept {
		return m_Particles.size();
	}

} // namespace sgre::scene
