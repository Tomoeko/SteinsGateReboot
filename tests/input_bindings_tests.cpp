#include "sgre/script/input_bindings.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view context) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(context) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("input binding safety rejection was not reported");
}

static void TestAutomaticNativeLifetime() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	sgre::script::SquirrelVm vm(limits);
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::input::KeyMappings mappings;
	sgre::script::ScriptInputBindings bindings(vm, objects, mappings);
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	sgre::script::SquirrelValue hub = vm.Invoke(bindings.InputHubClass(), receiver).Value;
	sgre::script::SquirrelValue device;
	{
		const std::array<sgre::script::SquirrelValue, 2> arguments{hub, std::int32_t(0)};
		device = vm.Invoke(vm.GetSlot(hub, std::string("inputAt")), arguments).Value;
	}

	const auto hub_weak = vm.CreateWeakReference(hub);
	const auto device_weak = vm.CreateWeakReference(device);
	sgre::input::MouseSample mouse_sample;
	mouse_sample.Client = {0, 0, 100, 80};
	mouse_sample.ClientX = 75;
	mouse_sample.ClientY = 20;
	mouse_sample.TargetWindowMatches = true;
	bindings.Mouse(device).Advance(mouse_sample);
	hub = {};
	vm.RethrowReleaseFailure();
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(hub_weak)), "Input wrappers must not strongly retain their native hub owner");
	RequireFailure([&] {
		static_cast<void>(bindings.Device(device));
	},
	    "hub has expired");
	Require(bindings.Mouse(device).Position().X == 0.5f, "native Input mouse lifetime must remain independent of its released hub");
	device = {};
	vm.RethrowReleaseFailure();
	Require(std::holds_alternative<std::monostate>(vm.ResolveWeakReference(device_weak)), "Input registry must not retain its script receiver");

	for (std::size_t iteration = 0; iteration < 128; ++iteration) {
		const auto transient = vm.Invoke(bindings.InputHubClass(), receiver).Value;
		Require(objects.HasInstance(transient), "reused VM identities must attach fresh native InputHub state");
	}
	vm.RethrowReleaseFailure();

	const auto derived = vm.CreateClass(bindings.InputHubClass());
	const auto class_root = vm.Retain(derived);
	vm.NewSlot(derived, std::string("cycle"), {});
	hub = vm.Invoke(derived, receiver).Value;
	vm.SetSlot(hub, std::string("cycle"), hub);
	const auto identity = vm.BorrowObjectIdentity(hub);
	auto root = vm.Retain(hub);
	vm.ResetExecution();
	hub = {};
	static_cast<void>(vm.CollectGarbage());
	Require(bindings.Hub(root.Value()).DeviceCount() == 8, "externally rooted InputHub must retain its native devices through collection");
	root = {};
	static_cast<void>(vm.CollectGarbage());
	vm.RethrowReleaseFailure();
	Require(!vm.ResolveObjectIdentity(identity), "unrooted InputHub cycle must release its native state during collection");
}

static void TestInputCloseCleanup() {
	sgre::script::SquirrelVm vm;
	sgre::script::ScriptObjectBindings objects(vm);
	sgre::input::KeyMappings mappings;
	sgre::script::ScriptInputBindings bindings(vm, objects, mappings);
	const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
	const auto derived = vm.CreateClass(bindings.InputHubClass());
	const auto hub = vm.Invoke(derived, receiver).Value;
	const auto identity = vm.BorrowObjectIdentity(hub);
	const std::array<sgre::script::SquirrelValue, 2> arguments{hub, std::int32_t(0)};
	const auto device = vm.Invoke(vm.GetSlot(hub, std::string("inputAt")), arguments).Value;
	const auto device_identity = vm.BorrowObjectIdentity(device);
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(vm.IsClosed() && !vm.ResolveObjectIdentity(identity) && !vm.ResolveObjectIdentity(device_identity), "explicit VM shutdown must finalize native hub/view owners before expiring their identities");
}

