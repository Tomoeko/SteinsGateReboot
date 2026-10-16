#pragma once

#include "sgre/script/squirrel_vm.hpp"

#include <span>

namespace sgre::script {
	// Native template scanner and recovered name-expression compiler/VM path; broader syntax stays explicit.
	[[nodiscard]] SquirrelNativeResult ExpandSquirrelTemplate(SquirrelVm& vm, std::span<const SquirrelValue> arguments);
} // namespace sgre::script
