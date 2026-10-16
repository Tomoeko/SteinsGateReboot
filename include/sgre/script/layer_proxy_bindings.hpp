#pragma once

#include "sgre/scene/layer_manager_frame.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace sgre::script {

	class ScriptLayerBindings;

	struct ScriptLayerProxyDrawServices {
		// Observes the actual target process. Neither service may destroy or reenter the borrowed target.
		std::function<scene::LayerFrameProcessState(scene::LayerHandle)> State;
		// Dispatches the target's concrete native draw, including its own camera, without updating it.
		std::function<void(scene::LayerHandle, std::uint32_t)> Draw;
	};

	struct ScriptLayerProxyLimits {
		std::size_t MaxInstances = 1024;
	};

	class ScriptLayerProxyBindings {
	public:
		// VM, Object/Layer/tree and actual draw services enclose every Proxy and its camera task through VM Close.
		// Configure the ordinary Layer frame owners first, or attach each genuine BaseFrame before drawing.
		ScriptLayerProxyBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::LayerTree& tree, ScriptLayerProxyDrawServices services, ScriptLayerProxyLimits limits = {});
		~ScriptLayerProxyBindings();
		ScriptLayerProxyBindings(const ScriptLayerProxyBindings&) = delete;
		ScriptLayerProxyBindings& operator=(const ScriptLayerProxyBindings&) = delete;
		ScriptLayerProxyBindings(ScriptLayerProxyBindings&&) = delete;
		ScriptLayerProxyBindings& operator=(ScriptLayerProxyBindings&&) = delete;

		[[nodiscard]] SquirrelValue LayerProxyClass() const;
		[[nodiscard]] bool Owns(scene::LayerHandle handle) const;
		// Host virtual dispatch calls this before the ordinary Base draw branch. Update remains genuine Base.Update.
		void Draw(scene::LayerHandle handle, std::uint32_t argument);
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
