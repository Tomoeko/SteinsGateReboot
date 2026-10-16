#pragma once

#include "sgre/scene/capture_task.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <functional>
#include <memory>

namespace sgre::script {
	class ScriptLayerBindings;
	struct ScriptCaptureLimits {
		std::size_t MaxInstances = 1024;
		scene::CaptureTaskLimits Tasks;
	};

	class ScriptCaptureBindings {
	public:
		using DrawCapture = std::function<void(const std::shared_ptr<scene::CaptureTask>&)>;
		// Actual VM/Object/Layer services enclose wrappers and deferred CommonFirst physical tasks.
		ScriptCaptureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptCaptureLimits limits = {});
		~ScriptCaptureBindings();
		ScriptCaptureBindings(const ScriptCaptureBindings&) = delete;
		ScriptCaptureBindings& operator=(const ScriptCaptureBindings&) = delete;
		void ConfigureFrameOwners(scene::MutableTextureBackend& actual_backend, DrawCapture actual_drawing);
		[[nodiscard]] SquirrelValue CaptureClass() const;
		// Actual Captured wrappers retain the source VM object; this returns its registered physical task.
		[[nodiscard]] std::shared_ptr<scene::CaptureTask> BorrowTask(const SquirrelValue& instance) const;
		[[nodiscard]] scene::CaptureTaskState State(const SquirrelValue& instance) const;
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
