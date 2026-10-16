#include "sgre/scene/layer_camera_draw.hpp"

#include "sgre/scene/affine_trigonometry.hpp"
#include "sgre/scene/graphics_model_stack.hpp"

#include <bit>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	class ScopedLayerCameraTraversal {
	public:
		explicit ScopedLayerCameraTraversal(std::size_t& actual_depth) : m_Depth(actual_depth) {
			if (m_Depth == 4096) {
				throw std::runtime_error("Layer camera traversal depth exceeds its bounded containing scope");
			}

			++m_Depth;
		}
		~ScopedLayerCameraTraversal() noexcept {
			--m_Depth;
		}
		ScopedLayerCameraTraversal(const ScopedLayerCameraTraversal&) = delete;
		ScopedLayerCameraTraversal& operator=(const ScopedLayerCameraTraversal&) = delete;

	private:
		std::size_t& m_Depth;
	};

	static void RequireFiniteCameraMatrix(const MotionScissorMatrix& matrix) {
		for (const auto value : matrix.Coefficients) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Layer camera matrix requires recovered finite coefficients");
			}
		}
	}

	static void RequireFiniteCameraBounds(const MotionBounds& bounds) {
		for (const auto value : {bounds.MinimumX, bounds.MinimumY, bounds.MaximumX, bounds.MaximumY}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Layer camera raw scissor requires finite coordinates");
			}
		}
	}

	static float NegateCameraFloat(float value) noexcept {
		return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
	}

	static MotionScissorMatrix MultiplyCameraMatrices(const MotionScissorMatrix& left, const MotionScissorMatrix& right) {
		MotionScissorMatrix result;
		for (std::size_t row = 0; row != 3; ++row) {
			for (std::size_t column = 0; column != 4; ++column) {
				const auto offset = row * 4;
				const auto pair = left.Coefficients[offset + 1] * right.Coefficients[column + 4] + left.Coefficients[offset] * right.Coefficients[column];
				const auto third = pair + left.Coefficients[offset + 2] * right.Coefficients[column + 8];
				result.Coefficients[offset + column] = column == 3 ? third + left.Coefficients[offset + 3] : third;
			}
		}

		return result;
	}

	static void TranslateCameraMatrix(MotionScissorMatrix& matrix, resource::MotionPoint point) {
		matrix.Coefficients[3] = point.X + matrix.Coefficients[3];
		matrix.Coefficients[7] = point.Y + matrix.Coefficients[7];
		matrix.Coefficients[11] = 0.0f + matrix.Coefficients[11];
	}

	MotionScissorMatrix PrepareLayerCameraMatrix(const MotionScissorMatrix& parent, const LayerCameraParameters& camera) {
		RequireFiniteCameraMatrix(parent);
		for (const auto value : {camera.Pivot.X, camera.Pivot.Y, camera.Offset.X, camera.Offset.Y, camera.Scale.X, camera.Scale.Y}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Layer camera transform requires finite pivot, offset and scale");
			}
		}

		const auto trigonometry = PrepareAffineTrigonometry(camera.RotationRadians);
		MotionScissorMatrix local;
		TranslateCameraMatrix(local, {NegateCameraFloat(camera.Pivot.X), NegateCameraFloat(camera.Pivot.Y)});
		for (std::size_t column = 0; column != 4; ++column) {
			local.Coefficients[column] *= camera.Scale.X;
			local.Coefficients[column + 4] *= camera.Scale.Y;
			local.Coefficients[column + 8] *= 1.0f;
		}

		MotionScissorMatrix rotation;
		rotation.Coefficients[0] = trigonometry.Cosine;
		rotation.Coefficients[1] = NegateCameraFloat(trigonometry.Sine);
		rotation.Coefficients[4] = trigonometry.Sine;
		rotation.Coefficients[5] = trigonometry.Cosine;
		local = MultiplyCameraMatrices(rotation, local);
		TranslateCameraMatrix(local, camera.Pivot);
		TranslateCameraMatrix(local, {NegateCameraFloat(camera.Offset.X), NegateCameraFloat(camera.Offset.Y)});
		auto result = MultiplyCameraMatrices(parent, local);
		RequireFiniteCameraMatrix(result);

		return result;
	}

	LayerCameraParameters PrepareLayerCameraParameters(const LayerScaleCameraState& state) {
		LayerCameraParameters camera;
		camera.Offset = state.Offset;
		camera.Scale = state.Scale;
		camera.RotationRadians = state.RotationRadians;

		return camera;
	}

	LayerCameraDrawScope::LayerCameraDrawScope(LayerCameraFrameState state, std::span<const MotionScissorMatrix> models, ScissorSubmission submit, std::size_t max_depth)
	    : m_State(state), m_Submit(std::move(submit)), m_MaxDepth(max_depth) {
		if (max_depth == 0 || max_depth > 4096 || models.size() > max_depth || state.Viewport.Width <= 0 || state.Viewport.Height <= 0) {
			throw std::runtime_error("Layer camera scope requires positive bounded depth and actual viewport dimensions");
		}

		RequireFiniteCameraMatrix(state.View);
		RequireFiniteCameraBounds(state.RawScissor);
		m_Models.reserve(max_depth);
		for (const auto& model : models) {
			RequireFiniteCameraMatrix(model);
			m_Models.push_back(model);
		}
	}

	LayerCameraDrawScope::LayerCameraDrawScope(LayerCameraFrameState state, GraphicsModelStack& models, ScissorSubmission submit, std::size_t max_depth)
	    : m_State(state), m_GraphicsModels(&models), m_Submit(std::move(submit)), m_MaxDepth(max_depth) {
		if (max_depth == 0 || max_depth > 4096 || models.Depth() > max_depth || state.Viewport.Width <= 0 || state.Viewport.Height <= 0) {
			throw std::runtime_error("Layer camera requires a bounded actual graphics model stack depth");
		}
		RequireFiniteCameraMatrix(state.View);
		RequireFiniteCameraBounds(state.RawScissor);
		if (models.Depth() != 0) {
			RequireFiniteCameraMatrix(models.Current());
		}
	}

	LayerCameraDrawScope::LayerCameraDrawScope(GraphicsModelStack& models, LayerCameraFrameState& actual_frame_state, ScissorSubmission submit, std::size_t max_depth, DrawAdmission admit_draw)
	    : LayerCameraDrawScope(actual_frame_state, models, std::move(submit), max_depth) {
		m_GraphicsFrameState = &actual_frame_state;
		m_AdmitDraw = std::move(admit_draw);
	}

	bool LayerCameraDrawScope::IsDrawing() const noexcept {
		return m_DrawDepth != 0;
	}

	LayerCameraFrameState& LayerCameraDrawScope::MutableState() noexcept {
		return m_GraphicsFrameState == nullptr ? m_State : *m_GraphicsFrameState;
	}

	const LayerCameraFrameState& LayerCameraDrawScope::State() const noexcept {
		return m_GraphicsFrameState == nullptr ? m_State : *m_GraphicsFrameState;
	}

	std::size_t LayerCameraDrawScope::Depth() const noexcept {
		return m_GraphicsModels == nullptr ? m_Models.size() : m_GraphicsModels->Depth();
	}

	const MotionScissorMatrix& LayerCameraDrawScope::CurrentModel() const {
		if (m_GraphicsModels != nullptr) {
			return m_GraphicsModels->Current();
		}
		if (m_Models.empty()) {
			throw std::runtime_error("Layer camera draw has no current model stack entry");
		}

		return m_Models.back();
	}

	void LayerCameraDrawScope::RequireCurrentState() const {
		const auto& state = State();
		RequireFiniteCameraMatrix(state.View);
		RequireFiniteCameraBounds(state.RawScissor);
		if (state.Viewport.Width <= 0 || state.Viewport.Height <= 0) {
			throw std::runtime_error("Layer camera requires its actual positive physical viewport");
		}
	}

	void LayerCameraDrawScope::SetScissor(const MotionBounds& requested) {
		auto& state = MutableState();
		const auto update = PrepareMotionScissorUpdate(requested, state.AppliedRoundedScissor);
		state.RawScissor = update.Raw;
		state.RoundedScissor = update.Rounded;
		if (update.Submission) {
			if (!m_Submit) {
				throw std::runtime_error("Layer camera scissor change requires its real renderer submission service");
			}

			m_Submit(*update.Submission);
			state.AppliedRoundedScissor = update.Rounded;
		}
	}

	void LayerCameraDrawScope::DrawClip(const MotionBounds& clip, ParallelTaskSet& children, std::uint32_t argument) {
		if (m_AdmitDraw) {
			m_AdmitDraw();
		}

		ScopedLayerCameraTraversal traversal(m_DrawDepth);
		RequireCurrentState();
		RequireFiniteCameraBounds(clip);
		if (clip.MaximumX < clip.MinimumX || clip.MaximumY < clip.MinimumY) {
			children.Draw(argument);

			return;
		}

		const auto saved_scissor = State().RawScissor;
		// Native Clip consumes the current model at end - 48; an enabled clip requires that entry.
		auto clipped = TransformMotionScissorBounds(clip, State().View, CurrentModel(), State().Viewport);
		clipped.MinimumX = clipped.MinimumX > saved_scissor.MinimumX ? clipped.MinimumX : saved_scissor.MinimumX;
		clipped.MinimumY = clipped.MinimumY > saved_scissor.MinimumY ? clipped.MinimumY : saved_scissor.MinimumY;
		clipped.MaximumX = clipped.MaximumX < saved_scissor.MaximumX ? clipped.MaximumX : saved_scissor.MaximumX;
		clipped.MaximumY = clipped.MaximumY < saved_scissor.MaximumY ? clipped.MaximumY : saved_scissor.MaximumY;
		if (clipped.MaximumX < clipped.MinimumX || clipped.MaximumY < clipped.MinimumY) {
			return;
		}

		std::exception_ptr failure;
		try {
			SetScissor(clipped);
			children.Draw(argument);
		} catch (...) {
			failure = std::current_exception();
		}

		try {
			SetScissor(saved_scissor);
		} catch (...) {
			if (!failure) {
				failure = std::current_exception();
			}
		}

		if (failure) {
			std::rethrow_exception(failure);
		}
	}

	void LayerCameraDrawScope::Draw(const LayerCameraParameters& camera, ParallelTaskSet& children, std::uint32_t argument) {
		if (m_AdmitDraw) {
			m_AdmitDraw();
		}

		ScopedLayerCameraTraversal traversal(m_DrawDepth);
		RequireCurrentState();
		if (Depth() >= m_MaxDepth) {
			throw std::runtime_error("Layer camera model stack capacity is exhausted before child drawing");
		}

		const auto saved_depth = Depth();
		const auto model = PrepareLayerCameraMatrix(saved_depth == 0 ? MotionScissorMatrix{} : CurrentModel(), camera);
		if (m_GraphicsModels == nullptr) {
			m_Models.push_back(model);
		} else {
			m_GraphicsModels->Push();
			m_GraphicsModels->SetCurrent(model);
		}
		const auto saved_scissor = State().RawScissor;
		bool restore_scissor = false;
		std::exception_ptr failure;
		try {
			if (camera.Clip.MaximumX >= camera.Clip.MinimumX && camera.Clip.MaximumY >= camera.Clip.MinimumY) {
				auto clipped = TransformMotionScissorBounds(camera.Clip, State().View, CurrentModel(), State().Viewport);
				clipped.MinimumX = clipped.MinimumX > saved_scissor.MinimumX ? clipped.MinimumX : saved_scissor.MinimumX;
				clipped.MinimumY = clipped.MinimumY > saved_scissor.MinimumY ? clipped.MinimumY : saved_scissor.MinimumY;
				clipped.MaximumX = clipped.MaximumX < saved_scissor.MaximumX ? clipped.MaximumX : saved_scissor.MaximumX;
				clipped.MaximumY = clipped.MaximumY < saved_scissor.MaximumY ? clipped.MaximumY : saved_scissor.MaximumY;
				if (clipped.MaximumX >= clipped.MinimumX && clipped.MaximumY >= clipped.MinimumY) {
					restore_scissor = true;
					SetScissor(clipped);
				}
			}

			children.Draw(argument);
		} catch (...) {
			failure = std::current_exception();
		}

		if (restore_scissor) {
			try {
				SetScissor(saved_scissor);
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}
		}

		if (m_GraphicsModels == nullptr) {
			m_Models.pop_back();
		} else {
			if (m_GraphicsModels->Depth() != saved_depth + 1) {
				throw std::runtime_error("Layer camera child changed its actual graphics model stack depth; containing frame is invalid");
			}
			m_GraphicsModels->Pop();
		}
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

} // namespace sgre::scene
