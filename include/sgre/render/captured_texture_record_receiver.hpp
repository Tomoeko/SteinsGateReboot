#pragma once

#include "sgre/render/sprite_render_records.hpp"
#include "sgre/scene/captured_opacity_map.hpp"
#include "sgre/scene/captured_task.hpp"
#include "sgre/scene/motion_textures.hpp"

#include <array>

namespace sgre::render {
	class CapturedTextureRecordReceiver {
	public:
		// The actual Records/renderer/backend enclose this receiver and every Capture source allocation.
		CapturedTextureRecordReceiver(SpriteRenderRecords& actual_records, SpritePassRenderer& actual_renderer, scene::MutableTextureBackend& actual_capture_backend);
		CapturedTextureRecordReceiver(const CapturedTextureRecordReceiver&) = delete;
		CapturedTextureRecordReceiver& operator=(const CapturedTextureRecordReceiver&) = delete;
		// Called from the genuine camera child; binding refreshes dirty main→companion on this frame's buffer.
		void Draw(scene::MutableTextureOwner& actual_capture_texture, const scene::RawTextureDrawPacket& packet);
		void DrawOpacityMap(scene::MutableTextureOwner& actual_capture_texture, scene::TextureCache& actual_cache, scene::MotionTextureWrapper::Borrowed actual_map, const scene::CapturedOpacityMapDrawPacket& packet);

	private:
		SpriteRenderRecords& m_Records;
		SpritePassRenderer& m_Renderer;
		scene::MutableTextureBackend& m_Backend;
		std::array<SpritePipelineHandle, 2> m_Pipelines;
		std::array<SpritePipelineHandle, 9> m_MapPipelines;
		bool m_Drawing = false;
	};
} // namespace sgre::render
