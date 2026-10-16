#pragma once

#include "sgre/resource/motion.hpp"
#include "sgre/scene/motion_layer_tasks.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace sgre::scene {

	struct LayerScaleCameraState {
		resource::MotionPoint Offset;
		resource::MotionPoint Scale{1, 1};
		float RotationRadians = 0;
	};

	struct LayerScaleViewState {
		resource::MotionPoint Offset;
		resource::MotionPoint Scale{1, 1};
		bool HasCamera = false;
		bool Detached = false;
	};

	struct LayerScaleTaskCallbacks {
		std::function<void()> Update;
		std::function<void(std::uint32_t)> Draw;
		std::function<void()> Release;
		// Observational admission for an actual retained frame/resource borrow; the parent checks it before clearing.
		std::function<bool()> CanRelease;
		// A recovered matrix/scissor service must bracket child drawing; no unscaled fallback is permitted.
		std::function<void(const LayerScaleCameraState&, ParallelTaskSet&, std::uint32_t)> DrawWithCamera;
		// Native payload constructors differ: RawTex/TextRender start paused; DrawingProxy starts unpaused.
		bool InitiallyDrawPaused = true;
	};

	class LayerScaleView {
	public:
		// The actual Layer owns task callbacks in First. This wrapper only borrows the task and optional camera.
		LayerScaleView(MotionLayerTasks& parent, LayerScaleTaskCallbacks task, std::size_t max_camera_tasks = 64);
		~LayerScaleView();
		LayerScaleView(const LayerScaleView&) = delete;
		LayerScaleView& operator=(const LayerScaleView&) = delete;
		LayerScaleView(LayerScaleView&&) = delete;
		LayerScaleView& operator=(LayerScaleView&&) = delete;

		[[nodiscard]] LayerScaleViewState State() const noexcept;
		[[nodiscard]] ParallelTaskState TaskState() const;
		[[nodiscard]] LayerScaleCameraState CameraState() const;
		void SetOffset(resource::MotionPoint offset);
		void SetScale(resource::MotionPoint scale);
		void SetPrimaryScale(float scale);
		void SetRotation(float degrees);
		void SetVisible(bool visible);
		// Wrapper retirement marks both native tasks; First update or parent disposal owns their actual release.
		void Retire();
		// Parent calls this after retiring task ownership and before destroying its borrowed-view registry.
		void DetachParent() noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
