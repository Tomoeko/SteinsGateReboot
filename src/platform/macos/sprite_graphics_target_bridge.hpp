#pragma once

#include "sgre/render/sprite_pass.hpp"

#include <functional>

namespace sgre::render {
	class SpriteGraphicsTargetBridge {
	public:
		static void RequireIdleEncoding(const SpritePassRenderer& renderer);
		static void ClearShaderTextures(SpritePassRenderer& renderer);
		[[nodiscard]] static std::unique_ptr<scene::MutableTextureBackend> CreateMutableTextureBackend(SpritePassRenderer& allocation_owner, std::function<std::shared_ptr<SpritePassRenderer>()> require_current_target, std::function<bool()> owner_available);
	};
} // namespace sgre::render
