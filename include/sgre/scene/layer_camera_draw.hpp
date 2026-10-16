#pragma once

#include "sgre/scene/layer_scale_view.hpp"
#include "sgre/scene/motion_scissor.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace sgre::scene {
	class GraphicsModelStack;

	struct LayerCameraParameters {
		resource::MotionPoint Pivot;
		resource::MotionPoint Offset;
		resource::MotionPoint Scale{1, 1};
		float RotationRadians = 0;
		MotionBounds Clip{1, 1, -1, -1};
	};

	struct LayerCameraFrameState {
		MotionScissorMatrix View;
		MotionScissorViewport Viewport;
		MotionBounds RawScissor;
		MotionBounds RoundedScissor;
		MotionBounds AppliedRoundedScissor;
	};

	[[nodiscard]] MotionScissorMatrix PrepareLayerCameraMatrix(const MotionScissorMatrix& parent, const LayerCameraParameters& camera);
	[[nodiscard]] LayerCameraParameters PrepareLayerCameraParameters(const LayerScaleCameraState& state);

	class LayerCameraDrawScope {
	public:
		using ScissorSubmission = std::function<void(const MotionIntegerScissor&)>;
		using DrawAdmission = std::function<void()>;
		// Actual frame/view/viewport and physical scissor submission belong to the renderer; all models are copied.
		// Submission returns after committing scissor, or throws before commit. Partial device failure invalidates the scope.
		LayerCameraDrawScope(LayerCameraFrameState state, std::span<const MotionScissorMatrix> models, ScissorSubmission submit, std::size_t max_depth = 64);
		// The genuine graphics stack encloses this scope; child drawing observes its actual pushed model.
		// Children must balance their model operations; an unbalanced depth invalidates the containing frame.
		LayerCameraDrawScope(LayerCameraFrameState state, GraphicsModelStack& models, ScissorSubmission submit, std::size_t max_depth = 64);
		// The actual serialized graphics frame state and model owner enclose this live scope.
		// Target/projection/scissor changes remain visible to every borrower; the concrete submission service must commit or reject before returning.
		LayerCameraDrawScope(GraphicsModelStack& models, LayerCameraFrameState& actual_frame_state, ScissorSubmission submit, std::size_t max_depth = 64, DrawAdmission admit_draw = {});
		// Includes disabled Clip forwarding and restoration during exception unwinding.
		[[nodiscard]] bool IsDrawing() const noexcept;
		LayerCameraDrawScope(const LayerCameraDrawScope&) = delete;
		LayerCameraDrawScope& operator=(const LayerCameraDrawScope&) = delete;
		LayerCameraDrawScope(LayerCameraDrawScope&&) = delete;
		LayerCameraDrawScope& operator=(LayerCameraDrawScope&&) = delete;
		[[nodiscard]] const LayerCameraFrameState& State() const noexcept;
		[[nodiscard]] std::size_t Depth() const noexcept;
		[[nodiscard]] const MotionScissorMatrix& CurrentModel() const;
		// Genuine child task drawing observes composed model and temporary clip, then prior state is restored.
		void Draw(const LayerCameraParameters& camera, ParallelTaskSet& children, std::uint32_t argument);
		// Native MClip suppresses children on an empty transformed intersection; Camera Draw does not.
		// Enabled Clip requires a current model; the disabled sentinel can draw with an empty stack.
		void DrawClip(const MotionBounds& clip, ParallelTaskSet& children, std::uint32_t argument);

	private:
		void RequireCurrentState() const;
		void SetScissor(const MotionBounds& requested);
		[[nodiscard]] LayerCameraFrameState& MutableState() noexcept;
		LayerCameraFrameState m_State;
		LayerCameraFrameState* m_GraphicsFrameState = nullptr;
		std::vector<MotionScissorMatrix> m_Models;
		GraphicsModelStack* m_GraphicsModels = nullptr;
		ScissorSubmission m_Submit;
		DrawAdmission m_AdmitDraw;
		std::size_t m_DrawDepth = 0;
		std::size_t m_MaxDepth;
	};

} // namespace sgre::scene
