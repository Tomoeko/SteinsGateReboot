#pragma once

#include "sgre/render/frame_host_pool.hpp"
#include "sgre/scene/screen_projection_packet.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace sgre::render {
	class ScreenProjectionRecordReceiver {
	public:
		// The exact records, shared-domain renderer and physical-frame pool enclose this receiver.
		ScreenProjectionRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, FrameHostPool& pool);
		~ScreenProjectionRecordReceiver();
		ScreenProjectionRecordReceiver(const ScreenProjectionRecordReceiver&) = delete;
		ScreenProjectionRecordReceiver& operator=(const ScreenProjectionRecordReceiver&) = delete;
		// The source is the live Screen's idle-registered borrowed sampled view, never a copied image.
		void Draw(SpriteTextureHandle actual_screen_texture, const scene::ScreenProjectionPacket& packet);
		void ReleaseFrameInputs();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::render
