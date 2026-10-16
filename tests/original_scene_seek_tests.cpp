#include "sgre/script/original_scene_seek.hpp"

#include "sgre/input/function_keys.hpp"
#include "sgre/input/hub.hpp"
#include "sgre/script/game_bindings.hpp"
#include "sgre/script/input_bindings.hpp"
#include "sgre/script/object_bindings.hpp"

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

static void RequireOriginalReleasedInput(const SquirrelValue& value, std::string_view context) {
	const auto* integer = std::get_if<std::int32_t>(&value);
	if (integer == nullptr || *integer != 0) {
		std::string actual = "variant-index=" + std::to_string(value.index());
		if (integer) {
			actual += " integer=" + std::to_string(*integer);
		}
		throw std::runtime_error("actual original released input must return integer0 at " + std::string(context) + "; actual " + actual);
	}
}

static void RunOriginalFixturePhase(std::string_view phase, const auto& operation) {
	std::cerr << "seekFixturePhase begin=" << phase << '\n';
	try {
		operation();
	} catch (const std::bad_variant_access&) {
		throw std::runtime_error("seek fixture untyped variant access failed in " + std::string(phase));
	}
	std::cerr << "seekFixturePhase completed=" << phase << '\n';
}

static void Reject(const auto& operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("expected original scene seek rejection");
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

static sgre::script::SquirrelRunResult InvokeMethodSteps(SquirrelVm& vm, const SquirrelValue& owner, std::string_view method, std::size_t steps) {
	sgre::script::BytecodeProgram caller;
	caller.MainFunction.Name = std::string("prepared-containing-method-loop");
	caller.MainFunction.SourceName = std::string("prepared-seek-containing-loop.nut");
	caller.MainFunction.Parameters = {std::string("this")};
	caller.MainFunction.StackSize = 3;
	caller.MainFunction.Literals = {std::string(method)};
	for (std::size_t step = 0; step != steps; ++step) {
		auto prepare = Operation(8, 1, 0);
		prepare.Argument2 = 0;
		prepare.Argument3 = 2;
		auto call = Operation(6, 1, 1);
		call.Argument2 = 2;
		call.Argument3 = 1;
		caller.MainFunction.Instructions.push_back(prepare);
		caller.MainFunction.Instructions.push_back(call);
	}
	caller.MainFunction.Instructions.push_back(Operation(19, 255));

	return vm.Invoke(vm.CreateClosure(std::move(caller)), std::array<SquirrelValue, 1>{owner});
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
	SquirrelVm Vm;
	sgre::script::ScriptObjectBindings Objects{Vm};
	sgre::input::KeyMappings Mappings;
	sgre::input::FunctionKeySnapshot FunctionKeys;
	sgre::script::ScriptInputBindings Inputs{Vm, Objects, Mappings};
	SquirrelValue Application;
	SquirrelValue Player;
	SquirrelValue Action;
	SquirrelValue CurrentScene;
	std::size_t StartCalls = 0;
	std::size_t Releases = 0;

	explicit SeekFixture(sgre::script::SquirrelVmLimits limits = {}) : Vm(limits) {
		Inputs.ConfigureFunctionKeys([this] {
			return FunctionKeys;
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

static void TestCurrentSceneArgumentPropagation() {
	SeekFixture fixture;
	fixture.CurrentScene = fixture.Vm.CreateTable();
	std::size_t current_calls = 0;
	fixture.Vm.NewSlot(fixture.Application, std::string("getMainScene"), fixture.Vm.CreateNativeClosure("prepared-original-current-scene", [&fixture, &current_calls](SquirrelVm&, auto arguments) {
		Require(arguments.size() == 1, "captured original getMainScene requires only its genuine receiver");
		++current_calls;

		return sgre::script::SquirrelNativeResult::ReturnValue(fixture.CurrentScene);
	}));
	sgre::script::OriginalSceneSeek seek(fixture.Vm, Target());
	static_cast<void>(fixture.Start(seek));
	Require(current_calls == 1 && fixture.StartCalls == 1 && seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Progressing, "the first entry must obtain the actual current scene once before the original two-argument start producer");
	const auto application = fixture.Vm.CreateInstance(fixture.Vm.GetSlot(fixture.Vm.RootTable(), std::string("MyApplication")));
	Require(Call(fixture.Vm, application, "getMainScene") == fixture.CurrentScene && current_calls == 2 && fixture.StartCalls == 1, "later getMainScene calls must retain the captured original current value without replaying new-game entry");
	seek.Cancel();
}

static void TestFirstEntryFailureLatchesAndPreservesException() {
	for (std::size_t stage = 0; stage != 4; ++stage) {
		SeekFixture fixture;
		auto& vm = fixture.Vm;
		const auto failure = vm.CreateString(stage == 1 ? "first-entry-original-current" : "first-entry-original-start");
		if (stage == 0) {
			vm.SetSlot(fixture.Action, std::string("pressing"), false);
		} else if (stage == 1 || stage == 2) {
			const auto throwing = vm.CreateNativeClosure("prepared-first-entry-original-failure", [failure](SquirrelVm&, auto) -> sgre::script::SquirrelNativeResult {
				throw sgre::script::SquirrelVmException(failure, "prepared first-entry original failure");
			});
			vm.NewSlot(fixture.Application, std::string(stage == 1 ? "getMainScene" : "getStartScene"), throwing);
		} else {
			vm.NewSlot(fixture.Application, std::string("getStartScene"), vm.CreateNativeClosure("prepared-nontable-original-entry", [](SquirrelVm&, auto arguments) {
				Require(arguments.size() == 2, "failed original start still requires both genuine arguments");

				return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{7});
			}));
		}
		sgre::script::OriginalSceneSeek seek(vm, Target());
		seek.InstallAvailableHooks();
		const auto derived = vm.CreateClass(fixture.Application);
		vm.NewSlot(vm.RootTable(), std::string("MyApplication"), derived);
		const auto application = vm.CreateInstance(derived);
		bool caught = false;
		try {
			static_cast<void>(Call(vm, application, "getMainScene"));
		} catch (const sgre::script::SquirrelVmException& error) {
			caught = true;
			if (stage == 1 || stage == 2) {
				Require(error.ThrownValue() == failure, "first-entry failure latch must preserve the exact original thrown value");
			}
		}
		Require(caught && seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Failed && fixture.StartCalls == 0 && seek.Observation().Snapshots == 0, "failed original entry must leave Failed with no fabricated start or target progression");
		const std::array<std::string_view, 4> expected{"recovered pressing-only force_skip_all action", "prepared first-entry original failure", "prepared first-entry original failure", "actual derived new-game scene table"};
		Require(seek.Observation().Reason.find(expected[stage]) != std::string::npos, "first-entry failure reason must retain its precise original producer error");
		vm.ResetContext(vm.RootContext());
		seek.Cancel();
		Require(seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Failed, "owner cleanup must preserve the first-entry failure diagnosis");
	}
}

static void TestInputArrivalAndOwnership() {
	SeekFixture fixture;
	auto target = Target();
	sgre::script::OriginalSceneSeek seek(fixture.Vm, target);
	target.Storage.assign("caller-retired.ks");
	const auto player = fixture.Start(seek);
	sgre::input::HubFrameSample sample;
	sample.Devices[0].Buttons = 1;
	Require(sample.Devices[0].Buttons == 1 && !sample.Devices[0].Connected && fixture.StartCalls == 1 && std::get<std::int32_t>(fixture.Vm.GetSlot(fixture.Action, std::string("key"))) == 0, "semantic seek must preserve the original unbound key0 map and genuine sampled input");
	Require(std::get<bool>(Call(fixture.Vm, player, "checkInputAction", std::array<SquirrelValue, 1>{fixture.Vm.CreateString("force_skip_all")})) && !std::get<bool>(Call(fixture.Vm, player, "checkInputAction", std::array<SquirrelValue, 1>{fixture.Vm.CreateString("go")})), "only the owned player's semantic force_skip_all query may be supplied by this launch policy");
	fixture.Vm.SetSlot(player, std::string("curLine"), std::int32_t{20});
	fixture.Vm.SetSlot(player, std::string("curPoint"), std::int32_t{1});
	static_cast<void>(Call(fixture.Vm, player, "checkSaveMode"));
	Require(seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Progressing && fixture.Releases == 0, "same source line at a different actual point must not finish a seek");
	fixture.Vm.SetSlot(player, std::string("curPoint"), std::int32_t{2});
	Require(InvokeMethodSteps(fixture.Vm, player, "checkSaveMode", 1).State == sgre::script::SquirrelRunState::Suspended, "arrival must yield its actual containing VM callsite");
	Require(seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Arrived && fixture.Releases == 1 && seek.Observation().Storage == "fixture.ks", "exact original snapshot hook must release the original skip path once");
	sample.Devices[0].Buttons = 1;
	Require(sample.Devices[0].Buttons == 1, "arrival must preserve genuine user input");
	Require(fixture.Vm.Resume().State == sgre::script::SquirrelRunState::Completed, "arrival's one-shot suspension must resume ordinary continuation");
	Require(!std::get<bool>(Call(fixture.Vm, player, "checkInputAction", std::array<SquirrelValue, 1>{fixture.Vm.CreateString("force_skip_all")})), "arrived query must immediately return to the original unbound action");
	static_cast<void>(Call(fixture.Vm, player, "checkSaveMode"));
	Require(fixture.Releases == 1, "later original snapshot callbacks must not repeat completed seek effects");
}

static void TestCancellationLimitsAndLateInstallation() {
	SeekFixture fixture;
	sgre::script::OriginalSceneSeekLimits limits;
	limits.MaximumPolls = 1;
	sgre::script::OriginalSceneSeek seek(fixture.Vm, Target(), limits);
	const auto player = fixture.Start(seek);
	seek.Poll();
	seek.Poll();
	Require(seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Blocked && seek.Observation().Reason.find("frame limit") != std::string::npos, "bounded progression must not claim arrival");
	sgre::input::HubFrameSample sample;
	Require(sample.Devices[0].Buttons == 0 && fixture.Releases == 0 && !std::get<bool>(Call(fixture.Vm, player, "checkInputAction", std::array<SquirrelValue, 1>{fixture.Vm.CreateString("force_skip_all")})), "idle blocking releases the semantic action without input rebinding or VM resume");
	SeekFixture cancelled;
	sgre::script::OriginalSceneSeek cancellation(cancelled.Vm, Target());
	const auto cancelled_player = cancelled.Start(cancellation);
	cancellation.Cancel();
	Require(cancellation.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Cancelled && sample.Devices[0].Buttons == 0 && !std::get<bool>(Call(cancelled.Vm, cancelled_player, "checkInputAction", std::array<SquirrelValue, 1>{cancelled.Vm.CreateString("force_skip_all")})), "explicit cancellation releases only its own semantic action");
	SeekFixture late;
	late.Vm.NewSlot(late.Vm.RootTable(), std::string("MyPlayer"), late.Vm.CreateClass(late.Player));
	sgre::script::OriginalSceneSeek late_seek(late.Vm, Target());
	Reject([&] {
		late_seek.InstallAvailableHooks();
	},
	    "player derivation");
}

static void TestAuthenticBoundaryContextPolicy() {
	SeekFixture fixture;
	sgre::script::OriginalSceneSeek seek(fixture.Vm, Target());
	const auto player = fixture.Start(seek);
	fixture.Vm.RegisterNative("preparedYield", [](SquirrelVm&, auto) {
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	});
	// This bounded prepared yield tests source/context classification only; the actual
	// original dispatcher/action functions are independently exercised below.
	sgre::script::BytecodeProgram program;
	program.MainFunction.Name = std::string("doSelect");
	program.MainFunction.SourceName = std::string("advengine/script/envplayer.nut");
	program.MainFunction.Parameters.emplace_back(std::string("this"));
	program.MainFunction.StackSize = 4;
	program.MainFunction.Literals.emplace_back(std::string("preparedYield"));
	auto prepare = Operation(8, 1, 0);
	prepare.Argument2 = 1;
	prepare.Argument3 = 2;
	auto call = Operation(6, 1, 1);
	call.Argument2 = 2;
	call.Argument3 = 1;
	program.MainFunction.Instructions = {Operation(21, 1), prepare, call, Operation(19, 255)};
	const auto closure = fixture.Vm.CreateClosure(std::move(program));
	const auto unrelated = fixture.Vm.CreateContext();
	Require(fixture.Vm.InvokeInContext(unrelated, closure, std::array<SquirrelValue, 1>{player}).State == sgre::script::SquirrelRunState::Suspended, "prepared unrelated context must yield");
	seek.Poll();
	Require(seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Progressing, "an unrelated context must not cancel the actual player seek");
	fixture.Vm.ResetContext(unrelated);
	fixture.Vm.ReleaseContext(unrelated);
	Require(fixture.Vm.Invoke(closure, std::array<SquirrelValue, 1>{player}).State == sgre::script::SquirrelRunState::Suspended, "prepared actual player boundary must yield");
	seek.Poll();
	Require(seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Blocked && seek.Observation().Function == "doSelect", "yielded actual-player route input must be reported without choosing or resuming it");
	sgre::input::HubFrameSample sample;
	Require(sample.Devices[0].Buttons == 0 && fixture.Vm.ContextState(fixture.Vm.RootContext()) == sgre::script::SquirrelContextState::Suspended, "blocked seek must release owned input and preserve the suspended original context");
	fixture.Vm.ResetContext(fixture.Vm.RootContext());
}

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
	Require(function != nullptr && function->OuterBindings.empty(), "original seek fixture prototype identity changed");
	sgre::script::BytecodeProgram isolated;
	if (!function->DefaultParameters.empty()) {
		Require(((name == "cancelForceSkip" && line == 7576) || (name == "startForceSkip" && line == 7567)) && function->DefaultParameters == std::vector<std::uint32_t>{4}, "original seek fixture requires its proven default-producing parent instructions");
		const std::size_t ordinal = name == "cancelForceSkip" ? 454 : 453;
		const std::size_t initializer_pc = name == "cancelForceSkip" ? 2312 : 2308;
		Require(program.MainFunction.Functions.size() > ordinal && &program.MainFunction.Functions[ordinal] == function && program.MainFunction.Instructions.size() > initializer_pc + 1, "actual force-skip parent identity changed");
		const auto initializer = program.MainFunction.Instructions[initializer_pc];
		const auto creation = program.MainFunction.Instructions[initializer_pc + 1];
		Require(initializer.Opcode == 22 && initializer.Argument0 == 4 && initializer.Argument1 == 0 && creation.Opcode == 48 && creation.Argument0 == 4 && creation.Argument1 == static_cast<std::int32_t>(ordinal), "actual force-skip false-default publication changed");
		isolated.MainFunction.Name = std::string("prepared-original-default-parent");
		isolated.MainFunction.SourceName = std::string("prepared-seek-default-parent.nut");
		isolated.MainFunction.Parameters = {std::string("this")};
		isolated.MainFunction.StackSize = 5;
		isolated.MainFunction.Functions.push_back(*function);
		auto local_creation = creation;
		local_creation.Argument1 = 0;
		isolated.MainFunction.Instructions = {initializer, local_creation, Operation(19, 1, 4)};
		const auto result = vm.Invoke(vm.CreateClosure(std::move(isolated)), std::array<SquirrelValue, 1>{vm.RootTable()});
		Require(result.State == sgre::script::SquirrelRunState::Completed && vm.Type(result.Value) == sgre::script::SquirrelValueType::Closure, "original default-producing closure factory failed");

		return result.Value;
	}
	isolated.MainFunction = *function;

	return vm.CreateClosure(std::move(isolated));
}

static SquirrelValue InstallOriginalActionInput(SeekFixture& fixture, const sgre::script::BytecodeProgram& program, const sgre::script::BytecodeProgram& input_program) {
	auto& vm = fixture.Vm;
	// The remaining original map fields use these prepared platform constants.
	vm.NewSlot(vm.RootTable(), std::string("KEY_OK"), std::int32_t{1});
	vm.NewSlot(vm.RootTable(), std::string("KEY_CANCEL"), std::int32_t{2});
	vm.NewSlot(vm.RootTable(), std::string("upperExtraKey"), std::int32_t{0});
	vm.NewSlot(vm.RootTable(), std::string("checkwheel"), false);
	vm.NewSlot(vm.RootTable(), std::string("checkmouse"), false);
	vm.NewSlot(vm.RootTable(), std::string("checktouch"), false);
	Require(vm.Invoke(Original(vm, input_program, "updateKeyBind", 633), std::array<SquirrelValue, 1>{vm.RootTable()}).State == sgre::script::SquirrelRunState::Completed, "actual original key-binding publication failed");
	const auto force_action = vm.GetSlot(vm.GetSlot(vm.RootTable(), std::string("actionKeyMap")), std::string("force_skip_all"));
	Require(std::get<std::int32_t>(vm.GetSlot(force_action, std::string("key"))) == 0 && std::get<bool>(vm.GetSlot(force_action, std::string("pressing"))), "actual default force_skip_all must remain unbound key0");
	vm.NewSlot(fixture.Player, std::string("checkInputAction"), Original(vm, program, "checkInputAction", 1601));
	const auto hub = vm.Invoke(fixture.Inputs.InputHubClass(), std::array<SquirrelValue, 1>{vm.RootTable()}).Value;
	const auto device = Call(vm, hub, "inputAt", std::array<SquirrelValue, 1>{std::int32_t{0}});
	const auto input = vm.CreateTable();
	vm.NewSlot(input, std::string("_input"), device);
	vm.NewSlot(input, std::string("actionKeyMap"), vm.GetSlot(vm.RootTable(), std::string("actionKeyMap")));
	vm.NewSlot(input, std::string("checkAction"), Original(vm, input_program, "checkAction", 836));
	vm.NewSlot(input, std::string("_checkAction"), Original(vm, input_program, "_checkAction", 857));
	vm.NewSlot(fixture.Player, std::string("input"), input);

	return hub;
}

static void TestOriginalSequentialDispatcher(const sgre::script::BytecodeProgram& program, const sgre::script::BytecodeProgram& utility, const sgre::script::BytecodeProgram& input_program) {
	SeekFixture fixture;
	auto& vm = fixture.Vm;
	Require(vm.Invoke(vm.CreateClosure(utility), std::array<SquirrelValue, 1>{vm.RootTable()}).State == sgre::script::SquirrelRunState::Completed, "actual utility helper publication failed");
	const auto player_class = fixture.Player;
	const auto hub = InstallOriginalActionInput(fixture, program, input_program);
	for (const auto& [name, line] : std::array<std::pair<std::string_view, std::uint32_t>, 8>{{{"sceneMain", 6894}, {"getLine", 4157}, {"doTag", 5189}, {"setFlag", 3391}, {"getFlag", 3371}, {"checkSaveMode", 3793}, {"onSceneStart", 8482}, {"cancelForceSkip", 7576}}}) {
		vm.NewSlot(player_class, std::string(name), Original(vm, program, name, line));
	}
	vm.NewSlot(player_class, std::string("forceSkip"), std::int32_t{2});
	vm.NewSlot(player_class, std::string("syncForceSkipStop"), false);
	std::size_t original_release_phases = 0;
	vm.NewSlot(player_class, std::string("checkPlayMode"), vm.CreateNativeClosure("prepared-play-mode-publication", [&](SquirrelVm&, auto arguments) {
		Require(arguments.size() == 2 && std::get<bool>(arguments[1]), "original cancelForceSkip changed its user flag");
		++original_release_phases;

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	vm.NewSlot(player_class, std::string("cur"), std::int32_t{0});
	vm.NewSlot(player_class, std::string("flags"), vm.CreateTable());
	vm.NewSlot(player_class, std::string("scenario"), vm.CreateTable());
	std::vector<std::string> dispatch;
	vm.NewSlot(player_class, std::string("tag_setflag"), vm.CreateNativeClosure("prepared-scenario-tag-service", [&](SquirrelVm& active, auto arguments) {
		const auto name = active.GetSlot(arguments[1], std::string("name"));
		const auto value = active.GetSlot(arguments[1], std::string("value"));
		dispatch.emplace_back(sgre::script::StringText(name));
		if (sgre::script::StringText(name) == "after") {
			const auto held = active.InvokeInContext(active.CurrentContext(), active.GetSlot(arguments[0], std::string("checkInputAction")), std::array<SquirrelValue, 2>{arguments[0], active.CreateString("force_skip_all")});
			Require(held.State == sgre::script::SquirrelRunState::Completed, "future original tag must observe the next real released InputHub sample");
			RequireOriginalReleasedInput(held.Value, "post-arrival original checkInputAction");
		}
		static_cast<void>(active.InvokeInContext(active.CurrentContext(), active.GetSlot(arguments[0], std::string("setFlag")), std::array<SquirrelValue, 3>{arguments[0], name, value}));

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	std::size_t read_markers = 0;
	vm.NewSlot(player_class, std::string("setReadFlag"), vm.CreateNativeClosure("prepared-read-flag-service", [&](SquirrelVm&, auto) {
		++read_markers;

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	const auto tag = [&](std::string name, std::int32_t value) {
		return vm.CreateArray(std::array<SquirrelValue, 5>{vm.CreateString("setflag"), vm.CreateString("name"), vm.CreateString(std::move(name)), vm.CreateString("value"), value});
	};
	const auto snapshot = [&](std::int32_t point, std::int32_t line) {
		return vm.CreateArray(std::array<SquirrelValue, 5>{point, SquirrelValue{}, SquirrelValue{}, SquirrelValue{}, line});
	};
	const auto lines = vm.CreateArray(std::array<SquirrelValue, 6>{tag("first", 5), snapshot(1, 10), vm.CreateString("actual-read-marker"), tag("second", 1), snapshot(2, 20), tag("after", 99)});
	vm.NewSlot(vm.GetSlot(player_class, std::string("scenario")), std::string("lines"), lines);
	sgre::script::OriginalSceneSeek seek(vm, Target());
	const auto player = fixture.Start(seek);
	sgre::input::HubFrameSample sample;
	fixture.Inputs.Hub(hub).Advance(sample.Devices, 1);
	Require(sample.Devices[0].Buttons == 0 && std::get<bool>(Call(vm, player, "checkInputAction", std::array<SquirrelValue, 1>{vm.CreateString("force_skip_all")})), "original coroutine receives the owned semantic action with genuine native input unchanged");
	Require(InvokeMethodSteps(vm, player, "sceneMain", 6).State == sgre::script::SquirrelRunState::Suspended, "real dispatcher must yield at arrival before its containing loop can execute the sixth command");
	Require(seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Arrived && dispatch == std::vector<std::string>{"first", "second"} && read_markers == 1, "real original sceneMain/doTag must execute every prefix side effect exactly once before arrival");
	Require(std::get<std::int32_t>(vm.GetSlot(player, std::string("cur"))) == 5 && std::get<std::int32_t>(vm.GetSlot(player, std::string("forceSkip"))) == 0 && original_release_phases == 1, "arrival must occur after original point publication and actual cancelForceSkip, before the next original tag");
	const auto flags = vm.GetSlot(player, std::string("flags"));
	Require(std::get<std::int32_t>(vm.GetSlot(flags, vm.CreateString("first"))) == 5 && std::get<std::int32_t>(vm.GetSlot(flags, vm.CreateString("second"))) == 1 && !vm.HasSlot(flags, vm.CreateString("after")), "seek must retain actual original flag mutations, without inventing future state or invoking the suspended context");
	sample = {};
	fixture.Inputs.Hub(hub).Advance(sample.Devices, 1);
	Require(vm.Resume().State == sgre::script::SquirrelRunState::Completed, "next ordinary released input sample must resume the same original dispatcher coroutine");
	Require(dispatch.back() == "after" && seek.Observation().Snapshots == 2, "ordinary continuation must execute the next genuine command without repeating seek effects");
	Require(std::get<std::int32_t>(Call(vm, player, "getFlag", std::array<SquirrelValue, 1>{vm.CreateString("after")})) == 99, "actual original getFlag must observe the ordinary continuation's mutation after resume");
}

static void TestOriginalActionConsumer(const sgre::script::BytecodeProgram& program, const sgre::script::BytecodeProgram& input_program) {
	SeekFixture fixture;
	auto& vm = fixture.Vm;
	const auto hub = InstallOriginalActionInput(fixture, program, input_program);
	for (const auto& [name, line] : std::array<std::pair<std::string_view, std::uint32_t>, 4>{{{"checkSkipStop", 7638}, {"checkSkip", 7705}, {"startForceSkip", 7567}, {"cancelForceSkip", 7576}}}) {
		vm.NewSlot(fixture.Player, std::string(name), Original(vm, program, name, line));
	}
	vm.NewSlot(fixture.Player, std::string("forceSkip"), std::int32_t{0});
	vm.NewSlot(fixture.Player, std::string("skipMode"), std::int32_t{0});
	vm.NewSlot(fixture.Player, std::string("syncForceSkipStop"), false);
	vm.NewSlot(fixture.Player, std::string("commandEnabled"), true);
	vm.NewSlot(fixture.Player, std::string("reqClearAssistInput"), false);
	vm.NewSlot(fixture.Player, std::string("playStatus"), std::int32_t{0});
	vm.NewSlot(fixture.Player, std::string("skipStartTick2"), SquirrelValue{});
	vm.NewSlot(fixture.Player, std::string("bStopSkipAssistInput"), false);
	vm.NewSlot(fixture.Player, std::string("FORCESKIP_DELAY"), std::int32_t{0});
	vm.NewSlot(vm.RootTable(), std::string("getCurrentTick"), vm.CreateNativeClosure("prepared-real-clock-service", [](SquirrelVm&, auto) {
		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{1000});
	}));
	std::size_t mode_publications = 0;
	vm.NewSlot(fixture.Player, std::string("checkPlayMode"), vm.CreateNativeClosure("prepared-mode-publication-observer", [&](SquirrelVm&, auto) {
		++mode_publications;

		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	sgre::script::OriginalSceneSeek seek(vm, Target());
	const auto player = fixture.Start(seek);
	sgre::input::HubFrameSample sample;
	fixture.Inputs.Hub(hub).Advance(sample.Devices, 1);
	const auto input = vm.GetSlot(player, std::string("input"));
	RequireOriginalReleasedInput(Call(vm, input, "checkAction", std::array<SquirrelValue, 1>{vm.CreateString("force_skip_all")}), "raw original InputEx.checkAction before semantic routing");
	Require(std::get<bool>(Call(vm, player, "checkSkip")) && std::get<std::int32_t>(vm.GetSlot(player, std::string("forceSkip"))) == 2 && mode_publications == 1, "owned semantic action must execute real original checkSkip and startForceSkip, not mutate flag/mode state directly");
	seek.Cancel();
	fixture.Inputs.Hub(hub).Advance(sample.Devices, 1);
	Require(std::get<bool>(Call(vm, player, "checkSkipStop")) && std::get<std::int32_t>(vm.GetSlot(player, std::string("forceSkip"))) == 0 && mode_publications == 2 && std::holds_alternative<std::monostate>(vm.GetSlot(player, std::string("skipStartTick2"))), "released semantic action must traverse real original checkSkipStop and cancelForceSkip without input rebinding");
}

static void TestScriptSnapshotReceiverResultAndFailure() {
	sgre::script::SquirrelVmLimits vm_limits;
	vm_limits.ReclaimObjects = true;
	SeekFixture fixture(vm_limits);
	auto& vm = fixture.Vm;
	std::size_t checks = 0;
	vm.NewSlot(fixture.Player, std::string("checkSaveMode"), vm.CreateNativeClosure("prepared-original-snapshot-result", [&](SquirrelVm&, auto arguments) {
		Require(arguments.size() == 1, "ordinary original snapshot CALL changed its receiver arity");
		++checks;

		return sgre::script::SquirrelNativeResult::ReturnValue(std::int32_t{37});
	}));
	sgre::script::OriginalSceneSeek seek(vm, Target());
	const auto player = fixture.Start(seek);
	const auto player_root = vm.Retain(player);
	const auto hooked = vm.GetSlot(player, std::string("checkSaveMode"));
	Reject([&] {
		static_cast<void>(vm.Invoke(hooked, std::array<SquirrelValue, 1>{vm.CreateTable()}));
	},
	    "actual ScenePlayer receiver");
	Require(checks == 0 && seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Progressing, "receiver admission must precede the original method without failing the live seek for an unrelated receiver");
	vm.ResetContext(vm.RootContext());
	vm.SetSlot(player, std::string("curLine"), std::int32_t{20});
	vm.SetSlot(player, std::string("curPoint"), std::int32_t{2});
	static_cast<void>(vm.CollectGarbage());
	const auto yielded = vm.Invoke(hooked, std::array<SquirrelValue, 1>{player});
	Require(yielded.State == sgre::script::SquirrelRunState::Suspended && seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Arrived && checks == 1 && fixture.Releases == 1, "retired setup context/GC must preserve typed captured callbacks and the one-shot arrival boundary");
	const auto resumed = vm.Resume();
	Require(resumed.State == sgre::script::SquirrelRunState::Completed && std::get<std::int32_t>(resumed.Value) == 37, "script snapshot continuation must return the exact original result after its boundary suspension");
	SeekFixture failing;
	const auto original_error = failing.Vm.CreateString("prepared-original-snapshot-failure");
	failing.Vm.NewSlot(failing.Player, std::string("checkSaveMode"), failing.Vm.CreateNativeClosure("prepared-original-snapshot-error", [original_error](SquirrelVm&, auto) -> sgre::script::SquirrelNativeResult {
		throw sgre::script::SquirrelVmException(original_error, "prepared original snapshot diagnostic");
	}));
	sgre::script::OriginalSceneSeek failed(failing.Vm, Target());
	const auto failed_player = failing.Start(failed);
	bool caught = false;
	try {
		static_cast<void>(Call(failing.Vm, failed_player, "checkSaveMode"));
	} catch (const sgre::script::SquirrelVmException& error) {
		caught = true;
		Require(error.ThrownValue() == original_error, "script snapshot trap must preserve the exact original thrown value");
	}
	Require(caught && failed.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Failed && failed.Observation().Reason.find("prepared-original-snapshot-failure") != std::string::npos && failing.Releases == 0, "an original snapshot failure must be diagnosed without fabricating release or arrival");
	failing.Vm.ResetContext(failing.Vm.RootContext());
}

static SquirrelValue OriginalPlayMode(SquirrelVm& vm, const sgre::script::BytecodeProgram& program) {
	const auto* function = Find(program.MainFunction, "checkPlayMode", 3758);
	Require(function != nullptr && function->OuterBindings.empty() && function->DefaultParameters == std::vector<std::uint32_t>{4, 5}, "actual play-mode prototype/default identity changed");
	Require(program.MainFunction.Functions.size() > 281 && &program.MainFunction.Functions[281] == function && program.MainFunction.Instructions.size() > 1557, "actual play-mode default-producing parent identity changed");
	const auto user_default = program.MainFunction.Instructions[1555];
	const auto force_default = program.MainFunction.Instructions[1556];
	const auto creation = program.MainFunction.Instructions[1557];
	Require(user_default.Opcode == 22 && user_default.Argument0 == 4 && user_default.Argument1 == 0 && force_default.Opcode == 22 && force_default.Argument0 == 5 && force_default.Argument1 == 1 && creation.Opcode == 48 && creation.Argument0 == 4 && creation.Argument1 == 281, "actual play-mode false/true default publication changed");
	sgre::script::BytecodeProgram factory;
	factory.MainFunction.Name = std::string("prepared-original-play-mode-default-parent");
	factory.MainFunction.SourceName = std::string("prepared-seek-default-parent.nut");
	factory.MainFunction.Parameters = {std::string("this")};
	factory.MainFunction.StackSize = 6;
	factory.MainFunction.Functions.push_back(*function);
	auto local_creation = creation;
	local_creation.Argument1 = 0;
	factory.MainFunction.Instructions = {user_default, force_default, local_creation, Operation(19, 1, 4)};
	const auto result = vm.Invoke(vm.CreateClosure(std::move(factory)), std::array<SquirrelValue, 1>{vm.RootTable()});
	Require(result.State == sgre::script::SquirrelRunState::Completed && vm.Type(result.Value) == sgre::script::SquirrelValueType::Closure, "actual play-mode default-producing factory failed");

	return result.Value;
}

static SquirrelValue PreparedAsyncSystemSave(SquirrelVm& vm) {
	// This named service boundary represents save storage completion. The
	// original cancellation and play-mode/autosave decisions execute unchanged.
	sgre::script::BytecodeProgram program;
	auto& function = program.MainFunction;
	function.Name = std::string("prepared-async-system-save-service");
	function.SourceName = std::string("prepared-seek-save-storage.nut");
	function.Parameters = {std::string("this"), std::string("force")};
	function.StackSize = 5;
	function.Literals = {std::string("awaitPreparedSaveStorage"), std::string("completePreparedSaveStorage")};
	for (std::int32_t literal = 0; literal != 2; ++literal) {
		auto prepare = Operation(8, 2, literal);
		prepare.Argument2 = 0;
		prepare.Argument3 = 3;
		auto argument = Operation(10, 4, 1);
		auto call = Operation(6, 255, 2);
		call.Argument2 = 3;
		call.Argument3 = 2;
		function.Instructions.insert(function.Instructions.end(), {prepare, argument, call});
	}
	function.Instructions.push_back(Operation(19, 255));

	return vm.CreateClosure(std::move(program));
}

static SquirrelValue QuerySuspendedPlayerAction(SquirrelVm& vm, const SquirrelValue& player) {
	const auto context = vm.CreateContext();
	try {
		const auto result = vm.InvokeInContext(context, vm.GetSlot(player, std::string("checkInputAction")), std::array<SquirrelValue, 2>{player, vm.CreateString("force_skip_all")});
		Require(result.State == sgre::script::SquirrelRunState::Completed, "independent readonly action query must not resume the suspended original dispatcher");
		vm.ReleaseContext(context);

		return result.Value;
	} catch (...) {
		vm.ReleaseContext(context);
		throw;
	}
}

static void TestOriginalYieldingSkipRelease(const sgre::script::BytecodeProgram& program, const sgre::script::BytecodeProgram& utility, const sgre::script::BytecodeProgram& input_program) {
	for (std::size_t policy = 0; policy != 4; ++policy) {
		sgre::script::SquirrelVmLimits vm_limits;
		vm_limits.ReclaimObjects = true;
		SeekFixture fixture(vm_limits);
		auto& vm = fixture.Vm;
		Require(vm.Invoke(vm.CreateClosure(utility), std::array<SquirrelValue, 1>{vm.RootTable()}).State == sgre::script::SquirrelRunState::Completed, "actual utility publication failed for yielding release");
		const auto hub = InstallOriginalActionInput(fixture, program, input_program);
		const auto hub_root = vm.Retain(hub);
		for (const auto& [name, line] : std::array<std::pair<std::string_view, std::uint32_t>, 8>{{{"sceneMain", 6894}, {"getLine", 4157}, {"doTag", 5189}, {"setFlag", 3391}, {"getFlag", 3371}, {"checkSaveMode", 3793}, {"onSceneStart", 8482}, {"cancelForceSkip", 7576}}}) {
			vm.NewSlot(fixture.Player, std::string(name), Original(vm, program, name, line));
		}
		vm.NewSlot(fixture.Player, std::string("checkPlayMode"), OriginalPlayMode(vm, program));
		vm.NewSlot(fixture.Player, std::string("doSystemSave"), PreparedAsyncSystemSave(vm));
		vm.NewSlot(fixture.Player, std::string("forceSkip"), std::int32_t{2});
		vm.NewSlot(fixture.Player, std::string("syncForceSkipStop"), false);
		vm.NewSlot(fixture.Player, std::string("playStatus"), std::int32_t{0});
		vm.NewSlot(fixture.Player, std::string("allSkip"), false);
		vm.NewSlot(fixture.Player, std::string("playMode"), std::int32_t{3});
		vm.NewSlot(fixture.Player, std::string("msg"), SquirrelValue{});
		vm.NewSlot(fixture.Player, std::string("submsg"), SquirrelValue{});
		vm.NewSlot(fixture.Player, std::string("screenSaver"), std::int32_t{1});
		vm.NewSlot(fixture.Player, std::string("doSyssaveOnSkipStop"), true);
		vm.NewSlot(fixture.Player, std::string("cur"), std::int32_t{0});
		vm.NewSlot(fixture.Player, std::string("flags"), vm.CreateTable());
		vm.NewSlot(fixture.Player, std::string("scenario"), vm.CreateTable());
		vm.NewSlot(vm.RootTable(), std::string("NOSCREENSAVER_ALWAYS"), false);
		std::size_t mode_calls = 0;
		std::size_t saver_calls = 0;
		std::size_t speed_calls = 0;
		std::size_t prints = 0;
		std::size_t save_started = 0;
		std::size_t save_completed = 0;
		bool save_started_while_progressing = false;
		vm.NewSlot(fixture.Player, std::string("isSystemPlayModeDisable"), vm.CreateNativeClosure("prepared-play-mode-disable-query", [](SquirrelVm&, auto arguments) {
			Require(arguments.size() == 1, "original disable query receiver changed");

			return sgre::script::SquirrelNativeResult::ReturnValue(false);
		}));
		vm.NewSlot(fixture.Player, std::string("onPlayMode"), vm.CreateNativeClosure("prepared-play-mode-publication", [&](SquirrelVm&, auto arguments) {
			Require(arguments.size() == 4 && std::get<std::int32_t>(arguments[1]) == 0 && std::get<std::int32_t>(arguments[2]) == 3 && std::get<bool>(arguments[3]), "original cancellation changed its current/previous/user mode publication");
			++mode_calls;

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(fixture.Player, std::string("setEnableScreenSaver"), vm.CreateNativeClosure("prepared-screen-saver-service", [&](SquirrelVm&, auto arguments) {
			Require(arguments.size() == 2 && std::holds_alternative<bool>(arguments[1]), "original screen-saver service argument changed");
			++saver_calls;

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(fixture.Player, std::string("updateSpeed"), vm.CreateNativeClosure("prepared-speed-publication", [&](SquirrelVm&, auto arguments) {
			Require(arguments.size() == 1, "original speed publication receiver changed");
			++speed_calls;

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(vm.RootTable(), std::string("printf"), vm.CreateNativeClosure("prepared-original-print-service", [&](SquirrelVm&, auto arguments) {
			Require(arguments.size() == 2 && sgre::script::TryStringText(arguments[1]) != nullptr, "original save-stop printf arity/type changed");
			++prints;

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
		sgre::script::OriginalSceneSeekLimits limits;
		limits.MaximumPolls = 1;
		sgre::script::OriginalSceneSeek seek(vm, Target(), limits);
		const auto original_error = vm.CreateString("prepared-save-storage-failure");
		vm.NewSlot(fixture.Player, std::string("awaitPreparedSaveStorage"), vm.CreateNativeClosure("prepared-save-storage-await", [&](SquirrelVm& active, auto arguments) {
			Require(arguments.size() == 2 && std::holds_alternative<bool>(arguments[1]) && !std::get<bool>(arguments[1]), "actual checkPlayMode must pass its original false doSystemSave argument");
			++save_started;
			save_started_while_progressing = seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Progressing && seek.Observation().Snapshots == 2;
			if (policy == 1) {
				throw sgre::script::SquirrelVmException(original_error, "prepared save storage diagnostic");
			}
			const auto suspended = active.RequestNativeSuspension();
			active.PushNativeValue(SquirrelValue{});

			return suspended;
		}));
		vm.NewSlot(fixture.Player, std::string("completePreparedSaveStorage"), vm.CreateNativeClosure("prepared-save-storage-completion", [&](SquirrelVm&, auto arguments) {
			Require(arguments.size() == 2 && std::holds_alternative<bool>(arguments[1]) && !std::get<bool>(arguments[1]), "save completion must retain the original force argument");
			Require(seek.Observation().Phase != sgre::script::OriginalSceneSeekPhase::Arrived, "original save completion must precede arrival publication");
			++save_completed;

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
		std::vector<std::string> dispatched;
		vm.NewSlot(fixture.Player, std::string("tag_setflag"), vm.CreateNativeClosure("prepared-sequential-flag-tag", [&](SquirrelVm& active, auto arguments) {
			const auto name = active.GetSlot(arguments[1], std::string("name"));
			const auto value = active.GetSlot(arguments[1], std::string("value"));
			dispatched.emplace_back(sgre::script::StringText(name));
			static_cast<void>(active.InvokeInContext(active.CurrentContext(), active.GetSlot(arguments[0], std::string("setFlag")), std::array<SquirrelValue, 3>{arguments[0], name, value}));

			return sgre::script::SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(fixture.Player, std::string("setReadFlag"), EmptyMethod(vm, "prepared-read-flag-service", 2));
		const auto tag = [&](std::string name, std::int32_t value) {
			return vm.CreateArray(std::array<SquirrelValue, 5>{vm.CreateString("setflag"), vm.CreateString("name"), vm.CreateString(std::move(name)), vm.CreateString("value"), value});
		};
		const auto snapshot = [&](std::int32_t point, std::int32_t line) {
			return vm.CreateArray(std::array<SquirrelValue, 5>{point, SquirrelValue{}, SquirrelValue{}, SquirrelValue{}, line});
		};
		const auto lines = vm.CreateArray(std::array<SquirrelValue, 6>{tag("first", 5), snapshot(1, 10), vm.CreateString("actual-read-marker"), tag("second", 1), snapshot(2, 20), tag("after", 99)});
		vm.NewSlot(vm.GetSlot(fixture.Player, std::string("scenario")), std::string("lines"), lines);
		const auto player = fixture.Start(seek);
		const auto player_root = vm.Retain(player);
		sgre::input::HubFrameSample sample;
		fixture.Inputs.Hub(hub).Advance(sample.Devices, 1);
		if (policy == 1) {
			bool caught = false;
			try {
				static_cast<void>(InvokeMethodSteps(vm, player, "sceneMain", 6));
			} catch (const sgre::script::SquirrelVmException& error) {
				caught = true;
				Require(error.ThrownValue() == original_error, "script seek trap must rethrow the exact original save exception value");
			}
			Require(caught && seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Failed && seek.Observation().Reason.find("prepared-save-storage-failure") != std::string::npos && save_started == 1 && save_completed == 0 && save_started_while_progressing, "failed original save must latch Failed before its exception propagates");
			Require(dispatched == std::vector<std::string>{"first", "second"} && !vm.HasSlot(vm.GetSlot(player, std::string("flags")), vm.CreateString("after")), "failed release must not fabricate arrival, retry a save or execute a future tag");
			vm.ResetContext(vm.RootContext());
			continue;
		}
		Require(InvokeMethodSteps(vm, player, "sceneMain", 6).State == sgre::script::SquirrelRunState::Suspended, "original cancel/checkPlayMode must suspend through an ordinary script call at the named async save service");
		Require(seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Progressing && save_started == 1 && save_completed == 0 && save_started_while_progressing && dispatched == std::vector<std::string>{"first", "second"}, "original release/save must remain in progress without dispatching a future command");
		Require(std::get<std::int32_t>(vm.GetSlot(player, std::string("forceSkip"))) == 0 && !std::get<bool>(vm.GetSlot(player, std::string("doSyssaveOnSkipStop"))), "actual cancellation must clear forceSkip and actual checkPlayMode must clear its save request before yielding");
		static_cast<void>(vm.CollectGarbage());
		Require(seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Progressing && vm.IsSuspended(), "GC must preserve the captured script wrapper after its setup context retires");
		RequireOriginalReleasedInput(QuerySuspendedPlayerAction(vm, player), "original save suspension before another InputHub sample");
		Require(vm.IsSuspended() && save_completed == 0, "readonly action query must not resume the original save coroutine or fabricate a Hub tick");
		if (policy == 2) {
			seek.Cancel();
			Require(seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Cancelled && save_completed == 0, "cancellation during save must only release the seek owner");
		} else if (policy == 3) {
			seek.Poll();
			seek.Poll();
			Require(seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Blocked && save_completed == 0, "release/save suspension must remain subject to the actual poll work bound");
		}
		fixture.Inputs.Hub(hub).Advance(sample.Devices, 1);
		const auto resumed = vm.Resume();
		Require(save_started == 1 && save_completed == 1 && mode_calls == 1 && saver_calls == 1 && speed_calls == 1 && prints == 1, "original release, mode publication and save must execute exactly once across suspension");
		if (policy == 0) {
			Require(resumed.State == sgre::script::SquirrelRunState::Suspended && seek.Observation().Phase == sgre::script::OriginalSceneSeekPhase::Arrived && dispatched == std::vector<std::string>{"first", "second"}, "completed original save must publish arrival and yield before another original command");
			fixture.Inputs.Hub(hub).Advance(sample.Devices, 1);
			Require(vm.Resume().State == sgre::script::SquirrelRunState::Completed, "the next ordinary Hub sample must resume the saved original snapshot return");
		} else {
			Require(resumed.State == sgre::script::SquirrelRunState::Completed && seek.Observation().Phase == (policy == 2 ? sgre::script::OriginalSceneSeekPhase::Cancelled : sgre::script::OriginalSceneSeekPhase::Blocked), "cancelled/blocked release completion must never relatch arrival or suspend a completed seek");
		}
		Require(dispatched == std::vector<std::string>{"first", "second", "after"} && std::get<std::int32_t>(Call(vm, player, "getFlag", std::array<SquirrelValue, 1>{vm.CreateString("after")})) == 99, "ordinary continuation must retain all prefix mutations and execute its future tag exactly once");
	}
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 4, "usage: original_scene_seek_tests [envplayer-bytecode util-bytecode init-bytecode]");
		TestFirstEntryFailureLatchesAndPreservesException();
		TestCurrentSceneArgumentPropagation();
		TestInputArrivalAndOwnership();
		TestCancellationLimitsAndLateInstallation();
		TestAuthenticBoundaryContextPolicy();
		if (argc == 4) {
			const auto program = sgre::script::ReadSquirrelBytecode(ReadBounded(argv[1]));
			RunOriginalFixturePhase("sequential-dispatcher", [&] {
				TestOriginalSequentialDispatcher(program, sgre::script::ReadSquirrelBytecode(ReadBounded(argv[2])), sgre::script::ReadSquirrelBytecode(ReadBounded(argv[3])));
			});
			RunOriginalFixturePhase("original-action-consumer", [&] {
				TestOriginalActionConsumer(program, sgre::script::ReadSquirrelBytecode(ReadBounded(argv[3])));
			});
			RunOriginalFixturePhase("original-yielding-skip-release", [&] {
				TestOriginalYieldingSkipRelease(program, sgre::script::ReadSquirrelBytecode(ReadBounded(argv[2])), sgre::script::ReadSquirrelBytecode(ReadBounded(argv[3])));
			});
		}
		TestScriptSnapshotReceiverResultAndFailure();
		std::cout << "original stateful seek tests passed; prepared tag/play-mode services, original dispatcher/flag/action/skip handlers when bytecode inputs supplied\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
