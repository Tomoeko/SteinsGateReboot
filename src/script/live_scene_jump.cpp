#include "sgre/script/live_scene_jump.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace sgre::script {

	static SquirrelInstruction JumpInstruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t operand = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
		return SquirrelInstruction{operand, opcode, destination, argument2, argument3};
	}

	static SquirrelValue MakeLiveJumpHook(SquirrelVm& vm, const SquirrelValue& original, const SquirrelValue& take, bool title) {
		BytecodeProgram program;
		auto& factory = program.MainFunction;
		factory.Name = std::string("live jump factory");
		factory.SourceName = std::string("sgre/live-scene-jump");
		factory.Parameters = {std::string("this"), std::string("original"), std::string("take")};
		factory.StackSize = 4;
		factory.Instructions = {JumpInstruction(48, 3, 0), JumpInstruction(19, 1, 3)};
		SquirrelFunction hook;
		hook.Name = std::string(title ? "live.TitleMenuFunction.checkInputAction" : "live.ScenePlayer.checkInputAction");
		hook.SourceName = factory.SourceName;
		hook.Parameters = {std::string("this"), std::string("action")};
		hook.StackSize = 7;
		hook.Literals = {std::string("exitGame"), std::string("restart")};
		if (title) {
			hook.Literals = {std::string("onProcess")};
		}
		for (std::int32_t index = 0; index < 2; ++index) {
			SquirrelOuterBinding binding;
			binding.Type = 0;
			binding.Source = index + 1;
			binding.Name = factory.Parameters[static_cast<std::size_t>(index + 1)];
			hook.OuterBindings.push_back(std::move(binding));
		}
		// Ordinary script CALLs let the original GameStateException unwind through
		// the real application's traps, without crossing an OS callback or VM call fence.
		hook.Instructions = {JumpInstruction(27, 2, 1), JumpInstruction(10, 3, 0), JumpInstruction(6, 6, 2, 3, 1), JumpInstruction(26, 6, 4), JumpInstruction(8, 2, 0, 0, 3), JumpInstruction(1, 4, 1), JumpInstruction(10, 5, 6), JumpInstruction(6, 255, 2, 3, 3), JumpInstruction(27, 2, 0), JumpInstruction(10, 3, 0), JumpInstruction(10, 4, 1), JumpInstruction(6, 2, 2, 3, 2), JumpInstruction(19, 1, 2)};
		if (title) {
			// Original titleLoop accepts a complete table returned by the panel.
			// Let the original panel close/retire through onProcess, without selecting New Game.
			hook.Instructions = {JumpInstruction(27, 2, 1), JumpInstruction(10, 3, 0), JumpInstruction(6, 6, 2, 3, 1), JumpInstruction(26, 6, 5), JumpInstruction(8, 2, 0, 0, 3), JumpInstruction(10, 4, 6), JumpInstruction(6, 255, 2, 3, 2), JumpInstruction(22, 2, 0), JumpInstruction(19, 1, 2), JumpInstruction(27, 2, 0), JumpInstruction(10, 3, 0), JumpInstruction(10, 4, 1), JumpInstruction(6, 2, 2, 3, 2), JumpInstruction(19, 1, 2)};
		}
		factory.Functions.push_back(std::move(hook));
		const auto context = vm.CreateContext();
		try {
			const auto closure = vm.CreateClosureInContext(context, program);
			const auto result = vm.InvokeInContext(context, closure, std::array<SquirrelValue, 3>{vm.RootTable(), original, take}, 256, false);
			if (result.State != SquirrelRunState::Completed || vm.Type(result.Value) != SquirrelValueType::Closure) {
				throw std::runtime_error("live jump hook factory did not publish its script closure");
			}
			const auto pin = vm.Retain(result.Value);
			vm.ReleaseContext(context);

			return result.Value;
		} catch (...) {
			try {
				vm.ReleaseContext(context);
			} catch (...) {
				// Preserve the first factory failure.
			}
			throw;
		}
	}

	class LiveSceneJump::Implementation {
	public:
		explicit Implementation(SquirrelVm& vm) : Vm(vm) {
		}

		[[nodiscard]] std::optional<SquirrelValue> Player() const {
			if (Vm.IsClosed() || !Vm.HasSlot(Vm.RootTable(), std::string("app"))) {
				return {};
			}
			const auto application = Vm.GetSlot(Vm.RootTable(), std::string("app"));
			if (Vm.Type(application) != SquirrelValueType::Instance || !Vm.HasSlot(application, std::string("player"))) {
				return {};
			}
			const auto player = Vm.GetSlot(application, std::string("player"));
			if (!Installed || Vm.Type(player) != SquirrelValueType::Instance || !Vm.IsInstanceOf(player, Vm.GetSlot(Vm.RootTable(), std::string("ScenePlayer")))) {
				return {};
			}

			return player;
		}

		[[nodiscard]] bool TitleAvailable() const {
			if (!TitleInstalled || Vm.IsClosed() || !Vm.HasSlot(Vm.RootTable(), std::string("TitleMenuFunction")) || !Vm.HasSlot(Vm.RootTable(), std::string("app"))) {
				return false;
			}
			const auto app = Vm.GetSlot(Vm.RootTable(), std::string("app"));

			return Vm.Type(app) == SquirrelValueType::Instance && Vm.HasSlot(app, std::string("player")) && Vm.Type(Vm.GetSlot(app, std::string("player"))) == SquirrelValueType::Null;
		}

		[[nodiscard]] SquirrelValue Take(const SquirrelValue& receiver, bool title) {
			if (!Queued) {
				return {};
			}
			const auto current = title ? std::optional<SquirrelValue>{} : Player();
			const bool admitted = title ? TitleAvailable() && Vm.IsInstanceOf(receiver, Vm.GetSlot(Vm.RootTable(), std::string("TitleMenuFunction"))) : current && Vm.BorrowObjectIdentity(*current) == Vm.BorrowObjectIdentity(receiver);
			if (!admitted) {
				return {};
			}
			if (OnConsumed) {
				const auto consumed = std::move(OnConsumed);
				consumed();
			}
			Queued = false;

			return Data.Value();
		}

		SquirrelVm& Vm;
		bool Installed = false;
		bool TitleInstalled = false;
		bool Queued = false;
		SquirrelHostRoot Data;
		std::function<void()> OnConsumed;
	};

	LiveSceneJump::LiveSceneJump(SquirrelVm& vm) : m_Implementation(std::make_shared<Implementation>(vm)) {
	}

	LiveSceneJump::~LiveSceneJump() = default;

	void LiveSceneJump::InstallAvailableHooks() {
		auto& state = *m_Implementation;
		if (state.Vm.IsClosed()) {
			return;
		}
		auto& vm = state.Vm;
		for (const bool title : {false, true}) {
			bool& installed = title ? state.TitleInstalled : state.Installed;
			const auto name = title ? "SaveLoadModeFunction" : "ScenePlayer";
			if (installed || !vm.HasSlot(vm.RootTable(), std::string(name))) {
				continue;
			}
			if (vm.HasSlot(vm.RootTable(), std::string(title ? "TitleMenuFunction" : "MyPlayer"))) {
				throw std::runtime_error("live jump hook must precede original player/menu derivation");
			}
			const auto base = vm.GetSlot(vm.RootTable(), std::string(name));
			const std::weak_ptr<Implementation> weak = m_Implementation;
			const auto take = vm.CreateNativeClosureWithCaptures("live.takeQueuedScene", [weak, title](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 2 || !active.IsInstanceOf(arguments.front(), active.NativeStackValue(-1))) {
					throw std::runtime_error("live jump requires its actual original player/menu receiver");
				}
				const auto owner = weak.lock();

				return SquirrelNativeResult::ReturnValue(owner ? owner->Take(arguments.front(), title) : SquirrelValue{});
			},
			    std::array<SquirrelValue, 1>{base});
			const auto take_root = vm.Retain(take);
			const auto original = vm.GetSlot(title ? vm.RootTable() : base, std::string("checkInputAction"));
			const auto hook = MakeLiveJumpHook(vm, original, take, title);
			const auto hook_root = vm.Retain(hook);
			vm.NewSlot(base, std::string("checkInputAction"), hook);
			installed = true;
		}
	}

	bool LiveSceneJump::CanJump() const {
		return m_Implementation->Player().has_value() || m_Implementation->TitleAvailable();
	}

	void LiveSceneJump::Queue(std::span<const std::uint8_t> checkpoint, const OriginalCheckpointLocation& location, std::string_view identity, std::function<void()> on_consumed) {
		auto& state = *m_Implementation;
		if (!CanJump()) {
			throw std::runtime_error("live scene restoration requires an original gameplay player or title menu");
		}
		const auto data = RestoreOriginalCheckpoint(state.Vm, checkpoint, location, identity);
		state.Data = state.Vm.Retain(data);
		state.OnConsumed = std::move(on_consumed);
		state.Queued = true;
	}

	bool LiveSceneJump::Pending() const noexcept {
		return m_Implementation->Queued;
	}

} // namespace sgre::script
