#pragma once

#include "sgre/scene/motion_scissor.hpp"
#include "sgre/scene/mutable_texture_owner.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace sgre::scene {
	struct CaptureFrameContext {
		MotionScissorMatrix View;
		MotionScissorMatrix Model;
		MotionScissorViewport Viewport;
		std::uint32_t OriginalFormat = 28;
		std::uint32_t NativeTargetType = 0;
		std::uint32_t NativeEye = 0;
	};

	struct CaptureTaskLimits {
		std::uint32_t MaximumExtent = 16384;
		std::size_t MaximumConsumers = 65536;
		std::size_t MaximumFrameReplacements = 64;
	};

	struct CaptureTaskState {
		resource::MotionPoint FixedSize;
		resource::MotionPoint Coordinate;
		MotionBounds PhysicalBounds;
		bool HasTexture = false;
		bool Captured = false;
		bool FramePending = false;
		bool Closed = false;
	};

	class CaptureTaskObserver {
	public:
		virtual ~CaptureTaskObserver() = default;
		// Native4fb1ef updates every actual MCaptured fixed size and readiness on full bounds replacement.
		virtual void CaptureResized(resource::MotionPoint physical_size) noexcept = 0;
		virtual void CaptureInvalidated() noexcept = 0;
		// Authored aborted-frame recovery: successful same-bounds copies restore readiness without a fake resize.
		virtual void CaptureCompleted() noexcept {
		}
		// Native4fad40 clears all consumer backlinks before deleting the staging/companion pair.
		virtual void CaptureRetired() noexcept = 0;
	};

	// Native4fafe0: project all four logical corners, clamp to the physical target, then5823e0 round.
	[[nodiscard]] MotionBounds PrepareCapturePhysicalBounds(const MotionBounds& logical, const CaptureFrameContext& actual_frame);

	class CaptureTask {
	public:
		using AllocateTexture = std::function<std::unique_ptr<MutableTextureOwner>(const MutableTextureSpec&)>;
		// The actual frame encoder owns same-buffer Store/blit/NotifyGpuWrite/Load; no CPU pixels cross this callback.
		using CopyTexture = std::function<void(MutableTextureOwner&, const MotionBounds&)>;
		CaptureTask(std::uint32_t width, std::uint32_t height, CaptureTaskLimits limits = {});
		~CaptureTask() noexcept;
		CaptureTask(const CaptureTask&) = delete;
		CaptureTask& operator=(const CaptureTask&) = delete;
		void SetOffset(resource::MotionPoint offset);
		// The backend encloses this task. Allocate is the encoder's typed suspended resource-preparation service.
		void Draw(MutableTextureBackend& actual_backend, const CaptureFrameContext& actual_frame, const AllocateTexture& allocate, const CopyTexture& copy);
		// The containing Cycle retains the actual shared task until End/Abort, then releases displaced owners here.
		void ReleaseFrame(bool submitted);
		void AddConsumer(CaptureTaskObserver& actual_consumer);
		void RemoveConsumer(CaptureTaskObserver& actual_consumer);
		[[nodiscard]] MutableTextureOwner& Texture();
		[[nodiscard]] CaptureTaskState State() const noexcept;
		[[nodiscard]] bool CanClose() const noexcept;
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::scene
