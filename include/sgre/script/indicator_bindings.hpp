#pragma once

#include "sgre/scene/bitmap_indicator.hpp"
#include "sgre/script/object_bindings.hpp"

#include <memory>

namespace sgre::scene {
	class BitmapFontAssetCatalog;
	class BitmapFontTextureOwner;
	class LayerTree;
	class TextureCache;
} // namespace sgre::scene

namespace sgre::script {
	class ScriptFontBindings;
	class ScriptLayerBindings;
	class ScriptPsbBindings;

	class ScriptIndicatorBindings {
	public:
		// VM Close and instance retirement occur while these borrowed native services remain alive.
		// Optional Font bindings enable the recovered PSBFont branch; SystemFont providers remain unsupported.
		ScriptIndicatorBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, ScriptPsbBindings& psb, scene::BitmapFontAssetCatalog& fonts, scene::TextureCache& textures, ScriptFontBindings* font_bindings = nullptr);
		~ScriptIndicatorBindings();
		ScriptIndicatorBindings(const ScriptIndicatorBindings&) = delete;
		ScriptIndicatorBindings& operator=(const ScriptIndicatorBindings&) = delete;
		ScriptIndicatorBindings(ScriptIndicatorBindings&&) = delete;
		ScriptIndicatorBindings& operator=(ScriptIndicatorBindings&&) = delete;

		[[nodiscard]] SquirrelValue LayerViewClass() const;
		[[nodiscard]] SquirrelValue IndicatorClass() const;
		// These views borrow the actual Layer-owned task and expire at task or parent retirement.
		[[nodiscard]] const scene::BitmapIndicator& BitmapState(const SquirrelValue& instance) const;
		[[nodiscard]] const scene::BitmapFontTextureOwner& FontSources(const SquirrelValue& instance) const;
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
