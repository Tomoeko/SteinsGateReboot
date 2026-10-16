#pragma once

#include "sgre/scene/fill_rectangle_task.hpp"
#include "sgre/scene/layer_scale_view.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <memory>

namespace sgre::scene {
	class LayerCameraDrawScope;
}
namespace sgre::script {
	class ScriptLayerBindings;
	struct ScriptFillRectangleLimits {
		std::size_t MaxInstances = 1024;
		std::size_t MaxCameraTasks = 64;
	};

	class ScriptFillRectangleBindings {
	public:
		// Actual VM/Object/Layer services enclose wrappers; Layer.First owns the deferred physical tasks.
		ScriptFillRectangleBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptFillRectangleLimits limits = {});
		~ScriptFillRectangleBindings();
		ScriptFillRectangleBindings(const ScriptFillRectangleBindings&) = delete;
		ScriptFillRectangleBindings& operator=(const ScriptFillRectangleBindings&) = delete;
		void ConfigureFrameOwners(scene::LayerCameraDrawScope& actual_renderer, scene::FillRectangleTask::DrawRectangle actual_drawing);
		[[nodiscard]] SquirrelValue FillRectangleClass() const;
		[[nodiscard]] scene::FillRectangleState State(const SquirrelValue& instance) const;
		[[nodiscard]] scene::LayerScaleView& ScaleView(const SquirrelValue& instance);
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
