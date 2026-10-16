#include "sgre/scene/motion_texture_binding.hpp"

#include <stdexcept>

namespace sgre::scene {
	MotionTextureBindingScope::MotionTextureBindingScope(MotionTextureBindingState& state) : m_State(state) {
		if (state.AuxiliaryTarget != nullptr) {
			m_SavedAuxiliary = state.Textures[1];
			state.Textures[1] = state.AuxiliaryTarget;
		}
		if (state.CopiedTarget != nullptr && (state.BlendMode & 0xf00u) == 0x100u) {
			m_SavedCopied = state.Textures[2];
			state.Textures[2] = state.CopiedTarget;
		}
	}

	MotionTextureBindingScope::~MotionTextureBindingScope() {
		Restore();
	}

	void MotionTextureBindingScope::Bind() {
		if (m_Restored || m_Bound) {
			throw std::runtime_error("motion texture binding scope is already bound or restored");
		}

		m_Bound = true;
		for (std::uint32_t slot = 0; slot < m_State.Textures.size(); ++slot) {
			if (auto* texture = m_State.Textures[slot]; texture != nullptr) {
				texture->Bind(slot, MutableTextureStage::Pixel);
			}
		}
	}

	void MotionTextureBindingScope::Restore() noexcept {
		if (m_Restored) {
			return;
		}

		m_Restored = true;
		if (m_State.AuxiliaryTarget != nullptr) {
			m_State.Textures[1] = m_SavedAuxiliary;
		}
		// Native family-0x100 restoration also writes the initial null temporary when no copy override occurred.
		if ((m_State.BlendMode & 0xf00u) == 0x100u) {
			m_State.Textures[2] = m_SavedCopied;
		}
	}
} // namespace sgre::scene
