#pragma once

#include "sgre/scene/screens.hpp"
#include "sgre/script/layer_bindings.hpp"

#include <memory>

namespace sgre::script {

	class ScriptScreenBindings {
	public:
		// The VM, Object/Layer adapters, and registry outlive this owner.
		ScriptScreenBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, scene::ScreenRegistry& screens);
		~ScriptScreenBindings();
		ScriptScreenBindings(const ScriptScreenBindings&) = delete;
		ScriptScreenBindings& operator=(const ScriptScreenBindings&) = delete;
		ScriptScreenBindings(ScriptScreenBindings&&) = delete;
		ScriptScreenBindings& operator=(ScriptScreenBindings&&) = delete;

		[[nodiscard]] SquirrelValue ScreenClass() const;
		[[nodiscard]] scene::ScreenHandle Handle(const SquirrelValue& instance) const;
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
