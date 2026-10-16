#pragma once

#include "sgre/scene/layer_scale_view.hpp"
#include "sgre/scene/picture_layer_task.hpp"
#include "sgre/scene/picture_packet.hpp"
#include "sgre/script/image_bindings.hpp"

#include <cstddef>
#include <functional>
#include <memory>

namespace sgre::scene {
	class LayerTree;
	class LayerCameraDrawScope;
} // namespace sgre::scene

namespace sgre::script {

	class ScriptLayerBindings;

	struct ScriptLayerPictureLimits {
		std::size_t MaxInstances = 1024;
		std::size_t MaxRegions = 4096;
		std::size_t MaxCameraTasks = 64;
	};

	class ScriptLayerPictureBindings {
	public:
		// Services outlive wrappers and First-owned tasks; explicit VM Close precedes adapter destruction.
		ScriptLayerPictureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptImageBindings& images, scene::LayerTree& tree, ScriptLayerPictureLimits limits = {});
		~ScriptLayerPictureBindings();
		ScriptLayerPictureBindings(const ScriptLayerPictureBindings&) = delete;
		ScriptLayerPictureBindings& operator=(const ScriptLayerPictureBindings&) = delete;
		// Actual camera and synchronous frame receiver enclose every deferred First task. Configure once before construction.
		void ConfigureFrameOwners(scene::LayerCameraDrawScope& actual_renderer, std::function<void(const scene::PictureDrawFrame&)> draw);
		[[nodiscard]] SquirrelValue LayerPictureClass() const;
		[[nodiscard]] scene::PictureLayerTaskState PreparedState(const SquirrelValue& instance) const;
		[[nodiscard]] scene::LayerScaleView& ScaleView(const SquirrelValue& instance);
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
