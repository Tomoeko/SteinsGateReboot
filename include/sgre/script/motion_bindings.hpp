#pragma once

#include "sgre/script/motion_native_owners.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace sgre::script {

	enum class MotionLayerSource {
		Secondary,
		Primary
	};

	struct ScriptMotionBindingServices {
		// Required observational, nonreentrant services. They cannot mutate VM/Layer/runtime ownership or retire this adapter.
		// The actual borrowed Layer and returned runtime remain alive through construction; no callback-created pin is added.
		// Returns the actual already registered runtime for this concrete Layer and native manager selector.
		std::function<scene::MotionManagerRuntime&(const SquirrelValue&, MotionLayerSource)> ResolveRuntime;
		// The actual application state supplies global a97278; true remains explicitly unsupported.
		std::function<bool()> ReadAsynchronousMode;
		// Optional until the genuine containing application publishes the same mutable state as its reader.
		// It is observational toward VM/Layer/runtime ownership and may only update that actual mode byte.
		std::function<void(bool)> WriteAsynchronousMode;
	};

	class ScriptMotionBindings {
	public:
		// VM/Object/Layer, actual runtimes/clock/seed and application services enclose this adapter and every wrapper.
		// Source-only new-route adapter; admission requires coherent validation before installation in the game bootstrap.
		ScriptMotionBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptMotionBindingServices services);
		~ScriptMotionBindings();
		ScriptMotionBindings(const ScriptMotionBindings&) = delete;
		ScriptMotionBindings& operator=(const ScriptMotionBindings&) = delete;
		[[nodiscard]] SquirrelValue MotionClass() const;
		[[nodiscard]] ScriptMotionNativeOwners& NativeOwners();
		// The actual NativeTextureRef adapter and its descriptor/source owners enclose this checked resolver.
		void ConfigureAlternateTextures(const SquirrelValue& actual_texture_class, std::function<const scene::NativeTextureReference&(const SquirrelValue&)> resolve_texture);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
