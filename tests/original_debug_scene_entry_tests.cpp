#include "sgre/script/debug_scene_entry.hpp"

#include "sgre/app/launch_arguments.hpp"
#include "sgre/core/frame_clock.hpp"
#include "sgre/resource/psb.hpp"
#include "sgre/script/game_bindings.hpp"
#include "sgre/script/object_bindings.hpp"
#include "sgre/script/psb_bindings.hpp"
#include "sgre/script/system_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
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

static void RequireFailure(const auto& operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected debug scene entry rejection: " + std::string(expected));
}

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t operand = 0) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = destination;
	instruction.Argument1 = operand;

	return instruction;
}

static SquirrelValue NullHook(SquirrelVm& vm) {
	sgre::script::BytecodeProgram program;
	program.MainFunction.Name = std::string("getMainScene");
	program.MainFunction.SourceName = std::string("authored-debug-scene-fixture.nut");
	program.MainFunction.Parameters.emplace_back(std::string("this"));
	program.MainFunction.StackSize = 2;
	program.MainFunction.Instructions = {Instruction(20, 1, 1), Instruction(19, 1, 1)};

	return vm.CreateClosure(std::move(program));
}

static SquirrelValue Application(SquirrelVm& vm) {
	const auto application = vm.CreateClass();
	vm.NewSlot(application, std::string("getMainScene"), NullHook(vm));
	vm.NewSlot(vm.RootTable(), std::string("Application"), application);
	vm.NewSlot(vm.RootTable(), std::string("app"), SquirrelValue{});

	return application;
}

static SquirrelValue Call(SquirrelVm& vm, const SquirrelValue& receiver, std::string_view method) {
	return vm.Invoke(vm.GetSlot(receiver, std::string(method)), std::array<SquirrelValue, 1>{receiver}).Value;
}

static void TestReleaseAndOwnedDebugProvider() {
	{
		SquirrelVm vm;
		sgre::core::FrameClock clock;
		sgre::script::ScriptSystemBindings system(vm, clock);
		const auto application = Application(vm);
		const auto instance = vm.CreateInstance(application);
		Require(std::holds_alternative<std::monostate>(Call(vm, instance, "getMainScene")), "normal startup changed the original null scene hook");
		Require(!std::get<bool>(Call(vm, system.SystemClass(), "getDebugBuild")), "normal startup changed the measured release build query");
	}

	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	const auto application = Application(vm);
	sgre::script::ScriptDebugSceneEntry entry;
	entry.Storage = "exact-fixture.ks";
	entry.TargetLine = 135;
	sgre::script::InstallOriginalDebugSceneEntry(vm, entry);
	entry.Storage = "caller-retired.ks";
	entry.TargetLine = 999;
	const auto derived = vm.CreateClass(application);
	vm.NewSlot(vm.RootTable(), std::string("MyApplication"), derived);
	const auto instance = vm.CreateInstance(derived);
	const auto instance_root = vm.Retain(instance);
	const auto first = Call(vm, instance, "getMainScene");
	const auto first_root = vm.Retain(first);
	Require(sgre::script::StringText(vm.GetSlot(first, std::string("storage"))) == "exact-fixture.ks" && std::get<std::int32_t>(vm.GetSlot(first, std::string("target"))) == 135, "derived application lost its exact owned debug scene request");
	vm.SetSlot(first, std::string("target"), std::int32_t{7});
	vm.ResetContext(vm.RootContext());
	static_cast<void>(vm.CollectGarbage());
	const auto second = Call(vm, instance, "getMainScene");
	Require(second != first && std::get<std::int32_t>(vm.GetSlot(second, std::string("target"))) == 135, "provider reused a scene table mutated by its previous consumer");
	Require(!std::get<bool>(Call(vm, system.SystemClass(), "getDebugBuild")), "explicit scene entry enabled unrelated original developer paths");
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("getMainScene")), std::array<SquirrelValue, 1>{vm.CreateTable()}));
	},
	    "actual Application-derived receiver");
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("getMainScene")), std::array<SquirrelValue, 2>{instance, false}));
	},
	    "actual Application-derived receiver");
}

