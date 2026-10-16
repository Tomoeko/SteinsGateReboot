#include "sgre/script/original_calendar_restore.hpp"

#include "sgre/resource/archive_reader.hpp"
#include "sgre/script/game_bindings.hpp"
#include "sgre/script/object_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using sgre::resource::PsbArray;
using sgre::resource::PsbObject;
using sgre::resource::PsbValue;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void Reject(const auto& operation, std::string_view needle) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(needle) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("missing calendar rejection: " + std::string(needle));
}

static PsbValue Text(std::string text) {
	PsbValue value;
	value.Data = std::move(text);
	return value;
}

static PsbValue Number(std::int64_t number) {
	PsbValue value;
	value.Data = number;
	return value;
}

static PsbValue Array(PsbArray array) {
	PsbValue value;
	value.Data = std::move(array);
	return value;
}

static PsbValue Object(PsbObject object) {
	PsbValue value;
	value.Data = std::move(object);
	return value;
}

static PsbValue Calendar() {
	return Array({Text("calendar"), Text("week"), Text("木"), Text("mon"), Text("7"), Text("day"), Text("29")});
}

static sgre::resource::PsbDocument Document(PsbArray prefix, bool preevals = false) {
	prefix.push_back(Array({Number(24), PsbValue{}, PsbValue{}, PsbValue{}, Number(120)}));
	PsbObject scene{{"label", Text("*start")}, {"firstLine", Number(0)}, {"version", Number(1)}, {"lines", Array(std::move(prefix))}};
	if (preevals) {
		scene.emplace_back("preevals", Array({}));
	}
	sgre::resource::PsbDocument document;
	document.Root = Object({{"name", Text("fixture.ks")}, {"languages", Array({Text("en")})}, {"scenes", Array({Object(std::move(scene))})}});
	return document;
}

static sgre::scenario::ScenarioCalendarCommand Command() {
	const auto document = Document({Calendar()});
	const auto target = sgre::scenario::ValidateScenarioJump(document, "fixture.ks", 120);
	const auto result = sgre::scenario::ReadScenarioCalendarRestore(document, target);
	Require(result.Command.has_value(), "literal original calendar command was not admitted");
	return *result.Command;
}

static void TestResourceAdmissionAndOwnership() {
	auto document = Document({Calendar(), Array({Text("resg_phone"), Text("cmd"), Text("phone"), Text("action"), Text("enable")})});
	const auto target = sgre::scenario::ValidateScenarioJump(document, "fixture.ks", 120);
	const auto result = sgre::scenario::ReadScenarioCalendarRestore(document, target);
	Require(result.Command && result.Command->CommandIndex == 0 && result.Command->SnapshotIndex == 2, "resource admission changed exact producer/snapshot identity");
	document = {};
	Require(result.Command->Storage == "fixture.ks" && result.Command->SceneLabel == "*start" && result.Command->Attributes[0] == std::pair<std::string, std::string>{"week", "木"}, "owned original command fields must survive source-first document retirement");
	const std::vector<PsbArray> ambiguous{{}, {Calendar(), Calendar()}, {Array({Text("eval"), Text("exp"), Text("f.subwin_day=9")}), Calendar()}, {Calendar(), Array({Text("jump"), Text("target"), Text("*else")})}, {Calendar(), Array({Text("resg_subwin"), Text("progress"), Text("0"), Text("exp"), Text("true")})}, {Calendar(), Array({Text("unknown")})}, {Array({Text("calendar"), Text("mon"), Text("7"), Text("mon"), Text("8"), Text("week"), Text("木")})}};
	for (const auto& prefix : ambiguous) {
		const auto candidate = Document(prefix);
		const auto point = sgre::scenario::ValidateScenarioJump(candidate, "fixture.ks", 120);
		Require(!sgre::scenario::ReadScenarioCalendarRestore(candidate, point).Command, "unknown/conditional/duplicate/missing calendar evidence must not fabricate state");
	}
	const auto candidate = Document({Calendar()}, true);
	Require(!sgre::scenario::ReadScenarioCalendarRestore(candidate, sgre::scenario::ValidateScenarioJump(candidate, "fixture.ks", 120)).Command, "original pre-evaluation remains authoritative even when a literal calendar exists");
	auto wrong = sgre::scenario::ValidateScenarioJump(candidate, "fixture.ks", 120);
	++wrong.Point;
	Reject([&] {
		static_cast<void>(sgre::scenario::ReadScenarioCalendarRestore(candidate, wrong));
	},
	    "exact validated");
}

