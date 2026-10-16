#pragma once

#include "sgre/scene/layer_scale_view.hpp"
#include "sgre/script/draw_object_bindings.hpp"

#include <cstddef>
#include <memory>

namespace sgre::scene {
	class LayerCameraDrawScope;
}

namespace sgre::script {

	class ScriptLayerBindings;

	struct ScriptLayerDrawLimits {
		std::size_t MaxInstances = 1024;
		std::size_t MaxCameraTasks = 64;
	};

	class ScriptLayerDrawBindings {
	public:
		// Services and an optional actual frame scope outlive wrappers and deferred First task disposal.
		ScriptLayerDrawBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptDrawObjectBindings& draws, ScriptLayerDrawLimits limits = {}, scene::LayerCameraDrawScope* camera = nullptr);
		~ScriptLayerDrawBindings();
		ScriptLayerDrawBindings(const ScriptLayerDrawBindings&) = delete;
		ScriptLayerDrawBindings& operator=(const ScriptLayerDrawBindings&) = delete;
		ScriptLayerDrawBindings(ScriptLayerDrawBindings&&) = delete;
		ScriptLayerDrawBindings& operator=(ScriptLayerDrawBindings&&) = delete;

		[[nodiscard]] SquirrelValue LayerDrawClass() const;
		[[nodiscard]] scene::LayerScaleView& ScaleView(const SquirrelValue& instance);
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
