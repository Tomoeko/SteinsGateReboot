#pragma once

#include "sgre/scene/captured_task.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <memory>

namespace sgre::core {
	class FrameClock;
}

namespace sgre::script {
	class ScriptLayerBindings;
	class ScriptCaptureBindings;
	class ScriptImageBindings;
	class ScriptCapturedBindings {
	public:
		// Actual VM/Object/Layer/Capture and frame services enclose all deferred physical consumers.
		ScriptCapturedBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptCaptureBindings& captures, scene::CapturedTask::DrawTexture actual_drawing, std::size_t max_instances = 1024);
		~ScriptCapturedBindings();
		ScriptCapturedBindings(const ScriptCapturedBindings&) = delete;
		ScriptCapturedBindings& operator=(const ScriptCapturedBindings&) = delete;
		void ConfigureAnimationClock(const core::FrameClock& actual_clock);
		void ConfigureOpacityMaps(ScriptImageBindings& actual_images, scene::CapturedTask::DrawOpacityMap actual_drawing);
		[[nodiscard]] SquirrelValue CapturedClass() const;
		[[nodiscard]] scene::CapturedTaskState State(const SquirrelValue& instance) const;
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
