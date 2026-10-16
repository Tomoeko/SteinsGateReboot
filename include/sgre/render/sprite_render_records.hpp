#pragma once

#include "sgre/render/frame_statistics.hpp"
#include "sgre/render/sprite_frame_encoder.hpp"
#include "sgre/render/sprite_graphics_camera.hpp"
#include "sgre/render/sprite_host_inputs.hpp"
#include "sgre/scene/fill_rectangle_packet.hpp"
#include "sgre/scene/motion_frame_submission.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <thread>

namespace sgre::render {

	struct SpriteRenderRecordScalars {
		std::uint8_t NativeByte00 = 1;
		std::uint32_t TextureAttributes = 0;
		std::uint32_t NativeAttribute24 = 1;
		bool EnableBlending = true;
		std::uint32_t BlendMode = 0;
		// Begin559bc0 leaves +38 untouched. It becomes known only after an actual packet writes it.
		std::optional<float> BlendParameter;
		std::uint32_t ColorFilter = 0;
		std::uint32_t FilterColor = 0xffffffff;
		bool UseIdentity = false;
		bool EnablePixelShader = true;
		float NativeScalar48 = 0;
		std::uint32_t UniformArgb = 0xffffffff;
		bool UseExplicitMatrix = false;
		bool WriteColor = true;
	};

	class SpriteRenderRecords {
	public:
		// Renderer, camera/model/target services, statistics and host-input allocations enclose this owner.
		// Only this owner may change the contained encoder phase. Construct outside ordinary traversal.
		SpriteRenderRecords(SpritePassRenderer& actual_renderer, SpriteGraphicsCamera& actual_camera, RenderFrameStatistics& actual_statistics);
		SpriteRenderRecords(const SpriteRenderRecords&) = delete;
		SpriteRenderRecords& operator=(const SpriteRenderRecords&) = delete;
		SpriteRenderRecords(SpriteRenderRecords&&) = delete;
		SpriteRenderRecords& operator=(SpriteRenderRecords&&) = delete;
		// Identity check for actual preparation/packet services; does not expose mutable encoder/camera state.
		void RequireRenderer(const SpritePassRenderer& actual_renderer) const;
		[[nodiscard]] const SpritePassRenderer& CurrentRenderTarget() const;
		[[nodiscard]] std::uint64_t TextureBindingRevision() const;
		void SelectRenderTarget(SpriteGraphicsTargets& graphics, const std::shared_ptr<SpritePassRenderer>& actual_target, const std::optional<std::array<float, 4>>& clear_color);
		// Values from the exact active contained encoder; they own no GPU resources or internal references.
		// Failed metadata queries do not mutate or latch a drawing failure.
		[[nodiscard]] SpritePreparedTextureMetadata PreparedTexture(SpriteTextureHandle actual_texture) const;
		[[nodiscard]] SpriteTextureHandle PrepareMutableTexture(scene::MutableTextureOwner& owner, std::uint32_t slot, scene::MutableTextureStage stage);
		// Native mode1 resources are prepared outside frame traversal, then the real mask phases use this encoder.
		void PrepareTextureMask();
		// Actual source-manager flush may prepare first-use resources only between balanced records and idle masks.
		void PrepareMotionPipelines(std::span<const scene::SpritePipeline> pipelines, std::span<SpritePipelineHandle> output);
		[[nodiscard]] SpritePreparedCopiedTarget PrepareMotionCopiedTarget(scene::MutableTextureBackend& backend, std::uint32_t width, std::uint32_t height);
		void ConfigureTextureMask(std::uint8_t operation, float alpha_multiplier, float alpha_offset);
		void ApplyTextureMask(scene::MotionStencilAction action);
		[[nodiscard]] scene::MotionMaskPhase TextureMaskPhase() const;
		[[nodiscard]] SpriteTextureHandle CopyCurrentTarget(scene::MutableTextureOwner& destination, const scene::MotionBounds& actual_rectangle);
		[[nodiscard]] std::unique_ptr<scene::MutableTextureOwner> PrepareCaptureTexture(scene::MutableTextureBackend& backend, const scene::MutableTextureSpec& spec);
		[[nodiscard]] std::unique_ptr<scene::MutableTextureOwner> PrepareMovieTexture(scene::MutableTextureBackend& backend, std::uint32_t width, std::uint32_t height);
		void CopyCaptureCurrentTarget(scene::MutableTextureOwner& destination, const scene::MotionBounds& rectangle);
		// Snapshot the real copied-region owner and native reciprocal/offset coefficients for the current record.
		void SetCopiedDestination(SpriteTextureHandle actual_texture, const std::array<float, 4>& uv_affine);
		void ClearCopiedDestination();
		[[nodiscard]] scene::SpritePipeline PreparedPipeline(SpritePipelineHandle actual_pipeline) const;
		[[nodiscard]] scene::MotionPreparedFrame OrdinaryFrameGlobals(std::span<const resource::MotionPoint> actual_texture_sizes) const;
		[[nodiscard]] scene::MotionPreparedFrame SceneFrameGlobals(std::span<const resource::MotionPoint> actual_texture_sizes) const;
		// Last successful actual device request; the native rounded cache may deliberately differ.
		[[nodiscard]] scene::MotionIntegerScissor SubmittedDeviceScissor() const;
		// Genuine Pre ordering: reset the actual counters/bindings before pool/target/default-camera phases.
		void ResetFrameState();
		// Consumes this owner's exact pending reset after the actual target/camera/pool work completes.
		void BeginPreparedFrame(const std::array<float, 4>& clear_color, SpritePassLoadAction load_action);
		void BeginFrame(const std::array<float, 4>& clear_color, SpritePassLoadAction load_action);
		// Native559bc0/553320 ordinary default branch. The true +45a copy producer remains unproved.
		void BeginRecord();
		void EndRecord();
		void SetBlend(std::uint32_t mode, float parameter);
		// Native Picture draws use the complete ordinary blend-state rows0..10.
		void SetPictureBlend(std::uint32_t mode);
		// Native55ab10, restricted to the recovered filter-zero Metal consumer.
		void SetFilterColor(std::uint32_t color);
		void SetScissor(scene::MotionIntegerScissor rectangle);
		// Real format4 indexed triangle strips only; resources are already prepared outside this frame.
		// The actual inputs remain live through their enclosing record/frame fence.
		void Append(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const SpriteHostInputs& actual_inputs, std::uint32_t texture_attributes);
		// Native bitmap font4c1d10 supplies format4 indexed triangle lists in this same record/camera scope.
		void AppendGlyphs(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const SpriteHostInputs& actual_inputs);
		// Native format2 copies the separate XY/UV quad and ARGB uniform into this same live encoder.
		void AppendRaw(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const scene::RawTextureDrawPacket& packet);
		void AppendMovieRaw(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const scene::RawTextureDrawPacket& packet);
		// Raw DWORD opacity selects attributes independently of the low-byte white ARGB uniform.
		void AppendPicture(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const scene::RawTextureDrawPacket& packet, std::uint32_t raw_opacity);
		void AppendMovieAlpha(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const scene::MovieAlphaDrawPacket& packet);
		void AppendCapturedOpacityMap(SpriteTextureHandle actual_capture, SpriteTextureHandle actual_map, SpritePipelineHandle pipeline, const scene::CapturedOpacityMapDrawPacket& packet);
		void AppendFillRectangle(SpritePipelineHandle pipeline, const scene::FillRectangleDrawPacket& packet);
		void AppendProjection(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const SpriteHostInputs& actual_inputs, std::uint32_t packed_argb);
		[[nodiscard]] std::uint64_t EndFrame();
		void AbortFrame();
		// Present only after this exact owner successfully ended/submitted its latest frame; Pre/Abort clears it.
		[[nodiscard]] std::optional<std::uint64_t> CompletedSubmission() const;
		[[nodiscard]] bool IsActive() const;
		[[nodiscard]] std::size_t RecordDepth() const;
		[[nodiscard]] SpriteRenderRecordScalars CurrentRecord() const;

