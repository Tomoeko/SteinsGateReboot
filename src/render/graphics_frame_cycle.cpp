#include "sgre/render/graphics_frame_cycle.hpp"

#include "sgre/scene/motion_scene_draw_context.hpp"
#include "sgre/scene/screens.hpp"
#include "sgre/scene/sprite_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace sgre::render {

	static scene::LayerCameraFrameState PrepareCycleStorage(const SpritePassRenderer& actual_primary) {
		const auto description = actual_primary.Description();
		const auto layout = scene::PrepareRenderTargetLayout(description.Width, description.Height, 1, 0);
		scene::LayerCameraFrameState state;
		state.View = layout.Projection;
		state.Viewport.Width = static_cast<std::int32_t>(layout.Viewport.Width);
		state.Viewport.Height = static_cast<std::int32_t>(layout.Viewport.Height);
		state.RawScissor = layout.FullScissor;
		state.RoundedScissor = layout.FullScissor;
		// Constructor storage is private; actual device admission is verified before drawing exposes this state.
		state.AppliedRoundedScissor = layout.FullScissor;

		return state;
	}

	GraphicsFrameCycle::GraphicsFrameCycle(SpriteGraphicsTargets& actual_graphics, SpritePassPresentation& actual_surface, core::ThreadTaskFrame& actual_workers, scene::TextureCache& actual_textures, RenderFrameCounters actual_current, RenderFrameCounters actual_previous, GraphicsFrameCycleLimits limits)
	    : m_Graphics(actual_graphics), m_Textures(actual_textures), m_Primary(actual_graphics.PrimaryTarget()), m_Models(limits.ModelDepth), m_FrameState(PrepareCycleStorage(*m_Primary)), m_Camera(actual_graphics, m_Models, m_FrameState), m_Statistics(actual_current, actual_previous), m_Records(*m_Primary, m_Camera, m_Statistics), m_Pool(actual_surface, actual_workers, m_Records, limits.Pool), m_Receiver(m_Records, *m_Primary, actual_graphics, actual_textures, m_Pool, limits.TextureSlots), m_BitmapReceiver(m_Records, *m_Primary, actual_textures, m_Pool), m_ProjectionReceiver(m_Records, *m_Primary, m_Pool), m_SceneTextureSizes(m_Primary->Description().MaxDraws), m_MaximumTextureSlots(limits.TextureSlots), m_CameraScope(PrepareCameraScope(limits.CameraDepth)) {
		m_SceneContexts.reserve(m_SceneTextureSizes.size());
		m_CaptureTasks.reserve(m_Primary->Description().MaxDraws);
	}

	scene::LayerCameraDrawScope GraphicsFrameCycle::PrepareCameraScope(std::size_t max_depth) {
		auto submit = [this](const scene::MotionIntegerScissor& rectangle) {
			m_Records.SetScissor(rectangle);
		};
		auto admit = [this] {
			RequireDrawing();
		};

		return scene::LayerCameraDrawScope(m_Models, m_FrameState, std::move(submit), max_depth, std::move(admit));
	}

	GraphicsFrameCycle::~GraphicsFrameCycle() noexcept {
		try {
			Close();
		} catch (...) {
			std::terminate();
		}
	}

	void GraphicsFrameCycle::RequireThread() const {
		if (std::this_thread::get_id() != m_Thread || m_Phase == Phase::Closed) {
			throw std::runtime_error("graphics cycle requires its actual live serialized owner thread");
		}
	}

	void GraphicsFrameCycle::RequireIdle() const {
		RequireThread();
		if (m_Phase != Phase::Idle || m_PoolFrameOpen || m_Records.IsActive() || m_CameraScope.IsDrawing() || m_Models.Depth() > 1 || m_ScreenDepth != 0) {
			throw std::runtime_error("graphics cycle preparation requires its actual idle CPU and encoder phases");
		}
	}

	void GraphicsFrameCycle::RequireDrawing() const {
		RequireThread();
		if (m_Phase != Phase::Drawing || !m_Records.IsActive() || m_Graphics.CurrentTarget().get() != &m_Records.CurrentRenderTarget()) {
			throw std::runtime_error("graphics cycle drawing requires its actual active primary encoder");
		}
	}

	void GraphicsFrameCycle::RequireRootTraversalComplete() const {
		if (m_CameraScope.IsDrawing() || m_Models.Depth() != m_ScreenDepth + 1) {
			throw std::runtime_error("graphics cycle root phase requires completed actual camera/clip traversal and balanced models");
		}
	}

	void GraphicsFrameCycle::BindTexture(std::uint32_t actual_slot, scene::CachedTextureHandle actual_texture) {
		RequireIdle();
		m_Receiver.BindTexture(actual_slot, actual_texture);
	}

	void GraphicsFrameCycle::UnbindTexture(std::uint32_t actual_slot) {
		RequireIdle();
		m_Receiver.UnbindTexture(actual_slot);
	}

	FrameHostWriterBinding GraphicsFrameCycle::BindWriter(core::ThreadTaskReceiver& actual_writer) {
		RequireIdle();

		return FrameHostWriterBinding(m_Pool, actual_writer);
	}

	void GraphicsFrameCycle::PreFrame() {
		RequireIdle();
		m_LastSubmission.reset();
		try {
			m_Records.ResetFrameState();
			m_Pool.Begin();
			m_PoolFrameOpen = true;
			static_cast<void>(m_Graphics.SelectTarget({}));
			static_cast<void>(m_Graphics.SelectTarget(m_Primary));
			m_Models.Reset();
			m_Camera.ResetDefaultCamera();
			const auto layout = m_Graphics.CurrentLayout();
			m_FrameState.Viewport.Width = static_cast<std::int32_t>(layout.Viewport.Width);
			m_FrameState.Viewport.Height = static_cast<std::int32_t>(layout.Viewport.Height);
			m_FrameState.RawScissor = layout.FullScissor;
			m_FrameState.RoundedScissor = layout.FullScissor;
			m_Phase = Phase::Prepared;
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::BeginDrawing() {
		RequireThread();
		if (m_Phase != Phase::Prepared || !m_PoolFrameOpen || m_Graphics.CurrentTarget() != m_Primary || m_Models.Depth() != 1) {
			throw std::runtime_error("graphics cycle root Begin requires its actual prepared pool/primary/default model");
		}
		RequireRootTraversalComplete();
		try {
			m_Pool.RequireCompletedWriters();
			// Native Display constructor clear words are RGB0/alpha255; ordinary Pre clear is enabled.
			const auto clear = scene::PrepareRenderTargetClearColor({0, 0, 0, 255});
			m_Records.BeginPreparedFrame(clear, SpritePassLoadAction::Clear);
			const auto actual_scissor = m_Records.SubmittedDeviceScissor();
			const auto layout = m_Graphics.CurrentLayout();
			if (actual_scissor.MinimumX != 0 || actual_scissor.MinimumY != 0 || actual_scissor.MaximumX != m_FrameState.Viewport.Width || actual_scissor.MaximumY != m_FrameState.Viewport.Height) {
				throw std::runtime_error("graphics cycle actual encoder did not establish its full primary scissor");
			}
			m_FrameState.AppliedRoundedScissor = layout.FullScissor;
			m_Phase = Phase::Drawing;
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::ResetRootCamera() {
		RequireDrawing();
		RequireRootTraversalComplete();
		m_Camera.ResetDefaultCamera();
	}

	scene::LayerCameraDrawScope& GraphicsFrameCycle::CameraScope() {
		RequireThread();

		return m_CameraScope;
	}

	const scene::LayerCameraFrameState& GraphicsFrameCycle::FrameState() const {
		RequireDrawing();

		return m_FrameState;
	}

	void GraphicsFrameCycle::Draw(scene::MotionSpriteDrawContext& actual_context, const scene::MotionFrameScratch& actual_scratch) {
		RequireDrawing();
		try {
			m_Pool.RequireCompletedWriters();
			m_Receiver.Draw(actual_context, actual_scratch);
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	scene::MutableTextureBackend& GraphicsFrameCycle::RawTextureBackend() {
		RequireIdle();

		if (!m_RawReceiver) {
			m_RawReceiver = std::make_unique<RawTextureRecordReceiver>(m_Records, *m_Primary);
		}

		return m_RawReceiver->Backend();
	}

	void GraphicsFrameCycle::DrawRawTexture(scene::RawTextureTask& actual_task, const scene::RawTextureDrawPacket& actual_packet) {
		RequireDrawing();
		try {
			m_Pool.RequireCompletedWriters();
			if (!m_RawReceiver) {
				throw std::runtime_error("RawTex drawing requires its actual prepared backend and ordinary pipelines");
			}
			m_RawReceiver->Draw(actual_task, actual_packet);
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::ResetSelectedTargetState() {
		m_Camera.ResetDefaultCamera();
		const auto layout = m_Graphics.CurrentLayout();
		m_FrameState.Viewport.Width = static_cast<std::int32_t>(layout.Viewport.Width);
		m_FrameState.Viewport.Height = static_cast<std::int32_t>(layout.Viewport.Height);
		m_FrameState.RawScissor = layout.FullScissor;
		m_FrameState.RoundedScissor = layout.FullScissor;
		m_FrameState.AppliedRoundedScissor = layout.FullScissor;
	}

	void GraphicsFrameCycle::RestoreRawScissor(const scene::MotionBounds& saved) {
		const auto update = scene::PrepareMotionScissorUpdate(saved, m_FrameState.AppliedRoundedScissor);
		m_FrameState.RawScissor = update.Raw;
		m_FrameState.RoundedScissor = update.Rounded;
		if (update.Submission) {
			m_Records.SetScissor(*update.Submission);
			m_FrameState.AppliedRoundedScissor = update.Rounded;
		}
	}

	void GraphicsFrameCycle::DrawScreen(SpritePassRenderer& actual_target, scene::LayerManagerFrame& actual_manager, const scene::ScreenState& state) {
		RequireDrawing();
		RequireRootTraversalComplete();
		m_Pool.RequireCompletedWriters();
		const auto target = m_Graphics.RequireRegisteredTarget(actual_target);
		const auto description = target->Description();
		if (description.Width != state.Description.Width || description.Height != state.Description.Height) {
			throw std::runtime_error("Screen draw target extent differs from its actual registered task payload");
		}
		const auto previous = m_Graphics.CurrentTarget();
		const auto saved_scissor = m_FrameState.RawScissor;
		const auto saved_depth = m_Models.Depth();
		m_Models.Push();
		++m_ScreenDepth;
		std::exception_ptr failure;
		try {
			m_Records.SelectRenderTarget(m_Graphics, target, scene::PrepareRenderTargetClearColor(state.BackgroundColor));
			ResetSelectedTargetState();
			actual_manager.Draw(0);
			RequireRootTraversalComplete();
			m_Records.SelectRenderTarget(m_Graphics, previous, std::nullopt);
			ResetSelectedTargetState();
			RestoreRawScissor(saved_scissor);
		} catch (...) {
			failure = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			try {
				static_cast<void>(m_Graphics.SelectTarget(previous));
				ResetSelectedTargetState();
				m_FrameState.RawScissor = saved_scissor;
				const auto restored = scene::PrepareMotionScissorUpdate(saved_scissor, m_FrameState.AppliedRoundedScissor);
				m_FrameState.RoundedScissor = restored.Rounded;
			} catch (...) {
			}
		}
		while (m_Models.Depth() > saved_depth) {
			m_Models.Pop();
		}
		--m_ScreenDepth;
		if (failure) {
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(failure);
		}
	}

	scene::MutableTextureBackend& GraphicsFrameCycle::CaptureBackend() {
		RequireIdle();
		if (!m_CaptureBackend) {
			m_CaptureBackend = m_Primary->CreateMutableTextureBackend();
		}

		return *m_CaptureBackend;
	}

	void GraphicsFrameCycle::DrawCapture(const std::shared_ptr<scene::CaptureTask>& actual_task) {
		RequireDrawing();
		try {
			m_Pool.RequireCompletedWriters();
			if (!actual_task || !m_CaptureBackend) {
				throw std::runtime_error("Capture drawing requires its actual task and idle-created backend");
			}
			if (std::find(m_CaptureTasks.begin(), m_CaptureTasks.end(), actual_task) == m_CaptureTasks.end()) {
				if (m_CaptureTasks.size() == m_Primary->Description().MaxDraws) {
					throw std::runtime_error("Capture frame task retention exceeds its actual prepared capacity");
				}
				// Retain before any allocation/copy so Abort also closes an exceptional first attempt.
				m_CaptureTasks.push_back(actual_task);
			}
			scene::CaptureFrameContext frame;
			frame.View = m_FrameState.View;
			frame.Model = m_Models.Current();
			frame.Viewport = m_FrameState.Viewport;
			frame.OriginalFormat = 28;
			// This graphics owner admits registered mono physical targets, whose actual layout is type1/eye0.
			frame.NativeTargetType = 1;
			frame.NativeEye = 0;
			actual_task->Draw(
			    *m_CaptureBackend, frame, [this](const auto& spec) {
				    return m_Records.PrepareCaptureTexture(*m_CaptureBackend, spec);
			    },
			    [this](auto& owner, const auto& bounds) {
				    m_Records.CopyCaptureCurrentTarget(owner, bounds);
			    });
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::PrepareCaptured() {
		RequireIdle();
		if (!m_CapturedReceiver) {
			m_CapturedReceiver = std::make_unique<CapturedTextureRecordReceiver>(m_Records, *m_Primary, CaptureBackend());
		}
	}

	void GraphicsFrameCycle::DrawCaptured(scene::MutableTextureOwner& actual_capture_texture, const scene::RawTextureDrawPacket& packet) {
		RequireDrawing();
		try {
			m_Pool.RequireCompletedWriters();
			if (!m_CapturedReceiver) {
				throw std::runtime_error("Captured drawing requires its actual idle-prepared frame receiver");
			}
			m_CapturedReceiver->Draw(actual_capture_texture, packet);
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::DrawCapturedOpacityMap(scene::MutableTextureOwner& actual_capture_texture, scene::MotionTextureWrapper::Borrowed actual_map, const scene::CapturedOpacityMapDrawPacket& packet) {
		RequireDrawing();
		try {
			m_Pool.RequireCompletedWriters();
			if (!m_CapturedReceiver) {
				throw std::runtime_error("Captured opacity map requires its actual idle-prepared frame receiver");
			}
			m_CapturedReceiver->DrawOpacityMap(actual_capture_texture, m_Textures, actual_map, packet);
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::ReleaseCaptureTasks(bool submitted) {
		std::exception_ptr first;
		for (const auto& task : m_CaptureTasks) {
			try {
				task->ReleaseFrame(submitted);
			} catch (...) {
				if (!first) {
					first = std::current_exception();
				}
			}
		}
		m_CaptureTasks.clear();
		if (first) {
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::PrepareFillRectangle() {
		RequireIdle();
		if (!m_FillRectanglePipeline) {
			scene::MotionSpriteMaterial material;
			material.BlendMode = 0;
			auto pipeline = scene::SelectSpritePipeline(material, {});
			pipeline.NativeVertexShaderIndex = 3;
			pipeline.NativePixelShaderIndex = 60;
			pipeline.VertexColor = scene::SpriteVertexColor::Multiply;
			m_FillRectanglePipeline = m_Primary->PreparePipeline(pipeline);
		}
	}

	void GraphicsFrameCycle::DrawFillRectangle(const scene::FillRectangleDrawPacket& packet) {
		RequireDrawing();
		try {
			m_Pool.RequireCompletedWriters();
			if (!m_FillRectanglePipeline) {
				throw std::runtime_error("FillRect drawing requires its actual idle-prepared shader pipeline");
			}
			m_Records.BeginRecord();
			m_Records.SetBlend(0, 0);
			m_Records.AppendFillRectangle(*m_FillRectanglePipeline, packet);
			m_Records.EndRecord();
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::DrawScreenProjection(SpriteTextureHandle actual_screen_texture, const scene::ScreenProjectionPacket& packet) {
		RequireDrawing();
		try {
			m_Pool.RequireCompletedWriters();
			m_ProjectionReceiver.Draw(actual_screen_texture, packet);
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::PreparePictures() {
		RequireIdle();
		if (!m_PictureReceiver) {
			m_PictureReceiver = std::make_unique<PictureRecordReceiver>(m_Records, *m_Primary, m_Textures);
		}
	}

	void GraphicsFrameCycle::DrawPicture(const scene::PictureDrawFrame& frame) {
		RequireDrawing();
		try {
			m_Pool.RequireCompletedWriters();
			if (!m_PictureReceiver) {
				throw std::runtime_error("Picture drawing requires its actual idle-prepared frame receiver");
			}
			m_PictureReceiver->Draw(frame);
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::PrepareMovies() {
		RequireIdle();
		if (!m_MovieReceiver) {
			m_MovieReceiver = std::make_unique<MovieRecordReceiver>(m_Records, *m_Primary);
		}
	}

	void GraphicsFrameCycle::DrawMovie(const media::MovieDrawFrame& frame) {
		RequireDrawing();
		try {
			m_Pool.RequireCompletedWriters();
			if (!m_MovieReceiver) {
				throw std::runtime_error("movie drawing requires its actual idle-prepared frame receiver");
			}
			m_MovieReceiver->Draw(frame);
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	bool GraphicsFrameCycle::CanRetireMovie(const void* owner) const {
		RequireThread();

		return !m_MovieReceiver || m_MovieReceiver->CanRetire(owner);
	}

	void GraphicsFrameCycle::RetireMovie(const void* owner) {
		RequireThread();
		if (m_MovieReceiver) {
			m_MovieReceiver->Retire(owner);
		}
	}

	void GraphicsFrameCycle::PrepareTextGlyphs() {
		RequireIdle();
		if (!m_TextGlyphReceiver) {
			m_TextGlyphReceiver = std::make_unique<TextGlyphRecordReceiver>(m_Records, *m_Primary, m_Pool);
		}
	}

	void GraphicsFrameCycle::DrawTextGlyphs(text::TextGlyphAtlasOwner& actual_atlas, std::span<const text::VectorGlyphDrawRectangle> rectangles, const text::TextBounds& clip) {
		RequireDrawing();
		const auto saved_scissor = m_FrameState.RawScissor;
		try {
			for (const auto value : {clip.Left, clip.Top, clip.Right, clip.Bottom}) {
				if (!std::isfinite(value)) {
					throw std::runtime_error("TextRender glyph clip requires the recovered finite rectangle domain");
				}
			}
			m_Pool.RequireCompletedWriters();
			if (!m_TextGlyphReceiver) {
				throw std::runtime_error("TextRender glyph receiving requires its actual idle-prepared frame receiver");
			}
			if (clip.Right >= clip.Left && clip.Bottom >= clip.Top) {
				const scene::MotionBounds bounds{clip.Left, clip.Top, clip.Right, clip.Bottom};
				auto clipped = scene::TransformMotionScissorBounds(bounds, m_FrameState.View, m_Models.Current(), m_FrameState.Viewport);
				clipped.MinimumX = std::max(clipped.MinimumX, saved_scissor.MinimumX);
				clipped.MinimumY = std::max(clipped.MinimumY, saved_scissor.MinimumY);
				clipped.MaximumX = std::min(clipped.MaximumX, saved_scissor.MaximumX);
				clipped.MaximumY = std::min(clipped.MaximumY, saved_scissor.MaximumY);
				if (clipped.MaximumX < clipped.MinimumX || clipped.MaximumY < clipped.MinimumY) {
					return;
				}
				RestoreRawScissor(clipped);
			}
			m_TextGlyphReceiver->Draw(actual_atlas, rectangles);
			RestoreRawScissor(saved_scissor);
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::DrawBitmapConsole(const scene::BitmapConsole& console, const scene::IndicatorPlacement& placement, const scene::BitmapFontTextureOwner& font, std::uint32_t argument) {
		RequireDrawing();
		try {
			m_Pool.RequireCompletedWriters();
			m_BitmapReceiver.Draw(console, placement, font, argument);
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::DrawScene(scene::MotionSceneDrawContext& actual_context, std::span<const scene::CachedTextureHandle> actual_local_slots, const scene::MotionFrameScratch& scratch) {
		RequireDrawing();
		if (actual_context.Size() == 0) {
			return;
		}
		if (actual_local_slots.size() > m_MaximumTextureSlots || m_SceneContexts.size() == m_SceneTextureSizes.size() || std::find(m_SceneContexts.begin(), m_SceneContexts.end(), &actual_context) != m_SceneContexts.end()) {
			throw std::runtime_error("graphics scene draw requires bounded distinct actual source-manager occurrences");
		}

		const auto occurrence = m_SceneContexts.size();
		m_SceneContexts.push_back(&actual_context);
		try {
			m_Pool.RequireCompletedWriters();
			auto& sizes = m_SceneTextureSizes[occurrence];
			sizes.resize(actual_local_slots.size());
			for (std::size_t index = 0; index < actual_local_slots.size(); ++index) {
				if (actual_local_slots[index] == scene::CachedTextureHandle{}) {
					sizes[index] = {};
					continue;
				}
				// Partial/stale/foreign real handles remain errors even when this occurrence does not sample them.
				const auto texture = m_Textures.Get(actual_local_slots[index]).Description;
				sizes[index].X = static_cast<float>(texture.Width);
				sizes[index].Y = static_cast<float>(texture.Height);
			}

			const auto globals = m_Records.SceneFrameGlobals(sizes);
			const auto prepared = actual_context.Prepare(globals);
			m_Receiver.DrawActualFrame(prepared, actual_local_slots, scratch);
			// Native512443 consumes this manager queue after synchronous encoding, before the outer End.
			actual_context.Clear();
			// Keep the bounded occurrence slot; copied inputs and GPU owners still retire at End/Abort.
			m_SceneContexts[occurrence] = nullptr;
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::ReleaseSceneContexts() {
		std::exception_ptr first;
		for (auto* context : m_SceneContexts) {
			if (context == nullptr) {
				continue;
			}
			try {
				context->Clear();
			} catch (...) {
				if (!first) {
					first = std::current_exception();
				}
			}
		}
		if (first) {
			std::rethrow_exception(first);
		}
		m_SceneContexts.clear();
	}

	void GraphicsFrameCycle::PostFrame() {
		RequireThread();
		if (!m_PoolFrameOpen || m_ScreenDepth != 0 || (m_Phase != Phase::Prepared && m_Phase != Phase::Drawing)) {
			throw std::runtime_error("graphics cycle Post requires its actual prepared or drawing CPU frame");
		}
		RequireRootTraversalComplete();
		try {
			m_Pool.RequireCompletedWriters();
			if (m_Phase == Phase::Prepared) {
				// Native Pre clears even when root Draw is paused. Commit the real clear without a root callback.
				BeginDrawing();
			}
			SpritePresentationSubmission submission;
			submission.Owner = m_Primary.get();
			submission.Identity = m_Records.EndFrame();
			ReleaseCaptureTasks(true);
			m_Receiver.ReleaseFrameInputs();
			m_BitmapReceiver.ReleaseFrameInputs();
			if (m_TextGlyphReceiver) {
				m_TextGlyphReceiver->ReleaseFrameInputs();
			}
			m_ProjectionReceiver.ReleaseFrameInputs();
			ReleaseSceneContexts();
			m_Pool.End();
			m_PoolFrameOpen = false;
			m_Phase = Phase::Idle;
			m_LastSubmission = submission;
		} catch (...) {
			const auto first = std::current_exception();
			try {
				AbortFrame();
			} catch (...) {
			}
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::AbortFrame() {
		RequireThread();
		m_Phase = Phase::Aborting;
		std::exception_ptr first;
		try {
			m_Records.AbortFrame();
		} catch (...) {
			first = std::current_exception();
		}
		try {
			ReleaseCaptureTasks(false);
		} catch (...) {
			if (!first) {
				first = std::current_exception();
			}
		}

		try {
			m_Receiver.ReleaseFrameInputs();
		} catch (...) {
			if (!first) {
				first = std::current_exception();
			}
		}
		try {
			m_BitmapReceiver.ReleaseFrameInputs();
		} catch (...) {
			if (!first) {
				first = std::current_exception();
			}
		}
		try {
			if (m_TextGlyphReceiver) {
				m_TextGlyphReceiver->ReleaseFrameInputs();
			}
		} catch (...) {
			if (!first) {
				first = std::current_exception();
			}
		}
		try {
			m_ProjectionReceiver.ReleaseFrameInputs();
		} catch (...) {
			if (!first) {
				first = std::current_exception();
			}
		}
		try {
			ReleaseSceneContexts();
		} catch (...) {
			if (!first) {
				first = std::current_exception();
			}
		}
		if (m_PoolFrameOpen) {
			try {
				m_Pool.End();
				m_PoolFrameOpen = false;
			} catch (...) {
				if (!first) {
					first = std::current_exception();
				}
			}
		}
		m_LastSubmission.reset();
		if (!m_PoolFrameOpen && !m_CameraScope.IsDrawing() && m_Models.Depth() <= 1) {
			m_Phase = Phase::Idle;
		}
		if (first) {
			std::rethrow_exception(first);
		}
	}

	void GraphicsFrameCycle::Close() {
		if (m_Phase == Phase::Closed) {
			return;
		}
		RequireIdle();
		if (m_MovieReceiver && !m_MovieReceiver->Empty()) {
			throw std::runtime_error("graphics close requires actual movie task texture retirement");
		}
		m_Receiver.ReleaseFrameInputs();
		m_BitmapReceiver.ReleaseFrameInputs();
		if (m_TextGlyphReceiver) {
			m_TextGlyphReceiver->ReleaseFrameInputs();
		}
		m_ProjectionReceiver.ReleaseFrameInputs();
		ReleaseSceneContexts();
		m_Pool.Close();
		m_Phase = Phase::Closed;
	}

	std::optional<SpritePresentationSubmission> GraphicsFrameCycle::LastSubmission() const {
		RequireThread();

		return m_LastSubmission;
	}

	RenderFrameCounters GraphicsFrameCycle::CurrentCounters() const {
		RequireThread();

		return m_Statistics.Current();
	}

	RenderFrameCounters GraphicsFrameCycle::PreviousCounters() const {
		RequireThread();

		return m_Statistics.Previous();
	}

} // namespace sgre::render