static void TestPreflightAndClassBoundary() {
	SquirrelVm vm;
	sgre::script::ScriptDebugSceneEntry entry;
	entry.Storage = "fixture.ks";
	RequireFailure([&] {
		sgre::script::InstallOriginalDebugSceneEntry(vm, entry);
	},
	    "class publication");
	const auto application = Application(vm);
	const auto original = vm.GetSlot(application, std::string("getMainScene"));
	for (const auto& invalid : {std::string{}, std::string("a\0b", 3), std::string(4097, 'x')}) {
		entry.Storage = invalid;
		RequireFailure([&] {
			sgre::script::InstallOriginalDebugSceneEntry(vm, entry);
		},
		    "exact bounded storage");
		Require(vm.GetSlot(application, std::string("getMainScene")) == original, "failed entry validation replaced the original script hook");
	}
	entry.Storage = "fixture.ks";
	entry.TargetLine = -1;
	RequireFailure([&] {
		sgre::script::InstallOriginalDebugSceneEntry(vm, entry);
	},
	    "nonnegative source line");
	entry.TargetLine = 0;
	static_cast<void>(vm.CreateInstance(application));
	RequireFailure([&] {
		sgre::script::InstallOriginalDebugSceneEntry(vm, entry);
	},
	    "class is locked");
	Require(vm.GetSlot(application, std::string("getMainScene")) == original, "late locked-class preparation changed original startup");

	SquirrelVm late;
	const auto base = Application(late);
	late.NewSlot(late.RootTable(), std::string("MyApplication"), late.CreateClass(base));
	RequireFailure([&] {
		sgre::script::InstallOriginalDebugSceneEntry(late, entry);
	},
	    "application derivation");
}

static std::vector<std::uint8_t> ReadBounded(const std::filesystem::path& path) {
	constexpr std::size_t kMaximumInputBytes = 2 * 1024 * 1024;
	const auto length = std::filesystem::file_size(path);
	if (length == 0 || length > kMaximumInputBytes) {
		throw std::runtime_error("debug entry private bytecode/metadata input exceeds its checked extent");
	}
	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
	std::ifstream input(path, std::ios::binary);
	input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	Require(static_cast<bool>(input), "debug entry original input read failed");

	return bytes;
}

static const sgre::script::SquirrelFunction* FindFunction(const sgre::script::SquirrelFunction& function, std::string_view name, std::uint32_t line) {
	const auto* text = std::get_if<std::string>(&function.Name);
	if (text && *text == name && !function.LineInformation.empty() && function.LineInformation.front().Line == line) {
		return &function;
	}
	for (const auto& child : function.Functions) {
		if (const auto* found = FindFunction(child, name, line)) {
			return found;
		}
	}

	return nullptr;
}

static SquirrelValue OriginalClosure(SquirrelVm& vm, const sgre::script::BytecodeProgram& program, std::string_view name, std::uint32_t line) {
	const auto* function = FindFunction(program.MainFunction, name, line);
	Require(function != nullptr && function->OuterBindings.empty(), "exact original debug entry function or its enclosing prototype changed");
	sgre::script::BytecodeProgram isolated;
	isolated.MainFunction = *function;

	return vm.CreateClosure(std::move(isolated));
}

