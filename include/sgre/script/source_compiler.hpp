#pragma once

#include "sgre/script/squirrel_bytecode.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <span>
#include <string_view>

namespace sgre::script {
	// The recovered domain emits real VM prototypes; unsupported grammar fails with source context.
	[[nodiscard]] BytecodeProgram CompileSquirrelSource(SquirrelVm& actual_vm, std::string_view source, std::string_view source_name = "unnamedbuffer");
	[[nodiscard]] SquirrelNativeResult CompileSquirrelString(SquirrelVm& actual_vm, std::span<const SquirrelValue> arguments);
} // namespace sgre::script
