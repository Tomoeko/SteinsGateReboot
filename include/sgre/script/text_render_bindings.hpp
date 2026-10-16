#pragma once

#include "sgre/script/object_bindings.hpp"
#include "sgre/script/text_font_owner.hpp"
#include "sgre/text/render_context.hpp"
#include "sgre/text/vector_glyph_draw.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace sgre::scene {
	class LayerTree;
	class LayerScaleView;
	class LayerCameraDrawScope;
} // namespace sgre::scene

namespace sgre::script {
	class ScriptLayerBindings;

	class ScriptTextRenderBindings {
	public:
		// Services remain alive through VM Close and actual Layer-owned task retirement.
		ScriptTextRenderBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, scene::MutableTextureBackend& backend);
		~ScriptTextRenderBindings();
		ScriptTextRenderBindings(const ScriptTextRenderBindings&) = delete;
		ScriptTextRenderBindings& operator=(const ScriptTextRenderBindings&) = delete;
		ScriptTextRenderBindings(ScriptTextRenderBindings&&) = delete;
		ScriptTextRenderBindings& operator=(ScriptTextRenderBindings&&) = delete;
		// Bind actual provider services before construction; all services outlive Layer-owned text tasks.
		void ConfigureFrameOwners(scene::LayerCameraDrawScope& actual_scope);
		using VectorGlyphDrawing = std::function<void(text::TextGlyphAtlasOwner&, std::span<const text::VectorGlyphDrawRectangle>, const text::TextBounds&)>;
		// Synchronous actual atlas/rectangle receiver, configured before task construction and enclosing all tasks.
		void ConfigureGlyphDrawing(VectorGlyphDrawing draw);
		void ConfigureFontOwners(ScriptPsbBindings& psb, ScriptVectorFontBindings& vectors, scene::BitmapFontAssetCatalog& fonts, scene::TextureCache& textures);
		[[nodiscard]] TextFontLookupResult SelectFont(const SquirrelValue& instance, text::TextFontFamily family, text::TextFontSelectionKey key, std::string_view face_name);
		[[nodiscard]] SquirrelValue TextRenderClass() const;
		[[nodiscard]] const scene::LayerScaleView& ScaleView(const SquirrelValue& instance) const;
		[[nodiscard]] bool IsDetached(const SquirrelValue& instance) const;
		// The returned context borrows the actual Layer-owned task until its retirement.
		[[nodiscard]] const text::TextRenderContext& Context(const SquirrelValue& instance) const;
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
