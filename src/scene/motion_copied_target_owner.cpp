#include "sgre/scene/motion_copied_target_owner.hpp"

#include <stdexcept>

namespace sgre::scene {
	MotionCopiedTargetOwner::MotionCopiedTargetOwner(MutableTextureBackend& texture_backend, MotionCopiedTargetBackend& target_backend, const MutableTextureLimits& limits) : m_TextureBackend(texture_backend), m_TargetBackend(target_backend), m_Limits(limits) {
	}

	MotionCopiedTargetOwner::~MotionCopiedTargetOwner() {
		Close();
	}

	void MotionCopiedTargetOwner::DisposeTarget() noexcept {
		// MRenderTarget's deleting destructor retires MTexture before its separately retained RTV.
		m_Texture.reset();
		m_RenderTargetView.reset();
		m_HasTarget = false;
	}

	bool MotionCopiedTargetOwner::Ensure(const MotionBounds& requested, std::uint32_t source_format) {
		if (m_Closed) {
			throw std::runtime_error("motion copied target owner has closed");
		}

		const auto update = PrepareMotionCopiedTargetUpdate(m_Size, requested);
		if (!update.Replace) {
			return false;
		}
		if (static_cast<std::uint32_t>(update.Size.Width) > m_Limits.MaximumExtent || static_cast<std::uint32_t>(update.Size.Height) > m_Limits.MaximumExtent) {
			throw std::runtime_error("motion copied target dimensions exceed the configured extent");
		}

		DisposeTarget();
		m_Size = update.Size;
		m_HasTarget = true;

		MutableTextureSpec spec;
		spec.Storage = MutableTextureStorage::RenderTarget;
		spec.Description.Width = static_cast<std::uint32_t>(m_Size.Width);
		spec.Description.Height = static_cast<std::uint32_t>(m_Size.Height);
		spec.Description.OriginalFormat = source_format;
		m_Texture = std::make_unique<MutableTextureOwner>(m_TextureBackend, spec, MutableTextureInitialData{}, m_Limits);

		try {
			m_RenderTargetView = m_TargetBackend.CreateRenderTargetView(m_Texture->Resource());
		} catch (...) {
			m_Texture.reset();
			throw;
		}
		if (!m_RenderTargetView) {
			m_Texture.reset();
		}

		return true;
	}

	void MotionCopiedTargetOwner::Close() noexcept {
		if (m_Closed) {
			return;
		}

		m_Closed = true;
		DisposeTarget();
	}

	MotionCopiedTargetState MotionCopiedTargetOwner::State() const noexcept {
		return {m_Size, m_HasTarget, static_cast<bool>(m_Texture), static_cast<bool>(m_RenderTargetView), m_Closed};
	}

	MutableTextureOwner* MotionCopiedTargetOwner::Texture() noexcept {
		return m_Texture.get();
	}

	const MutableTextureOwner* MotionCopiedTargetOwner::Texture() const noexcept {
		return m_Texture.get();
	}
} // namespace sgre::scene