static void TestOriginalMainEntry(const sgre::script::BytecodeProgram& program, const sgre::script::BytecodeProgram& utility, std::int32_t target_line) {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	sgre::script::InstallGameResourceProfile(vm);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	const auto utility_result = vm.Invoke(vm.CreateClosure(utility), std::array<SquirrelValue, 1>{vm.RootTable()});
	Require(utility_result.State == sgre::script::SquirrelRunState::Completed, "original utility module did not complete its actual helper/default-parameter publication");
	vm.RegisterNative("printf", [](SquirrelVm&, auto) {
		return sgre::script::SquirrelNativeResult{};
	});
	vm.RegisterNative("sync", [](SquirrelVm&, auto) {
		return sgre::script::SquirrelNativeResult{};
	});
	const auto application = vm.CreateClass();
	vm.NewSlot(vm.RootTable(), std::string("Application"), application);
	vm.NewSlot(application, std::string("getMainScene"), OriginalClosure(vm, program, "getMainScene", 981));
	vm.NewSlot(application, std::string("main"), OriginalClosure(vm, program, "main", 990));
	vm.NewSlot(application, std::string("firstState"), std::string("title"));
	vm.NewSlot(application, std::string("recordEnable"), false);
	std::vector<std::string> calls;
	for (const auto* name : {"startup", "onStartup", "clearActionSet", "updateConfig", "onTitleLoop", "cleanup", "setup", "stopBGM", "onStartScene"}) {
		vm.NewSlot(application, std::string(name), vm.CreateNativeClosure(name, [&, name](SquirrelVm&, auto) {
			calls.emplace_back(name);

			return sgre::script::SquirrelNativeResult{};
		}));
	}
	bool scene_entered = false;
	vm.NewSlot(application, std::string("sceneMain"), vm.CreateNativeClosure("prepared-scene-consumer", [&](SquirrelVm& active, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 2 && sgre::script::StringText(active.GetSlot(arguments[1], std::string("storage"))) == "reSG00_01.ks" && std::get<std::int32_t>(active.GetSlot(arguments[1], std::string("target"))) == target_line, "original Application.main changed the exact debug request shape");
		Require(calls.size() >= 2 && calls[0] == "startup" && calls[1] == "onStartup", "debug entry bypassed original application startup callbacks");
		scene_entered = true;
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	}));
	vm.NewSlot(application, std::string("titleLoop"), vm.CreateNativeClosure("forbidden-title-loop", [](SquirrelVm&, auto) -> sgre::script::SquirrelNativeResult {
		throw std::runtime_error("original debug entry unexpectedly entered titleLoop");
	}));
	sgre::script::InstallOriginalDebugSceneEntry(vm, {"reSG00_01.ks", target_line});
	const auto derived = vm.CreateClass(application);
	vm.NewSlot(vm.RootTable(), std::string("MyApplication"), derived);
	const auto instance = vm.CreateInstance(derived);
	const auto arguments = vm.CreateTable();
	std::string first_failure;
	vm.SetTraceCallback([&](const sgre::script::SquirrelTraceEvent& event) {
		// Observe the original caught value before a missing prepared catch type can obscure it.
		if (first_failure.empty() && event.Source == "advengine/script/application.nut" && event.Function == "main" && event.Instruction == 158) {
			const auto error = vm.LastError();
			if (const auto* failure = sgre::script::TryStringText(error)) {
				first_failure = *failure;
			}
		}
	});
	sgre::script::SquirrelRunResult result;
	try {
		result = vm.Invoke(vm.GetSlot(instance, std::string("main")), std::array<SquirrelValue, 2>{instance, arguments});
	} catch (const std::runtime_error&) {
		if (!first_failure.empty()) {
			throw std::runtime_error("original Application.main prepared consumer failure: " + first_failure);
		}
		throw;
	}
	vm.SetTraceCallback({});
	Require(result.State == sgre::script::SquirrelRunState::Suspended && scene_entered, "original main did not reach its actual scene consumer through the null-default extension point");
	vm.ResetContext(vm.RootContext());
	Require(!std::get<bool>(Call(vm, system.SystemClass(), "getDebugBuild")), "original main entry required global debug-build admission");
}

