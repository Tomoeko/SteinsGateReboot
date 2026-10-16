#pragma once

#include "sgre/script/native_regexp.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <cstddef>

namespace sgre::script {
	// Native 2663f0 root registration. Closures and genuine instance release hooks own the registry;
	// its instance identities do not root receivers, so collection releases compiled programs normally.
	[[nodiscard]] SquirrelValue InstallRecoveredRegexpLibrary(SquirrelVm& vm, NativeRegexpLimits limits = {}, std::size_t maximum_instances = 1024);
} // namespace sgre::script
