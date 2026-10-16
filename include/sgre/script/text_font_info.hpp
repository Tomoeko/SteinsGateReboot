#pragma once

#include "sgre/scene/bitmap_font_assets.hpp"
#include "sgre/script/psb_bindings.hpp"
#include "sgre/script/vector_font_bindings.hpp"
#include "sgre/text/font_selection.hpp"
#include "sgre/text/glyph_atlas_owner.hpp"
#include "sgre/text/glyph_run.hpp"
#include "sgre/text/vector_glyph_cache.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace sgre::script {

	enum class TextFontProviderKind {
		Unavailable,
		Bitmap,
		Vector
	};

	class ScriptTextFontInfo {
	public:
		// All borrowed adapters/cache and the VM outlive this prepared source owner.
		// Close before VM Close. Construction retains the exact findFont result, including unavailable sources.
		ScriptTextFontInfo(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptPsbBindings& psb, ScriptVectorFontBindings& vector_fonts, scene::BitmapFontAssetCatalog& bitmap_fonts, scene::TextureCache& textures, const SquirrelValue& source, text::TextFontSelectionKey key);
		~ScriptTextFontInfo();
		ScriptTextFontInfo(const ScriptTextFontInfo&) = delete;
		ScriptTextFontInfo& operator=(const ScriptTextFontInfo&) = delete;
		ScriptTextFontInfo(ScriptTextFontInfo&&) = delete;
		ScriptTextFontInfo& operator=(ScriptTextFontInfo&&) = delete;

		[[nodiscard]] TextFontProviderKind Kind() const;
		[[nodiscard]] const text::TextFontSelectionKey& Key() const;
		[[nodiscard]] SquirrelValue Source() const;
		void SetSize(float requested_size);
		void SetFiltering(bool enabled);
		[[nodiscard]] resource::GlyphSize BitmapScale() const;
		[[nodiscard]] resource::GlyphSize MeasureBitmapCodeUnit(std::uint16_t code_unit) const;
		// The unavailable and bitmap branches are recovered; vector unit measurement requires actual shaping.
		[[nodiscard]] resource::GlyphSize MeasureCodeUnit(std::uint16_t code_unit);
		[[nodiscard]] text::OwnedTextGlyphRun PrepareCodeUnitRun(std::uint16_t code_unit);
		[[nodiscard]] text::OwnedTextGlyphRun PrepareRun(std::u16string_view text);
		[[nodiscard]] const scene::BitmapFontTextureOwner* BitmapProvider() const;
		[[nodiscard]] std::uint32_t VectorSizeWord() const;
		// The caller supplies an actual shaped glyph ID; this does not substitute for native shaping.
		[[nodiscard]] text::CffPreparedGlyph PrepareVectorGlyph(std::uint32_t glyph_index);
		// The first real atlas context becomes fixed for this interface and outlives all of its cached regions.
		// Nonnegative edges use the actual fill or round-join/round-cap CFF producer; negative pixels remain unsupported.
		void PrepareVectorRun(text::TextGlyphAtlasOwner& atlas, std::span<const text::TextRunGlyph> run, std::int32_t edge_width = 0);
		[[nodiscard]] std::size_t PrepareVectorDrawRun(text::TextCursor origin, std::uint32_t color_rgba, std::span<const text::TextRunGlyph> run, std::span<text::CachedVectorGlyphDraw> output, std::int32_t edge_width = 0);
		[[nodiscard]] text::VectorGlyphCacheCounts VectorCacheCounts() const;
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
