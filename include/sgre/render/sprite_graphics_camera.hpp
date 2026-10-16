#pragma once

#include "sgre/scene/graphics_model_stack.hpp"
#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/scene/motion_frame_submission.hpp"
#include "sgre/scene/motion_shader_program.hpp"

#include <array>
#include <memory>
#include <span>

namespace sgre::render {
	class SpriteGraphicsTargets;
	class SpritePassRenderer;

	class SpriteGraphicsCamera {
	public:
		// The genuine selected-target owner and stable model stack enclose this serialized camera state.
		SpriteGraphicsCamera(SpriteGraphicsTargets& graphics, scene::GraphicsModelStack& models);
		// The containing renderer owns this same stable state used by its live camera/clip traversal.
		SpriteGraphicsCamera(SpriteGraphicsTargets& graphics, scene::GraphicsModelStack& models, scene::LayerCameraFrameState& actual_frame_state);
		SpriteGraphicsCamera(const SpriteGraphicsCamera&) = delete;
		SpriteGraphicsCamera& operator=(const SpriteGraphicsCamera&) = delete;
		SpriteGraphicsCamera(SpriteGraphicsCamera&&) = delete;
		SpriteGraphicsCamera& operator=(SpriteGraphicsCamera&&) = delete;
		// Native root-manager reset: physical target projection followed by the default current model.
		// The separate frame-start model reset must already have created a real current slot.
		void ResetDefaultCamera();
		[[nodiscard]] scene::MotionScissorMatrix Projection() const;
		// Copies current view/model/clip values from this exact shared graphics owner; dimensions remain borrowed.
		// Ordinary unmasked submission only. Texture-size storage outlives the immediate consuming Flush.
		[[nodiscard]] scene::MotionPreparedFrame OrdinaryFrameGlobals(std::span<const resource::MotionPoint> actual_texture_sizes) const;
		// Actual manager slots may contain zero-sized holes; each sampled ordinary command must name a live positive-sized slot.
		[[nodiscard]] scene::MotionPreparedFrame SceneFrameGlobals(std::span<const resource::MotionPoint> actual_texture_sizes) const;
		// Reject before binding-record mutation if its real encoder target differs from this current projected target.
		void RequireTarget(const SpritePassRenderer& actual_target) const;
		// Admitted ordinary combined path only; explicit/identity render-record modes remain caller-owned.
		[[nodiscard]] std::array<float, 16> OrdinaryShaderRegisters() const;

	private:
		void RequireCurrentProjection() const;
		[[nodiscard]] scene::MotionPreparedFrame FrameGlobals(std::span<const resource::MotionPoint> actual_texture_sizes, bool allow_empty_slots) const;
		SpriteGraphicsTargets& m_Graphics;
		scene::GraphicsModelStack& m_Models;
		std::weak_ptr<SpritePassRenderer> m_ProjectionTarget;
		scene::MotionScissorMatrix m_Projection;
		scene::LayerCameraFrameState* m_FrameState = nullptr;
		bool m_Initialized = false;
	};
} // namespace sgre::render