static void TestOriginalSnapshotRestore(const sgre::script::BytecodeProgram& program, const std::vector<std::uint8_t>& bytes, const sgre::script::BytecodeProgram& utility, std::int32_t requested_line, std::size_t expected_index, std::int32_t expected_point, std::int32_t expected_line) {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::script::ScriptPsbBindings psb(vm, objects);
	const auto utility_result = vm.Invoke(vm.CreateClosure(utility), std::array<SquirrelValue, 1>{vm.RootTable()});
	Require(utility_result.State == sgre::script::SquirrelRunState::Completed, "original utility module did not publish genuine source-line integer conversion");
	const auto document = std::make_shared<const sgre::resource::PsbDocument>(sgre::resource::ReadPsbMetadata(bytes, bytes.size()));
	const auto wrapped = psb.WrapDocument(document);
	const auto wrapped_root = vm.Retain(wrapped);
	const auto root = vm.ReadSlot(wrapped, std::string("root"));
	const auto owner = vm.CreateTable();
	vm.NewSlot(owner, std::string("scenes"), vm.ReadSlot(root, std::string("scenes")));
	const auto scene = vm.Invoke(OriginalClosure(vm, program, "findSceneLine", 399), std::array<SquirrelValue, 2>{owner, requested_line}).Value;
	const auto* scene_value = psb.ValueAsset(scene).Value;
	Require(std::get<std::string>(scene_value->Find("label")->Data) == "*dummy1", "original source-line lookup selected another actual scene");
	const auto& lines = std::get<sgre::resource::PsbArray>(scene_value->Find("lines")->Data);
	if (requested_line == 135) {
		Require(lines.size() > 33 && std::get<std::int64_t>(lines[33].Data) == 1, "actual first dialogue reference changed");
		const auto& first_text = std::get<sgre::resource::PsbArray>(std::get<sgre::resource::PsbArray>(scene_value->Find("texts")->Data).front().Data);
		Require(first_text.size() > 3 && (std::get<std::int64_t>(first_text[3].Data) & 2) == 0, "actual first dialogue unexpectedly requests continuation redraw during original restoreText(1)");
	}
	Require(expected_index < lines.size(), "actual direct-entry snapshot index is outside the selected scene");
	const auto& snapshot = std::get<sgre::resource::PsbArray>(lines[expected_index].Data);
	if (requested_line == 135) {
		Require(snapshot.size() == 5 && std::get<std::int64_t>(snapshot[0].Data) == 4 && std::get<std::int64_t>(snapshot[4].Data) == 135, "actual post-movie snapshot identity changed");
	}
	Require(snapshot.size() >= 5 && std::get<std::int64_t>(snapshot[0].Data) == expected_point && std::get<std::int64_t>(snapshot[4].Data) == expected_line, "actual direct-entry snapshot point/source-line metadata changed");
	std::int32_t expected_text_id = 0;
	bool first_snapshot_found = false;
	for (std::size_t index = 0; index < lines.size(); ++index) {
		if (const auto* text_id = std::get_if<std::int64_t>(&lines[index].Data)) {
			Require(*text_id >= 0 && *text_id < std::numeric_limits<std::int32_t>::max(), "actual preceding text reference exceeds the original signed integer domain");
			expected_text_id = static_cast<std::int32_t>(*text_id);
		}
		const auto* candidate = std::get_if<sgre::resource::PsbArray>(&lines[index].Data);
		if (candidate == nullptr || candidate->size() < 5 || !std::holds_alternative<std::int64_t>((*candidate)[0].Data)) {
			continue;
		}
		const auto source_line = std::get<std::int64_t>((*candidate)[4].Data);
		if (source_line >= requested_line) {
			Require(index == expected_index, "direct entry did not select the first original snapshot at or after the requested line");
			first_snapshot_found = true;
			break;
		}
	}
	Require(first_snapshot_found, "actual direct-entry request has no original qualifying snapshot");
	if (requested_line == 135) {
		Require(expected_text_id == 0, "post-movie snapshot changed its original preceding text reference");
	}
	vm.NewSlot(owner, std::string("scenario"), scene);
	for (const auto* field : {"curTextId", "cur", "curPoint", "curLine"}) {
		vm.NewSlot(owner, std::string(field), std::int32_t{0});
	}
	vm.NewSlot(owner, std::string("curReaded"), false);
	vm.NewSlot(owner, std::string("getLine"), OriginalClosure(vm, program, "getLine", 4157));
	vm.NewSlot(owner, std::string("sceneMode"), std::int32_t{0});
	vm.NewSlot(owner, std::string("sysflagMode"), false);
	vm.NewSlot(owner, std::string("isNormalPlay"), OriginalClosure(vm, program, "isNormalPlay", 8122));
	vm.NewSlot(owner, std::string("setReadFlag"), OriginalClosure(vm, program, "setReadFlag", 4626));
	std::map<std::string, bool> system_flags;
	vm.NewSlot(owner, std::string("setSystemFlag"), vm.CreateNativeClosure("prepared-system-flag-store", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		Require(arguments.size() == 3 && std::holds_alternative<bool>(arguments[2]), "original debug source-line entry changed its actual read-flag producer arguments");
		system_flags[std::string(sgre::script::StringText(arguments[1]))] = std::get<bool>(arguments[2]);

		return sgre::script::SquirrelNativeResult{};
	}));
	std::size_t restorations = 0;
	vm.NewSlot(owner, std::string("restore"), vm.CreateNativeClosure("prepared-environment-restore", [&](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		if (requested_line == 135) {
			Require(arguments.size() == 2 && psb.ValueAsset(arguments[1]).Value == &snapshot[1], "original goToLine did not restore the exact owned serialized post-movie environment");
		} else {
			Require(std::get<std::int64_t>(snapshot[1].Data) == 231, "actual later snapshot changed its original one-based text/state reference");
			Require(arguments.size() == 2 && std::get<std::int32_t>(arguments[1]) == std::get<std::int64_t>(snapshot[1].Data), "original goToLine changed the actual integer text/state restoration reference");
		}
		++restorations;

		return sgre::script::SquirrelNativeResult{};
	}));
	vm.NewSlot(owner, std::string("restoreText"), vm.CreateNativeClosure("prepared-text-restore", [requested_line, expected_text_id](SquirrelVm&, std::span<const SquirrelValue> arguments) {
		if (requested_line == 135) {
			Require(arguments.size() == 2 && std::get<std::int32_t>(arguments[1]) == 1, "original source-line entry changed its next text-info query");
		}
		Require(arguments.size() == 2 && std::get<std::int32_t>(arguments[1]) == expected_text_id + 1, "original direct source-line entry changed its actual next text reference");

		return sgre::script::SquirrelNativeResult{};
	}));
	vm.NewSlot(owner, std::string("isReaded"), vm.CreateNativeClosure("prepared-read-state", [](SquirrelVm&, auto) {
		return sgre::script::SquirrelNativeResult::ReturnValue(false);
	}));
	const auto result = vm.Invoke(OriginalClosure(vm, program, "goToLine", 4637), std::array<SquirrelValue, 2>{owner, requested_line});
	if (requested_line == 135) {
		Require(std::get<bool>(result.Value) && restorations == 1 && std::get<std::int32_t>(vm.GetSlot(owner, std::string("cur"))) == 17 && std::get<std::int32_t>(vm.GetSlot(owner, std::string("curPoint"))) == 4 && std::get<std::int32_t>(vm.GetSlot(owner, std::string("curLine"))) == 135 && std::get<std::int32_t>(vm.GetSlot(owner, std::string("curTextId"))) == 0, "original goToLine publication differs at the first post-movie gameplay snapshot");
		Require(system_flags.size() == 2 && system_flags.at("cg_bg_white") && system_flags.at("cg_prologue02.wmv"), "original goToLine skipped or invented metadata read-flag mutations before source135");
	}
	Require(std::get<bool>(result.Value) && restorations == 1 && std::get<std::int32_t>(vm.GetSlot(owner, std::string("cur"))) == static_cast<std::int64_t>(expected_index) && std::get<std::int32_t>(vm.GetSlot(owner, std::string("curPoint"))) == expected_point && std::get<std::int32_t>(vm.GetSlot(owner, std::string("curLine"))) == expected_line && std::get<std::int32_t>(vm.GetSlot(owner, std::string("curTextId"))) == expected_text_id, "original goToLine publication differs from the exact direct-entry snapshot/text identity");
	std::cout << "original direct snapshot requested=" << requested_line << " index=" << expected_index << " point=" << expected_point << " source=" << expected_line << " preceding_text=" << expected_text_id << " restore_count=" << restorations << '\n';
}

