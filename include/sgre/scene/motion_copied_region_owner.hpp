#pragma once

#include "sgre/scene/motion_copied_target_owner.hpp"
#include "sgre/scene/motion_copy_region.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {
	class MotionRegionCopyBackend {
	public:
		virtual ~MotionRegionCopyBackend() = default;
		// The backend borrows its current rendering target and enqueues this source box into destination origin zero.
		virtual void CopyFromCurrentTarget(MutableTextureResource& destination, const MotionBounds& rectangle) = 0;
	};

	struct MotionCopiedRegionState {
		MotionCopiedTargetState Target;
		std::array<float, 4> UvAffine{};
		bool Selected = false;
	};

	class MotionCopiedRegionOwner {
	public:
		// One device identity supplies these focused capabilities; all backends outlive the owner.
		MotionCopiedRegionOwner(MutableTextureBackend& textures, MotionCopiedTargetBackend& targets, MotionRegionCopyBackend& copies, const MutableTextureLimits& limits = {});
		~MotionCopiedRegionOwner();
		MotionCopiedRegionOwner(const MotionCopiedRegionOwner&) = delete;
		MotionCopiedRegionOwner& operator=(const MotionCopiedRegionOwner&) = delete;
		MotionCopiedRegionOwner(MotionCopiedRegionOwner&&) = delete;
		MotionCopiedRegionOwner& operator=(MotionCopiedRegionOwner&&) = delete;
		// True directs the caller to reset accumulated bounds, after the native copy/setters/selection sequence.
		[[nodiscard]] bool Apply(const MotionCopyRegionPlan& plan, std::uint32_t source_format);
		void Close() noexcept;
		[[nodiscard]] MotionCopiedRegionState State() const noexcept;
		// Borrowed until a replacing Apply or Close; auxiliary shader binding is a separate operation.
		[[nodiscard]] MutableTextureOwner* SelectedTexture() noexcept;
		[[nodiscard]] const MutableTextureOwner* TargetTexture() const noexcept;

	private:
		MotionCopiedTargetOwner m_Target;
		MotionRegionCopyBackend& m_Copies;
		std::array<float, 4> m_UvAffine{};
		bool m_Selected = false;
	};
} // namespace sgre::scene
