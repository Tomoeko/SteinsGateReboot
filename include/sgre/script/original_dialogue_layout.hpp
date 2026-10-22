#pragma once

#include "sgre/script/squirrel_vm.hpp"

#include <memory>

namespace sgre::script {
	class ScriptTextRenderBindings;

	class OriginalDialogueLayout {
	public:
		// User-authored improvement. The VM and actual text bindings outlive this policy.
		OriginalDialogueLayout(SquirrelVm& vm, ScriptTextRenderBindings& text);
		~OriginalDialogueLayout();
		OriginalDialogueLayout(const OriginalDialogueLayout&) = delete;
		OriginalDialogueLayout& operator=(const OriginalDialogueLayout&) = delete;
		// Install before BasicRender is instantiated, at the original module-publication boundary.
		void InstallAvailableHook();

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
