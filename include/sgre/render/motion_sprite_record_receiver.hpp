#pragma once

#include "sgre/render/frame_host_heap.hpp"
#include "sgre/render/sprite_graphics_targets.hpp"
#include "sgre/render/sprite_render_records.hpp"
#include "sgre/scene/texture_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace sgre::scene {
	class MotionSpriteDrawContext;
}

namespace sgre::render {
	class FrameHostPool;

	class MotionSpriteRecordReceiver {
	public:
		// Actual records/renderer/graphics/cache/heap services enclose this stable packet-input owner.
		// Idle bindings or typed actual-occurrence preparation share this exact containing input retirement fence.
		MotionSpriteRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, SpriteGraphicsTargets& graphics, scene::TextureCache& textures, FrameHostHeap& heap, std::size_t texture_slots = 1024);
		// The actual pool belongs to these exact records and commits one qualified borrow per stable input cell.
		MotionSpriteRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, SpriteGraphicsTargets& graphics, scene::TextureCache& textures, FrameHostPool& pool, std::size_t texture_slots = 1024);
		~MotionSpriteRecordReceiver();
		MotionSpriteRecordReceiver(const MotionSpriteRecordReceiver&) = delete;
		MotionSpriteRecordReceiver& operator=(const MotionSpriteRecordReceiver&) = delete;
		MotionSpriteRecordReceiver(MotionSpriteRecordReceiver&&) = delete;
		MotionSpriteRecordReceiver& operator=(MotionSpriteRecordReceiver&&) = delete;
		void BindTexture(std::uint32_t actual_slot, scene::CachedTextureHandle actual_texture);
		void UnbindTexture(std::uint32_t actual_slot);
		// The real outer records frame is already active. This opens/closes only scene record scopes.
		// Empty contexts emit no scope/submission. Dense actual texture prefixes are the admitted slot domain.
		// The containing camera/clip scope supplies real device scissor changes; rounded-cache equality is not device identity.
		// Admission errors before Begin retain the outer frame. Later failures abort it and preserve inputs until retirement.
		void Draw(scene::MotionSpriteDrawContext& context, const scene::MotionFrameScratch& scratch);
		// The published scene and every borrowed command/packet/source stay pinned until ReleaseFrameInputs.
		// Prepare outside the GPU frame; it preallocates only actual copied-region replacements.
		void PrepareFrame(const scene::MotionPreparedFrame& actual_frame, const scene::MotionFrameScratch& scratch);
		// Replays that exact deterministic publication against the current camera/target and real mask phases.
		void DrawPreparedFrame(const scene::MotionFrameScratch& scratch);
		// One real manager flush: each element is its actual local slot, not a cache index or another manager's slot.
		// The actual local table and frame.TextureSizes must agree; cache owners enclose outer Post/Abort.
		// Borrowed publication spans are retired before a successful return, allowing manager queue consumption.
		// Earlier occurrence inputs and copied GPU owners remain retained until ReleaseFrameInputs.
		void DrawActualFrame(const scene::MotionPreparedFrame& actual_frame, std::span<const scene::CachedTextureHandle> actual_local_slots, const scene::MotionFrameScratch& scratch);
		// Call only at genuine outer frame quiescence, before recycling the real heap's physical frame.
		void ReleaseFrameInputs();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::render