static void TestParsedJumpUsesOwnedOriginalEntryHook() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	sgre::core::FrameClock clock;
	sgre::script::ScriptSystemBindings system(vm, clock);
	const auto application = Application(vm);
	std::string argument = "ExactFixture.ks:1355";
	const std::array<std::string_view, 2> arguments{"--jump", argument};
	const auto parsed = sgre::app::ParseLaunchArguments(arguments);
	Require(parsed.Jump && parsed.DebugStartInGame == true, "parsed jump must request the original direct-entry extension hook");
	sgre::script::InstallOriginalDebugSceneEntry(vm, {parsed.Jump->Storage, parsed.Jump->TargetLine});
	argument.assign("caller-retired.ks:9");
	const auto derived = vm.CreateClass(application);
	vm.NewSlot(vm.RootTable(), std::string("MyApplication"), derived);
	const auto instance = vm.CreateInstance(derived);
	const auto instance_root = vm.Retain(instance);
	const auto scene = Call(vm, instance, "getMainScene");
	const auto scene_root = vm.Retain(scene);
	Require(sgre::script::StringText(vm.GetSlot(scene, std::string("storage"))) == "ExactFixture.ks" && std::get<std::int32_t>(vm.GetSlot(scene, std::string("target"))) == 1355, "actual inherited original hook must return the exact owned jump storage/source line");
	vm.SetSlot(scene, std::string("target"), std::int32_t{7});
	vm.ResetContext(vm.RootContext());
	static_cast<void>(vm.CollectGarbage());
	const auto second = Call(vm, instance, "getMainScene");
	Require(second != scene && std::get<std::int32_t>(vm.GetSlot(second, std::string("target"))) == 1355 && !std::get<bool>(Call(vm, system.SystemClass(), "getDebugBuild")), "a custom jump must retain the original fresh-scene provider contract without enabling unrelated developer flags");
}

