#pragma once

#include "sgre/scene/capture_task.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace sgre::script {
	class ScriptLayerBindings;
	class ScriptRawImageBindings;
	struct ScriptCaptureLimits {
		std::size_t MaxInstances = 1024;
		scene::CaptureTaskLimits Tasks;
	};

	class ScriptCaptureBindings {
	public:
		using DrawCapture = std::function<void(const std::shared_ptr<scene::CaptureTask>&)>;
		// Synchronous menu-only operation. Actual submitted capture texture -> tight top-to-bottom BGRA.
		using ReadThumbnail = std::function<std::vector<std::uint8_t>(scene::MutableTextureOwner&, std::uint32_t, std::uint32_t)>;
		// Actual VM/Object/Layer services enclose wrappers and deferred CommonFirst physical tasks.
		ScriptCaptureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptCaptureLimits limits = {});
		~ScriptCaptureBindings();
		ScriptCaptureBindings(const ScriptCaptureBindings&) = delete;
		ScriptCaptureBindings& operator=(const ScriptCaptureBindings&) = delete;
		void ConfigureFrameOwners(scene::MutableTextureBackend& actual_backend, DrawCapture actual_drawing);
		void ConfigureThumbnailReadback(ScriptRawImageBindings& actual_images, ReadThumbnail actual_readback);
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
