#include "sgre/script/debug_scene_entry.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::script {
	struct DebugSceneEntryState {
		ScriptDebugSceneEntry Entry;
		bool Consumed = false;
	};

	static SquirrelInstruction EntryInstruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t operand = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
		return SquirrelInstruction{operand, opcode, destination, argument2, argument3};
	}

	static BytecodeProgram PendingEntryFactory() {
		// Wait through ordinary suspend CALLs so the real application continues its
		// normal presentation pump while a different owner reconstructs saved state.
		BytecodeProgram program;
		auto& factory = program.MainFunction;
		factory.Name = std::string("checkpoint entry factory");
		factory.SourceName = std::string("sgre/checkpoint-entry");
		factory.Parameters = {std::string("this"), std::string("ready"), std::string("publish"), std::string("wait")};
		factory.StackSize = 5;
		factory.Instructions = {EntryInstruction(48, 4, 0), EntryInstruction(19, 1, 4)};
		SquirrelFunction hook;
		hook.Name = std::string("checkpoint.Application.getMainScene");
		hook.SourceName = factory.SourceName;
		hook.Parameters = {std::string("this")};
		hook.StackSize = 4;
		for (std::int32_t index = 0; index < 3; ++index) {
			SquirrelOuterBinding binding;
			binding.Type = 0;
			binding.Source = index + 1;
			binding.Name = factory.Parameters[static_cast<std::size_t>(index + 1)];
			hook.OuterBindings.push_back(std::move(binding));
		}
		hook.Instructions = {EntryInstruction(27, 2, 0), EntryInstruction(10, 3, 0), EntryInstruction(6, 1, 2, 3, 1), EntryInstruction(26, 1, 1), EntryInstruction(24, 0, 4), EntryInstruction(27, 2, 2), EntryInstruction(10, 3, 0), EntryInstruction(6, 255, 2, 3, 1), EntryInstruction(24, 0, -9), EntryInstruction(27, 2, 1), EntryInstruction(10, 3, 0), EntryInstruction(6, 1, 2, 3, 1), EntryInstruction(19, 1, 1)};
		factory.Functions.push_back(std::move(hook));

		return program;
	}

	static SquirrelValue MakePendingEntry(SquirrelVm& vm, const SquirrelValue& ready, const SquirrelValue& provider) {
		const auto context = vm.CreateContext();
		try {
			const auto factory = vm.CreateClosureInContext(context, PendingEntryFactory());
			const auto root = vm.RootTable();
			const auto result = vm.InvokeInContext(context, factory, std::array<SquirrelValue, 4>{root, ready, provider, vm.GetSlot(root, std::string("suspend"))}, 256, false);
			if (result.State != SquirrelRunState::Completed || vm.Type(result.Value) != SquirrelValueType::Closure) {
				throw std::runtime_error("original checkpoint pending entry factory did not complete");
			}
			const auto pin = vm.Retain(result.Value);
			vm.ReleaseContext(context);

			return result.Value;
		} catch (...) {
			try {
				vm.ReleaseContext(context);
			} catch (...) {
				// Retain the first setup failure.
			}
			throw;
		}
	}

	void InstallOriginalDebugSceneEntry(SquirrelVm& vm, ScriptDebugSceneEntry entry) {
		constexpr std::size_t kMaximumStorageBytes = 4096;
		if (vm.IsClosed() || entry.Storage.empty() || entry.Storage.size() > kMaximumStorageBytes || entry.Storage.find('\0') != std::string::npos || entry.TargetLine < 0) {
			throw std::runtime_error("original debug scene entry requires its live VM, exact bounded storage and nonnegative source line");
		}

		const auto root = vm.RootTable();
		if (!vm.HasSlot(root, std::string("Application"))) {
			throw std::runtime_error("original debug scene entry precedes the original Application class publication");
		}
		if (vm.HasSlot(root, std::string("MyApplication")) || (vm.HasSlot(root, std::string("app")) && !std::holds_alternative<std::monostate>(vm.GetSlot(root, std::string("app"))))) {
			throw std::runtime_error("original debug scene entry must precede the actual application derivation and construction");
		}

		const auto application = vm.GetSlot(root, std::string("Application"));
		if (vm.Type(application) != SquirrelValueType::Class || !vm.HasSlot(application, std::string("getMainScene")) || vm.Type(vm.GetSlot(application, std::string("getMainScene"))) != SquirrelValueType::Closure) {
			throw std::runtime_error("original debug scene entry requires the original script getMainScene extension hook");
		}

		if (!entry.Checkpoint.empty()) {
			if (entry.CheckpointLocation.Storage != entry.Storage) {
				throw std::runtime_error("original debug checkpoint does not match the requested scene entry");
			}
			static_cast<void>(RestoreOriginalCheckpoint(vm, entry.Checkpoint, entry.CheckpointLocation, entry.CheckpointIdentity));
		}

		const auto pending = static_cast<bool>(entry.CheckpointPoll);
		const auto state = std::make_shared<DebugSceneEntryState>();
		state->Entry = std::move(entry);
		const std::array<SquirrelValue, 2> captures{application, vm.GetSlot(application, std::string("getMainScene"))};
		const auto provider = vm.CreateNativeClosureWithCaptures("debug.Application.getMainScene", [state](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
			const auto& entry = state->Entry;
			const auto application_class = active.NativeStackValue(-1);
			if (arguments.size() != 3 || active.Type(arguments.front()) != SquirrelValueType::Instance || !active.IsInstanceOf(arguments.front(), application_class)) {
				throw std::runtime_error("original debug scene provider requires its actual Application-derived receiver");
			}
			if (state->Consumed) {
				const auto original = active.NativeStackValue(-2);
				const auto result = active.InvokeInContext(active.CurrentContext(), original, arguments.first(1));
				if (result.State != SquirrelRunState::Completed) {
					throw std::runtime_error("original getMainScene did not complete its recovered scene-selection hook");
				}

				return SquirrelNativeResult::ReturnValue(result.Value);
			}
			if (!entry.Checkpoint.empty()) {
				const auto scene = RestoreOriginalCheckpoint(active, entry.Checkpoint, entry.CheckpointLocation, entry.CheckpointIdentity);
				state->Consumed = true;

				return SquirrelNativeResult::ReturnValue(scene);
			}
			if (entry.CheckpointPoll) {
				throw std::runtime_error("original full checkpoint provider was called before its host completion");
			}

			const auto scene = active.CreateTable();
			const auto scene_root = active.Retain(scene);
			active.NewSlot(scene, std::string("storage"), active.CreateString(entry.Storage));
			active.NewSlot(scene, std::string("target"), entry.TargetLine);
			state->Consumed = true;

			return SquirrelNativeResult::ReturnValue(scene);
		},
		    captures);
		const auto provider_pin = vm.Retain(provider);
		if (!pending) {
			vm.NewSlot(application, std::string("getMainScene"), provider);
			return;
		}
		const auto ready = vm.CreateNativeClosureWithCaptures("debug.checkpoint.ready", [state](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
			if (arguments.size() != 2 || active.Type(arguments.front()) != SquirrelValueType::Instance || !active.IsInstanceOf(arguments.front(), active.NativeStackValue(-1))) {
				throw std::runtime_error("original checkpoint wait requires its actual Application-derived receiver");
			}
			auto& entry = state->Entry;
			if (!state->Consumed && entry.Checkpoint.empty()) {
				if (auto bytes = entry.CheckpointPoll()) {
					static_cast<void>(RestoreOriginalCheckpoint(active, *bytes, entry.CheckpointLocation, entry.CheckpointIdentity));
					entry.Checkpoint = std::move(*bytes);
					entry.CheckpointPoll = {};
				}
			}

			return SquirrelNativeResult::ReturnValue(state->Consumed || !entry.Checkpoint.empty());
		},
		    std::array<SquirrelValue, 1>{application});
		const auto ready_pin = vm.Retain(ready);
		const auto hook = MakePendingEntry(vm, ready, provider);
		const auto hook_pin = vm.Retain(hook);
		vm.NewSlot(application, std::string("getMainScene"), hook);
	}

} // namespace sgre::script