static void TestVisualEffectBoundary() {
	const auto update = [](PsbValue action) {
		return Array({Text("envupdate"), Text("update"), Array({Object({{"name", Text("actor")}, {"class", Text("character")}, {"action", Array({std::move(action)})}})})});
	};
	for (const bool allowed : {false, true}) {
		const auto candidate = Document({Calendar(), update(Array({Text(allowed ? "xpos" : "eval"), Number(1)}))});
		Require(sgre::scenario::ReadScenarioCalendarRestore(candidate, sgre::scenario::ValidateScenarioJump(candidate, "fixture.ks", 120)).Command.has_value() == allowed, "visual-only action classification must reject arbitrary computed effects");
	}
	PsbObject variables;
	for (std::size_t index = 0; index < 1024; ++index) {
		variables.emplace_back("value" + std::to_string(index), Number(static_cast<std::int64_t>(index)));
	}
	for (const bool duplicate : {false, true}) {
		auto values = variables;
		if (duplicate) {
			values.push_back(values.front());
		}
		const auto bounded = Document({Calendar(), Array({Text("envupdate"), Text("update"), Array({Object({{"name", Text("actor")}, {"class", Text("character")}, {"redraw", Object({{"imageFile", Object({{"options", Object({{"variables", Object(std::move(values))}})}})}})}})})})});
		Require(sgre::scenario::ReadScenarioCalendarRestore(bounded, sgre::scenario::ValidateScenarioJump(bounded, "fixture.ks", 120)).Command.has_value() != duplicate, "bounded unique literal variables remain admitted while duplicate keys remain rejected");
	}
	const auto reset = [](PsbValue value) {
		return Array({Text("envupdate"), Text("update"), Array({Array({Text("init"), std::move(value)})})});
	};
	const auto initial = Document({reset(Number(0)), Calendar()});
	Require(sgre::scenario::ReadScenarioCalendarRestore(initial, sgre::scenario::ValidateScenarioJump(initial, "fixture.ks", 120)).Command.has_value(), "literal initial environment reset before the original calendar must be admitted");
	for (const auto& prefix : std::vector<PsbArray>{{Calendar(), reset(Number(0))}, {reset(Number(1)), Calendar()}, {reset(PsbValue{}), Calendar()}, {Array({Text("delayrun"), Number(0), Text("envupdate"), Text("update"), Array({Array({Text("init"), Number(0)})})}), Calendar()}, {Array({Text("envupdate"), Text("update"), Array({Array({Text("new"), Text("actor"), Text("character")}), Array({Text("init"), Number(0)})})}), Calendar()}}) {
		const auto unsupported = Document(prefix);
		Require(!sgre::scenario::ReadScenarioCalendarRestore(unsupported, sgre::scenario::ValidateScenarioJump(unsupported, "fixture.ks", 120)).Command, "delayed, later, nonzero or noninitial environment resets require an authentic checkpoint");
	}
	const auto candidate = Document({Calendar(), Array({Text("envupdate"), Text("update"), Array({Object({{"name", Text("actor")}, {"link", Text("&f.subwin_day=1")}})})})});
	Require(!sgre::scenario::ReadScenarioCalendarRestore(candidate, sgre::scenario::ValidateScenarioJump(candidate, "fixture.ks", 120)).Command, "original ampersand expression path must remain outside literal proof");
}

static sgre::script::SquirrelInstruction Op(std::uint8_t code, std::uint8_t first = 0, std::int32_t second = 0, std::uint8_t third = 0, std::uint8_t fourth = 0) {
	sgre::script::SquirrelInstruction result;
	result.Opcode = code;
	result.Argument0 = first;
	result.Argument1 = second;
	result.Argument2 = third;
	result.Argument3 = fourth;
	return result;
}

static SquirrelValue OriginalResult(SquirrelVm& vm, bool pause = false) {
	sgre::script::BytecodeProgram program;
	auto& function = program.MainFunction;
	function.Name = std::string("prepared-onLoadScene");
	function.SourceName = std::string("prepared-calendar-fixture");
	function.Parameters = {std::string("this"), std::string("type"), std::string("current"), std::string("previous")};
	function.StackSize = 7;
	if (pause) {
		function.Literals = {std::string("pauseOriginal")};
		function.Instructions = {Op(8, 4, 0, 0, 5), Op(6, 255, 4, 5, 1)};
	}
	function.Instructions.push_back(Op(2, 4, 73));
	function.Instructions.push_back(Op(19, 1, 4));
	return vm.CreateClosure(std::move(program));
}

