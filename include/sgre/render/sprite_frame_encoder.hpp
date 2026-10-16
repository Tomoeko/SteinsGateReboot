#pragma once

#include "sgre/render/sprite_pass.hpp"
#include "sgre/scene/motion_clip_bounds.hpp"
#include "sgre/scene/motion_scissor.hpp"
#include "sgre/scene/motion_shader_program.hpp"
#include "sgre/scene/motion_stencil_state.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace sgre::render {
	struct SpritePreparedTextureMetadata {
		scene::MotionShaderTexture Channels = scene::MotionShaderTexture::None;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		bool Mutable = false;
		bool RenderTarget = false;
		// Actual mutable/render-target owners and compressed BC7 storage retain their exact physical format.
		std::optional<std::uint32_t> OriginalDxgiFormat;
	};

	struct SpritePreparedCopiedTarget {
		std::unique_ptr<scene::MutableTextureOwner> Owner;
		SpriteTextureHandle Handle;
	};

	class SpriteFrameEncoder {
	public:
		// Construct outside traversal. The real renderer and serialized graphics services enclose this borrower.
		explicit SpriteFrameEncoder(SpritePassRenderer& actual_renderer);
		~SpriteFrameEncoder();
		SpriteFrameEncoder(const SpriteFrameEncoder&) = delete;
		SpriteFrameEncoder& operator=(const SpriteFrameEncoder&) = delete;
		SpriteFrameEncoder(SpriteFrameEncoder&&) = delete;
		SpriteFrameEncoder& operator=(SpriteFrameEncoder&&) = delete;
		// Idle setup allocates the real two R8 targets and all derived shader states. Native default mask scale is one.
		void PrepareTextureMask();
		void ConfigureTextureMask(std::uint8_t operation, float alpha_multiplier, float alpha_offset);
		void ApplyTextureMask(scene::MotionStencilAction action);
		[[nodiscard]] scene::MotionMaskPhase TextureMaskPhase() const;
		[[nodiscard]] std::uint32_t ActivePixelShader(SpritePipelineHandle pipeline, bool has_texture = true) const;
		// Destination is the real previously allocated copied target; this operation never allocates a texture.
		[[nodiscard]] SpriteTextureHandle CopyCurrentTarget(scene::MutableTextureOwner& destination, const scene::MotionBounds& actual_rectangle);
		// Native Capture allocates staging+companion and copies the current target without binding a sampled texture.
		[[nodiscard]] std::unique_ptr<scene::MutableTextureOwner> PrepareCaptureTexture(scene::MutableTextureBackend& backend, const scene::MutableTextureSpec& spec);
		[[nodiscard]] std::unique_ptr<scene::MutableTextureOwner> PrepareMovieTexture(scene::MutableTextureBackend& backend, std::uint32_t width, std::uint32_t height);
		void CopyCaptureCurrentTarget(scene::MutableTextureOwner& destination, const scene::MotionBounds& rectangle);
		// Typed first-use work only: suspend Store, prepare exact resources, resume Load on the same owned buffer.
		// Neither operation accepts a user callback, uploads pixels, submits, or changes the target/camera.
		void PrepareMotionPipelines(std::span<const scene::SpritePipeline> pipelines, std::span<SpritePipelineHandle> output);
		[[nodiscard]] SpritePreparedCopiedTarget PrepareMotionCopiedTarget(scene::MutableTextureBackend& backend, std::uint32_t width, std::uint32_t height);
		void Begin(const std::array<float, 4>& clear_color, SpritePassLoadAction load_action);
		// Actual registered same-domain target only, balanced outside masks; clear or resume this same buffer.
		// The concrete target owner outlives this scope and every containing draw; no additional Submit occurs.
		void SelectRenderTarget(SpritePassRenderer& actual_target, const std::optional<std::array<float, 4>>& clear_color);
		[[nodiscard]] const SpritePassRenderer& CurrentRenderTarget() const;
		// Native integer scissors are intersected with the real target before binding the actual live encoder.
		// Degenerate/empty physical scissors remain outside this initial encoding domain.
		void SetScissor(scene::MotionIntegerScissor actual_device_rectangle);
		// Actual last successful request before attachment clamping; rounded native caches may differ.
		// This is authored platform-state tracking, available only while this exact encoder is active.
		[[nodiscard]] scene::MotionIntegerScissor SubmittedDeviceScissor() const;
		// Input spans and transforms are copied into the genuine free upload slot before return.
		// Draw.Scissor must be absent: camera/clip traversal supplies live state through SetScissor.
		void Append(const SpritePassDraw& draw, const std::array<float, 16>& current_transform);
		// Store/suspend the actual pass, order dirty uploads in its same command buffer, then resume Load.
		[[nodiscard]] SpriteTextureHandle PrepareMutableTexture(scene::MutableTextureOwner& owner, std::uint32_t slot, scene::MutableTextureStage stage);
		[[nodiscard]] std::uint64_t End();
		// Discard uncommitted encoding; never publish a nominal frame for rejected traversal.
		void Abort();
		[[nodiscard]] bool IsActive() const;
		// Read-only values from this exact active encoder owner; never lend internal GPU/container pointers.
		[[nodiscard]] scene::SpritePipeline PreparedPipeline(SpritePipelineHandle actual_pipeline) const;
		[[nodiscard]] SpritePreparedTextureMetadata PreparedTexture(SpriteTextureHandle actual_texture) const;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::render
