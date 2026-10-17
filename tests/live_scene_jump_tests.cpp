#include "sgre/script/live_scene_jump.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

static void Require(bool condition, std::string_view reason) {
	if (!condition) {
		throw std::runtime_error(std::string(reason));
	}
}

static void TestTitleFullStateReturn() {
	sgre::script::SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	const auto base = vm.CreateClass();
	std::size_t original_calls = 0;
	vm.NewSlot(base, std::string("checkInputAction"), vm.CreateNativeClosure("fixture.title.action", [&](auto&, auto) {
		++original_calls;
		return sgre::script::SquirrelNativeResult::ReturnValue(false);
	}));
	sgre::script::SquirrelValue received;
	vm.NewSlot(base, std::string("onProcess"), vm.CreateNativeClosure("fixture.title.original-panel-close", [&](auto&, auto arguments) {
		Require(arguments.size() == 2, "title panel close lost its original result arity");
		received = arguments[1];
		return sgre::script::SquirrelNativeResult::NoValues();
	}));
	vm.NewSlot(vm.RootTable(), std::string("SaveLoadModeFunction"), base);
	vm.NewSlot(vm.RootTable(), std::string("checkInputAction"), vm.GetSlot(base, std::string("checkInputAction")));
	sgre::script::LiveSceneJump jump(vm);
	jump.InstallAvailableHooks();
	const auto title_class = vm.CreateClass(base);
	vm.NewSlot(vm.RootTable(), std::string("TitleMenuFunction"), title_class);
	const auto title = vm.CreateInstance(title_class);
	const auto title_root = vm.Retain(title);
	const auto app_class = vm.CreateClass();
	vm.NewSlot(app_class, std::string("player"), {});
	vm.NewSlot(vm.RootTable(), std::string("app"), vm.CreateInstance(app_class));
	Require(jump.CanJump(), "original title menu cannot receive a complete scene state");
	const auto data = vm.CreateTable();
	const auto data_root = vm.Retain(data);
	vm.NewSlot(data, std::string("storage"), vm.CreateString("title-fixture.ks"));
	vm.NewSlot(data, std::string("target"), vm.CreateString("*saved"));
	vm.NewSlot(data, std::string("point"), std::int32_t{8});
	const auto flags = vm.CreateTable();
	const auto flags_root = vm.Retain(flags);
	vm.NewSlot(flags, std::string("phone-history"), std::int32_t{29});
	vm.NewSlot(data, std::string("flags"), flags);
	const sgre::script::OriginalCheckpointLocation location{"title-fixture.ks", "*saved", 90, 8, 3};
	const auto bytes = sgre::script::EncodeOriginalCheckpoint(vm, data, location, "title-resource", "title-parent");
	jump.Queue(bytes, location, "title-resource");
	const auto action = vm.GetSlot(title, std::string("checkInputAction"));
	const std::array<sgre::script::SquirrelValue, 2> arguments{title, vm.CreateString("ok")};
	Require(vm.Invoke(action, arguments).Value == sgre::script::SquirrelValue{false}, "title handoff fabricated a physical input action");
	Require(!jump.Pending() && original_calls == 0 && vm.GetSlot(vm.GetSlot(received, std::string("flags")), std::string("phone-history")) == sgre::script::SquirrelValue{std::int32_t{29}}, "title result failed to preserve the whole original checkpoint");
	static_cast<void>(vm.Invoke(action, arguments));
	Require(original_calls == 1, "title instrumentation repeated its one-shot result");
}

