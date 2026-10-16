#pragma once

#include "sgre/render/sprite_pass.hpp"

#import <Metal/Metal.h>

#include <cstdint>
#include <functional>

namespace sgre::render {

	struct MetalSpritePresentationFrame {
		id<MTLTexture> Target = nil;
		id<MTLCommandBuffer> Producer = nil;
	};

	struct MetalSpritePresentationSource {
		id<MTLDevice> Device = nil;
		id<MTLCommandQueue> Queue = nil;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		// Checks the exact weak receiver and source metadata outside live encoding, without requiring a Submit.
		std::function<void(const MetalSpritePresentationSource&)> RequireAlive;
		// This callback borrows the renderer's existing weak receiver lifetime, without retaining its target.
		std::function<MetalSpritePresentationFrame(std::uint64_t)> RequireLatest;
		// A commit with an uncertain outcome fences the existing shared GPU resource owner.
		std::function<void()> FaultCommit;
		// Derives the identity from the live renderer Submit state; zero/uninitialized frames reject.
		std::function<std::uint64_t()> CurrentSubmission;
	};

	class SpritePassPresentationBridge {
	public:
		[[nodiscard]] static MetalSpritePresentationSource CreateSource(SpritePassRenderer& renderer);
	};

} // namespace sgre::render
