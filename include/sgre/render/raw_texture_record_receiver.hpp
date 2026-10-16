#pragma once

#include "sgre/render/sprite_render_records.hpp"
#include "sgre/scene/raw_texture_task.hpp"

#include <array>
#include <memory>

namespace sgre::render {

	class RawTextureRecordReceiver {
	public:
		// The actual records/renderer enclose this backend. All script texture owners retire before it.
		// Prepare every ordinary blend/color pipeline before any frame begins.
		RawTextureRecordReceiver(SpriteRenderRecords& actual_records, SpritePassRenderer& actual_renderer);
		RawTextureRecordReceiver(const RawTextureRecordReceiver&) = delete;
		RawTextureRecordReceiver& operator=(const RawTextureRecordReceiver&) = delete;
		RawTextureRecordReceiver(RawTextureRecordReceiver&&) = delete;
		RawTextureRecordReceiver& operator=(RawTextureRecordReceiver&&) = delete;
		[[nodiscard]] scene::MutableTextureBackend& Backend();
		// The genuine camera/clip traversal and shared outer frame are already active.
		// Packet spans expire after AppendRaw copies them into that frame's real upload slot.
		void Draw(scene::RawTextureTask& actual_task, const scene::RawTextureDrawPacket& actual_packet);

	private:
		SpriteRenderRecords& m_Records;
		SpritePassRenderer& m_Renderer;
		std::unique_ptr<scene::MutableTextureBackend> m_Backend;
		std::array<SpritePipelineHandle, 24> m_Pipelines;
		bool m_Drawing = false;
	};

} // namespace sgre::render
