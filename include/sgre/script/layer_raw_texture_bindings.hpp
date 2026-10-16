#pragma once

#include "sgre/scene/layer_scale_view.hpp"
#include "sgre/scene/layers.hpp"
#include "sgre/scene/raw_texture_task.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace sgre::script {

	class ScriptLayerBindings;
	class ScriptMathBindings;
	class ScriptPsbBindings;
	class ScriptRawImageBindings;

	struct ScriptLayerRawTextureLimits {
		std::size_t MaxInstances = 1024;
		std::size_t MaxShadowBytes = 64 * 1024 * 1024;
		std::size_t MaxTasksPerGroup = 64;
		std::uint64_t OnProcessInstructionBudget = 1000000;
	};

	class ScriptLayerRawTextureBindings {
	public:
		// VM/Object/Layer/image/PSB/shared RNG/backend services remain live through Close and parent task disposal.
		ScriptLayerRawTextureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptRawImageBindings& images, ScriptPsbBindings& psb, ScriptMathBindings& math, scene::LayerTree& tree, scene::MutableTextureBackend& backend, ScriptLayerRawTextureLimits limits = {});
		~ScriptLayerRawTextureBindings();
		ScriptLayerRawTextureBindings(const ScriptLayerRawTextureBindings&) = delete;
		ScriptLayerRawTextureBindings& operator=(const ScriptLayerRawTextureBindings&) = delete;
		ScriptLayerRawTextureBindings(ScriptLayerRawTextureBindings&&) = delete;
		ScriptLayerRawTextureBindings& operator=(ScriptLayerRawTextureBindings&&) = delete;

		[[nodiscard]] SquirrelValue LayerRawTextureClass() const;
		// Borrowed task expires on actual parent disposal; the script wrapper alone does not retain it.
		[[nodiscard]] scene::RawTextureTask& Task(const SquirrelValue& instance);
		[[nodiscard]] scene::LayerScaleView& ScaleView(const SquirrelValue& instance);
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
