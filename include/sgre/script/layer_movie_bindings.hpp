#pragma once

#include "sgre/scene/movie_layer_task.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <memory>

namespace sgre::scene {
	class LayerCameraDrawScope;
	class LayerScaleView;
	class LayerTree;
} // namespace sgre::scene

namespace sgre::script {
	class ScriptLayerBindings;

	struct ScriptLayerMovieLimits {
		std::size_t MaxInstances = 1024;
		std::size_t MaxCameraTasks = 64;
	};

	class ScriptLayerMovieBindings {
	public:
		ScriptLayerMovieBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, ScriptLayerMovieLimits limits = {});
		~ScriptLayerMovieBindings();
		ScriptLayerMovieBindings(const ScriptLayerMovieBindings&) = delete;
		ScriptLayerMovieBindings& operator=(const ScriptLayerMovieBindings&) = delete;
		// Real stream/audio/clock/receiver and camera owners enclose all deferred First tasks.
		void ConfigureFrameOwners(scene::LayerCameraDrawScope& actual_renderer, media::MoviePlaybackServices services);
		[[nodiscard]] SquirrelValue LayerMovieClass() const;
		[[nodiscard]] scene::MovieLayerState State(const SquirrelValue& instance) const;
		[[nodiscard]] scene::LayerScaleView& ScaleView(const SquirrelValue& instance);
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