int main(int argc, char** argv) {
	try {
		if (argc != 1 && argc != 5) {
			throw std::runtime_error("usage: original_debug_scene_entry_tests [application-bytecode envplayer-bytecode first-scenario-psb util-bytecode]");
		}
		TestReleaseAndOwnedDebugProvider();
		TestPreflightAndClassBoundary();
		TestParsedJumpUsesOwnedOriginalEntryHook();
		if (argc == 5) {
			const auto utility = sgre::script::ReadSquirrelBytecode(ReadBounded(argv[4]));
			const auto application = sgre::script::ReadSquirrelBytecode(ReadBounded(argv[1]));
			TestOriginalMainEntry(application, utility, 135);
			TestOriginalMainEntry(application, utility, 1480);
			const auto player = sgre::script::ReadSquirrelBytecode(ReadBounded(argv[2]));
			const auto scenario = ReadBounded(argv[3]);
			TestOriginalSnapshotRestore(player, scenario, utility, 135, 17, 4, 135);
			TestOriginalSnapshotRestore(player, scenario, utility, 1480, 1154, 317, 1480);
			TestOriginalSnapshotRestore(player, scenario, utility, 1479, 1154, 317, 1480);
			std::cout << "original Application.main/findSceneLine/goToLine bytecode receiving passed; direct requested135/1480/1479; prepared startup/scene/environment consumer boundaries only\n";
		}
		std::cout << "original debug scene entry tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
