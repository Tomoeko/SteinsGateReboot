#pragma once

#include "sgre/render/sprite_render_records.hpp"
#include "sgre/text/glyph_atlas_owner.hpp"
#include "sgre/text/vector_glyph_draw.hpp"

#include <memory>
#include <span>

namespace sgre::render {
	class FrameHostPool;

	class TextGlyphRecordReceiver {
	public:
		// Actual renderer/records/pool enclose this receiver and every retained physical-frame input.
		// Both ordinary atlas filtering pipelines and scratch storage are prepared outside the frame.
		TextGlyphRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, FrameHostPool& pool);
		~TextGlyphRecordReceiver();
		TextGlyphRecordReceiver(const TextGlyphRecordReceiver&) = delete;
		TextGlyphRecordReceiver& operator=(const TextGlyphRecordReceiver&) = delete;
		void Draw(text::TextGlyphAtlasOwner& actual_atlas, std::span<const text::VectorGlyphDrawRectangle> rectangles);
		void ReleaseFrameInputs();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::render
