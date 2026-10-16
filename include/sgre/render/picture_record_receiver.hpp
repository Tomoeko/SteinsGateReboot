#pragma once

#include "sgre/render/sprite_render_records.hpp"
#include "sgre/scene/picture_packet.hpp"

#include <array>

namespace sgre::render {

	class PictureRecordReceiver {
	public:
		// Actual frame records, renderer and immutable Image texture cache enclose this receiving owner.
		PictureRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, scene::TextureCache& textures);
		void Draw(const scene::PictureDrawFrame& frame);

	private:
		static constexpr std::size_t kPipelineCount = 3 * 11 * 2 * 4;
		SpriteRenderRecords& m_Records;
		SpritePassRenderer& m_Renderer;
		scene::TextureCache& m_Textures;
		std::array<SpritePipelineHandle, kPipelineCount> m_Pipelines{};
		bool m_Drawing = false;
	};

} // namespace sgre::render
