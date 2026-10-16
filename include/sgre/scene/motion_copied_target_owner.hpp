#pragma once

#include "sgre/scene/motion_copy_target.hpp"
#include "sgre/scene/mutable_texture_owner.hpp"

#include <cstdint>
#include <memory>

namespace sgre::scene {
	class MotionCopiedTargetView {
	public:
		virtual ~MotionCopiedTargetView() = default;
	};

	class MotionCopiedTargetBackend {
	public:
		virtual ~MotionCopiedTargetBackend() = default;
		// Null models the measured failed-RTV result; this owner retains the requested outer target.
		[[nodiscard]] virtual std::unique_ptr<MotionCopiedTargetView> CreateRenderTargetView(MutableTextureResource& resource) = 0;
	};

	struct MotionCopiedTargetState {
		MotionCopiedTargetSize Size;
		bool HasTarget = false;
		bool HasTexture = false;
		bool HasRenderTargetView = false;
		bool Closed = false;
	};

	class MotionCopiedTargetOwner {
	public:
		// Both backends outlive this exclusive owner and its borrowed texture/resource accessors.
		MotionCopiedTargetOwner(MutableTextureBackend& texture_backend, MotionCopiedTargetBackend& target_backend, const MutableTextureLimits& limits = {});
		~MotionCopiedTargetOwner();
		MotionCopiedTargetOwner(const MotionCopiedTargetOwner&) = delete;
		MotionCopiedTargetOwner& operator=(const MotionCopiedTargetOwner&) = delete;
		MotionCopiedTargetOwner(MotionCopiedTargetOwner&&) = delete;
		MotionCopiedTargetOwner& operator=(MotionCopiedTargetOwner&&) = delete;
		// The current source format is read only when the native size policy replaces the target.
		[[nodiscard]] bool Ensure(const MotionBounds& requested, std::uint32_t source_format);
		void Close() noexcept;
		[[nodiscard]] MotionCopiedTargetState State() const noexcept;
		// Borrowed until replacement or Close; failed RTV creation yields null, never a retained stale owner.
		[[nodiscard]] MutableTextureOwner* Texture() noexcept;
		[[nodiscard]] const MutableTextureOwner* Texture() const noexcept;

	private:
		void DisposeTarget() noexcept;
		MutableTextureBackend& m_TextureBackend;
		MotionCopiedTargetBackend& m_TargetBackend;
		MutableTextureLimits m_Limits;
		MotionCopiedTargetSize m_Size;
		std::unique_ptr<MutableTextureOwner> m_Texture;
		std::unique_ptr<MotionCopiedTargetView> m_RenderTargetView;
		bool m_HasTarget = false;
		bool m_Closed = false;
	};
} // namespace sgre::scene
