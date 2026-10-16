#include "sgre/render/sprite_graphics_camera.hpp"

#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/scene/default_render_model.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace sgre::render {
	SpriteGraphicsCamera::SpriteGraphicsCamera(SpriteGraphicsTargets& graphics, scene::GraphicsModelStack& models)
	    : m_Graphics(graphics), m_Models(models) {
	}

	SpriteGraphicsCamera::SpriteGraphicsCamera(SpriteGraphicsTargets& graphics, scene::GraphicsModelStack& models, scene::LayerCameraFrameState& actual_frame_state)
	    : m_Graphics(graphics), m_Models(models), m_FrameState(&actual_frame_state) {
	}

	void SpriteGraphicsCamera::ResetDefaultCamera() {
		const auto target = m_Graphics.CurrentTarget();
		static_cast<void>(m_Models.Current());
		const auto layout = m_Graphics.CurrentLayout();
		m_Projection = layout.Projection;
		if (m_FrameState != nullptr) {
			m_FrameState->View = layout.Projection;
		}
		m_Models.SetCurrent(scene::PrepareDefaultRenderModel());
		m_ProjectionTarget = target;
		m_Initialized = true;
	}

	void SpriteGraphicsCamera::RequireCurrentProjection() const {
		const auto target = m_Graphics.CurrentTarget();
		if (!m_Initialized || m_ProjectionTarget.lock() != target) {
			throw std::runtime_error("graphics camera requires the actual selected target's current projection reset");
		}
		static_cast<void>(m_Models.Current());
	}

	scene::MotionScissorMatrix SpriteGraphicsCamera::Projection() const {
		RequireCurrentProjection();

		return m_FrameState != nullptr ? m_FrameState->View : m_Projection;
	}

	static constexpr std::size_t kMaximumGraphicsMotionTextureSlots = 1024;
	static constexpr float kMaximumGraphicsMotionTextureDimension = 16384;

	static void RequireFiniteOrdinaryFrameBounds(const scene::MotionBounds& bounds) {
		if (!std::isfinite(bounds.MinimumX) || !std::isfinite(bounds.MinimumY) || !std::isfinite(bounds.MaximumX) || !std::isfinite(bounds.MaximumY)) {
			throw std::runtime_error("ordinary graphics frame requires finite actual scissor values");
		}
	}

	scene::MotionPreparedFrame SpriteGraphicsCamera::OrdinaryFrameGlobals(std::span<const resource::MotionPoint> actual_texture_sizes) const {
		return FrameGlobals(actual_texture_sizes, false);
	}

	scene::MotionPreparedFrame SpriteGraphicsCamera::SceneFrameGlobals(std::span<const resource::MotionPoint> actual_texture_sizes) const {
		return FrameGlobals(actual_texture_sizes, true);
	}

	scene::MotionPreparedFrame SpriteGraphicsCamera::FrameGlobals(std::span<const resource::MotionPoint> actual_texture_sizes, bool allow_empty_slots) const {
		RequireCurrentProjection();
		if (m_FrameState == nullptr) {
			throw std::runtime_error("ordinary graphics frame requires the genuine shared camera/clip owner");
		}
		const auto layout = m_Graphics.CurrentLayout();
		const auto& actual = *m_FrameState;
		if (actual.Viewport.Width <= 0 || actual.Viewport.Height <= 0 || layout.Viewport.X != 0 || layout.Viewport.Y != 0 || static_cast<float>(actual.Viewport.Width) != layout.Viewport.Width || static_cast<float>(actual.Viewport.Height) != layout.Viewport.Height) {
			throw std::runtime_error("ordinary graphics frame viewport differs from its actual selected mono target");
		}
		if (actual_texture_sizes.size() > kMaximumGraphicsMotionTextureSlots) {
			throw std::runtime_error("ordinary graphics frame texture dimensions exceed the supported source-slot bound");
		}
		for (const auto& size : actual_texture_sizes) {
			if (allow_empty_slots && size.X == 0 && size.Y == 0) {
				continue;
			}
			if (!std::isfinite(size.X) || !std::isfinite(size.Y) || size.X <= 0 || size.Y <= 0 || size.X > kMaximumGraphicsMotionTextureDimension || size.Y > kMaximumGraphicsMotionTextureDimension) {
				throw std::runtime_error("ordinary graphics frame requires actual finite supported texture dimensions");
			}
		}
		RequireFiniteOrdinaryFrameBounds(actual.RawScissor);
		RequireFiniteOrdinaryFrameBounds(actual.AppliedRoundedScissor);
		if (actual.RawScissor.MaximumX < actual.RawScissor.MinimumX || actual.RawScissor.MaximumY < actual.RawScissor.MinimumY) {
			throw std::runtime_error("ordinary graphics frame requires a valid actual initial scissor");
		}
		const auto& model = m_Models.Current();
		for (const auto value : actual.View.Coefficients) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("ordinary graphics frame requires its finite actual shared view");
			}
		}
		for (const auto value : model.Coefficients) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("ordinary graphics frame requires its finite actual current model");
			}
		}

		scene::MotionPreparedFrame frame;
		frame.TextureSizes = actual_texture_sizes;
		frame.InitialScissor = actual.RawScissor;
		frame.AppliedRoundedScissor = actual.AppliedRoundedScissor;
		frame.View = actual.View;
		frame.Model = model;
		frame.Viewport = actual.Viewport;

		return frame;
	}

	void SpriteGraphicsCamera::RequireTarget(const SpritePassRenderer& actual_target) const {
		RequireCurrentProjection();
		if (m_Graphics.CurrentTarget().get() != &actual_target) {
			throw std::runtime_error("graphics camera projection belongs to another actual render target");
		}
	}

	std::array<float, 16> SpriteGraphicsCamera::OrdinaryShaderRegisters() const {
		RequireCurrentProjection();
		scene::MotionShaderTransform transform;
		transform.Graphics.Rows = Projection().Coefficients;
		transform.Current.Rows = m_Models.Current().Coefficients;

		return scene::PrepareMotionShaderMatrix(transform);
	}
} // namespace sgre::render
