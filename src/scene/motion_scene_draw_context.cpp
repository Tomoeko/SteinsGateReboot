#include "sgre/scene/motion_scene_draw_context.hpp"

#include "sgre/scene/motion_scene_frame_commands.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <vector>

namespace sgre::scene {
	class MotionSceneDrawContext::Implementation {
	public:
		Implementation(const MotionManagerRuntime& runtime, std::size_t maximum_scenes, std::size_t maximum_rows)
		    : Runtime(runtime), MaximumScenes(maximum_scenes), Commands(maximum_rows) {
			if (maximum_scenes == 0 || maximum_scenes > 65536) {
				throw std::runtime_error("motion scene context requires bounded actual task occurrence storage");
			}
			Scenes.reserve(maximum_scenes);
		}

		const MotionManagerRuntime& Runtime;
		std::size_t MaximumScenes;
		std::vector<const MotionSceneEvaluation*> Scenes;
		MotionSceneFrameCommands Commands;
		bool Operating = false;
		bool Prepared = false;
	};

	MotionSceneDrawContext::MotionSceneDrawContext(const MotionManagerRuntime& actual_runtime, std::size_t maximum_scenes, std::size_t maximum_rows)
	    : m_Implementation(std::make_unique<Implementation>(actual_runtime, maximum_scenes, maximum_rows)) {
	}

	MotionSceneDrawContext::~MotionSceneDrawContext() {
		if (!CanClear()) {
			std::terminate();
		}
	}

	void MotionSceneDrawContext::Append(const MotionSceneEvaluation& scene) {
		auto& state = *m_Implementation;
		if (state.Operating || state.Prepared || state.Scenes.size() == state.MaximumScenes || IsBorrowing(scene) || !scene.UsesRuntime(state.Runtime)) {
			throw std::runtime_error("motion scene context requires a distinct actual task Draw and consumption of its previous publication");
		}
		static_cast<void>(scene.Packets());
		state.Scenes.push_back(&scene);
	}

	bool MotionSceneDrawContext::IsBorrowing(const MotionSceneEvaluation& scene) const noexcept {
		const auto& scenes = m_Implementation->Scenes;
		return std::find(scenes.begin(), scenes.end(), &scene) != scenes.end();
	}

	std::size_t MotionSceneDrawContext::Size() const noexcept {
		return m_Implementation->Scenes.size();
	}

	bool MotionSceneDrawContext::CanClear() const noexcept {
		return !m_Implementation->Operating;
	}

	MotionPreparedFrame MotionSceneDrawContext::Prepare(const MotionPreparedFrame& globals) {
		auto& state = *m_Implementation;
		if (state.Operating || state.Prepared) {
			throw std::runtime_error("motion scene context preparation requires an unconsumed actual queue");
		}
		state.Operating = true;
		try {
			auto prepared = state.Commands.Prepare(state.Scenes, globals);
			state.Prepared = true;
			state.Operating = false;
			return prepared;
		} catch (...) {
			state.Operating = false;
			throw;
		}
	}

	void MotionSceneDrawContext::Clear() {
		auto& state = *m_Implementation;
		if (!CanClear()) {
			throw std::runtime_error("motion scene context cannot retire during preparation");
		}
		state.Commands.Clear();
		state.Scenes.clear();
		state.Prepared = false;
	}
} // namespace sgre::scene
