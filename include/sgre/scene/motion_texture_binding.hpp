#pragma once

#include "sgre/scene/mutable_texture_owner.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {
	struct MotionTextureBindingState {
		// The rendering/resource owners outlive the prepared draw and every borrowing scope.
		std::array<MutableTextureOwner*, 3> Textures{};
		MutableTextureOwner* AuxiliaryTarget = nullptr;
		MutableTextureOwner* CopiedTarget = nullptr;
		std::uint32_t BlendMode = 0;
	};

	class MotionTextureBindingScope {
	public:
		// State and all referenced actual MTexture owners outlive this single prepared-row scope.
		explicit MotionTextureBindingScope(MotionTextureBindingState& state);
		~MotionTextureBindingScope();
		MotionTextureBindingScope(const MotionTextureBindingScope&) = delete;
		MotionTextureBindingScope& operator=(const MotionTextureBindingScope&) = delete;
		MotionTextureBindingScope(MotionTextureBindingScope&&) = delete;
		MotionTextureBindingScope& operator=(MotionTextureBindingScope&&) = delete;
		// Each nonnull actual owner binds its native pixel slot; null entries do not clear device bindings.
		void Bind();
		void Restore() noexcept;

	private:
		MotionTextureBindingState& m_State;
		MutableTextureOwner* m_SavedAuxiliary = nullptr;
		MutableTextureOwner* m_SavedCopied = nullptr;
		bool m_Bound = false;
		bool m_Restored = false;
	};
} // namespace sgre::scene
