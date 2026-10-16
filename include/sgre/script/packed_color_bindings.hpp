#pragma once

#include "sgre/script/squirrel_vm.hpp"

namespace sgre::script {
	// Native0455c0 registers stateless packed-color functions, independent of the CRT math owner.
	void RegisterPackedColorFunctions(SquirrelVm& vm);
} // namespace sgre::script