static SquirrelValue Caller(SquirrelVm& vm, std::string source = "advengine/script/envplayer.nut", std::size_t next = 157) {
	Require(next >= 5 && next <= 200, "bounded prepared original-caller metadata");
	sgre::script::BytecodeProgram program;
	auto& function = program.MainFunction;
	function.Name = std::string("main");
	function.SourceName = std::move(source);
	function.Parameters = {std::string("this"), std::string("type"), std::string("current"), std::string("previous")};
	function.StackSize = 10;
	function.Literals = {std::string("onLoadScene")};
	function.Instructions.resize(next - 5, Op(0));
	function.Instructions.insert(function.Instructions.end(), {Op(8, 5, 0, 0, 6), Op(10, 7, 1), Op(10, 8, 2), Op(10, 9, 3), Op(6, 4, 5, 6, 4), Op(19, 1, 4)});
	return vm.CreateClosure(std::move(program));
}

struct Fixture {
	SquirrelVm Vm{[] {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;
		return limits;
	}()};
	sgre::script::ScriptObjectBindings Objects{Vm};
	SquirrelValue Base;
	SquirrelValue Player;
	SquirrelValue Flags;
	SquirrelValue Application;
	sgre::script::SquirrelHostRoot PlayerRoot;
	std::size_t CalendarCalls = 0;

	Fixture() {
		Vm.InstallRecoveredBaseLibrary();
		sgre::script::InstallGameResourceProfile(Vm);
		const auto application_class = Vm.CreateClass();
		Vm.NewSlot(application_class, std::string("player"), SquirrelValue{});
		Application = Vm.CreateInstance(application_class);
		Vm.NewSlot(Vm.RootTable(), std::string("app"), Application);
		Base = Vm.CreateClass();
		Flags = Vm.CreateTable();
		Vm.NewSlot(Vm.RootTable(), std::string("ScenePlayer"), Base);
		Vm.NewSlot(Base, std::string("onLoadScene"), OriginalResult(Vm));
		Vm.NewSlot(Base, std::string("flags"), Flags);
		Vm.NewSlot(Base, std::string("cur"), std::int32_t{78});
		Vm.NewSlot(Base, std::string("curPoint"), std::int32_t{24});
		Vm.NewSlot(Base, std::string("curLine"), std::int32_t{120});
		Vm.NewSlot(Base, std::string("tag_calendar"), Vm.CreateNativeClosure("prepared-calendar-service", [this](SquirrelVm& active, auto arguments) {
			Require(arguments.size() == 2 && active.Type(arguments[1]) == sgre::script::SquirrelValueType::Table, "ordinary calendar CALL must receive original converted attribute table");
			Require(sgre::script::StringText(active.GetSlot(arguments[1], std::string("mon"))) == "7" && sgre::script::StringText(active.GetSlot(arguments[1], std::string("day"))) == "29" && sgre::script::StringText(active.GetSlot(arguments[1], std::string("week"))) == "木", "owned command conversion changed exact original strings");
			++CalendarCalls;
			return sgre::script::SquirrelNativeResult::NoValues();
		}));
	}

	void Derive() {
		const auto derived = Vm.CreateClass(Base);
		Vm.NewSlot(Vm.RootTable(), std::string("MyPlayer"), derived);
		Player = Vm.CreateInstance(derived);
		PlayerRoot = Vm.Retain(Player);
		Vm.SetSlot(Application, std::string("player"), Player);
	}

	[[nodiscard]] sgre::script::SquirrelRunResult Run(std::string source = "advengine/script/envplayer.nut", std::size_t next = 157, std::string current = "fixture.ks*start", SquirrelValue type = std::int32_t{0}) {
		return Vm.Invoke(Caller(Vm, std::move(source), next), std::array<SquirrelValue, 4>{Player, std::move(type), Vm.CreateString(std::move(current)), Vm.CreateString("")});
	}
};

