#include "sgre/script/original_scene_seek.hpp"

#include "sgre/input/hub.hpp"
#include "sgre/script/driver_logs.hpp"
#include "sgre/script/game_bindings.hpp"
#include "sgre/script/input_bindings.hpp"
#include "sgre/script/native_format.hpp"
#include "sgre/script/object_bindings.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static SquirrelValue Call(SquirrelVm& vm, const SquirrelValue& owner, std::string_view name, std::span<const SquirrelValue> parameters = {}) {
	std::vector<SquirrelValue> arguments{owner};
	arguments.insert(arguments.end(), parameters.begin(), parameters.end());
	const auto result = vm.Invoke(vm.GetSlot(owner, std::string(name)), arguments);
	Require(result.State == sgre::script::SquirrelRunState::Completed, "fixture original method unexpectedly suspended");

	return result.Value;
}

static sgre::script::SquirrelInstruction Operation(std::uint8_t code, std::uint8_t destination, std::int32_t operand = 0) {
	sgre::script::SquirrelInstruction operation;
	operation.Opcode = code;
	operation.Argument0 = destination;
	operation.Argument1 = operand;

	return operation;
}

static SquirrelValue EmptyMethod(SquirrelVm& vm, std::string name, std::size_t parameters) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.Name = std::move(name);
	program.MainFunction.SourceName = std::string("prepared-seek-extension.nut");
	program.MainFunction.StackSize = static_cast<std::uint32_t>(parameters + 1);
	for (std::size_t index = 0; index != parameters; ++index) {
		program.MainFunction.Parameters.emplace_back(std::string(index == 0 ? "this" : "saveData"));
	}
	program.MainFunction.Instructions.push_back(Operation(19, 255));

	return vm.CreateClosure(std::move(program));
}

static sgre::script::OriginalSceneSeekTarget Target() {
	sgre::script::OriginalSceneSeekTarget target;
	target.Storage = "fixture.ks";
	target.RequestedLine = 19;
	target.SnapshotLine = 20;
	target.Point = 2;

	return target;
}

struct SeekFixture {
	sgre::script::ScriptDriverLogs Logs{[](std::string_view message) {
		throw std::runtime_error("unexpected original movie fixture script error: " + std::string(message));
	}};
	SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects{Vm};
	sgre::input::KeyMappings Mappings;
	sgre::script::ScriptInputBindings Inputs{Vm, Objects, Mappings};
	SquirrelValue Application;
	SquirrelValue Player;
	SquirrelValue Action;
	SquirrelValue CurrentScene;
	std::size_t StartCalls = 0;
	std::size_t Releases = 0;