	private:
		friend class FrameHostPool;
		void AppendRawPacket(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const scene::RawTextureDrawPacket& packet, bool movie);
		void AppendPreparedFormat2(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const scene::RawTextureDrawPacket& packet);
		struct Record {
			SpriteRenderRecordScalars Scalars;
			std::optional<SpriteTextureHandle> Texture;
			const SpriteHostInputs* Inputs = nullptr;
			std::optional<SpriteTextureHandle> CopiedDestination;
			std::array<float, 4> CopiedUvAffine{};
		};

		void RequireThread() const;
		void RequireFrame() const;
		void RequireRecord() const;
		void InvalidateBindings() noexcept;
		void BindCopiedDraw(SpritePassDraw& draw, const scene::SpritePipeline& pipeline) const;
		void AppendIndexed(SpriteTextureHandle texture, SpritePipelineHandle pipeline, const SpriteHostInputs& actual_inputs, std::uint32_t texture_attributes, scene::MotionDrawTopology topology);
		SpritePassRenderer& m_Renderer;
		SpriteGraphicsCamera& m_Camera;
		RenderFrameStatistics& m_Statistics;
		SpriteFrameEncoder m_Encoder;
		const std::thread::id m_Thread;
		// No constructor/default bytes are asserted for native record zero or untouched matrix/padding.
		std::array<std::optional<Record>, 8> m_Records;
		std::size_t m_Depth = 0;
		std::uint32_t m_BlendMode = 0;
		float m_BlendParameter = 0;
		std::optional<std::uint32_t> m_FilterColor;
		std::optional<SpritePipelineHandle> m_CurrentPipeline;
		const SpriteHostInputs* m_CurrentInputs = nullptr;
		std::exception_ptr m_Failure;
		bool m_ResetPending = false;
		std::optional<std::uint64_t> m_CompletedSubmission;
		std::uint64_t m_TextureBindingRevision = 0;
	};

} // namespace sgre::render