static void TestOriginalBoundaryAndOneShot() {
	Fixture fixture;
	auto command = Command();
	sgre::script::OriginalCalendarRestore restore(fixture.Vm, command);
	command.Attributes[0].second = "caller-retired";
	restore.InstallAvailableHook();
	fixture.Derive();
	for (const auto& [source, pc, current] : std::array<std::tuple<std::string, std::size_t, std::string>, 3>{{{"wrong.nut", 157, "fixture.ks*start"}, {"advengine/script/envplayer.nut", 156, "fixture.ks*start"}, {"advengine/script/envplayer.nut", 157, "fixture.ks*other"}}}) {
		Require(std::get<std::int32_t>(fixture.Run(source, pc, current).Value) == 73 && fixture.CalendarCalls == 0, "out-of-scope call must preserve original result without date mutation");
	}
	Require(std::get<std::int32_t>(fixture.Run("advengine/script/envplayer.nut", 157, "fixture.ks*start", false).Value) == 73 && fixture.CalendarCalls == 0, "boolfalse must not substitute for original integer type0");
	const auto direct = fixture.Vm.Invoke(fixture.Vm.GetSlot(fixture.Player, std::string("onLoadScene")), std::array<SquirrelValue, 4>{fixture.Player, std::int32_t{0}, fixture.Vm.CreateString("fixture.ks*start"), fixture.Vm.CreateString("")});
	Require(std::get<std::int32_t>(direct.Value) == 73 && fixture.CalendarCalls == 0, "external native invocation must not fabricate original caller coordinates");
	const auto actual_player = fixture.Player;
	fixture.Player = fixture.Vm.CreateInstance(fixture.Vm.CreateClass(fixture.Base));
	Require(std::get<std::int32_t>(fixture.Run().Value) == 73 && fixture.CalendarCalls == 0, "another ScenePlayer-derived class must not receive this jump calendar");
	fixture.Player = fixture.Vm.CreateInstance(fixture.Vm.GetSlot(fixture.Vm.RootTable(), std::string("MyPlayer")));
	Require(std::get<std::int32_t>(fixture.Run().Value) == 73 && fixture.CalendarCalls == 0, "another same-scene MyPlayer instance must not replace the actual app.player owner");
	fixture.Player = actual_player;
	Require(std::get<std::int32_t>(fixture.Run().Value) == 73 && fixture.CalendarCalls == 1 && restore.Observation().Phase == sgre::script::OriginalCalendarRestorePhase::Applied, "exact ordinary onLoadScene boundary must execute the owned original command once");
	Require(std::get<std::int32_t>(fixture.Vm.GetSlot(fixture.Player, std::string("cur"))) == 78 && std::get<std::int32_t>(fixture.Vm.GetSlot(fixture.Player, std::string("curPoint"))) == 24 && std::get<std::int32_t>(fixture.Vm.GetSlot(fixture.Player, std::string("curLine"))) == 120, "calendar seam must not change scenario cursor/point/line");
	Require(std::get<std::int32_t>(fixture.Run().Value) == 73 && fixture.CalendarCalls == 1, "later scene loads must not reapply the jump calendar");
}

static void TestSavedFlagsAndPublication() {
	for (const std::size_t count : {std::size_t{1}, std::size_t{3}}) {
		Fixture fixture;
		const std::array<std::string, 3> keys{"subwin_month", "subwin_day", "subwin_week"};
		for (std::size_t index = 0; index < count; ++index) {
			fixture.Vm.NewSlot(fixture.Flags, keys[index], index == 0 ? SquirrelValue{} : SquirrelValue{std::int32_t{0}});
		}
		sgre::script::OriginalCalendarRestore restore(fixture.Vm, Command());
		restore.InstallAvailableHook();
		fixture.Derive();
		Require(fixture.Run().State == sgre::script::SquirrelRunState::Completed && fixture.CalendarCalls == 0 && restore.Observation().Phase == sgre::script::OriginalCalendarRestorePhase::Preserved, "complete/partial/null/zero original flags must be preserved without filling a subset");
	}
	Fixture late;
	late.Derive();
	sgre::script::OriginalCalendarRestore restore(late.Vm, Command());
	Reject([&] {
		restore.InstallAvailableHook();
	},
	    "precede MyPlayer");
	Fixture locked;
	static_cast<void>(locked.Vm.CreateInstance(locked.Base));
	sgre::script::OriginalCalendarRestore locked_restore(locked.Vm, Command());
	const auto original = locked.Vm.GetSlot(locked.Base, std::string("onLoadScene"));
	Reject([&] {
		locked_restore.InstallAvailableHook();
	},
	    "class is locked");
	Require(locked.Vm.GetSlot(locked.Base, std::string("onLoadScene")) == original, "late installation must preserve the original class hook");
}