	SeekFixture() {
		Logs.Initialize();
		sgre::script::InstallNativePrinting(Vm, [this](std::string_view message, bool error) {
			Logs.EmitFormatted(message, error);
			std::cerr << "original movie script output: " << message;
		});
		Vm.InstallRecoveredBaseLibrary();
		sgre::script::InstallGameResourceProfile(Vm);
		Application = Vm.CreateClass();
		Player = Vm.CreateClass();
		Vm.NewSlot(Vm.RootTable(), std::string("Application"), Application);
		Vm.NewSlot(Vm.RootTable(), std::string("ScenePlayer"), Player);
		Vm.NewSlot(Application, std::string("getMainScene"), EmptyMethod(Vm, "getMainScene", 1));
		Vm.NewSlot(Application, std::string("getStartScene"), Vm.CreateNativeClosure("prepared-actual-new-game-producer", [this](SquirrelVm& vm, auto arguments) {
			Require(arguments.size() == 2 && arguments[1] == CurrentScene, "original getStartScene requires this and the exact captured current value");
			++StartCalls;
			const auto result = vm.CreateTable();
			vm.NewSlot(result, std::string("storage"), vm.CreateString("first.ks"));

			return sgre::script::SquirrelNativeResult::ReturnValue(result);
		}));
		Vm.NewSlot(Player, std::string("checkInputAction"), Vm.CreateNativeClosure("prepared-unbound-original-query", [](SquirrelVm&, auto) {
			return sgre::script::SquirrelNativeResult::ReturnValue(false);
		}));
		Vm.NewSlot(Player, std::string("onSceneStart"), EmptyMethod(Vm, "onSceneStart", 2));
		Vm.NewSlot(Player, std::string("checkSaveMode"), EmptyMethod(Vm, "checkSaveMode", 1));
		Vm.NewSlot(Player, std::string("curSceneName"), Vm.CreateString("fixture.ks*real-label"));
		Vm.NewSlot(Player, std::string("curLine"), std::int32_t{0});
		Vm.NewSlot(Player, std::string("curPoint"), std::int32_t{0});
		Vm.NewSlot(Player, std::string("cancelForceSkip"), Vm.CreateNativeClosure("prepared-release-observer", [this](SquirrelVm&, auto arguments) {
			Require(arguments.size() == 2 && std::get<bool>(arguments[1]), "seek arrival changed original force-skip release arguments");
			++Releases;

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
		Action = Vm.CreateTable();
		Vm.NewSlot(Action, std::string("key"), std::int32_t{0});
		Vm.NewSlot(Action, std::string("relkey"), SquirrelValue{});
		Vm.NewSlot(Action, std::string("pressing"), true);
		Vm.NewSlot(Action, std::string("func"), std::int32_t{0});
		const auto actions = Vm.CreateTable();
		Vm.NewSlot(actions, std::string("force_skip_all"), Action);
		Vm.NewSlot(Vm.RootTable(), std::string("actionKeyMap"), actions);
	}

	SquirrelValue Start(sgre::script::OriginalSceneSeek& seek) {
		seek.InstallAvailableHooks();
		const auto derived = Vm.CreateClass(Application);
		Vm.NewSlot(Vm.RootTable(), std::string("MyApplication"), derived);
		const auto application = Vm.CreateInstance(derived);
		const auto start = Call(Vm, application, "getMainScene");
		Require(sgre::script::StringText(Vm.GetSlot(start, std::string("storage"))) == "first.ks" && !Vm.HasSlot(start, std::string("target")) && !Vm.HasSlot(start, std::string("flags")), "stateful seek fabricated a line target or route flags instead of using the real new-game producer");
		const auto player = Vm.CreateInstance(Player);
		static_cast<void>(Call(Vm, player, "onSceneStart", std::array<SquirrelValue, 1>{SquirrelValue{}}));

		return player;
	}
};

static std::vector<std::uint8_t> ReadBounded(const std::filesystem::path& path) {
	const auto length = std::filesystem::file_size(path);
	Require(length > 0 && length <= 2 * 1024 * 1024, "original seek fixture bytecode exceeds its bounded input");
	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
	std::ifstream input(path, std::ios::binary);
	input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	Require(static_cast<bool>(input), "original seek fixture bytecode read failed");

	return bytes;
}

static const sgre::script::SquirrelFunction* Find(const sgre::script::SquirrelFunction& function, std::string_view name, std::uint32_t line) {
	const auto* text = std::get_if<std::string>(&function.Name);
	if (text && *text == name && !function.LineInformation.empty() && function.LineInformation.front().Line == line) {
		return &function;
	}
	for (const auto& child : function.Functions) {
		if (const auto* found = Find(child, name, line)) {
			return found;
		}
	}

	return nullptr;
}

static SquirrelValue Original(SquirrelVm& vm, const sgre::script::BytecodeProgram& program, std::string_view name, std::uint32_t line) {
	const auto* function = Find(program.MainFunction, name, line);
	Require(function != nullptr && function->OuterBindings.empty(), "original movie fixture prototype identity/capture changed");
	sgre::script::BytecodeProgram isolated;
	if (function->DefaultParameters.empty()) {
		isolated.MainFunction = *function;

		return vm.CreateClosure(std::move(isolated));
	}
	std::size_t ordinal = 0;
	while (ordinal != program.MainFunction.Functions.size() && &program.MainFunction.Functions[ordinal] != function) {
		++ordinal;
	}
	Require(ordinal != program.MainFunction.Functions.size(), "movie default fixture requires a direct original module child");
	std::size_t creation_pc = 0;
	for (; creation_pc != program.MainFunction.Instructions.size(); ++creation_pc) {
		const auto& operation = program.MainFunction.Instructions[creation_pc];
		if (operation.Opcode == 48 && operation.Argument1 == static_cast<std::int32_t>(ordinal)) {
			break;
		}
	}
	Require(creation_pc != program.MainFunction.Instructions.size(), "original movie default publication was not found");
	auto remaining = function->DefaultParameters;
	std::size_t first_pc = creation_pc;
	while (!remaining.empty()) {
		Require(first_pc > 0 && creation_pc - first_pc < 16, "original movie default initializer exceeds its fixture bound");
		const auto& operation = program.MainFunction.Instructions[--first_pc];
		Require(operation.Opcode == 1 || operation.Opcode == 2 || operation.Opcode == 3 || operation.Opcode == 20 || operation.Opcode == 22, "original movie default requires an unrecovered prepared parent producer");
		const auto count = operation.Opcode == 20 ? static_cast<std::uint32_t>(operation.Argument1) : 1U;
		Require(count > 0 && count <= 16, "original movie default register span exceeds its bound");
		std::erase_if(remaining, [&](std::uint32_t slot) {
			return slot >= operation.Argument0 && slot - operation.Argument0 < count;
		});
	}
	isolated.MainFunction.Name = std::string("prepared-original-default-parent");
	isolated.MainFunction.SourceName = std::string("prepared-movie-default-parent.nut");
	isolated.MainFunction.Parameters = {std::string("this")};
	isolated.MainFunction.StackSize = program.MainFunction.StackSize;
	isolated.MainFunction.Literals = program.MainFunction.Literals;
	isolated.MainFunction.Functions.push_back(*function);
	isolated.MainFunction.Instructions.assign(program.MainFunction.Instructions.begin() + static_cast<std::ptrdiff_t>(first_pc), program.MainFunction.Instructions.begin() + static_cast<std::ptrdiff_t>(creation_pc + 1));
	auto& creation = isolated.MainFunction.Instructions.back();
	creation.Argument1 = 0;
	isolated.MainFunction.Instructions.push_back(Operation(19, 1, creation.Argument0));
	const auto created = vm.Invoke(vm.CreateClosure(std::move(isolated)), std::array<SquirrelValue, 1>{vm.RootTable()});
	Require(created.State == sgre::script::SquirrelRunState::Completed, "original movie default parent did not complete");

	return created.Value;
}

struct MoviePrograms {
	sgre::script::BytecodeProgram Utility;
	sgre::script::BytecodeProgram Override;
	sgre::script::BytecodeProgram Dialog;
	sgre::script::BytecodeProgram Panel;
	sgre::script::BytecodeProgram Confirm;
	sgre::script::BytecodeProgram Exception;
};

struct MovieFixture {
	SeekFixture Core;
	std::int32_t Tick = 0;
	bool Ready = false;
	std::size_t Presses = 0;
	std::size_t Ends = 0;
	std::size_t OriginalUpdates = 0;
	SquirrelValue Dialog;
	SquirrelValue Panel;
	SquirrelValue Button;
	SquirrelValue Params;

	explicit MovieFixture(const MoviePrograms& programs) {
		auto& vm = Core.Vm;
		Require(vm.Invoke(vm.CreateClosure(programs.Exception), std::array<SquirrelValue, 1>{vm.RootTable()}).State == sgre::script::SquirrelRunState::Completed, "actual original Exception/GameStateException/printException publication failed");
		Require(vm.Invoke(vm.CreateClosure(programs.Utility), std::array<SquirrelValue, 1>{vm.RootTable()}).State == sgre::script::SquirrelRunState::Completed, "actual movie utility publication failed");
		vm.NewSlot(vm.RootTable(), std::string("YESNO_MOVIESKIP"), vm.CreateString("prepared-system-message-id"));
		vm.NewSlot(vm.RootTable(), std::string("getCurrentTick"), vm.CreateNativeClosure("prepared-original-clock", [this](SquirrelVm&, auto) {
			return sgre::script::SquirrelNativeResult::ReturnValue(Tick);
		}));
		vm.NewSlot(Core.Player, std::string("getCurrentTick"), vm.GetSlot(vm.RootTable(), std::string("getCurrentTick")));
		vm.NewSlot(Core.Player, std::string("getBaseScreen"), EmptyMethod(vm, "prepared-base-screen", 1));
		vm.NewSlot(Core.Player, std::string("bPlayingTagWaitMovie"), false);
		vm.NewSlot(Core.Player, std::string("cur"), std::int32_t{1});
		vm.NewSlot(Core.Player, std::string("selectUpdateFlag"), false);
		vm.NewSlot(Core.Player, std::string("tag_waitmovie"), Original(vm, programs.Override, "tag_waitmovie", 1609));
		vm.NewSlot(Core.Player, std::string("showMovieSkipConfirm"), Original(vm, programs.Override, "showMovieSkipConfirm", 1595));
		vm.NewSlot(Core.Player, std::string("playSync"), vm.CreateNativeClosure("prepared-original-frame-yield", [](SquirrelVm& active, auto) {
			active.PushNativeValue({});

			return sgre::script::SquirrelNativeResult::StackStatus(-666);
		}));
		const auto movie = vm.CreateTable();
		vm.NewSlot(movie, std::string("canWaitMovie"), vm.CreateNativeClosure("prepared-live-movie-query", [](SquirrelVm&, auto) {
			return sgre::script::SquirrelNativeResult::ReturnValue(true);
		}));
		const auto environment = vm.CreateTable();
		const auto objects = vm.CreateTable();
		vm.NewSlot(objects, std::string("movie"), movie);
		vm.NewSlot(environment, std::string("objects"), objects);
		vm.NewSlot(Core.Player, std::string("env"), environment);
		const auto confirm = vm.CreateClass();
		for (const auto& [name, count] : std::array<std::pair<std::string_view, std::size_t>, 4>{{{"constructor", 4}, {"destructor", 1}, {"changeNormalMot", 1}, {"onMotionProcess", 3}}}) {
			vm.NewSlot(confirm, std::string(name), EmptyMethod(vm, std::string(name), count));
		}
		vm.NewSlot(confirm, std::string("onMotionUpdate"), vm.CreateNativeClosure("prepared-base-update-observer", [this](SquirrelVm&, auto) {
			++OriginalUpdates;

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(confirm, std::string("_fixturePanel"), SquirrelValue{});
		vm.NewSlot(confirm, std::string("confirm"), Original(vm, programs.Confirm, "confirm", 104));
		vm.NewSlot(vm.RootTable(), std::string("ConfirmDialog"), confirm);
		const auto button_class = vm.CreateClass();
		vm.NewSlot(button_class, std::string("onPress"), Original(vm, programs.Panel, "onPress", 1482));
		vm.NewSlot(button_class, std::string("onPressDone"), Original(vm, programs.Panel, "onPressDone", 1150));
		vm.NewSlot(button_class, std::string("getPressEnable"), vm.CreateNativeClosure("prepared-button-readiness-service", [this](SquirrelVm&, auto) {
			return sgre::script::SquirrelNativeResult::ReturnValue(Ready);
		}));
		vm.NewSlot(button_class, std::string("setSelect"), vm.CreateNativeClosure("prepared-motion-selection-service", [this](SquirrelVm&, auto arguments) {
			Require(arguments.size() == 3 && std::get<std::int32_t>(arguments[1]) == 4 && std::get<std::int32_t>(arguments[2]) == 1, "actual original onPress changed its press-state request");
			++Presses;

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
		for (const auto name : {"owner", "name", "change", "exp", "wait", "motion", "result", "done", "mainfunc"}) {
			vm.NewSlot(button_class, std::string(name), SquirrelValue{});
		}
		vm.NewSlot(button_class, std::string("enterFlag"), false);
		Button = vm.CreateInstance(button_class);
		vm.SetSlot(Button, std::string("name"), vm.CreateString("yes"));
		vm.SetSlot(Button, std::string("result"), std::int32_t{1});
		const auto group_class = vm.CreateClass();
		vm.NewSlot(group_class, std::string("_work"), Original(vm, programs.Panel, "_work", 4049));
		const auto parts = vm.CreateTable();
		vm.NewSlot(parts, std::string("yes"), Button);
		vm.NewSlot(group_class, std::string("_motionPartsMap"), parts);
		const auto group = vm.CreateInstance(group_class);
		const auto panel_class = vm.CreateClass();
		vm.NewSlot(panel_class, std::string("_mainFocus"), group);
		vm.NewSlot(panel_class, std::string("_owner"), SquirrelValue{});
		vm.NewSlot(panel_class, std::string("result"), SquirrelValue{});
		vm.NewSlot(panel_class, std::string("mainPanel"), true);
		vm.NewSlot(panel_class, std::string("exec"), Original(vm, programs.Panel, "exec", 5718));
		vm.NewSlot(panel_class, std::string("_exec"), Original(vm, programs.Panel, "_exec", 5696));
		vm.NewSlot(panel_class, std::string("onMotionButton"), Original(vm, programs.Panel, "onMotionButton", 5792));
		vm.NewSlot(panel_class, std::string("onProcess"), Original(vm, programs.Panel, "onProcess", 7450));
		vm.NewSlot(panel_class, std::string("isMotion"), vm.CreateNativeClosure("prepared-no-extra-hide-motion", [](SquirrelVm&, auto) {
			return sgre::script::SquirrelNativeResult::ReturnValue(false);
		}));
		vm.NewSlot(panel_class, std::string("lockMotion"), EmptyMethod(vm, "prepared-motion-lock", 1));
		vm.NewSlot(panel_class, std::string("endMotionWork"), vm.CreateNativeClosure("prepared-end-work-observer", [this](SquirrelVm& active, auto arguments) {
			Require(std::get<std::int32_t>(active.GetSlot(arguments[0], std::string("result"))) == 1, "original onProcess must publish actual yes-button result before ending work");
			++Ends;

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
		Panel = vm.CreateInstance(panel_class);
		vm.SetSlot(Button, std::string("owner"), Panel);
		vm.RegisterNative("preparedMovieOpen", [this](SquirrelVm& active, auto arguments) {
			Require(arguments.size() == 5 && sgre::script::StringText(arguments[2]) == "YESNO_MOVIESKIP" && std::get<std::int32_t>(arguments[3]) == 0 && sgre::script::StringText(arguments[4]) == "YESNO", "real original movie/ConfirmDialog chain changed its message/default/character arguments");
			Dialog = arguments[1];
			active.SetSlot(Panel, std::string("_owner"), Dialog);
			active.SetSlot(Dialog, std::string("_fixturePanel"), Panel);
			active.PushNativeValue({});

			return sgre::script::SquirrelNativeResult::StackStatus(-666);
		});
		sgre::script::BytecodeProgram open;
		open.MainFunction.Name = std::string("prepared-open-ui-service");
		open.MainFunction.SourceName = std::string("prepared-movie-ui-service.nut");
		open.MainFunction.Parameters = {std::string("this"), std::string("message"), std::string("cur"), std::string("chara")};
		open.MainFunction.StackSize = 11;
		open.MainFunction.Literals = {std::string("preparedMovieOpen"), std::string("_fixturePanel"), std::string("result")};
		auto prepare = Operation(8, 5, 0);
		prepare.Argument2 = 4;
		prepare.Argument3 = 6;
		auto call = Operation(6, 5, 5);
		call.Argument2 = 6;
		call.Argument3 = 5;
		auto panel_get = Operation(9, 1, 1);
		panel_get.Argument2 = 0;
		auto result_get = Operation(9, 1, 2);
		result_get.Argument2 = 1;
		open.MainFunction.Instructions = {Operation(21, 4), prepare, Operation(10, 7, 0), Operation(10, 8, 1), Operation(10, 9, 2), Operation(10, 10, 3), call, panel_get, result_get, Operation(19, 1, 1)};
		vm.NewSlot(confirm, std::string("open"), vm.CreateClosure(std::move(open)));
		Require(vm.Invoke(vm.CreateClosure(programs.Dialog), std::array<SquirrelValue, 1>{vm.RootTable()}).State == sgre::script::SquirrelRunState::Completed, "actual MovieSkipConfirmDialog class publication failed");
		Params = vm.CreateTable();
		vm.NewSlot(Params, std::string("name"), vm.CreateString("movie"));
		vm.NewSlot(Params, std::string("time"), std::int32_t{100000});
		vm.NewSlot(Params, std::string("canskip"), true);
	}

	void OnMotionUpdate() {
		auto& vm = Core.Vm;
		const auto context = vm.CreateContext();
		try {
			Require(vm.InvokeInContext(context, vm.GetSlot(Dialog, std::string("onMotionUpdate")), std::array<SquirrelValue, 2>{Dialog, Panel}).State == sgre::script::SquirrelRunState::Completed, "original movie update/owned-button request unexpectedly suspended");
			vm.ReleaseContext(context);
		} catch (...) {
			vm.ResetContext(context);
			vm.ReleaseContext(context);
			throw;
		}
	}
};

static void TestActualStartSceneRequiredCurrent(const MoviePrograms& programs) {
	SeekFixture fixture;
	auto& vm = fixture.Vm;
	const auto* function = Find(programs.Override.MainFunction, "getStartScene", 2614);
	Require(function != nullptr && function->Parameters.size() == 2 && function->DefaultParameters.empty() && function->OuterBindings.empty(), "actual getStartScene must retain both required parameters and its original no-capture prototype");
	Require(std::get<std::string>(function->Parameters[0]) == "this" && std::get<std::string>(function->Parameters[1]) == "cur", "actual getStartScene parameter identity changed");
	sgre::script::OriginalSceneSeek seek(vm, Target());
	seek.InstallAvailableHooks();
	const auto derived = vm.CreateClass(fixture.Application);
	vm.NewSlot(derived, std::string("getStartScene"), Original(vm, programs.Override, "getStartScene", 2614));
	vm.NewSlot(vm.RootTable(), std::string("MyApplication"), derived);
	const auto application = vm.CreateInstance(derived);
	const auto start = Call(vm, application, "getMainScene");
	Require(vm.Type(start) == sgre::script::SquirrelValueType::Table && sgre::script::StringText(vm.GetSlot(start, std::string("storage"))) == "reSG00_01.ks", "stateful entry must execute the actual two-argument original start producer and preserve its exact returned storage");
	Require(seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::WaitingForPlayer && fixture.StartCalls == 0 && !vm.HasSlot(start, std::string("target")) && !vm.HasSlot(start, std::string("flags")), "actual entry must publish only its genuine original scene table before the player starts");
	Require(std::holds_alternative<std::monostate>(Call(vm, application, "getMainScene")), "after original entry the captured null-current base method must remain the ordinary getMainScene producer");
	Require(fixture.Logs.Print().Cursor() == 0, "actual start producer must not enter a formatted script catch path");
	seek.Cancel();
}

static void TestActualMovieGatesAndConfirmation(const MoviePrograms& programs) {
	MovieFixture fixture(programs);
	auto& vm = fixture.Core.Vm;
	sgre::script::OriginalSceneSeek seek(vm, Target());
	const auto player = fixture.Core.Start(seek);
	Require(!std::get<bool>(Call(vm, player, "checkInputAction", std::array<SquirrelValue, 1>{vm.CreateString("cancel")})), "cancel outside the actual movie callsite must remain original");
	Require(vm.Invoke(vm.GetSlot(player, std::string("tag_waitmovie")), std::array<SquirrelValue, 2>{player, fixture.Params}).State == sgre::script::SquirrelRunState::Suspended, "original movie wait must reach its first genuine sync");
	fixture.Tick = 1000;
	Require(vm.Resume().State == sgre::script::SquirrelRunState::Suspended && vm.Resume().State == sgre::script::SquirrelRunState::Suspended && seek.Observation().MovieRequests == 0, "original elapsed gate must prevent early cancellation");
	fixture.Tick = 3000;
	Require(vm.Resume().State == sgre::script::SquirrelRunState::Suspended, "original movie must reach its actual confirmation UI service and retain that suspended UI continuation until the genuine button result");
	Require(seek.Observation().MovieRequests == 1 && std::get<bool>(vm.GetSlot(player, std::string("selectUpdateFlag"))) && !std::holds_alternative<std::monostate>(fixture.Dialog), "original cancellable movie must construct/enter real MovieSkipConfirmDialog exactly once");
	fixture.OnMotionUpdate();
	Require(fixture.Presses == 0 && fixture.Ends == 0 && seek.Observation().MovieConfirmations == 0, "unready original yes button must not be forced");
	fixture.Ready = true;
	fixture.OnMotionUpdate();
	Require(fixture.Presses == 1 && fixture.Ends == 1 && seek.Observation().MovieConfirmations == 1 && fixture.OriginalUpdates == 2, "original callback/readiness/press/process chain must run in order before resuming movie control");
	Require(fixture.Core.Logs.Print().Cursor() == 0, "the actual null-expression button route must complete without entering its original catch-only printf/printException diagnostics");
	fixture.OnMotionUpdate();
	Require(fixture.Presses == 1 && fixture.Ends == 1, "a completed owned confirmation request must not be replayed");
	Require(vm.Resume().State == sgre::script::SquirrelRunState::Completed && !std::get<bool>(vm.GetSlot(player, std::string("bPlayingTagWaitMovie"))), "actual original confirmation result1 must return through showMovieSkipConfirm and exit tag_waitmovie");
	static_cast<void>(Call(vm, fixture.Dialog, "destructor"));
	Require(!std::get<bool>(vm.GetSlot(player, std::string("selectUpdateFlag"))), "actual original MovieSkipConfirmDialog destructor must restore player update ownership");
	seek.Cancel();
}

static void TestUncancellableMovieRetainsAuthoredGate(const MoviePrograms& programs) {
	MovieFixture fixture(programs);
	auto& vm = fixture.Core.Vm;
	vm.SetSlot(fixture.Params, std::string("canskip"), false);
	vm.SetSlot(fixture.Params, std::string("time"), std::int32_t{4000});
	sgre::script::OriginalSceneSeek seek(vm, Target());
	const auto player = fixture.Core.Start(seek);
	Require(vm.Invoke(vm.GetSlot(player, std::string("tag_waitmovie")), std::array<SquirrelValue, 2>{player, fixture.Params}).State == sgre::script::SquirrelRunState::Suspended, "uncancellable original movie must run its genuine sync");
	fixture.Tick = 3000;
	Require(vm.Resume().State == sgre::script::SquirrelRunState::Suspended && vm.Resume().State == sgre::script::SquirrelRunState::Suspended, "uncancellable original movie must continue after the elapsed gate");
	Require(seek.Observation().MovieRequests == 0 && std::holds_alternative<std::monostate>(fixture.Dialog), "explicit seek must not fabricate cancellation for authored canskipfalse");
	fixture.Tick = 4000;
	Require(vm.Resume().State == sgre::script::SquirrelRunState::Completed && !std::get<bool>(vm.GetSlot(player, std::string("bPlayingTagWaitMovie"))), "uncancellable movie may finish only through its original timeout at this prepared service boundary");
	seek.Cancel();
}

int main(int argc, char** argv) {
	try {
		Require(argc == 7, "usage: original_scene_seek_movie_tests util-bytecode override-bytecode exdialog-bytecode motionpanel-bytecode confirmdialog-bytecode exception-bytecode");
		MoviePrograms programs;
		programs.Utility = sgre::script::ReadSquirrelBytecode(ReadBounded(argv[1]));
		programs.Override = sgre::script::ReadSquirrelBytecode(ReadBounded(argv[2]));
		programs.Dialog = sgre::script::ReadSquirrelBytecode(ReadBounded(argv[3]));
		programs.Panel = sgre::script::ReadSquirrelBytecode(ReadBounded(argv[4]));
		programs.Confirm = sgre::script::ReadSquirrelBytecode(ReadBounded(argv[5]));
		programs.Exception = sgre::script::ReadSquirrelBytecode(ReadBounded(argv[6]));
		TestActualStartSceneRequiredCurrent(programs);
		TestActualMovieGatesAndConfirmation(programs);
		TestUncancellableMovieRetainsAuthoredGate(programs);
		std::cout << "original movie skip control tests passed; real original script handlers with prepared clock/UI/motion readiness services\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
