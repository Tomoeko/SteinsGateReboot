#include "sgre/script/original_calendar_restore.hpp"

#include <array>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::script {

	static SquirrelInstruction CalendarInstruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t operand = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
		SquirrelInstruction result;
		result.Opcode = opcode;
		result.Argument0 = destination;
		result.Argument1 = operand;
		result.Argument2 = argument2;
		result.Argument3 = argument3;

		return result;
	}

	static BytecodeProgram CalendarHookFactory() {
		BytecodeProgram program;
		auto& factory = program.MainFunction;
		factory.Name = std::string("calendar hook factory");
		factory.SourceName = std::string("sgre/original-calendar-restore");
		factory.Parameters = {std::string("this"), std::string("original"), std::string("predicate"), std::string("command"), std::string("finish"), std::string("failure")};
		factory.StackSize = 7;
		factory.Instructions = {CalendarInstruction(48, 6), CalendarInstruction(19, 1, 6)};
		SquirrelFunction hook;
		hook.Name = std::string("calendar.ScenePlayer.onLoadScene");
		hook.SourceName = factory.SourceName;
		hook.Parameters = {std::string("this"), std::string("type"), std::string("current"), std::string("previous")};
		hook.StackSize = 12;
		hook.Literals = {std::string("tag_calendar")};
		for (std::int32_t index = 0; index < 5; ++index) {
			SquirrelOuterBinding binding;
			binding.Type = 0;
			binding.Source = index + 1;
			binding.Name = factory.Parameters[static_cast<std::size_t>(index + 1)];
			hook.OuterBindings.push_back(std::move(binding));
		}
		// Preserve the original result across ordinary CALLs. Any legitimate yield
		// remains in this same coroutine; catch rethrows the exact original value.
		hook.Instructions = {
		    CalendarInstruction(56, 11, 21),
		    CalendarInstruction(27, 5, 0),
		    CalendarInstruction(10, 6, 0),
		    CalendarInstruction(10, 7, 1),
		    CalendarInstruction(10, 8, 2),
		    CalendarInstruction(10, 9, 3),
		    CalendarInstruction(6, 4, 5, 6, 4),
		    CalendarInstruction(27, 5, 1),
		    CalendarInstruction(10, 6, 0),
		    CalendarInstruction(10, 7, 1),
		    CalendarInstruction(10, 8, 2),
		    CalendarInstruction(10, 9, 3),
		    CalendarInstruction(6, 10, 5, 6, 4),
		    CalendarInstruction(26, 10, 6),
		    CalendarInstruction(8, 5, 0, 0, 6),
		    CalendarInstruction(27, 7, 2),
		    CalendarInstruction(6, 255, 5, 6, 2),
		    CalendarInstruction(27, 5, 3),
		    CalendarInstruction(10, 6, 0),
		    CalendarInstruction(6, 255, 5, 6, 1),
		    CalendarInstruction(57, 1),
		    CalendarInstruction(19, 1, 4),
		    CalendarInstruction(27, 5, 4),
		    CalendarInstruction(10, 6, 0),
		    CalendarInstruction(10, 7, 11),
		    CalendarInstruction(6, 255, 5, 6, 2),
		    CalendarInstruction(58, 11)};
		factory.Functions.push_back(std::move(hook));

		return program;
	}

	class OriginalCalendarRestore::Implementation {
	public:
		Implementation(SquirrelVm& vm, scenario::ScenarioCalendarCommand command) : Vm(vm), Command(std::move(command)) {
			if (Vm.IsClosed() || Command.Storage.empty() || Command.Storage.size() > 4096 || Command.Storage.find('\0') != std::string::npos || Command.SceneLabel.empty() || Command.SceneLabel.front() != '*' || Command.SceneLabel.size() > 4096 || Command.SceneLabel.find('\0') != std::string::npos || Command.CommandIndex >= Command.SnapshotIndex) {
				throw std::runtime_error("original calendar restoration requires a live VM and owned validated scene/command coordinates");
			}
			std::array<bool, 3> present{};
			for (const auto& [key, value] : Command.Attributes) {
				const auto index = key == "mon" ? 0U : key == "day" ? 1U
				    : key == "week"                                 ? 2U
				                                                    : 3U;
				if (index == 3 || present[index] || value.empty() || value.size() > 4096 || value.find('\0') != std::string::npos) {
					throw std::runtime_error("original calendar restoration requires its three exact owned original attribute pairs");
				}
				present[index] = true;
			}
			SceneName = Command.Storage + Command.SceneLabel;
		}

		[[nodiscard]] bool Match(SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
			if (State.Phase != OriginalCalendarRestorePhase::Armed) {
				return false;
			}
			const auto* type = std::get_if<std::int32_t>(&arguments[1]);
			const auto* current = TryStringText(arguments[2]);
			if (type == nullptr || *type != 0 || current == nullptr || *current != SceneName || !vm.NativeCallerMatches("advengine/script/envplayer.nut", "main", 157)) {
				return false;
			}
			const auto root = vm.RootTable();
			const auto derived = vm.FindRawSlot(root, SquirrelString("MyPlayer"));
			const auto application = vm.FindRawSlot(root, SquirrelString("app"));
			if (!derived || vm.Type(*derived) != SquirrelValueType::Class || !vm.IsInstanceOf(arguments.front(), *derived) || !application || vm.Type(*application) != SquirrelValueType::Instance || !vm.HasSlot(*application, std::string("player")) || vm.GetSlot(*application, std::string("player")) != arguments.front()) {
				return false;
			}
			const auto flags = vm.GetSlot(arguments.front(), std::string("flags"));
			if (vm.Type(flags) != SquirrelValueType::Table) {
				throw std::runtime_error("original calendar restoration requires the actual ScenePlayer flags table");
			}
			for (const auto* key : {"subwin_month", "subwin_day", "subwin_week"}) {
				if (vm.FindRawSlot(flags, SquirrelString(key))) {
					State.Phase = OriginalCalendarRestorePhase::Preserved;
					State.Reason = "existing complete or partial original calendar flags preserved";
					return false;
				}
			}
			Receiver = vm.BorrowObjectIdentity(arguments.front());
			State.Phase = OriginalCalendarRestorePhase::Applying;
			State.Reason = "ordinary original calendar command is executing";

			return true;
		}

		SquirrelVm& Vm;
		scenario::ScenarioCalendarCommand Command;
		std::string SceneName;
		OriginalCalendarRestoreObservation State;
		std::optional<SquirrelObjectIdentity> Receiver;
	};

	OriginalCalendarRestore::OriginalCalendarRestore(SquirrelVm& vm, scenario::ScenarioCalendarCommand command) : m_Implementation(std::make_shared<Implementation>(vm, std::move(command))) {
	}

	OriginalCalendarRestore::~OriginalCalendarRestore() {
		Cancel();
	}

	void OriginalCalendarRestore::InstallAvailableHook() {
		auto& owner = *m_Implementation;
		if (owner.State.Phase != OriginalCalendarRestorePhase::Installing) {
			return;
		}
		auto& vm = owner.Vm;
		if (vm.IsClosed()) {
			throw std::runtime_error("original calendar restoration cannot install into a closed VM");
		}
		const auto root = vm.RootTable();
		if (vm.HasSlot(root, std::string("MyPlayer"))) {
			throw std::runtime_error("original calendar restoration must precede MyPlayer derivation");
		}
		if (!vm.HasSlot(root, std::string("ScenePlayer"))) {
			return;
		}
		const auto player = vm.GetSlot(root, std::string("ScenePlayer"));
		if (vm.Type(player) != SquirrelValueType::Class || !vm.HasSlot(player, std::string("onLoadScene"))) {
			throw std::runtime_error("original calendar restoration requires its original ScenePlayer extension hook");
		}
		const auto original = vm.GetSlot(player, std::string("onLoadScene"));
		if (vm.Type(original) != SquirrelValueType::Closure) {
			throw std::runtime_error("original calendar restoration requires its original script onLoadScene closure");
		}
		const std::weak_ptr<Implementation> weak = m_Implementation;
		const std::array<SquirrelValue, 1> captures{player};
		const auto predicate = vm.CreateNativeClosureWithCaptures("calendar.restorePredicate", [weak](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
			if (arguments.size() != 5 || !active.IsInstanceOf(arguments.front(), active.NativeStackValue(-1))) {
				throw std::runtime_error("original calendar hook requires its actual ScenePlayer receiver and four original arguments");
			}
			const auto state = weak.lock();

			return SquirrelNativeResult::ReturnValue(state && state->Match(active, arguments.first(4)));
		},
		    captures);
		const auto finish = vm.CreateNativeClosureWithCaptures("calendar.restoreFinished", [weak](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
			if (arguments.size() != 2 || !active.IsInstanceOf(arguments.front(), active.NativeStackValue(-1))) {
				throw std::runtime_error("original calendar completion requires its actual ScenePlayer receiver");
			}
			if (const auto state = weak.lock(); state && state->State.Phase == OriginalCalendarRestorePhase::Applying) {
				if (!state->Receiver || *state->Receiver != active.BorrowObjectIdentity(arguments.front())) {
					throw std::runtime_error("original calendar completion changed its actual receiver");
				}
				state->State.Phase = OriginalCalendarRestorePhase::Applied;
				state->State.Reason = "actual original tag_calendar completed before snapshot restoration";
			}

			return SquirrelNativeResult::NoValues();
		},
		    captures);
		const auto failure = vm.CreateNativeClosure("calendar.restoreFailed", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			if (const auto state = weak.lock(); state && state->State.Phase == OriginalCalendarRestorePhase::Applying) {
				state->State.Phase = OriginalCalendarRestorePhase::Failed;
				try {
					state->State.Reason = "original calendar command failed";
					if (arguments.size() == 2) {
						if (const auto* text = TryStringText(arguments[1])) {
							state->State.Reason.append(": ").append(text->substr(0, 1024));
						}
					}
				} catch (...) {
					// Preserve the exact thrown script value even if diagnostic allocation fails.
				}
			}

			return SquirrelNativeResult::NoValues();
		});
		const auto command = vm.CreateTable();
		const auto command_root = vm.Retain(command);
		for (const auto& [key, value] : owner.Command.Attributes) {
			vm.NewSlot(command, SquirrelString(key), SquirrelString(value));
		}
		const auto context = vm.CreateContext();
		try {
			const auto factory = vm.CreateClosureInContext(context, CalendarHookFactory());
			const std::array<SquirrelValue, 6> arguments{root, original, predicate, command, finish, failure};
			const auto result = vm.InvokeInContext(context, factory, arguments, 256, false);
			if (result.State != SquirrelRunState::Completed || vm.Type(result.Value) != SquirrelValueType::Closure) {
				throw std::runtime_error("original calendar factory did not return its captured script closure");
			}
			const auto retained = vm.Retain(result.Value);
			vm.NewSlot(player, std::string("onLoadScene"), result.Value);
			vm.ReleaseContext(context);
		} catch (...) {
			try {
				vm.ReleaseContext(context);
			} catch (...) {
				// Preserve the first factory or class-publication failure.
			}
			throw;
		}
		owner.State.Phase = OriginalCalendarRestorePhase::Armed;
		owner.State.Reason = "awaiting the exact original cold-jump onLoadScene boundary";
	}

	void OriginalCalendarRestore::Cancel() noexcept {
		auto& owner = *m_Implementation;
		if (owner.State.Phase == OriginalCalendarRestorePhase::Installing || owner.State.Phase == OriginalCalendarRestorePhase::Armed || owner.State.Phase == OriginalCalendarRestorePhase::Applying) {
			owner.State.Phase = OriginalCalendarRestorePhase::Cancelled;
		}
	}

	const OriginalCalendarRestoreObservation& OriginalCalendarRestore::Observation() const noexcept {
		return m_Implementation->State;
	}

} // namespace sgre::script