static void TestYieldErrorGcAndCancellation() {
	for (const bool pause_original : {false, true}) {
		Fixture fixture;
		if (pause_original) {
			fixture.Vm.NewSlot(fixture.Base, std::string("pauseOriginal"), fixture.Vm.CreateNativeClosure("prepared-original-await", [](SquirrelVm& active, auto) {
				return active.RequestNativeSuspension();
			}));
			fixture.Vm.NewSlot(fixture.Base, std::string("onLoadScene"), OriginalResult(fixture.Vm, true));
		} else {
			fixture.Vm.NewSlot(fixture.Base, std::string("tag_calendar"), fixture.Vm.CreateNativeClosure("prepared-calendar-await", [&fixture](SquirrelVm& active, auto) {
				++fixture.CalendarCalls;
				return active.RequestNativeSuspension();
			}));
		}
		sgre::script::OriginalCalendarRestore restore(fixture.Vm, Command());
		restore.InstallAvailableHook();
		fixture.Derive();
		fixture.Vm.ResetContext(fixture.Vm.RootContext());
		static_cast<void>(fixture.Vm.CollectGarbage());
		Require(fixture.Run().State == sgre::script::SquirrelRunState::Suspended, "ordinary original/calendar CALL must retain a legitimate coroutine await");
		static_cast<void>(fixture.Vm.CollectGarbage());
		const auto resumed = fixture.Vm.Resume();
		Require(resumed.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(resumed.Value) == 73 && restore.Observation().Phase == sgre::script::OriginalCalendarRestorePhase::Applied && fixture.CalendarCalls == 1, "GC/resume must preserve original result, captures and one completed calendar call");
	}
	Fixture failure;
	failure.Vm.NewSlot(failure.Base, std::string("tag_calendar"), failure.Vm.CreateNativeClosure("prepared-original-error", [](SquirrelVm&, auto) -> sgre::script::SquirrelNativeResult {
		throw sgre::script::SquirrelVmException(std::string("exact-original-calendar-error"), "original calendar diagnostic");
	}));
	sgre::script::OriginalCalendarRestore restore(failure.Vm, Command());
	restore.InstallAvailableHook();
	failure.Derive();
	bool caught = false;
	try {
		static_cast<void>(failure.Run());
	} catch (const sgre::script::SquirrelVmException& error) {
		caught = true;
		Require(sgre::script::StringText(error.ThrownValue()) == "exact-original-calendar-error", "calendar wrapper must rethrow the exact original script value");
	}
	Require(caught && restore.Observation().Phase == sgre::script::OriginalCalendarRestorePhase::Failed, "original calendar failure must remain visible and cannot arm a retry");
	Fixture cancelled;
	{
		sgre::script::OriginalCalendarRestore owner(cancelled.Vm, Command());
		owner.InstallAvailableHook();
		owner.Cancel();
		cancelled.Derive();
		Require(cancelled.Run().State == sgre::script::SquirrelRunState::Completed && cancelled.CalendarCalls == 0, "cancelled jump completion must leave ordinary script flow untouched");
	}
	Require(cancelled.Run().State == sgre::script::SquirrelRunState::Completed && cancelled.CalendarCalls == 0, "expired weak host owner must preserve the installed original script path");
}

