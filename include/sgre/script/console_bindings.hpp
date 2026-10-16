#pragma once

#include "sgre/scene/bitmap_console.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace sgre::scene {
	class BitmapFontAssetCatalog;
	class BitmapFontTextureOwner;
	class IndicatorPlacement;
	class LayerTree;
	class TextureCache;
}

namespace sgre::script {
	class ScriptFontBindings;
	class ScriptIndicatorBindings;
	class ScriptLayerBindings;
	class ScriptPsbBindings;

	struct ScriptConsoleServices {
		// The real containing Looper has already selected this frame's elapsed time.
		std::function<float()> ReadFrameDelta;
		// A supplied renderer consumes the actual font textures and displayed elements synchronously.
		std::function<void(const scene::BitmapConsole&, const scene::IndicatorPlacement&, const scene::BitmapFontTextureOwner&, std::uint32_t)> Draw;
	};

	class ScriptConsoleBindings {
	public:
		// All borrowed services and the shared LayerView class remain alive through VM Close and task retirement.
		ScriptConsoleBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, ScriptPsbBindings& psb, scene::BitmapFontAssetCatalog& fonts, scene::TextureCache& textures, ScriptIndicatorBindings& views, ScriptConsoleServices services, ScriptFontBindings* font_bindings = nullptr);
		~ScriptConsoleBindings();
		ScriptConsoleBindings(const ScriptConsoleBindings&) = delete;
		ScriptConsoleBindings& operator=(const ScriptConsoleBindings&) = delete;
		ScriptConsoleBindings(ScriptConsoleBindings&&) = delete;
		ScriptConsoleBindings& operator=(ScriptConsoleBindings&&) = delete;
		[[nodiscard]] SquirrelValue ConsoleClass() const;
		[[nodiscard]] const scene::BitmapConsole& BitmapState(const SquirrelValue& instance) const;
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
}