int main() {
	try {
		TestAutomaticNativeLifetime();
		TestInputCloseCleanup();
		sgre::script::SquirrelVm vm;
		vm.InstallRecoveredBaseLibrary();
		sgre::script::ScriptObjectBindings objects(vm);
		sgre::input::KeyMappings mappings;
		auto bindings = std::make_unique<sgre::script::ScriptInputBindings>(vm, objects, mappings);
		const std::array<sgre::script::SquirrelValue, 1> receiver{vm.RootTable()};
		const auto call = [&](const auto& instance, std::string name, std::vector<sgre::script::SquirrelValue> arguments = {}) {
			arguments.insert(arguments.begin(), instance);

			return vm.Invoke(vm.GetSlot(instance, std::move(name)), arguments).Value;
		};

		RequireFailure([&] {
			static_cast<void>(vm.Invoke(bindings->InputClass(), receiver));
		},
		    "can't create instance");
		Require(!vm.HasSlot(vm.RootTable(), std::string("KEY")) && std::get<std::int32_t>(vm.GetSlot(vm.GetSlot(vm.ConstantTable(), std::string("KEY")), std::string("ALL"))) == 0x1fffffff, "native input constants were not registered in the shared constant table");
		const auto hub = vm.Invoke(bindings->InputHubClass(), receiver).Value;
		Require(vm.IsInstanceOf(hub, objects.ObjectClass()) && objects.HasInstance(hub) && std::get<std::int32_t>(call(hub, "getInputNum")) == 8, "native InputHub construction/inheritance/count differs");
		Require(!std::get<bool>(call(hub, "getTransferAnalogToDigital")) && !std::get<bool>(call(hub, "getTransferRightAnalogToDigital")), "native analog transfer defaults differ");
		static_cast<void>(call(hub, "setTransferAnalogToDigital", {std::string("")}));
		Require(std::get<bool>(vm.ReadSlot(hub, std::string("transferAnalogToDigital"))), "native input boolean property did not reach hub state");
		static_cast<void>(call(hub, "setTransferRightAnalogToDigital", {true}));
		Require(bindings->Hub(hub).TransferRightAnalogToDigital(), "right analog state was lost");

		static_cast<void>(call(hub, "setAnalogPressSensitivity", {std::int32_t(7)}));
		static_cast<void>(call(hub, "setAnalogToDigitalDeadZone", {0.75f}));
		Require(std::get<std::int32_t>(call(hub, "getAnalogPressSensitivity")) == 0 && std::get<float>(call(hub, "getAnalogToDigitalDeadZone")) == 0, "native desktop no-op controls changed their fixed getters");
		const auto input = call(hub, "inputAt", {std::int32_t(0)});
		static_cast<void>(call(input, "setAccelerometerEnabled", {false}));
		static_cast<void>(call(input, "setTouchPadEnabled", {true}));
		Require(std::get<bool>(call(input, "getAccelerometerEnabled")) && !std::get<bool>(call(input, "getTouchPadEnabled")) && std::get<float>(call(input, "getAccelerometerZ")) == 9.80665f, "native fixed desktop sensor/touch behavior differs");
		const auto alias = call(hub, "inputAt", {std::int32_t(0)});
		const auto clamped = call(hub, "inputAt", {std::int32_t(999)});
		Require(input != alias && vm.IsInstanceOf(input, objects.ObjectClass()) && objects.HasInstance(input) && &bindings->Device(input) == &bindings->Device(alias) && &bindings->Device(clamped) == &bindings->Hub(hub).DeviceAt(7), "inputAt did not create fresh wrappers over the native device identity");
		RequireFailure([&] {
			static_cast<void>(call(hub, "inputAt", {std::int32_t(-1)}));
		},
		    "negative");
		RequireFailure([&] {
			static_cast<void>(call(hub, "inputAt", {std::string("bad")}));
		},
		    "integer-convertible");
		Require(std::get<std::int32_t>(call(input, "getControllerType")) == 0 && !std::get<bool>(call(input, "getConnected")), "initialized native device state differs");
		sgre::input::DeviceSample sample;
		sample.Buttons = 0x11;
		sample.LeftStick = {0.5f, -0.25f};
		sample.Connected = true;
		bindings->Device(input).Advance(sample, 1);
		Require(std::get<std::int32_t>(call(alias, "key", {std::int32_t(-1)})) == 0x11 && std::get<std::int32_t>(call(input, "keyPressed", {std::int32_t(-1)})) == 0x11 && std::get<std::int32_t>(call(input, "getStick")) == 0, "native button query or shared view state differs");
		Require(std::get<float>(vm.ReadSlot(input, std::string("analogStickX"))) == 0.5f && std::get<float>(call(input, "getTouchPointY")) == -1.25f && std::get<bool>(call(input, "getConnected")), "native axis/cursor/connectivity properties differ");
		static_cast<void>(call(input, "setKeyRepeat", {std::int32_t(1), std::int32_t(1), std::int32_t(1)}));
		bindings->Device(input).Advance(sample, 1);
		Require(std::get<std::int32_t>(call(alias, "keyPressed", {std::int32_t(1)})) == 1, "native repeat controls did not update the shared device");

		static_cast<void>(call(bindings->InputHubClass(), "ClearKeyConv"));
		Require(mappings.ScanConversions().empty() && mappings.VirtualConversions().empty(), "static input conversion clear did not reach the session owner");
		Require(std::get<bool>(call(bindings->InputHubClass(), "AddKeyConv", {std::string("DIK_A"), std::string("A"), std::string("B")})) && !std::get<bool>(call(bindings->InputHubClass(), "AddKeyConv", {std::string("bad"), std::string("A"), std::string("B")})), "static input name conversion result differs");
		static_cast<void>(call(bindings->InputHubClass(), "DefaultKeyConv"));
		Require(mappings.ScanConversions().size() == 19 && mappings.VirtualConversions().size() == 3, "static input defaults were not restored");
		sgre::input::MouseSample mouse_sample;
		mouse_sample.Client = {0, 0, 100, 80};
		mouse_sample.ClientX = 50;
		mouse_sample.ClientY = 40;
		mouse_sample.TargetWindowMatches = true;
		mouse_sample.EnterTracking = true;
		mouse_sample.WheelTicks = -120;
		bindings->Mouse(input).Advance(mouse_sample);
		Require(std::get<bool>(call(input, "getMouseEntered")) && std::get<std::int32_t>(call(input, "getMouseWheel")) == -1 && !std::get<bool>(call(alias, "isMouseEnter")), "fresh Input wrappers did not retain separate native mouse owners");
		const auto mouse_position = call(input, "getMousePos");
		Require(std::get<float>(vm.GetSlot(mouse_position, std::string("x"))) == 0 && std::get<float>(vm.GetSlot(mouse_position, std::string("y"))) == 0, "native mouse result table differs");
		RequireFailure([&] {
			static_cast<void>(call(input, "setMousePos", {1.0f, -1.0f}));
		},
		    "configured platform boundary");
		sgre::input::MousePixels planned_cursor;
		bindings->ConfigureCursorRequests([&] {
			return mouse_sample.Client;
		},
		    [&](const auto pixels) {
			    planned_cursor = pixels;
		    });
		static_cast<void>(call(input, "setMousePos", {1.0f, -1.0f}));
		Require(planned_cursor.X == 100 && planned_cursor.Y == 0 && bindings->Mouse(input).Position().X == 0, "native position setter did not issue a separate platform request");
		bindings->ReleaseInstance(hub);
		RequireFailure([&] {
			static_cast<void>(call(input, "key", {std::int32_t(1)}));
		},
		    "hub has expired");
		bindings->ReleaseInstance(input);
		Require(!objects.HasInstance(input) && !objects.HasInstance(hub), "native input release did not remove Object state");
		const auto getter = vm.GetSlot(alias, std::string("getConnected"));
		bindings.reset();
		const std::array<sgre::script::SquirrelValue, 1> expired_receiver{alias};
		RequireFailure([&] {
			static_cast<void>(vm.Invoke(getter, expired_receiver));
		},
		    "owner has expired");
		std::cout << "native InputHub/Input construction, shared views, properties, mappings, and lifetime passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