static const sgre::script::SquirrelFunction* Find(const sgre::script::SquirrelFunction& function, std::string_view name, std::uint32_t line) {
	if (std::get_if<std::string>(&function.Name) != nullptr && std::get<std::string>(function.Name) == name && !function.LineInformation.empty() && function.LineInformation.front().Line == line) {
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
	Require(function != nullptr && function->OuterBindings.empty() && function->DefaultParameters.empty(), "original calendar fixture prototype identity/default/capture changed");
	sgre::script::BytecodeProgram isolated;
	isolated.MainFunction = *function;
	return vm.CreateClosure(std::move(isolated));
}

static std::string ReadSeed(const std::filesystem::path& path) {
	Require(std::filesystem::file_size(path) <= 256, "bounded archive seed input");
	std::ifstream stream(path, std::ios::binary);
	std::string seed;
	std::getline(stream, seed);
	Require(static_cast<bool>(stream) && !seed.empty(), "archive seed read failed");
	if (seed.back() == '\r') {
		seed.pop_back();
	}
	return seed;
}

static void TestActualMainCallSite(const sgre::script::BytecodeProgram& environment, const sgre::script::BytecodeProgram& override_program, const sgre::script::BytecodeProgram& utility, const sgre::scenario::ScenarioCalendarCommand& command) {
	Fixture fixture;
	auto& vm = fixture.Vm;
	Require(vm.Invoke(vm.CreateClosure(utility), std::array<SquirrelValue, 1>{vm.RootTable()}).State == sgre::script::SquirrelRunState::Completed, "actual utility default/helper publication failed");
	for (const auto* field : {"forcePlayVoice", "doSyssaveOnSkipStop", "sceneMode", "sysflagMode", "playScenes", "playStartTime", "curSceneName", "curStorage", "sceneStart", "sceneCur", "sceneArg"}) {
		vm.NewSlot(fixture.Base, std::string(field), SquirrelValue{});
	}
	vm.NewSlot(fixture.Base, std::string("curSceneName"), vm.CreateString(""));
	for (const auto& [name, line] : std::array<std::pair<std::string_view, std::uint32_t>, 7>{{{"onLoadScene", 8452}, {"setFlag", 3391}, {"initFlag", 6132}, {"restoreFlag", 6160}, {"setSaveData", 6363}, {"getStorage", 4129}, {"getLabel", 4141}}}) {
		vm.NewSlot(fixture.Base, std::string(name), Original(vm, environment, name, line));
	}
	vm.NewSlot(fixture.Base, std::string("tag_calendar"), Original(vm, override_program, "tag_calendar", 1017));
	const auto weekdays = vm.CreateTable();
	const std::array<std::string, 7> names{"日", "月", "火", "水", "木", "金", "土"};
	for (std::size_t index = 0; index < names.size(); ++index) {
		vm.NewSlot(weekdays, names[index], static_cast<std::int32_t>(index));
	}
	vm.NewSlot(fixture.Base, std::string("wdayTbl"), weekdays);
	for (const auto* method : {"updateConfig", "clear", "setupTransition", "onSceneInit"}) {
		vm.NewSlot(fixture.Base, std::string(method), vm.CreateNativeClosure(method, [](SquirrelVm&, auto) {
			return sgre::script::SquirrelNativeResult::NoValues();
		}));
	}
	vm.RegisterNative("getCurrentSecond", [](SquirrelVm&, auto) {
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{0});
	});
	vm.NewSlot(fixture.Base, std::string("loadSceneLine"), vm.CreateNativeClosure("prepared-original-scenario-loader-boundary", [&](SquirrelVm& active, auto arguments) {
		Require(arguments.size() == 3 && sgre::script::StringText(arguments[1]) == command.Storage && std::get<std::int32_t>(arguments[2]) == 120, "actual main must forward exact canonical storage and requested line");
		active.SetSlot(arguments.front(), std::string("curSceneName"), active.CreateString(command.Storage + command.SceneLabel));
		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	bool snapshot_entered = false;
	vm.NewSlot(fixture.Base, std::string("goToLine"), vm.CreateNativeClosure("prepared-next-original-snapshot-boundary", [&](SquirrelVm& active, auto arguments) {
		Require(arguments.size() == 2 && std::get<std::int32_t>(arguments[1]) == 120, "original main must retain the requested source line");
		Require(std::get<std::int32_t>(active.GetSlot(fixture.Flags, std::string("subwin_month"))) == 7 && std::get<std::int32_t>(active.GetSlot(fixture.Flags, std::string("subwin_day"))) == 29 && std::get<std::int32_t>(active.GetSlot(fixture.Flags, std::string("subwin_week"))) == 4, "actual mainPC156 must restore original date before goToLinePC162");
		snapshot_entered = true;
		return active.RequestNativeSuspension();
	}));
	sgre::script::OriginalCalendarRestore restore(vm, command);
	restore.InstallAvailableHook();
	fixture.Derive();
	const auto scene = vm.CreateTable();
	vm.NewSlot(scene, std::string("storage"), vm.CreateString(command.Storage));
	vm.NewSlot(scene, std::string("target"), std::int32_t{120});
	std::string first_error;
	vm.SetTraceCallback([&](const sgre::script::SquirrelTraceEvent& event) {
		if (event.Source == "advengine/script/envplayer.nut" && event.Function == "main" && event.Instruction == 249 && first_error.empty()) {
			const auto value = vm.LastError();
			if (const auto* error = sgre::script::TryStringText(value)) {
				first_error = *error;
			}
		}
	});
	try {
		const auto result = vm.Invoke(Original(vm, environment, "main", 6422), std::array<SquirrelValue, 2>{fixture.Player, scene});
		Require(result.State == sgre::script::SquirrelRunState::Suspended && snapshot_entered && restore.Observation().Phase == sgre::script::OriginalCalendarRestorePhase::Applied, "unchanged original main must reach its actual calendar hook returnPC157 before snapshot restoration");
	} catch (const std::runtime_error&) {
		if (!first_error.empty()) {
			throw std::runtime_error("actual original main prepared boundary failed: " + first_error);
		}
		throw;
	}
	vm.SetTraceCallback({});
	vm.ResetContext(vm.RootContext());
}

static void TestActualCalendarHandlers(const std::filesystem::path& installation, const std::filesystem::path& seed_path) {
	const auto seed = ReadSeed(seed_path);
	sgre::resource::ArchiveReader scenarios(installation / "scenario_info.psb.m", installation / "scenario_body.bin", seed);
	const auto bytes = scenarios.Read("resg01_13.ks", "resg01_13.ks.scn.m");
	auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
	const auto target = sgre::scenario::ValidateScenarioJump(document, "resg01_13.ks", 120);
	auto plan = sgre::scenario::ReadScenarioCalendarRestore(document, target);
	Require(plan.Command && plan.Command->CommandIndex == 7 && plan.Command->SnapshotIndex == 78 && target.Point == 24, "actual calendar producer/snapshot differs from independent resource audit: " + plan.Reason);
	const auto& scenes = std::get<PsbArray>(document.Root.Find("scenes")->Data);
	const auto& lines = std::get<PsbArray>(scenes.front().Find("lines")->Data);
	std::vector<std::string> original_command;
	for (const auto& value : std::get<PsbArray>(lines[7].Data)) {
		original_command.push_back(std::get<std::string>(value.Data));
	}
	document = {};
	const auto owned_command = *plan.Command;
	sgre::resource::ArchiveReader scripts(installation / "script_info.psb.m", installation / "script_body.bin", seed);
	const auto environment = sgre::script::ReadSquirrelBytecode(scripts.Read("envplayer", "envplayer.nut.m"));
	const auto override_program = sgre::script::ReadSquirrelBytecode(scripts.Read("override", "override.nut.m"));
	const auto message = sgre::script::ReadSquirrelBytecode(scripts.Read("message", "message.nut.m"));
	const auto utility = sgre::script::ReadSquirrelBytecode(scripts.Read("util", "util.nut.m"));
	Fixture reference;
	reference.Vm.NewSlot(reference.Base, std::string("tag_calendar"), Original(reference.Vm, override_program, "tag_calendar", 1017));
	reference.Vm.NewSlot(reference.Base, std::string("setFlag"), Original(reference.Vm, environment, "setFlag", 3391));
	const auto reference_weekdays = reference.Vm.CreateTable();
	const std::array<std::string, 7> reference_names{"日", "月", "火", "水", "木", "金", "土"};
	for (std::size_t index = 0; index < reference_names.size(); ++index) {
		reference.Vm.NewSlot(reference_weekdays, reference_names[index], static_cast<std::int32_t>(index));
	}
	reference.Vm.NewSlot(reference.Base, std::string("wdayTbl"), reference_weekdays);
	reference.Derive();
	std::vector<SquirrelValue> raw_command;
	for (const auto& text : original_command) {
		raw_command.push_back(reference.Vm.CreateString(text));
	}
	Require(reference.Vm.Invoke(Original(reference.Vm, environment, "doTag", 5189), std::array<SquirrelValue, 2>{reference.Player, reference.Vm.CreateArray(raw_command)}).State == sgre::script::SquirrelRunState::Completed, "actual doTag pair-to-table conversion must reach original tag_calendar");
	Fixture fixture;
	fixture.Vm.NewSlot(fixture.Base, std::string("onLoadScene"), Original(fixture.Vm, environment, "onLoadScene", 8452));
	fixture.Vm.NewSlot(fixture.Base, std::string("tag_calendar"), Original(fixture.Vm, override_program, "tag_calendar", 1017));
	fixture.Vm.NewSlot(fixture.Base, std::string("setFlag"), Original(fixture.Vm, environment, "setFlag", 3391));
	const auto weekdays = fixture.Vm.CreateTable();
	const std::array<std::string, 7> names{"日", "月", "火", "水", "木", "金", "土"};
	for (std::size_t index = 0; index < names.size(); ++index) {
		fixture.Vm.NewSlot(weekdays, names[index], static_cast<std::int32_t>(index));
	}
	fixture.Vm.NewSlot(fixture.Base, std::string("wdayTbl"), weekdays);
	sgre::script::OriginalCalendarRestore restore(fixture.Vm, *plan.Command);
	plan = {};
	restore.InstallAvailableHook();
	fixture.Derive();
	Require(std::holds_alternative<std::monostate>(fixture.Run("advengine/script/envplayer.nut", 157, "resg01_13.ks*start").Value), "actual inherited onLoadScene null return must be preserved");
	Require(std::get<std::int32_t>(fixture.Vm.GetSlot(fixture.Flags, std::string("subwin_month"))) == 7 && std::get<std::int32_t>(fixture.Vm.GetSlot(fixture.Flags, std::string("subwin_day"))) == 29 && std::get<std::int32_t>(fixture.Vm.GetSlot(fixture.Flags, std::string("subwin_week"))) == 4, "actual original tag_calendar/setFlag must produce exact resource date and original weekday index");
	for (const auto* key : {"subwin_month", "subwin_day", "subwin_week"}) {
		Require(reference.Vm.GetSlot(reference.Flags, std::string(key)) == fixture.Vm.GetSlot(fixture.Flags, std::string(key)), "owned calendar table conversion must equal actual doTag conversion from independent raw resource command");
	}
	std::vector<std::pair<std::string, std::int32_t>> published;
	const auto subwin = fixture.Vm.CreateTable();
	fixture.Vm.NewSlot(subwin, std::string("setVariable"), fixture.Vm.CreateNativeClosure("prepared-actual-subwin-receiver", [&](SquirrelVm&, auto arguments) {
		Require(arguments.size() == 3 && arguments.front() == subwin, "original updateMsg changed actual subwin receiver");
		published.emplace_back(sgre::script::StringText(arguments[1]), std::get<std::int32_t>(arguments[2]));
		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	fixture.Vm.NewSlot(fixture.Flags, std::string("subwin_mail_on"), std::int32_t{0});
	fixture.Vm.NewSlot(fixture.Flags, std::string("subwin_phone_on"), std::int32_t{0});
	const auto receiver = fixture.Vm.CreateTable();
	fixture.Vm.NewSlot(receiver, std::string("f"), fixture.Flags);
	fixture.Vm.NewSlot(receiver, std::string("findEnvObject"), fixture.Vm.CreateNativeClosure("prepared-exact-subwin-lookup", [&](SquirrelVm&, auto arguments) {
		Require(arguments.size() == 2 && sgre::script::StringText(arguments[1]) == "subwin", "original date update changed lookup identity");
		return sgre::script::SquirrelNativeResult::ReturnValue(subwin);
	}));
	Require(fixture.Vm.Invoke(Original(fixture.Vm, message, "updateMsg", 818), std::array<SquirrelValue, 1>{receiver}).State == sgre::script::SquirrelRunState::Completed, "original message date publication failed");
	Require(published == std::vector<std::pair<std::string, std::int32_t>>{{"month", 7}, {"day", 29}, {"week", 4}, {"mail_on", 0}, {"phone_on", 0}}, "original updateMsg must publish exact original calendar values/order");
	TestActualMainCallSite(environment, override_program, utility, owned_command);
	std::cout << "actual calendar command7 snapshot78 point24; original onLoadScene/tag_calendar/setFlag/updateMsg; prepared caller/platform receiver boundaries\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 3, "usage: original_calendar_restore_tests [original-data-directory archive-seed-file]");
		TestResourceAdmissionAndOwnership();
		TestVisualEffectBoundary();
		TestOriginalBoundaryAndOneShot();
		TestSavedFlagsAndPublication();
		TestYieldErrorGcAndCancellation();
		if (argc == 3) {
			TestActualCalendarHandlers(argv[1], argv[2]);
		}
		std::cout << "original calendar restore tests passed; five synthetic groups plus optional actual resource/bytecode group\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
