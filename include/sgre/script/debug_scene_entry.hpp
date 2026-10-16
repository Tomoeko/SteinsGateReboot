#pragma once

#include "sgre/script/squirrel_vm.hpp"

#include <cstdint>
#include <string>

namespace sgre::script {

	struct ScriptDebugSceneEntry {
		std::string Storage;
		std::int32_t TargetLine = 0;
	};

	// Explicit rewritten-host debug instrumentation, never installed by normal startup.
	// Install after the original Application class is published and before MyApplication derives
	// or any Application instance locks the class. The VM owns the installed provider and its entry.
	// Original main consumes its fresh {storage, target} through the existing getMainScene hook;
	// System.getDebugBuild and original bootstrap/developer branches remain unchanged.
	void InstallOriginalDebugSceneEntry(SquirrelVm& vm, ScriptDebugSceneEntry entry);

} // namespace sgre::script