int main() {
	try {
		TestTitleFullStateReturn();
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = true;
		sgre::script::SquirrelVm vm(limits);
		vm.InstallRecoveredBaseLibrary();
		const auto type = vm.CreateClass();
		vm.NewSlot(vm.RootTable(), std::string("ScenePlayer"), type);
		sgre::script::BytecodeProgram ordinary_action;
		ordinary_action.MainFunction.Name = std::string("fixture ordinary input");
		ordinary_action.MainFunction.SourceName = std::string("fixture/input");
		ordinary_action.MainFunction.Parameters = {std::string("this"), std::string("action")};
		ordinary_action.MainFunction.StackSize = 3;
		ordinary_action.MainFunction.Instructions = {{0, 22, 2, 0, 0}, {2, 19, 1, 0, 0}};
		const auto ordinary = vm.CreateClosure(std::move(ordinary_action));
		const auto ordinary_root = vm.Retain(ordinary);
		std::size_t original_calls = 0;
		vm.NewSlot(type, std::string("checkInputAction"), vm.CreateNativeClosure("fixture.original.action", [&](auto& active, auto arguments) {
			Require(arguments.size() == 2, "live jump changed the original action argument count");
			++original_calls;

			return sgre::script::SquirrelNativeResult::ReturnValue(active.InvokeInContext(active.CurrentContext(), ordinary, arguments).Value);
		}));
		sgre::script::BytecodeProgram exit;
		exit.MainFunction.Name = std::string("exitGame");
		exit.MainFunction.SourceName = std::string("fixture/exit");
		exit.MainFunction.Parameters = {std::string("this"), std::string("command"), std::string("data")};
		exit.MainFunction.StackSize = 3;
		exit.MainFunction.Instructions = {{0, 58, 2, 0, 0}};
		vm.NewSlot(type, std::string("exitGame"), vm.CreateClosure(std::move(exit)));
		sgre::script::LiveSceneJump jump(vm);
		jump.InstallAvailableHooks();
		Require(!jump.CanJump(), "title state fabricated an active player");
		const auto player = vm.CreateInstance(type);
		const auto player_root = vm.Retain(player);
		const auto application_type = vm.CreateClass();
		const auto application_type_root = vm.Retain(application_type);
		vm.NewSlot(application_type, std::string("player"), player);
		const auto app = vm.CreateInstance(application_type);
		vm.NewSlot(vm.RootTable(), std::string("app"), app);
		Require(jump.CanJump(), "active original player was not admitted");
		const auto action = vm.GetSlot(player, std::string("checkInputAction"));
		const auto action_root = vm.Retain(action);
		const std::array<sgre::script::SquirrelValue, 2> arguments{player, vm.CreateString("unrelated")};
		Require(vm.Invoke(action, arguments).Value == sgre::script::SquirrelValue{false} && original_calls == 1, "idle debug instrumentation changed original input");
		const auto flags = vm.CreateTable();
		const auto flags_root = vm.Retain(flags);
		vm.NewSlot(flags, std::string("phone-history"), std::int32_t{17});
		const auto data = vm.CreateTable();
		const auto data_root = vm.Retain(data);
		vm.NewSlot(data, std::string("storage"), vm.CreateString("fixture.ks"));
		vm.NewSlot(data, std::string("target"), vm.CreateString("*actual"));
		vm.NewSlot(data, std::string("point"), std::int32_t{9});
		vm.NewSlot(data, std::string("flags"), flags);
		const sgre::script::OriginalCheckpointLocation location{"fixture.ks", "*actual", 47, 9, 13};
		const auto identity = std::string(64, 'a');
		const auto bytes = sgre::script::EncodeOriginalCheckpoint(vm, data, location, identity, "fixture-parent");
		std::size_t consumed = 0;
		jump.Queue(bytes, location, identity, [&] {
			++consumed;
		});
		Require(consumed == 0, "queued jump prematurely changed checkpoint ancestry");
		static_cast<void>(vm.CollectGarbage());
		bool caught = false;
		try {
			static_cast<void>(vm.Invoke(action, arguments));
		} catch (const sgre::script::SquirrelVmException& error) {
			caught = true;
			const auto restored = error.ThrownValue();
			Require(vm.Type(restored) == sgre::script::SquirrelValueType::Table && vm.GetSlot(vm.GetSlot(restored, std::string("flags")), std::string("phone-history")) == sgre::script::SquirrelValue{std::int32_t{17}}, "live jump lost the original full saved state or exception value");
		}
		Require(caught && !jump.Pending() && original_calls == 1 && consumed == 1, "live jump did not consume its one-shot command through the script exception path");
		vm.ResetContext(vm.RootContext());
		Require(vm.Invoke(action, arguments).Value == sgre::script::SquirrelValue{false} && original_calls == 2, "consumed jump repeated or retained input ownership");
		std::cout << "live jump full-state handoff, script exception propagation, GC and original input passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
