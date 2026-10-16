#pragma once

#include "sgre/script/squirrel_vm.hpp"

#include <optional>
#include <string_view>

namespace sgre::script {
	// Compile the recovered unqualified-name expression domain into ordinary VM bytecode.
	// Other syntax stays unsupported; this is not the full Squirrel source compiler.
	[[nodiscard]] std::optional<BytecodeProgram> TryCompileSquirrelNameExpression(SquirrelVm& actual_vm, std::string_view expression);
} // namespace sgre::script
