#pragma once

#include "sgre/render/sprite_render_records.hpp"
#include "sgre/scene/bitmap_font_assets.hpp"

#include <cstdint>
#include <memory>

namespace sgre::scene {
	class BitmapConsole;
	class IndicatorPlacement;
}

namespace sgre::render {
	class FrameHostPool;

	class BitmapFontRecordReceiver {
	public:
		// Actual records/renderer/cache/pool enclose this nonmoving receiver and its retained physical-frame inputs.
		// All native bitmap channel/filter pipeline variants are prepared here, outside ordinary drawing.
		BitmapFontRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, scene::TextureCache& cache, FrameHostPool& pool);
		~BitmapFontRecordReceiver();
		BitmapFontRecordReceiver(const BitmapFontRecordReceiver&) = delete;
		BitmapFontRecordReceiver& operator=(const BitmapFontRecordReceiver&) = delete;
		BitmapFontRecordReceiver(BitmapFontRecordReceiver&&) = delete;
		BitmapFontRecordReceiver& operator=(BitmapFontRecordReceiver&&) = delete;
		// The containing Console task lends its exact placement/font/text. The real outer frame is already active.
		void Draw(const scene::BitmapConsole& console, const scene::IndicatorPlacement& placement, const scene::BitmapFontTextureOwner& actual_font, std::uint32_t argument);
		// Retire exact CPU input borrows after actual frame end/abort and before its pool pages recycle.
		void ReleaseFrameInputs();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::render
