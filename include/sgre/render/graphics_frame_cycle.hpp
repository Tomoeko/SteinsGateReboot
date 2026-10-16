#pragma once

#include "sgre/render/bitmap_font_record_receiver.hpp"
#include "sgre/render/captured_texture_record_receiver.hpp"
#include "sgre/render/frame_host_pool.hpp"
#include "sgre/render/motion_sprite_record_receiver.hpp"
#include "sgre/render/movie_record_receiver.hpp"
#include "sgre/render/picture_record_receiver.hpp"
#include "sgre/render/raw_texture_record_receiver.hpp"
#include "sgre/render/screen_projection_record_receiver.hpp"
#include "sgre/render/text_glyph_record_receiver.hpp"
#include "sgre/scene/capture_task.hpp"
#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/scene/render_target_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace sgre::scene {
	class MotionSceneDrawContext;
	class LayerManagerFrame;
	struct ScreenState;
} // namespace sgre::scene

namespace sgre::render {

	struct GraphicsFrameCycleLimits {
		FrameHostPoolLimits Pool;
		std::size_t ModelDepth = 64;
		std::size_t CameraDepth = 64;
		std::size_t TextureSlots = 1024;
	};

	class GraphicsFrameCycle {
	public:
		// Actual graphics primary/surface/workers/cache owners enclose the complete contained frame services.
		// Prepare this owner, bindings and shader resources before ordinary iteration.
		GraphicsFrameCycle(SpriteGraphicsTargets& actual_graphics, SpritePassPresentation& actual_surface, core::ThreadTaskFrame& actual_workers, scene::TextureCache& actual_textures, RenderFrameCounters actual_current, RenderFrameCounters actual_previous, GraphicsFrameCycleLimits limits = {});
		~GraphicsFrameCycle() noexcept;
		GraphicsFrameCycle(const GraphicsFrameCycle&) = delete;
		GraphicsFrameCycle& operator=(const GraphicsFrameCycle&) = delete;
		GraphicsFrameCycle(GraphicsFrameCycle&&) = delete;
		GraphicsFrameCycle& operator=(GraphicsFrameCycle&&) = delete;

		void BindTexture(std::uint32_t actual_slot, scene::CachedTextureHandle actual_texture);
		void UnbindTexture(std::uint32_t actual_slot);
		[[nodiscard]] FrameHostWriterBinding BindWriter(core::ThreadTaskReceiver& actual_writer);
		// Actual counter/binding reset, pool Begin, primary selection, model and default camera phases.
		void PreFrame();
		// Start the real encoder immediately before actual root traversal, after worker/texture updates finish.
		void BeginDrawing();
		// The exact LayerManager reset callback observes this contained model/camera owner.
		void ResetRootCamera();
		// Startup registrations borrow this exact scope; its admission callback requires a real active frame.
		[[nodiscard]] scene::LayerCameraDrawScope& CameraScope();
		[[nodiscard]] const scene::LayerCameraFrameState& FrameState() const;
		// Actual selected-player contexts feed this contained receiver; no external frame globals are accepted.
		void Draw(scene::MotionSpriteDrawContext& actual_context, const scene::MotionFrameScratch& actual_scratch);
		// Consume the actual manager Draw publication synchronously at its current camera/model scope.
		// A completed queue may be reused in this outer frame; copied GPU inputs remain owned through End/Abort.
		void DrawScene(scene::MotionSceneDrawContext& actual_context, std::span<const scene::CachedTextureHandle> actual_local_slots, const scene::MotionFrameScratch& scratch);
		// Construct script RawTex owners against this exact backend before ordinary drawing.
		[[nodiscard]] scene::MutableTextureBackend& RawTextureBackend();
		void DrawRawTexture(scene::RawTextureTask& actual_task, const scene::RawTextureDrawPacket& actual_packet);
		// Native4fa250: actual target clear and manager.Draw(0), then prior target/default camera/raw clip/model.
		// This is one scope in the existing command buffer; the task/target/root remain genuine borrowed owners.
		void DrawScreen(SpritePassRenderer& actual_target, scene::LayerManagerFrame& actual_manager, const scene::ScreenState& state);
		[[nodiscard]] scene::MutableTextureBackend& CaptureBackend();
		void DrawCapture(const std::shared_ptr<scene::CaptureTask>& actual_task);
		void PrepareCaptured();
		void DrawCaptured(scene::MutableTextureOwner& actual_capture_texture, const scene::RawTextureDrawPacket& packet);
		void DrawCapturedOpacityMap(scene::MutableTextureOwner& actual_capture_texture, scene::MotionTextureWrapper::Borrowed actual_map, const scene::CapturedOpacityMapDrawPacket& packet);
		void PrepareFillRectangle();
		void DrawFillRectangle(const scene::FillRectangleDrawPacket& packet);
		void DrawScreenProjection(SpriteTextureHandle actual_screen_texture, const scene::ScreenProjectionPacket& packet);
		void PreparePictures();
		void DrawPicture(const scene::PictureDrawFrame& frame);
		void PrepareMovies();
		void DrawMovie(const media::MovieDrawFrame& frame);
		[[nodiscard]] bool CanRetireMovie(const void* owner) const;
		void RetireMovie(const void* owner);
		void PrepareTextGlyphs();
		void DrawTextGlyphs(text::TextGlyphAtlasOwner& actual_atlas, std::span<const text::VectorGlyphDrawRectangle> rectangles, const text::TextBounds& clip);
		void DrawBitmapConsole(const scene::BitmapConsole& console, const scene::IndicatorPlacement& placement, const scene::BitmapFontTextureOwner& font, std::uint32_t argument);
		// Finish actual encoding and release exact input borrows, then publish the native pool entry byte.
		// A draw-paused cycle still commits the genuine native clear through actual empty encoding.
		void PostFrame();
		// Authored failure cleanup preserves the first failure; pending real writers remain a retirement barrier.
		void AbortFrame();
		void Close();
		[[nodiscard]] std::optional<SpritePresentationSubmission> LastSubmission() const;
		[[nodiscard]] RenderFrameCounters CurrentCounters() const;
		[[nodiscard]] RenderFrameCounters PreviousCounters() const;

