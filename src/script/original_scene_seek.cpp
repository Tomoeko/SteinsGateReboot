#include "sgre/script/original_scene_seek.hpp"

#include <array>
#include <limits>
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

	static BytecodeProgram PhoneHookFactory() {
		// Keep both the original update and its button expression in script frames:
		// phone transitions legitimately suspend while their original animation runs.
		BytecodeProgram program;
		auto& factory = program.MainFunction;
		factory.Name = std::string("seek phone hook factory");
		factory.SourceName = std::string("sgre/original-scene-seek");
		factory.Parameters = {std::string("this"), std::string("original"), std::string("prepare"), std::string("finish"), std::string("failure")};
		factory.StackSize = 6;
		factory.Instructions = {SeekInstruction(48, 5, 0), SeekInstruction(19, 1, 5)};
		SquirrelFunction hook;
		hook.Name = std::string("seek.phone.onPhoneMain");
		hook.SourceName = factory.SourceName;
		hook.Parameters = {std::string("this")};
		hook.StackSize = 7;
		hook.Literals = {std::string("focus"), std::string("panel"), std::string("setMotionFocus"), std::string("name"), std::string("pressButton")};
		for (std::int32_t index = 0; index < 4; ++index) {
			SquirrelOuterBinding binding;
			binding.Type = 0;
			binding.Source = index + 1;
			binding.Name = factory.Parameters[static_cast<std::size_t>(index + 1)];
			hook.OuterBindings.push_back(std::move(binding));
		}

		auto& code = hook.Instructions;
		code.push_back(SeekInstruction(56, 6, 22));
		code.push_back(SeekInstruction(27, 3, 0));
		code.push_back(SeekInstruction(10, 4, 0));
		code.push_back(SeekInstruction(6, 1, 3, 4, 1));

		code.push_back(SeekInstruction(27, 3, 1));
		code.push_back(SeekInstruction(10, 4, 0));
		code.push_back(SeekInstruction(6, 2, 3, 4, 1));
		code.push_back(SeekInstruction(26, 2, 13));
		code.push_back(SeekInstruction(9, 6, 0, 2));
		code.push_back(SeekInstruction(26, 6, 4));

		code.push_back(SeekInstruction(9, 4, 1, 2));
		code.push_back(SeekInstruction(8, 3, 2, 4, 4));
		code.push_back(SeekInstruction(9, 5, 3, 2));
		code.push_back(SeekInstruction(6, 255, 3, 4, 2));

		code.push_back(SeekInstruction(9, 4, 1, 2));
		code.push_back(SeekInstruction(8, 3, 4, 4, 4));
		code.push_back(SeekInstruction(9, 5, 3, 2));
		code.push_back(SeekInstruction(6, 255, 3, 4, 2));
		code.push_back(SeekInstruction(27, 3, 2));
		code.push_back(SeekInstruction(10, 4, 0));
		code.push_back(SeekInstruction(6, 255, 3, 4, 1));

		code.push_back(SeekInstruction(57, 1));
		code.push_back(SeekInstruction(19, 1, 1));
		code.push_back(SeekInstruction(27, 3, 3));
		code.push_back(SeekInstruction(10, 4, 0));
		code.push_back(SeekInstruction(10, 5, 6));
		code.push_back(SeekInstruction(6, 255, 3, 4, 2));
		code.push_back(SeekInstruction(58, 6));
		factory.Functions.push_back(std::move(hook));

		return program;
	}

	static void InstallScriptPhoneHook(SquirrelVm& vm, const SquirrelValue& type, const SquirrelValue& prepare, const SquirrelValue& finish, const SquirrelValue& failure) {
		const auto context = vm.CreateContext();
		try {
			const auto factory = vm.CreateClosureInContext(context, PhoneHookFactory());
			const std::array<SquirrelValue, 5> arguments{vm.RootTable(), vm.GetSlot(type, std::string("onPhoneMain")), prepare, finish, failure};
			const auto result = vm.InvokeInContext(context, factory, arguments, 256, false);
			if (result.State != SquirrelRunState::Completed || vm.Type(result.Value) != SquirrelValueType::Closure) {
				throw std::runtime_error("original phone seek factory did not publish its captured script closure");
			}

			const auto pin = vm.Retain(result.Value);
			vm.NewSlot(type, std::string("onPhoneMain"), result.Value);
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
		struct PhoneCallRoute {
			std::string Chara;
			std::string Label;
			std::string Storage;
			bool Requested = false;
			bool Opened = false;
			std::size_t RequestPolls = 0;
			bool ContactPressed = false;
			bool DialogPressed = false;
			std::size_t Polls = 0;
			std::array<std::optional<SquirrelObjectIdentity>, 2> PendingControls{};
		};

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
			if (PhoneCall && PhoneCall->Requested && name == PhoneCall->Storage + PhoneCall->Label) {
				PhoneCall.reset();
			}
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

		void ObserveNumpad(const SquirrelValue& function) {
			if (!Holding()) {
				return;
			}
			NumpadFunction = Vm.BorrowObjectIdentity(function);
			NumpadLayer.reset();
			NumpadNumbers.reset();
			NumpadPolls = 0;
			AwaitingNumpadLayer = true;
		}

		[[nodiscard]] std::optional<bool> MailInput(const SquirrelValue& player) {
			if (!Holding() || !Target.Navigation || !PlayerContext || Vm.CurrentContext() != *PlayerContext) {
				return {};
			}
			const bool down = Vm.NativeCallerMatches("rsc_project/script/phone.nut", "waitMailView", 68);
			const bool accept = Vm.NativeCallerMatches("rsc_project/script/phone.nut", "waitMailView", 94);
			if (!down && !accept) {
				return {};
			}
			const auto phone = Vm.GetSlot(Vm.GetSlot(player, std::string("autoPhoneInfo")), std::string("func"));
			if (Vm.Type(phone) != SquirrelValueType::Instance) {
				throw std::runtime_error("original mail seek input requires its actual phone function receiver");
			}
			const auto scroll = CallSeekMethod(Vm, Vm.ReadSlot(phone, std::string("getVariable")), std::array<SquirrelValue, 2>{phone, Vm.CreateString("isScrollDownEnabled")});
			if (!std::holds_alternative<bool>(scroll) && !std::holds_alternative<std::int32_t>(scroll) && !std::holds_alternative<float>(scroll)) {
				throw std::runtime_error("original mail seek requires its actual scroll-readiness value");
			}
			const bool can_scroll = std::holds_alternative<bool>(scroll) ? std::get<bool>(scroll) : std::holds_alternative<std::int32_t>(scroll) ? std::get<std::int32_t>(scroll) != 0
			                                                                                                                                     : std::get<float>(scroll) != 0;
			const bool pressed = down ? can_scroll : !can_scroll;
			if (pressed) {
				if (State.PhoneActions >= Limits.MaximumSnapshots) {
					Block("original mail navigation exceeded the bounded seek work limit");
					return {};
				}
				++State.PhoneActions;
			}

			return pressed;
		}

		void ObserveNumpadLayer(const SquirrelValue& layer, const SquirrelValue& delegate) {
			if (!Holding() || !AwaitingNumpadLayer) {
				return;
			}
			if (!Player || Vm.Type(delegate) != SquirrelValueType::Instance || Vm.BorrowObjectIdentity(delegate) != *Player) {
				Block("original numpad layer has an unexpected player delegate; route input required");
				return;
			}
			NumpadLayer = Vm.BorrowObjectIdentity(layer);
			AwaitingNumpadLayer = false;
		}

		void AdvanceNumpad(const SquirrelValue& layer, const SquirrelValue& panel) {
			if (!Holding() || !NumpadLayer || Vm.BorrowObjectIdentity(layer) != *NumpadLayer || !NumpadFunction || !Player || !Target.Navigation) {
				return;
			}
			const auto function = Vm.ResolveObjectIdentity(*NumpadFunction);
			const auto player = Vm.ResolveObjectIdentity(*Player);
			if (!function || !player) {
				Block("original numpad owner retired before completing its route input");
				return;
			}
			if (!NumpadNumbers) {
				const auto scene = StringText(Vm.GetSlot(*player, std::string("curSceneName")));
				const auto split = scene.find('*');
				if (split == std::string::npos) {
					throw std::runtime_error("original numpad requires its complete current storage/label identity");
				}
				NumpadNumbers = Target.Navigation->PhoneNumbers(scene.substr(0, split), scene.substr(split), Target.Storage, Target.Label);
				if (!NumpadNumbers || NumpadNumbers->size() != static_cast<std::size_t>(SeekInteger(Vm, *function, "digits"))) {
					Block("original numpad has no unique resource-defined input toward the requested scene; select a recorded route state");
					return;
				}
			}
			const auto numbers = StringText(Vm.GetSlot(*function, std::string("numbers")));
			if (!NumpadNumbers->starts_with(numbers)) {
				Block("original numpad input differs from the requested route; automatic seek released");
				return;
			}
			if (numbers.size() == NumpadNumbers->size()) {
				NumpadLayer.reset();
				NumpadFunction.reset();
				return;
			}
			if (++NumpadPolls > Limits.MaximumConfirmationPolls) {
				Block("original numpad button readiness exceeded its bounded wait");
				return;
			}
			const auto characters = StringText(Vm.GetSlot(*function, std::string("padchars")));
			const auto digit = characters.find((*NumpadNumbers)[numbers.size()]);
			if (digit == std::string::npos || digit > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
				throw std::runtime_error("original numpad has no button for its resource-defined input");
			}
			const auto name = CallSeekMethod(Vm, Vm.GetSlot(*function, std::string("getKeyName")), std::array<SquirrelValue, 2>{*function, static_cast<std::int32_t>(digit)});
			const auto focus = Vm.GetSlot(panel, std::string("_mainFocus"));
			if (Vm.Type(focus) != SquirrelValueType::Instance) {
				return;
			}
			const auto parts = Vm.GetSlot(focus, std::string("_motionPartsMap"));
			if (!Vm.HasSlot(parts, name)) {
				return;
			}
			const auto button = Vm.GetSlot(parts, name);
			const auto ready = CallSeekMethod(Vm, Vm.GetSlot(button, std::string("getPressEnable")), std::array<SquirrelValue, 1>{button});
			if (!std::holds_alternative<bool>(ready)) {
				throw std::runtime_error("original numpad button readiness is not boolean");
			}
			if (std::get<bool>(ready)) {
				// Stay inside workSync's original panel callback. A post-Update press can
				// invalidate the motion ancestry already prepared for the pending Draw.
				static_cast<void>(CallSeekMethod(Vm, Vm.GetSlot(focus, std::string("_work")), std::array<SquirrelValue, 2>{focus, name}));
				++State.PhoneActions;
			}
		}

		[[nodiscard]] bool PhoneQuery(const SquirrelValue& player, const SquirrelValue& action) {
			if (!PhoneCall || PhoneCall->Opened) {
				return false;
			}
			if (const auto* name = TryStringText(action)) {
				return *name == "phone";
			}
			if (Vm.Type(action) != SquirrelValueType::Array || Vm.Length(action) > 4096) {
				return false;
			}
			// The command executor passes its whole action list, while UI availability
			// queries pass one name. Delegate list matching to the original command policy.
			const auto matches = CallSeekMethod(Vm, Vm.GetSlot(player, std::string("checkCommandAction")), std::array<SquirrelValue, 3>{player, Vm.CreateString("phone"), action});
			if (!std::holds_alternative<bool>(matches)) {
				throw std::runtime_error("original phone seek requires a boolean command-action match");
			}

			return std::get<bool>(matches);
		}

		void FindPhoneCall() {
			if (!Holding() || !Target.Navigation || !PhoneHooks[0] || !PhoneHooks[1] || !Player || PhoneCall || State.Storage.empty()) {
				return;
			}
			const auto player = Vm.ResolveObjectIdentity(*Player);
			if (!player) {
				return;
			}
			const auto app = Vm.GetSlot(Vm.RootTableInContext(*PlayerContext), std::string("app"));
			if (Vm.Type(app) != SquirrelValueType::Instance || !Vm.HasSlot(app, std::string("player")) || Vm.GetSlot(app, std::string("player")) != *player) {
				Block("original phone route query requires its current application/player owner");
				return;
			}
			if (!Vm.HasSlot(app, std::string("getActiveTrigger"))) {
				return;
			}
			const auto enabled = CallSeekMethod(Vm, Vm.GetSlot(app, std::string("getPhoneTriggerEnabled")), std::array<SquirrelValue, 1>{app});
			if (!std::holds_alternative<bool>(enabled) && !std::holds_alternative<std::int32_t>(enabled)) {
				throw std::runtime_error("original phone seek requires its actual integer/boolean trigger flag");
			}
			const bool is_enabled = std::holds_alternative<bool>(enabled) ? std::get<bool>(enabled) : std::get<std::int32_t>(enabled) != 0;
			if (!is_enabled) {
				return;
			}
			const auto contacts = CallSeekMethod(Vm, Vm.GetSlot(app, std::string("getContacts")), std::array<SquirrelValue, 1>{app});
			if (Vm.Type(contacts) != SquirrelValueType::Array || Vm.Length(contacts) > 4096) {
				throw std::runtime_error("original phone seek requires a bounded actual contact array");
			}
			std::optional<PhoneCallRoute> selected;
			for (std::size_t index = 0; index < Vm.Length(contacts); ++index) {
				const auto contact = Vm.GetSlot(contacts, static_cast<std::int32_t>(index));
				// Contact records may be original PSB-backed objects rather than plain tables.
				const auto id = Vm.ReadSlot(contact, std::string("id"));
				const auto active = CallSeekMethod(Vm, Vm.GetSlot(app, std::string("getActiveTrigger")), std::array<SquirrelValue, 2>{app, id});
				if (Vm.Type(active) != SquirrelValueType::Table || !Vm.HasSlot(active, std::string("call"))) {
					continue;
				}
				const auto label = StringText(Vm.GetSlot(active, std::string("call")));
				if (!Target.Navigation->Reaches(State.Storage, label, Target.Storage, Target.Label)) {
					continue;
				}
				const auto chara = StringText(id);
				if (chara.empty() || chara.size() > 4096 || label.size() > 4096) {
					throw std::runtime_error("original phone seek route identity exceeds its bound");
				}
				if (selected && (selected->Chara != chara || selected->Label != label)) {
					Block("original phone has multiple active calls toward the requested scene; genuine route input required");
					return;
				}
				selected = PhoneCallRoute{chara, label, State.Storage};
			}
			PhoneCall = std::move(selected);
		}

		[[nodiscard]] bool PhoneState(const SquirrelValue& function, std::string_view getter) {
			const auto result = CallSeekMethod(Vm, Vm.ReadSlot(function, std::string(getter)), std::array<SquirrelValue, 1>{function});
			if (!std::holds_alternative<bool>(result)) {
				throw std::runtime_error("original phone seek requires a boolean UI state predicate");
			}

			return std::get<bool>(result);
		}

		[[nodiscard]] SquirrelValue PreparePhoneButton(const SquirrelValue& function, const SquirrelValue& name, bool focus, bool dialog) {
			const auto panel = CallSeekMethod(Vm, Vm.ReadSlot(function, std::string("getPanel")), std::array<SquirrelValue, 1>{function});
			if (Vm.Type(panel) != SquirrelValueType::Instance || PhoneState(panel, "getMotionLock")) {
				return {};
			}
			const auto button = CallSeekMethod(Vm, Vm.GetSlot(panel, std::string("getButton")), std::array<SquirrelValue, 2>{panel, name});
			if (Vm.Type(button) != SquirrelValueType::Instance || !PhoneState(button, "getPressEnable")) {
				return {};
			}
			const auto action = Vm.CreateTable();
			Vm.NewSlot(action, std::string("panel"), panel);
			Vm.NewSlot(action, std::string("name"), name);
			Vm.NewSlot(action, std::string("focus"), focus);
			PhoneCall->PendingControls[dialog ? 1 : 0] = Vm.BorrowObjectIdentity(function);
			++State.PhoneActions;

			return action;
		}

		void FinishPhoneControl(const SquirrelValue& function, bool dialog) {
			if (!PhoneCall) {
				return;
			}
			auto& pending = PhoneCall->PendingControls[dialog ? 1 : 0];
			if (!pending || *pending != Vm.BorrowObjectIdentity(function)) {
				throw std::runtime_error("original phone seek control returned through a different callback owner");
			}
			pending.reset();
		}

		void ObservePhoneFailure(const SquirrelValue& error) noexcept {
			if (!Holding()) {
				return;
			}
			try {
				// The original phone transfers control by throwing this exact typed event.
				// Its containing original handler must receive and consume the same value.
				const auto root = PlayerContext ? Vm.RootTableInContext(*PlayerContext) : SquirrelValue{};
				if (PhoneCall && PhoneCall->Requested && PlayerContext && Vm.Type(error) == SquirrelValueType::Instance && Vm.HasSlot(root, std::string("PhoneJumpException")) && Vm.IsInstanceOf(error, Vm.GetSlot(root, std::string("PhoneJumpException"))) && Vm.HasSlot(error, std::string("target")) && StringText(Vm.GetSlot(error, std::string("target"))) == PhoneCall->Label) {
					return;
				}
				State.Phase = OriginalSceneSeekPhase::Failed;
				State.Reason = "original phone seek callback failed; original exception propagated";
			} catch (...) {
				State.Phase = OriginalSceneSeekPhase::Failed;
			}
		}

		[[nodiscard]] bool PhonePanelBelongsToPlayer(const SquirrelValue& panel) {
			const auto app = Vm.GetSlot(Vm.RootTableInContext(*PlayerContext), std::string("app"));
			const auto player = Vm.ResolveObjectIdentity(*Player);
			if (!player || Vm.Type(app) != SquirrelValueType::Instance || !Vm.HasSlot(app, std::string("player")) || Vm.GetSlot(app, std::string("player")) != *player) {
				return false;
			}
			// Original panel ownership can forward through more than one Object delegate.
			// Follow only actual Object delegates, with a finite guard for malformed cycles.
			auto owner = Vm.GetSlot(panel, std::string("_owner"));
			constexpr std::size_t kMaximumOwnerDelegates = 16;
			for (std::size_t depth = 0; depth < kMaximumOwnerDelegates; ++depth) {
				if (owner == *player || owner == app) {
					return true;
				}
				if (Vm.Type(owner) != SquirrelValueType::Instance || !Vm.HasSlot(owner, std::string("getDelegate"))) {
					return false;
				}
				owner = CallSeekMethod(Vm, Vm.GetSlot(owner, std::string("getDelegate")), std::array<SquirrelValue, 1>{owner});
			}

			return false;
		}

		[[nodiscard]] SquirrelValue PreparePhoneControl(const SquirrelValue& function, bool dialog) {
			if (!Holding() || !PhoneCall || !PhoneCall->Requested || !Player) {
				return {};
			}
			if (PhoneCall->PendingControls[dialog ? 1 : 0] || (dialog && (!PhoneCall->ContactPressed || PhoneCall->DialogPressed))) {
				return {};
			}
			if (!dialog) {
				const auto automatic = Vm.ReadSlot(function, std::string("isAuto"));
				if (!std::holds_alternative<bool>(automatic)) {
					throw std::runtime_error("original phone seek requires its actual automatic-mode flag");
				}
				if (std::get<bool>(automatic)) {
					return {};
				}
			}
			if (++PhoneCall->Polls > Limits.MaximumConfirmationPolls) {
				Block("original phone call controls exceeded their bounded readiness wait; genuine input required");
				return {};
			}
			const auto panel = CallSeekMethod(Vm, Vm.ReadSlot(function, std::string("getPanel")), std::array<SquirrelValue, 1>{function});
			if (Vm.Type(panel) != SquirrelValueType::Instance || !Vm.HasSlot(panel, std::string("_owner"))) {
				return {};
			}
			if (!PhonePanelBelongsToPlayer(panel)) {
				return {};
			}
			PhoneCall->Opened = true;
			if (dialog) {
				const auto state = SeekInteger(Vm, function, "state");
				// Original onContactsOk selects state13 for call-only, state14 for mail/call.
				if (state == 13 || state == 14) {
					const auto action = PreparePhoneButton(function, Vm.CreateString(state == 13 ? "dialog/sel1" : "dialog/sel2"), true, true);
					PhoneCall->DialogPressed = Vm.Type(action) != SquirrelValueType::Null;
					return action;
				}
				return {};
			}
			if (PhoneCall->ContactPressed) {
				return {};
			}
			if (PhoneState(function, "getIsPowerOn")) {
				return PreparePhoneButton(function, Vm.CreateString("poweron/ok"), false, false);
			} else if (PhoneState(function, "getIsRoot")) {
				return PreparePhoneButton(function, Vm.CreateString("root/contacts"), true, false);
			} else if (PhoneState(function, "getIsContacts")) {
				const auto list = Vm.ReadSlot(function, std::string("sl"));
				const auto infos = Vm.GetSlot(list, std::string("infos"));
				const auto names = Vm.GetSlot(list, std::string("panelNames"));
				if (Vm.Type(infos) != SquirrelValueType::Array || Vm.Type(names) != SquirrelValueType::Array || Vm.Length(infos) > 4096) {
					throw std::runtime_error("original phone seek requires actual bounded contact list metadata");
				}
				for (std::size_t index = 0; index < Vm.Length(infos); ++index) {
					const auto info = Vm.GetSlot(infos, static_cast<std::int32_t>(index));
					if (StringText(Vm.ReadSlot(info, std::string("id"))) == PhoneCall->Chara) {
						if (index >= Vm.Length(names)) {
							Block("original phone call contact requires scrolling; genuine route input required");
							return {};
						}
						const auto action = PreparePhoneButton(function, Vm.GetSlot(names, static_cast<std::int32_t>(index)), true, false);
						PhoneCall->ContactPressed = Vm.Type(action) != SquirrelValueType::Null;
						return action;
					}
				}
				Block("original phone call contact is absent from its actual list; genuine input required");
			}

			return {};
		}

		void Block(std::string reason) {
			Releasing = false;
			State.Phase = OriginalSceneSeekPhase::Blocked;
			State.Reason = std::move(reason);
		}

		std::optional<PhoneCallRoute> PhoneCall;
		std::array<bool, 2> PhoneHooks{};
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
		bool NumpadLayerInstalled = false;
		bool NumpadFunctionInstalled = false;
		bool AwaitingNumpadLayer = false;
		std::optional<SquirrelObjectIdentity> NumpadLayer;
		std::optional<SquirrelObjectIdentity> NumpadFunction;
		std::optional<std::string> NumpadNumbers;
		std::size_t NumpadPolls = 0;
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
		const std::array<std::string_view, 2> phone_classes{"PhoneFunction", "PhoneDialogFunction"};
		for (std::size_t index = 0; state.Target.Navigation && index < phone_classes.size(); ++index) {
			if (state.PhoneHooks[index] || !state.Vm.HasSlot(root, std::string(phone_classes[index]))) {
				continue;
			}
			const auto type = state.Vm.GetSlot(root, std::string(phone_classes[index]));
			const std::weak_ptr<Implementation> weak = m_Implementation;
			const auto prepare = state.Vm.CreateNativeClosure("seek.phone.prepare", [weak, dialog = index == 1](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 1) {
					throw std::runtime_error("original phone seek preparation requires its function receiver");
				}
				if (const auto owner = weak.lock()) {
					return SquirrelNativeResult::ReturnValue(owner->PreparePhoneControl(arguments[0], dialog));
				}

				return SquirrelNativeResult::NoValues();
			});
			const auto finish = state.Vm.CreateNativeClosure("seek.phone.finish", [weak, dialog = index == 1](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 1) {
					throw std::runtime_error("original phone seek completion requires its function receiver");
				}
				if (const auto owner = weak.lock()) {
					owner->FinishPhoneControl(arguments[0], dialog);
				}

				return SquirrelNativeResult::NoValues();
			});
			const auto failure = state.Vm.CreateNativeClosure("seek.phone.failure", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				if (const auto owner = weak.lock(); owner && arguments.size() == 2) {
					owner->ObservePhoneFailure(arguments[1]);
				}

				return SquirrelNativeResult::NoValues();
			});
			InstallScriptPhoneHook(state.Vm, type, prepare, finish, failure);
			state.PhoneHooks[index] = true;
		}

		if (state.Target.Navigation && !state.NumpadLayerInstalled && state.Vm.HasSlot(root, std::string("MotionPanelLayer"))) {
			const auto layer = state.Vm.GetSlot(root, std::string("MotionPanelLayer"));
			const auto original_delegate = state.Vm.GetSlot(layer, std::string("setDelegate"));
			const std::weak_ptr<Implementation> weak = m_Implementation;
			const auto delegate_hook = state.Vm.CreateNativeClosureWithCaptures("seek.MotionPanelLayer.setDelegate", [weak](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 3) {
					throw std::runtime_error("original seek numpad delegate requires its layer and player");
				}
				const auto receiver = arguments[0];
				const auto delegate = arguments[1];
				const auto result = CallSeekMethod(vm, vm.NativeStackValue(-1), std::array<SquirrelValue, 2>{receiver, delegate});
				if (const auto owner = weak.lock()) {
					owner->ObserveNumpadLayer(receiver, delegate);
				}

				return SquirrelNativeResult::ReturnValue(result);
			},
			    std::array<SquirrelValue, 1>{original_delegate});
			state.Vm.NewSlot(layer, std::string("setDelegate"), delegate_hook);
			const auto original_update = state.Vm.GetSlot(layer, std::string("onMotionUpdate"));
			const auto update_hook = state.Vm.CreateNativeClosureWithCaptures("seek.MotionPanelLayer.onMotionUpdate", [weak](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				if (arguments.size() < 2) {
					throw std::runtime_error("original seek panel update requires its receiver and captured method");
				}
				const auto receiver = arguments[0];
				const auto original_arguments = arguments.first(arguments.size() - 1);
				// The original panel method supplies its own optional argument. ExDialog
				// calls base.onMotionUpdate() without a panel; preserve that call exactly.
				const auto result = CallSeekMethod(vm, vm.NativeStackValue(-1), original_arguments);
				if (const auto owner = weak.lock(); owner && original_arguments.size() >= 2) {
					owner->AdvanceNumpad(receiver, original_arguments[1]);
				}

				return SquirrelNativeResult::ReturnValue(result);
			},
			    std::array<SquirrelValue, 1>{original_update});
			state.Vm.NewSlot(layer, std::string("onMotionUpdate"), update_hook);
			state.NumpadLayerInstalled = true;
		}
		if (state.Target.Navigation && !state.NumpadFunctionInstalled && state.Vm.HasSlot(root, std::string("PhoneNumpadFunction"))) {
			const auto function = state.Vm.GetSlot(root, std::string("PhoneNumpadFunction"));
			const auto original = state.Vm.GetSlot(function, std::string("getNumpadMotionInfo"));
			const std::weak_ptr<Implementation> weak = m_Implementation;
			const auto hook = state.Vm.CreateNativeClosureWithCaptures("seek.PhoneNumpadFunction.getNumpadMotionInfo", [weak](SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 2) {
					throw std::runtime_error("original seek numpad metadata requires its actual function receiver");
				}
				const auto receiver = arguments[0];
				const auto result = CallSeekMethod(vm, vm.NativeStackValue(-1), std::array<SquirrelValue, 1>{receiver});
				if (const auto owner = weak.lock()) {
					owner->ObserveNumpad(receiver);
				}

				return SquirrelNativeResult::ReturnValue(result);
			},
			    std::array<SquirrelValue, 1>{original});
			state.Vm.NewSlot(function, std::string("getNumpadMotionInfo"), hook);
			state.NumpadFunctionInstalled = true;
		}
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
					const auto& initial = owner->Target.InitialState;
					const auto start = initial ? RestoreOriginalCheckpoint(vm, initial->Checkpoint, initial->CheckpointLocation, initial->CheckpointIdentity) : CallSeekMethod(vm, vm.GetSlot(arguments.front(), std::string("getStartScene")), start_arguments);
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
				if (owner && owner->Holding() && owner->Player && *owner->Player == vm.BorrowObjectIdentity(arguments.front())) {
					if (const auto pressed = owner->MailInput(arguments.front())) {
						return SquirrelNativeResult::ReturnValue(*pressed);
					}
				}
				if (owner && owner->Holding() && owner->Player && *owner->Player == vm.BorrowObjectIdentity(arguments.front())) {
					const auto receiver = arguments[0];
					const auto action = arguments[1];
					if (owner->PhoneQuery(receiver, action)) {
						if (!owner->PhoneCall->Requested) {
							owner->PhoneCall->Requested = true;
							++owner->State.PhoneActions;
						}
						return SquirrelNativeResult::ReturnValue(true);
					}
					if (name && *name == "force_skip_all") {
						return SquirrelNativeResult::ReturnValue(!owner->PhoneCall || !owner->PhoneCall->Requested);
					}
					if (name && *name == "cancel" && owner->OriginalMovieCancelCall() && owner->RequestMovieCancel(arguments.front())) {
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
		const auto observed = state.Vm.ObserveContext(*state.PlayerContext, 1, 0);
		if (observed.State == SquirrelContextState::Idle && observed.FrameCount == 0) {
			return;
		}
		if (observed.State != SquirrelContextState::Suspended) {
			return;
		}
		if (state.MoviePending && !state.Vm.FindContextFrame(*state.PlayerContext, "rsc_project/script/override.nut", "tag_waitmovie")) {
			state.MoviePending = false;
			state.MovieDialog.reset();
		}
		state.FindPhoneCall();
		if (!state.Holding()) {
			return;
		}
		if (state.PhoneCall && state.PhoneCall->Requested && !state.PhoneCall->Opened && ++state.PhoneCall->RequestPolls > state.Limits.MaximumConfirmationPolls) {
			state.Block("original phone open input exceeded its bounded readiness wait; genuine route input required");
			return;
		}
		const auto choice = state.Vm.FindContextFrame(*state.PlayerContext, "advengine/script/envplayer.nut", "doSelect");
		auto phone = state.Vm.FindContextFrame(*state.PlayerContext, "rsc_project/script/override.nut", "openUserInteractionPhone");
		if (phone && state.PhoneCall && state.PhoneCall->Requested) {
			state.PhoneCall->Opened = true;
			phone.reset();
		}
		if (choice || phone) {
			const bool selected_choice = choice && (!phone || choice->FrameIndex < phone->FrameIndex);
			state.State.Source = selected_choice ? "advengine/script/envplayer.nut" : "rsc_project/script/override.nut";
			state.State.Function = selected_choice ? "doSelect" : "openUserInteractionPhone";
			state.State.ScriptLine = selected_choice ? choice->SourceLine : phone->SourceLine;
			state.Block(selected_choice ? "original selection requires genuine route input; automatic seek released" : "original phone interaction requires genuine input; automatic seek released");
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
