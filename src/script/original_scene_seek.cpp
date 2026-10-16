#include "sgre/script/original_scene_seek.hpp"

#include <array>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace sgre::script {

	static SquirrelValue CallSeekMethod(SquirrelVm& vm, const SquirrelValue& method, std::span<const SquirrelValue> arguments) {
		const auto result = vm.InvokeInContext(vm.CurrentContext(), method, arguments);
		if (result.State != SquirrelRunState::Completed) {
			throw std::runtime_error("original scene seek hook requires its original synchronous extension method");
		}

		return result.Value;
	}

	static SquirrelInstruction SeekInstruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t operand = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
		SquirrelInstruction instruction;
		instruction.Opcode = opcode;
		instruction.Argument0 = destination;
		instruction.Argument1 = operand;
		instruction.Argument2 = argument2;
		instruction.Argument3 = argument3;

		return instruction;
	}

	static BytecodeProgram SnapshotHookFactory() {
		// This is authored launch orchestration. Ordinary script CALLs retain the
		// original coroutine when cancellation legitimately awaits a system save.
		BytecodeProgram program;
		auto& factory = program.MainFunction;
		factory.Name = std::string("seek snapshot hook factory");
		factory.SourceName = std::string("sgre/original-scene-seek");
		factory.Parameters = {std::string("this"), std::string("original"), std::string("validate"), std::string("predicate"), std::string("finish"), std::string("failure")};
		factory.StackSize = 7;
		factory.Instructions = {SeekInstruction(48, 6, 0), SeekInstruction(19, 1, 6)};
		SquirrelFunction hook;
		hook.Name = std::string("seek.ScenePlayer.checkSaveMode");
		hook.SourceName = factory.SourceName;
		hook.Parameters = {std::string("this")};
		hook.StackSize = 7;
		hook.Literals = {std::string("cancelForceSkip")};
		for (std::int32_t index = 0; index < 5; ++index) {
			SquirrelOuterBinding binding;
			binding.Type = 0;
			binding.Source = index + 1;
			binding.Name = factory.Parameters[static_cast<std::size_t>(index + 1)];
			hook.OuterBindings.push_back(std::move(binding));
		}
		// try { result=original(this); if(predicate(this)) {
		// this.cancelForceSkip(true); finish(this,result); } return result; }
		// catch(error) { failure(this,error); throw error; }
		hook.Instructions = {SeekInstruction(27, 2, 1), SeekInstruction(10, 3, 0), SeekInstruction(6, 255, 2, 3, 1), SeekInstruction(56, 6, 16), SeekInstruction(27, 2, 0), SeekInstruction(10, 3, 0), SeekInstruction(6, 1, 2, 3, 1), SeekInstruction(27, 2, 2), SeekInstruction(10, 3, 0), SeekInstruction(6, 5, 2, 3, 1), SeekInstruction(26, 5, 7), SeekInstruction(8, 2, 0, 0, 3), SeekInstruction(22, 4, 1), SeekInstruction(6, 255, 2, 3, 2), SeekInstruction(27, 2, 3), SeekInstruction(10, 3, 0), SeekInstruction(10, 4, 1), SeekInstruction(6, 255, 2, 3, 2), SeekInstruction(57, 1), SeekInstruction(19, 1, 1), SeekInstruction(27, 2, 4), SeekInstruction(10, 3, 0), SeekInstruction(10, 4, 6), SeekInstruction(6, 255, 2, 3, 2), SeekInstruction(58, 6)};
		factory.Functions.push_back(std::move(hook));

		return program;
	}

	static void InstallScriptSnapshotHook(SquirrelVm& vm, const SquirrelValue& player, const SquirrelValue& original, const SquirrelValue& validate, const SquirrelValue& predicate, const SquirrelValue& finish, const SquirrelValue& failure) {
		const auto context = vm.CreateContext();
		try {
			const auto factory = vm.CreateClosureInContext(context, SnapshotHookFactory());
			const std::array<SquirrelValue, 6> arguments{vm.RootTable(), original, validate, predicate, finish, failure};
			const auto result = vm.InvokeInContext(context, factory, arguments, 256, false);
			if (result.State != SquirrelRunState::Completed || vm.Type(result.Value) != SquirrelValueType::Closure) {
				throw std::runtime_error("original scene seek snapshot factory did not publish its captured script closure");
			}

			const auto pin = vm.Retain(result.Value);
			vm.NewSlot(player, std::string("checkSaveMode"), result.Value);
			vm.ReleaseContext(context);
		} catch (...) {
			try {
				vm.ReleaseContext(context);
			} catch (...) {
				// Preserve the first setup failure while retiring its private context.
			}
			throw;
		}
	}

	static std::int32_t SeekInteger(SquirrelVm& vm, const SquirrelValue& owner, std::string_view key) {
		const auto value = vm.GetSlot(owner, std::string(key));
		const auto* integer = std::get_if<std::int32_t>(&value);
		if (integer == nullptr) {
			throw std::runtime_error("original scene seek requires the actual integer " + std::string(key));
		}

		return *integer;
	}

	class OriginalSceneSeek::Implementation {
	public:
		Implementation(SquirrelVm& vm, OriginalSceneSeekTarget target, OriginalSceneSeekLimits limits)
		    : Vm(vm), Target(std::move(target)), Limits(limits) {
			if (Vm.IsClosed() || Target.Storage.empty() || Target.Storage.size() > 4096 || Target.Storage.find('\0') != std::string::npos || Target.RequestedLine < 0 || Target.SnapshotLine < Target.RequestedLine || Target.Point < 0 || Limits.MaximumPolls == 0 || Limits.MaximumPolls > 1000000 || Limits.MaximumSnapshots == 0 || Limits.MaximumSnapshots > 10000000 || Limits.MaximumMovieRequests == 0 || Limits.MaximumMovieRequests > 10000 || Limits.MaximumConfirmationPolls == 0 || Limits.MaximumConfirmationPolls > 100000) {
				throw std::runtime_error("original scene seek requires a live VM, validated snapshot coordinates and bounded work");
			}
		}

		[[nodiscard]] bool Holding() const noexcept {
			return !Releasing && (State.Phase == OriginalSceneSeekPhase::WaitingForPlayer || State.Phase == OriginalSceneSeekPhase::Progressing);
		}

		void RequireAction() {
			const auto root = Vm.RootTable();
			const auto action = Vm.GetSlot(Vm.GetSlot(root, std::string("actionKeyMap")), std::string("force_skip_all"));
			const auto key = SeekInteger(Vm, action, "key");
			const auto pressing = Vm.GetSlot(action, std::string("pressing"));
			const auto relative = Vm.GetSlot(action, std::string("relkey"));
			const auto function = SeekInteger(Vm, action, "func");
			if (key < 0 || !std::holds_alternative<bool>(pressing) || !std::get<bool>(pressing) || !std::holds_alternative<std::monostate>(relative) || function != 0) {
				throw std::runtime_error("original scene seek requires the recovered pressing-only force_skip_all action");
			}
		}

		void StartPlayer(const SquirrelValue& player) {
			if (!Holding()) {
				return;
			}
			const auto identity = Vm.BorrowObjectIdentity(player);
			if (Player && *Player != identity) {
				Block("original seek player was replaced before reaching the requested snapshot");
				return;
			}
			Player = identity;
			PlayerContext = Vm.CurrentContext();
			State.Phase = OriginalSceneSeekPhase::Progressing;
		}

		[[nodiscard]] bool Snapshot(const SquirrelValue& player) {
			if (!Holding()) {
				return false;
			}
			StartPlayer(player);
			if (!Holding()) {
				return false;
			}
			if (State.Snapshots == Limits.MaximumSnapshots) {
				Block("original scene seek exceeded its snapshot work bound");
				return false;
			}
			++State.Snapshots;
			const auto scene_name = Vm.GetSlot(player, std::string("curSceneName"));
			const auto name = StringText(scene_name);
			if (name.size() > 8192) {
				throw std::runtime_error("original scene seek current scene identity exceeds its bound");
			}
			// Original getStorage(4129) returns the prefix before the first '*'.
			State.Storage.assign(name.substr(0, name.find('*')));
			State.SourceLine = SeekInteger(Vm, player, "curLine");
			State.Point = SeekInteger(Vm, player, "curPoint");
			if (State.Storage != Target.Storage || State.Point != Target.Point || State.SourceLine != Target.SnapshotLine) {
				return false;
			}
			State.Reason = "original snapshot reached; awaiting original force-skip release";
			Releasing = true;

			return true;
		}

		[[nodiscard]] SquirrelNativeResult FinishSnapshot(const SquirrelValue& player, const SquirrelValue& original_result) {
			if (!Releasing || State.Phase != OriginalSceneSeekPhase::Progressing) {
				return SquirrelNativeResult::NoValues();
			}
			if (!Player || *Player != Vm.BorrowObjectIdentity(player) || !PlayerContext || *PlayerContext != Vm.CurrentContext()) {
				throw std::runtime_error("original scene seek release requires its actual player and suspended snapshot context");
			}
			// Validate suspension before publishing arrival. The original save completed;
			// its containing dispatcher resumes only after the next ordinary input sample.
			const auto suspension = Vm.RequestNativeSuspension();
			Vm.PushNativeValue(original_result);
			State.Reason = "original sequential progression reached the validated snapshot";
			State.Phase = OriginalSceneSeekPhase::Arrived;
			Releasing = false;

			return suspension;
		}

		void FailSnapshot(const SquirrelValue& error) noexcept {
			if (!Holding() && !Releasing) {
				return;
			}
			State.Phase = OriginalSceneSeekPhase::Failed;
			Releasing = false;
			try {
				State.Reason = "original snapshot or force-skip release failed: ";
				if (const auto* detail = TryStringText(error)) {
					State.Reason.append(detail->substr(0, 1024));
				} else {
					State.Reason.append("original non-string exception propagated");
				}
			} catch (...) {
				// The script wrapper must rethrow its original value even if logging fails.
			}
		}

		[[nodiscard]] bool OriginalMovieCancelCall() const {
			if (!PlayerContext || Vm.CurrentContext() != *PlayerContext) {
				return false;
			}
			return Vm.NativeCallerMatches("rsc_project/script/override.nut", "tag_waitmovie", 62);
		}

		[[nodiscard]] bool RequestMovieCancel(const SquirrelValue& player) {
			const auto cursor = SeekInteger(Vm, player, "cur");
			const auto scene = StringText(Vm.GetSlot(player, std::string("curSceneName")));
			if (scene.size() > 8192) {
				throw std::runtime_error("original movie command identity exceeds its bounded scene name");
			}
			if (MoviePending && MovieCursor == cursor && MovieScene == scene) {
				return false;
			}
			if (!MovieInstalled || State.MovieRequests == Limits.MaximumMovieRequests) {
				Block(!MovieInstalled ? "original movie confirmation handler is not published; seek requires genuine input" : "original movie skip requests exceed their bounded work limit");
				return false;
			}
			MoviePending = true;
			MovieCursor = cursor;
			MovieScene = scene;
			MovieRequestIssued = false;
			MovieDialog.reset();
			ConfirmationPolls = 0;
			++State.MovieRequests;

			return true;
		}

		void ConfirmMovie(const SquirrelValue& dialog, const SquirrelValue& panel) {
			if (!Holding() || !MoviePending || MovieRequestIssued || !Player) {
				return;
			}
			const auto actual_player = Vm.GetSlot(dialog, std::string("player"));
			if (Vm.Type(actual_player) != SquirrelValueType::Instance || Vm.BorrowObjectIdentity(actual_player) != *Player) {
				return;
			}
			const auto identity = Vm.BorrowObjectIdentity(dialog);
			if (MovieDialog && *MovieDialog != identity) {
				Block("original movie confirmation owner changed before its requested action completed");
				return;
			}
			MovieDialog = identity;
			if (ConfirmationPolls == Limits.MaximumConfirmationPolls) {
				Block("original movie confirmation button did not become ready within its bounded work limit");
				return;
			}
			++ConfirmationPolls;
			if (Vm.Type(panel) != SquirrelValueType::Instance || !Vm.HasSlot(panel, std::string("_owner"))) {
				return;
			}
			const auto actual_owner = Vm.GetSlot(panel, std::string("_owner"));
			if (Vm.Type(actual_owner) != SquirrelValueType::Instance || Vm.BorrowObjectIdentity(actual_owner) != identity) {
				throw std::runtime_error("original movie confirmation requires its actual owned panel");
			}
			const auto focus = Vm.GetSlot(panel, std::string("_mainFocus"));
			if (Vm.Type(focus) != SquirrelValueType::Instance || !Vm.HasSlot(focus, std::string("_motionPartsMap"))) {
				return;
			}
			const auto parts = Vm.GetSlot(focus, std::string("_motionPartsMap"));
			if (!Vm.HasSlot(parts, std::string("yes"))) {
				return;
			}
			const auto button = Vm.GetSlot(parts, std::string("yes"));
			const auto ready = CallSeekMethod(Vm, Vm.GetSlot(button, std::string("getPressEnable")), std::array<SquirrelValue, 1>{button});
			if (!std::holds_alternative<bool>(ready)) {
				throw std::runtime_error("original movie confirmation requires its actual boolean press readiness");
			}
			if (!std::get<bool>(ready)) {
				return;
			}
			MovieRequestIssued = true;
			try {
				// Original dialog metadata gives this actual button result1. The group's
				// command handler executes readiness/onPress/onMotionButton/onProcess.
				static_cast<void>(CallSeekMethod(Vm, Vm.GetSlot(focus, std::string("_work")), std::array<SquirrelValue, 2>{focus, Vm.CreateString("yes")}));
				++State.MovieConfirmations;
			} catch (...) {
				State.Phase = OriginalSceneSeekPhase::Failed;
				State.Reason = "original movie confirmation action failed; original exception propagated";
				throw;
			}
		}

		void FailFirstEntry(std::string_view detail) noexcept {
			State.Phase = OriginalSceneSeekPhase::Failed;
			try {
				State.Reason.assign("original scene seek first entry failed: ");
				State.Reason.append(detail);
			} catch (...) {
				// Preserve the active original exception even if diagnostic storage fails.
			}
		}

		void Block(std::string reason) {
			Releasing = false;
			State.Phase = OriginalSceneSeekPhase::Blocked;
			State.Reason = std::move(reason);
		}

		SquirrelVm& Vm;
		OriginalSceneSeekTarget Target;
		OriginalSceneSeekLimits Limits;
		OriginalSceneSeekObservation State;
		std::optional<SquirrelObjectIdentity> Player;
		std::optional<SquirrelContextHandle> PlayerContext;
		std::size_t Polls = 0;
		bool ApplicationInstalled = false;
		bool PlayerInstalled = false;
		bool Started = false;
		bool Releasing = false;
		bool MovieInstalled = false;
		bool MoviePending = false;
		bool MovieRequestIssued = false;
		std::int32_t MovieCursor = 0;
		std::string MovieScene;
		std::size_t ConfirmationPolls = 0;
		std::optional<SquirrelObjectIdentity> MovieDialog;
	};

	OriginalSceneSeek::OriginalSceneSeek(SquirrelVm& vm, OriginalSceneSeekTarget target, OriginalSceneSeekLimits limits)
	    : m_Implementation(std::make_shared<Implementation>(vm, std::move(target), limits)) {
	}

	OriginalSceneSeek::~OriginalSceneSeek() {
		Cancel();
	}

	void OriginalSceneSeek::InstallAvailableHooks() {
		auto& state = *m_Implementation;
		if (state.Vm.IsClosed()) {
			throw std::runtime_error("original scene seek cannot install into a closed VM");
		}
		if (state.State.Phase != OriginalSceneSeekPhase::Installing && !state.Holding()) {
			return;
		}
		const auto root = state.Vm.RootTable();
		if (!state.MovieInstalled && state.Vm.HasSlot(root, std::string("MovieSkipConfirmDialog"))) {
			const auto movie = state.Vm.GetSlot(root, std::string("MovieSkipConfirmDialog"));
			const auto original = state.Vm.GetSlot(movie, std::string("onMotionUpdate"));
			const std::array<SquirrelValue, 2> captures{movie, original};
			const std::weak_ptr<Implementation> weak = m_Implementation;
			const auto hook = state.Vm.CreateNativeClosureWithCaptures("seek.MovieSkipConfirmDialog.onMotionUpdate", [weak](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 4 || !vm.IsInstanceOf(arguments.front(), vm.NativeStackValue(-1))) {
					throw std::runtime_error("original movie seek requires its actual MovieSkipConfirmDialog receiver");
				}
				const auto result = CallSeekMethod(vm, vm.NativeStackValue(-2), arguments.first(2));
				if (const auto owner = weak.lock()) {
					owner->ConfirmMovie(arguments[0], arguments[1]);
				}

				return SquirrelNativeResult::ReturnValue(result);
			},
			    captures);
			state.Vm.NewSlot(movie, std::string("onMotionUpdate"), hook);
			state.MovieInstalled = true;
		}
		if (state.State.Phase != OriginalSceneSeekPhase::Installing) {
			return;
		}
		if ((!state.ApplicationInstalled && state.Vm.HasSlot(root, std::string("MyApplication"))) || (!state.PlayerInstalled && state.Vm.HasSlot(root, std::string("ScenePlayer")) && state.Vm.HasSlot(root, std::string("MyPlayer")))) {
			throw std::runtime_error("original stateful seek must precede application and player derivation");
		}
		const std::weak_ptr<Implementation> weak = m_Implementation;
		if (!state.ApplicationInstalled && state.Vm.HasSlot(root, std::string("Application"))) {
			if (state.Vm.HasSlot(root, std::string("MyApplication"))) {
				throw std::runtime_error("original stateful seek must precede application derivation");
			}
			const auto application = state.Vm.GetSlot(root, std::string("Application"));
			const auto original = state.Vm.GetSlot(application, std::string("getMainScene"));
			const std::array<SquirrelValue, 2> captures{application, original};
			const auto hook = state.Vm.CreateNativeClosureWithCaptures("seek.Application.getMainScene", [weak](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 3 || !vm.IsInstanceOf(arguments.front(), vm.NativeStackValue(-1))) {
					throw std::runtime_error("original stateful entry requires the actual Application-derived receiver");
				}
				const auto owner = weak.lock();
				if (!owner || owner->Started || owner->State.Phase == OriginalSceneSeekPhase::Cancelled) {
					return SquirrelNativeResult::ReturnValue(CallSeekMethod(vm, vm.NativeStackValue(-2), arguments.first(1)));
				}
				try {
					if (!owner->PlayerInstalled) {
						throw std::runtime_error("original stateful entry precedes its original player hook publication");
					}
					owner->RequireAction();
					const auto current = CallSeekMethod(vm, vm.NativeStackValue(-2), arguments.first(1));
					const std::array<SquirrelValue, 2> start_arguments{arguments.front(), current};
					const auto start = CallSeekMethod(vm, vm.GetSlot(arguments.front(), std::string("getStartScene")), start_arguments);
					if (vm.Type(start) != SquirrelValueType::Table) {
						throw std::runtime_error("original stateful entry requires the actual derived new-game scene table");
					}
					owner->Started = true;
					owner->State.Phase = OriginalSceneSeekPhase::WaitingForPlayer;

					return SquirrelNativeResult::ReturnValue(start);
				} catch (const std::exception& error) {
					owner->FailFirstEntry(error.what());
					throw;
				} catch (...) {
					owner->FailFirstEntry("non-standard exception from the original entry producer");
					throw;
				}
			},
			    captures);
			state.Vm.NewSlot(application, std::string("getMainScene"), hook);
			state.ApplicationInstalled = true;
		}
		if (!state.PlayerInstalled && state.Vm.HasSlot(root, std::string("ScenePlayer"))) {
			if (state.Vm.HasSlot(root, std::string("MyPlayer"))) {
				throw std::runtime_error("original stateful seek must precede player derivation");
			}
			const auto player = state.Vm.GetSlot(root, std::string("ScenePlayer"));
			const auto original_start = state.Vm.GetSlot(player, std::string("onSceneStart"));
			const std::array<SquirrelValue, 2> start_captures{player, original_start};
			const auto start_hook = state.Vm.CreateNativeClosureWithCaptures("seek.ScenePlayer.onSceneStart", [weak](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 4 || !vm.IsInstanceOf(arguments.front(), vm.NativeStackValue(-1))) {
					throw std::runtime_error("original seek snapshot hook requires its actual ScenePlayer receiver");
				}
				const auto result = CallSeekMethod(vm, vm.NativeStackValue(-2), arguments.first(2));
				if (const auto owner = weak.lock()) {
					owner->StartPlayer(arguments.front());
				}

				return SquirrelNativeResult::ReturnValue(result);
			},
			    start_captures);
			state.Vm.NewSlot(player, std::string("onSceneStart"), start_hook);
			const std::array<SquirrelValue, 1> snapshot_captures{player};
			const auto validate = state.Vm.CreateNativeClosureWithCaptures("seek.snapshotReceiver", [](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 2 || !vm.IsInstanceOf(arguments.front(), vm.NativeStackValue(-1))) {
					throw std::runtime_error("original seek snapshot hook requires its actual ScenePlayer receiver");
				}

				return SquirrelNativeResult::NoValues();
			},
			    snapshot_captures);
			const auto predicate = state.Vm.CreateNativeClosureWithCaptures("seek.snapshotPredicate", [weak](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 2 || !vm.IsInstanceOf(arguments.front(), vm.NativeStackValue(-1))) {
					throw std::runtime_error("original seek snapshot hook requires its actual ScenePlayer receiver");
				}
				const auto owner = weak.lock();

				return SquirrelNativeResult::ReturnValue(owner && owner->Snapshot(arguments.front()));
			},
			    snapshot_captures);
			const auto finish = state.Vm.CreateNativeClosureWithCaptures("seek.snapshotFinished", [weak](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 3 || !vm.IsInstanceOf(arguments.front(), vm.NativeStackValue(-1))) {
					throw std::runtime_error("original seek release requires its actual ScenePlayer receiver");
				}
				// Pushing a native result may grow the borrowed operand storage.
				const auto original_result = arguments[1];
				if (const auto owner = weak.lock()) {
					return owner->FinishSnapshot(arguments.front(), original_result);
				}

				return SquirrelNativeResult::NoValues();
			},
			    snapshot_captures);
			const auto failure = state.Vm.CreateNativeClosureWithCaptures("seek.snapshotFailed", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				if (arguments.size() == 3) {
					if (const auto owner = weak.lock()) {
						owner->FailSnapshot(arguments[1]);
					}
				}

				return SquirrelNativeResult::NoValues();
			},
			    snapshot_captures);
			InstallScriptSnapshotHook(state.Vm, player, state.Vm.GetSlot(player, std::string("checkSaveMode")), validate, predicate, finish, failure);
			const auto original_action = state.Vm.GetSlot(player, std::string("checkInputAction"));
			const std::array<SquirrelValue, 2> action_captures{player, original_action};
			const auto action_hook = state.Vm.CreateNativeClosureWithCaptures("seek.ScenePlayer.checkInputAction", [weak](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 4 || !vm.IsInstanceOf(arguments.front(), vm.NativeStackValue(-1))) {
					throw std::runtime_error("original semantic seek action requires its actual ScenePlayer receiver");
				}
				const auto* name = TryStringText(arguments[1]);
				const auto owner = weak.lock();
				if (owner && owner->Holding() && owner->Player && *owner->Player == vm.BorrowObjectIdentity(arguments.front()) && name) {
					if (*name == "force_skip_all") {
						return SquirrelNativeResult::ReturnValue(true);
					}
					if (*name == "cancel" && owner->OriginalMovieCancelCall() && owner->RequestMovieCancel(arguments.front())) {
						return SquirrelNativeResult::ReturnValue(true);
					}
				}

				return SquirrelNativeResult::ReturnValue(CallSeekMethod(vm, vm.NativeStackValue(-2), arguments.first(2)));
			},
			    action_captures);
			state.Vm.NewSlot(player, std::string("checkInputAction"), action_hook);
			state.PlayerInstalled = true;
		}
	}

	void OriginalSceneSeek::Poll() {
		auto& state = *m_Implementation;
		if (!state.Holding() && !state.Releasing) {
			return;
		}
		if (state.Polls == state.Limits.MaximumPolls) {
			state.Block("original sequential seek reached its bounded frame limit before the requested snapshot");
			return;
		}
		++state.Polls;
		if (!state.PlayerContext) {
			return;
		}
		if (!state.Player || !state.Vm.ResolveObjectIdentity(*state.Player)) {
			state.Block("original seek player retired before the requested snapshot");
			return;
		}
		const auto observed = state.Vm.ObserveContext(*state.PlayerContext, kSquirrelObservationFrames, 0);
		if (observed.State == SquirrelContextState::Idle && observed.FrameCount == 0) {
			return;
		}
		if (observed.FramesTruncated) {
			state.Block("original scene seek context exceeds its bounded observation depth");
			return;
		}
		if (observed.State != SquirrelContextState::Suspended) {
			return;
		}
		if (state.MoviePending) {
			bool movie_wait = false;
			for (const auto& frame : observed.Frames) {
				movie_wait = movie_wait || (frame.Source == "rsc_project/script/override.nut" && frame.Function == "tag_waitmovie");
			}
			if (!movie_wait) {
				state.MoviePending = false;
				state.MovieDialog.reset();
			}
		}
		for (const auto& frame : observed.Frames) {
			const bool choice = frame.Source == "advengine/script/envplayer.nut" && frame.Function == "doSelect";
			const bool phone = frame.Source == "rsc_project/script/override.nut" && frame.Function == "openUserInteractionPhone";
			if (choice || phone) {
				state.State.Source = frame.Source;
				state.State.Function = frame.Function;
				state.State.ScriptLine = frame.SourceLine;
				state.Block(choice ? "original selection requires genuine route input; automatic seek released" : "original phone interaction requires genuine input; automatic seek released");
				return;
			}
		}
	}

	void OriginalSceneSeek::Cancel() noexcept {
		auto& state = *m_Implementation;
		if (state.State.Phase == OriginalSceneSeekPhase::Installing || state.Holding() || state.Releasing) {
			state.Releasing = false;
			state.State.Phase = OriginalSceneSeekPhase::Cancelled;
		}
	}

	const OriginalSceneSeekObservation& OriginalSceneSeek::Observation() const noexcept {
		return m_Implementation->State;
	}

} // namespace sgre::script