	private:
		enum class Phase {
			Idle,
			Prepared,
			Drawing,
			Aborting,
			Closed
		};

		[[nodiscard]] scene::LayerCameraDrawScope PrepareCameraScope(std::size_t max_depth);
		void RequireThread() const;
		void RequireIdle() const;
		void RequireDrawing() const;
		void RequireRootTraversalComplete() const;
		void ReleaseSceneContexts();
		void ReleaseCaptureTasks(bool submitted);
		void ResetSelectedTargetState();
		void RestoreRawScissor(const scene::MotionBounds& saved);
		SpriteGraphicsTargets& m_Graphics;
		scene::TextureCache& m_Textures;
		std::shared_ptr<SpritePassRenderer> m_Primary;
		scene::GraphicsModelStack m_Models;
		scene::LayerCameraFrameState m_FrameState;
		SpriteGraphicsCamera m_Camera;
		RenderFrameStatistics m_Statistics;
		SpriteRenderRecords m_Records;
		FrameHostPool m_Pool;
		MotionSpriteRecordReceiver m_Receiver;
		BitmapFontRecordReceiver m_BitmapReceiver;
		ScreenProjectionRecordReceiver m_ProjectionReceiver;
		// Completed occurrences retain a null slot until End/Abort; only unfinished queues remain borrowed.
		std::vector<scene::MotionSceneDrawContext*> m_SceneContexts;
		std::vector<std::vector<resource::MotionPoint>> m_SceneTextureSizes;
		std::size_t m_MaximumTextureSlots = 0;
		std::unique_ptr<RawTextureRecordReceiver> m_RawReceiver;
		std::optional<SpritePipelineHandle> m_FillRectanglePipeline;
		std::unique_ptr<TextGlyphRecordReceiver> m_TextGlyphReceiver;
		std::unique_ptr<MovieRecordReceiver> m_MovieReceiver;
		std::unique_ptr<PictureRecordReceiver> m_PictureReceiver;
		std::unique_ptr<scene::MutableTextureBackend> m_CaptureBackend;
		std::unique_ptr<CapturedTextureRecordReceiver> m_CapturedReceiver;
		std::vector<std::shared_ptr<scene::CaptureTask>> m_CaptureTasks;
		scene::LayerCameraDrawScope m_CameraScope;
		const std::thread::id m_Thread = std::this_thread::get_id();
		std::optional<SpritePresentationSubmission> m_LastSubmission;
		Phase m_Phase = Phase::Idle;
		bool m_PoolFrameOpen = false;
		std::size_t m_ScreenDepth = 0;
	};

} // namespace sgre::render
