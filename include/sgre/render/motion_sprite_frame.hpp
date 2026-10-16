#pragma once

#include "sgre/render/sprite_pass.hpp"
#include "sgre/scene/motion_scissor.hpp"
#include "sgre/scene/motion_selected_sprite_player.hpp"
#include "sgre/scene/texture_cache.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace sgre::render {

	struct MotionSpriteFrameSubmission {
		std::uint64_t Identity = 0;
		scene::MotionIntegerScissor AppliedDevice;
	};

	class MotionSpriteFrameRenderer {
	public:
		// Actual renderer/cache/context/selected-resource owners outlive this packet receiver.
		// Pipeline compilation and binding refresh occur during preparation, never in Submit.
		MotionSpriteFrameRenderer(SpritePassRenderer& renderer, scene::TextureCache& textures, scene::SpritePipelineOptions options, std::size_t texture_slots = 1024);
		~MotionSpriteFrameRenderer();
		MotionSpriteFrameRenderer(const MotionSpriteFrameRenderer&) = delete;
		MotionSpriteFrameRenderer& operator=(const MotionSpriteFrameRenderer&) = delete;
		void BindTexture(std::uint32_t actual_slot, scene::CachedTextureHandle actual_texture);
		void UnbindTexture(std::uint32_t actual_slot);
		// Admitted ordinary indexed quads, raw blend0..5/16..21, zero blend parameter, no filter/mask/auxiliary target.
		// Device state is supplied separately from the rounded CPU cache; the result retains the final unclamped state.
		// Backend rejection occurs inside Flush, preserving actual queued row borrows for retry.
		// Packet spans are copied into bounded preallocated frame storage before the scene sink returns.
		[[nodiscard]] MotionSpriteFrameSubmission Submit(scene::MotionSpriteDrawContext& context, const scene::MotionPreparedFrame& globals, const scene::MotionFrameScratch& scratch, scene::MotionIntegerScissor actual_applied_device, const std::array<float, 16>& actual_projection, const std::array<float, 4>& clear_color, SpritePassLoadAction load_action);

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::render
