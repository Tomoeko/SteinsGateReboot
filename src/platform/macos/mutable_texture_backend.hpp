#pragma once

#include "sgre/render/sprite_pass.hpp"
#include "sgre/scene/motion_copied_region_owner.hpp"
#include "sgre/scene/mutable_texture_owner.hpp"

#import <Metal/Metal.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace sgre::render {
	struct MetalSpriteCopySource {
		id<MTLTexture> Target = nil;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t OriginalFormat = 28;
		const void* ResourceDomain = nullptr;
	};

	struct SpriteMutableTextureContext {
		const void* ResourceDomain = nullptr;
		bool DirectMotionPreparation = false;
		id<MTLDevice> Device = nil;
		id<MTLCommandQueue> Queue = nil;
		std::size_t MaximumBytes = 0;
		std::function<MetalSpriteCopySource()> RequireCopySource;
		std::function<id<MTLCommandBuffer>(id<MTLTexture>, const scene::MotionBounds&, const MetalSpriteCopySource&)> CopyFromTarget;
		std::function<bool()> Available;
		// Upload/sampler/bind only, either idle or inside the exact suspended frame owner.
		std::function<bool()> BindingAvailable;
		std::function<bool(id<MTLCommandBuffer>)> IsPendingFrameUpload;
		std::function<id<MTLCommandBuffer>(id<MTLBuffer>, std::size_t, id<MTLTexture>, std::uint32_t, std::uint32_t)> EncodeUpload;
		// Optional until the genuine staging-companion recipient is used. Returns the command buffer owning this exact blit.
		std::function<id<MTLCommandBuffer>(id<MTLTexture>, id<MTLTexture>, std::uint32_t, std::uint32_t)> EncodeTextureCopy;
		std::function<SpriteTextureHandle(std::size_t, std::uint32_t)> Register;
		std::function<void(SpriteTextureHandle)> Retire;
		std::function<void(SpriteTextureHandle, id<MTLTexture>)> SetView;
		std::function<void(SpriteTextureHandle, id<MTLSamplerState>, std::uint32_t, scene::MutableTextureStage)> Bind;
	};

	[[nodiscard]] bool SpriteMutableBackendAllowsMotionPreparation(const scene::MutableTextureBackend& backend, const void* domain) noexcept;
	[[nodiscard]] SpriteTextureHandle SpriteMutableTextureHandle(const scene::MutableTextureResource& resource);
	// GPU-copy recipient only: the actual resource must be a live nonsampled staging main in this domain.
	[[nodiscard]] id<MTLTexture> SpriteMutableStagingTexture(const scene::MutableTextureResource& resource, const void* actual_domain);
	[[nodiscard]] std::unique_ptr<scene::MutableTextureBackend> CreateSpriteMutableTextureBackend(SpriteMutableTextureContext context);
} // namespace sgre::render
