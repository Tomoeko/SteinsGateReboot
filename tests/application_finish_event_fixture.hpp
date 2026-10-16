#pragma once

#include "sgre/core/application_event_control.hpp"
#include "sgre/io/file_reader.hpp"
#include "sgre/script/application_finish_bindings.hpp"
#include "sgre/script/squirrel_bytecode.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void RequireFinish(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <typename Operation>
static void RequireFinishError(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		RequireFinish(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("missing expected finish transport rejection");
}

static void RegisterFinish(sgre::script::SquirrelVm& vm, const sgre::script::SquirrelValue& callback) {
	const auto entry = vm.GetSlot(vm.RootTable(), std::string("entryRequestFinishCallback"));
	const std::array<sgre::script::SquirrelValue, 2> arguments{vm.RootTable(), callback};
	RequireFinish(vm.Invoke(entry, arguments).State == sgre::script::SquirrelRunState::Completed, "finish registration must complete");
}

static sgre::core::ApplicationEventCallbackOperations FinishOperations(sgre::core::ApplicationEventControl& events, sgre::script::ScriptApplicationFinishBindings& finish) {
	sgre::core::ApplicationEventCallbackOperations operations;
	operations.Suspend = [] {
		throw std::runtime_error("fixture registered an unsupported suspend callback");
	};
	operations.Resume = [] {
		throw std::runtime_error("fixture registered an unsupported resume callback");
	};
	operations.Resize = [](auto) {
		throw std::runtime_error("fixture registered an unsupported resize callback");
	};
	operations.Close = [&events, &finish] {
		return finish.EvaluateCloseRequest(events.State().CloseCode);
	};

	return operations;
}

static sgre::script::SquirrelValue InstallOriginalFinish(sgre::script::SquirrelVm& vm, const std::filesystem::path& filename) {
	sgre::io::FileReader input(filename);
	RequireFinish(input.Size() > 0 && input.Size() <= 8 * 1024 * 1024, "original override bytecode exceeds bounded fixture input");
	auto program = sgre::script::ReadSquirrelBytecode(input.Read(0, static_cast<std::size_t>(input.Size())));
	RequireFinish(!program.MainFunction.Functions.empty(), "original override has no finish prototype");
	auto& function = program.MainFunction.Functions[0];
	RequireFinish(function.Parameters.size() == 2 && function.Parameters[1] == sgre::script::SquirrelLiteral(std::string("reason")) && function.Instructions.size() == 29 && function.Functions.size() == 1 && function.OuterBindings.empty(), "original finish prototype differs from the pinned original control receipt");
	RequireFinish(function.Literals.size() == 5 && function.Literals[0] == sgre::script::SquirrelLiteral(std::string("app")) && function.Literals[4] == sgre::script::SquirrelLiteral(std::string("sync")), "original finish prototype identities differ");
	sgre::script::BytecodeProgram selected;
	selected.MainFunction = std::move(function);
	// Original PCs7..11 compare app with null before reading cmdTitleState. Keep that genuine short-circuit branch.
	vm.NewSlot(vm.RootTable(), std::string("app"), {});
	const auto previous = vm.CreateNativeClosure("unexecuted prepared sync", [](auto&, auto) -> sgre::script::SquirrelNativeResult {
		throw std::runtime_error("finish transport must not execute the confirmation continuation");
	});
	vm.NewSlot(vm.RootTable(), std::string("sync"), previous);
	RegisterFinish(vm, vm.CreateClosure(std::move(selected)));

	return previous;
}
