#include "sgre/script/emote_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

using sgre::script::ScriptEmoteRegistrationFacet;
using sgre::script::ScriptObjectBindings;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelValueType;
using sgre::script::SquirrelVm;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation&& operation, std::string_view expected) {
	try {
		std::forward<Operation>(operation)();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected Emote facet rejection was not reported");
}

// Independent expected names/integers are transcribed from the complete native registration inventory.
static constexpr std::array<std::string_view, 76> kExpectedMethods = {{
    "setSpeed",
    "getSpeed",
    "setCoord",
    "getLeft",
    "getTop",
    "setScale",
    "getScale",
    "setRot",
    "getRot",
    "setColor",
    "getColor",
    "setVariable",
    "getVariable",
    "setQueuing",
    "getQueuing",
    "setMeshDivisionRatio",
    "getMeshDivisionRatio",
    "setVelvetyTranslateLimitVelocity",
    "getVelvetyTranslateLimitVelocity",
    "startWind",
    "stopWind",
    "setHairScale",
    "getHairScale",
    "setPartsScale",
    "getPartsScale",
    "setBustScale",
    "getBustScale",
    "setMirror",
    "getMirror",
    "setBlinkEnabled",
    "getBlinkEnabled",
    "skip",
    "pass",
    "setVisible",
    "getVisible",
    "pause",
    "clear",
    "setRoundRobin",
    "contains",
    "hitTest",
    "getMarkerPosition",
    "playMotion",
    "stopMotion",
    "getMotionPlaying",
    "setMotionBlendRatio",
    "getMotionBlendRatio",
    "fadeInMotion",
    "fadeOutMotion",
    "getLoopMotion",
    "getPlayingMotionInfoList",
    "getMainMotionLabelList",
    "getDiffMotionLabelList",
    "playTimeline",
    "stopTimeline",
    "getTimelinePlaying",
    "setTimelineBlendRatio",
    "getTimelineBlendRatio",
    "fadeInTimeline",
    "fadeOutTimeline",
    "getLoopTimeline",
    "getPlayingTimelineInfoList",
    "getMainTimelineLabelList",
    "getDiffTimelineLabelList",
    "assignState",
    "getAnimating",
    "startRecordAPILog",
    "stopRecordAPILog",
    "getRecordingAPILog",
    "startReplayAPILog",
    "stopReplayAPILog",
    "getReplayingAPILog",
    "clearAPILog",
    "setAPILog",
    "getAPILog",
    "describeMeshChainStructure",
    "configureCoordinateSystem",
}};

static constexpr std::array<std::pair<std::string_view, std::int32_t>, 13> kExpectedConstants = {{
    {"MOTION_PLAY_PARALLEL", 1},
    {"MOTION_PLAY_DIFFERENCE", 2},
    {"TIMELINE_PLAY_PARALLEL", 1},
    {"TIMELINE_PLAY_DIFFERENCE", 2},
    {"COORDINATE_SYSTEM_MODE_PIXELS", 0},
    {"COORDINATE_SYSTEM_MODE_METERS", 1},
    {"TRANSFORM_ALIGNMENT_ORIGIN", 1},
    {"TRANSFORM_ALIGNMENT_TOP", 2},
    {"TRANSFORM_ALIGNMENT_BOTTOM", 3},
    {"TRANSFORM_ALIGNMENT_CENTER", 4},
    {"TRANSFORM_ALIGNMENT_BUST", 5},
    {"TRANSFORM_ALIGNMENT_MOUTH", 6},
    {"TRANSFORM_ALIGNMENT_EYE", 7},
}};

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t second = 0, std::uint8_t third = 0, std::uint8_t fourth = 0) {
	sgre::script::SquirrelInstruction result;
	result.Opcode = opcode;
	result.Argument0 = destination;
	result.Argument1 = second;
	result.Argument2 = third;
	result.Argument3 = fourth;

	return result;
}

static void TestRegistryConstantsAndActualDeclaration() {
	SquirrelVm vm;
	vm.InstallRecoveredBaseLibrary();
	ScriptObjectBindings objects(vm);
	const auto constants = vm.CreateTable();
	vm.NewSlot(constants, std::string("unrelated"), std::int32_t{73});
	vm.NewSlot(vm.ConstantTable(), std::string("EMOTE"), constants);
	ScriptEmoteRegistrationFacet facet(vm, objects);
	const auto class_value = facet.EmoteClass();
	Require(vm.GetSlot(vm.RootTable(), std::string("Emote")) == class_value && vm.ClassTypeTag(class_value) != nullptr, "actual root Emote must be the tagged Object-derived class");
	Require(!vm.HasSlot(vm.RootTable(), std::string("EMOTE")) && vm.GetSlot(vm.ConstantTable(), std::string("EMOTE")) == constants, "native EMOTE constants must retain the real separate destination table");
	Require(std::get<std::int32_t>(vm.GetSlot(constants, std::string("unrelated"))) == 73, "constant installation must preserve independent existing fields");
	for (const auto& [name, value] : kExpectedConstants) {
		Require(std::get<std::int32_t>(vm.GetSlot(constants, std::string(name))) == value, "an independently retained EMOTE integer differs");
		Require(!vm.FindRawSlot(class_value, std::string(name)), "EMOTE integer must not be fabricated as an own class slot");
	}

	for (const auto name : kExpectedMethods) {
		const auto callable = vm.GetSlot(class_value, std::string(name));
		Require(vm.Type(callable) == SquirrelValueType::NativeClosure, "every recovered ordinary route must be an actual native closure");
		const std::array<SquirrelValue, 1> receiver{callable};
		const auto info = vm.Invoke(vm.GetSlot(callable, std::string("getinfos")), receiver).Value;
		Require(std::get<bool>(vm.GetSlot(info, std::string("native"))) && std::holds_alternative<std::monostate>(vm.GetSlot(info, std::string("name"))), "diagnostic labels must not invent native name metadata");
		Require(std::get<std::int32_t>(vm.GetSlot(info, std::string("paramscheck"))) == 0 && std::holds_alternative<std::monostate>(vm.GetSlot(info, std::string("typecheck"))), "Sqrat closure signature metadata must retain its default zero/no-mask domain");
	}

	// Synthetic first-party fixture reproduces observed21/9/59 and nonstatic null-field60, without executing original scripts.
	sgre::script::BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-EmotePicture-declaration");
	program.MainFunction.Name = std::string("main");
	program.MainFunction.Parameters = {std::string("this")};
	program.MainFunction.Literals = {std::string("Emote"), std::string("_owner")};
	program.MainFunction.StackSize = 5;
	program.MainFunction.Instructions = {Instruction(21, 2), Instruction(9, 2, 0, 2), Instruction(59, 2, 2, 255), Instruction(1, 3, 1), Instruction(20, 4, 1), Instruction(60, 0, 2, 3, 4), Instruction(19, 1, 2)};
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	const auto derived = vm.Invoke(vm.CreateClosure(std::move(program)), arguments).Value;
	Require(vm.Type(derived) == SquirrelValueType::Class && vm.ClassTypeTag(derived) == nullptr, "actual NEWCLASS must derive while preserving its own null tag");
	const auto instance = vm.CreateInstance(derived);
	Require(vm.IsInstanceOf(instance, class_value) && vm.IsInstanceOf(instance, objects.ObjectClass()), "the actual derived class must retain genuine Object/Emote ancestry");
	Require(vm.InstanceClassHasTypeTag(instance, vm.ClassTypeTag(class_value)) && vm.InstanceClassTypeTag(instance) == nullptr, "native immediate-tag versus ancestry matching must remain distinct in the real derived instance");
	Require(std::holds_alternative<std::monostate>(vm.GetSlot(instance, std::string("_owner"))) && !objects.HasInstance(instance), "a class field declaration must not fabricate a native Object or Emote payload");
	vm.ResetExecution();

	SquirrelVm delegated_vm;
	delegated_vm.InstallRecoveredBaseLibrary();
	ScriptObjectBindings delegated_objects(delegated_vm);
	const auto delegated_constants = delegated_vm.CreateTable();
	const auto delegate = delegated_vm.CreateTable();
	std::size_t lookups = 0;
	const auto getter = delegated_vm.CreateNativeClosure("independent-EMOTE-namespace-get", [&lookups, delegated_constants](SquirrelVm&, auto values) {
		Require(values.size() == 2 && std::holds_alternative<sgre::script::SquirrelString>(values[1]) && std::get<sgre::script::SquirrelString>(values[1]).Text() == "EMOTE", "actual constants general get must receive the exact namespace key");
		++lookups;

		return sgre::script::SquirrelNativeResult::ReturnValue(delegated_constants);
	});
	delegated_vm.NewSlot(delegate, std::string("_get"), getter);
	const auto destination = delegated_vm.ConstantTable();
	// This Squirrel2.2 build sets table delegates through opcode53, not a table method.
	sgre::script::BytecodeProgram delegate_program;
	delegate_program.MainFunction.SourceName = std::string("independent-EMOTE-namespace-delegate");
	delegate_program.MainFunction.Name = std::string("main");
	delegate_program.MainFunction.Parameters = {std::string("this"), std::string("destination"), std::string("delegate")};
	delegate_program.MainFunction.StackSize = 4;
	delegate_program.MainFunction.Instructions = {Instruction(53, 3, 1, 2), Instruction(19, 255)};
	const std::array<SquirrelValue, 3> delegate_arguments{delegated_vm.RootTable(), destination, delegate};
	static_cast<void>(delegated_vm.InvokeWithoutResultInContext(delegated_vm.RootContext(), delegated_vm.CreateClosure(std::move(delegate_program)), delegate_arguments));
	Require(!delegated_vm.HasSlot(destination, std::string("EMOTE")), "delegated getter must differ from ordinary slot existence");
	ScriptEmoteRegistrationFacet delegated_facet(delegated_vm, delegated_objects);
	Require(lookups == 1 && !delegated_vm.FindRawSlot(destination, std::string("EMOTE")), "native general-get success must reuse its actual returned table without creating an own namespace");
	for (const auto& [name, value] : kExpectedConstants) {
		Require(std::get<std::int32_t>(delegated_vm.GetSlot(delegated_constants, std::string(name))) == value, "delegated namespace must receive the exact independent integer constants");
	}

	delegated_vm.ResetExecution();
}

static void TestOwnWeakrefAndExpiry() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;
	SquirrelVm vm(limits);
	ScriptObjectBindings objects(vm);
	ScriptEmoteRegistrationFacet facet(vm, objects);
	const auto class_value = facet.EmoteClass();
	Require(vm.FindRawSlot(class_value, std::string("weakref")).has_value(), "Emote must own its native weakref slot");
	const auto derived = vm.CreateClass(class_value);
	SquirrelValue instance = vm.CreateInstance(derived);
	const auto callback = vm.GetSlot(instance, std::string("weakref"));
	SquirrelValue weak;
	{
		const std::array<SquirrelValue, 1> arguments{instance};
		weak = vm.Invoke(callback, arguments).Value;
	}

	Require(vm.ResolveWeakReference(weak) == instance, "native weakref must borrow the actual last receiver identity");
	{
		const std::array<SquirrelValue, 2> arguments{instance, std::int32_t{5}};
		Require(std::get<std::int32_t>(vm.Invoke(callback, arguments).Value) == 5, "an extra primitive last argument must pass through without a receiver-payload check");
	}

	vm.ResetExecution();
	Require(!objects.HasInstance(instance), "weakref must not attach a native Object or runtime owner");
	instance = {};
	Require(vm.Type(vm.ResolveWeakReference(weak)) == SquirrelValueType::Null, "zero-capture weakref must not retain its target instance");
}

static void TestMissingProducerAndArity() {
	SquirrelVm vm;
	ScriptObjectBindings objects(vm);
	ScriptEmoteRegistrationFacet facet(vm, objects);
	const auto class_value = facet.EmoteClass();
	const auto instance = vm.CreateInstance(class_value);
	const auto constructor = vm.GetSlot(instance, std::string("constructor"));
	const std::array<SquirrelValue, 1> too_few{instance};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(constructor, too_few));
	},
	    "invalid argument.");
	const std::array<SquirrelValue, 6> too_many{instance, {}, {}, {}, {}, {}};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(constructor, too_many));
	},
	    "invalid argument.");
	for (std::size_t count = 3; count <= 5; ++count) {
		const std::array<SquirrelValue, 5> arguments{instance, {}, {}, {}, {}};
		RequireFailure([&] {
			static_cast<void>(vm.Invoke(constructor, std::span<const SquirrelValue>(arguments.data(), count)));
		},
		    "actual MEmote/player/Layer/PSB lifetime producer");
	}

	for (const auto name : kExpectedMethods) {
		const std::array<SquirrelValue, 1> arguments{instance};
		RequireFailure([&] {
			static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string(name)), arguments));
		},
		    "actual MEmote/player/control/resource lifetime producer");
	}

	vm.ResetExecution();
	Require(!objects.HasInstance(instance), "constructor and control rejection must leave no fabricated native Object/payload");
	const auto foreign_instance = vm.CreateInstance(objects.ObjectClass());
	const std::array<SquirrelValue, 1> wrong_receiver{foreign_instance};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("getSpeed")), wrong_receiver));
	},
	    "invalid argument.");
	Require(!objects.HasInstance(foreign_instance), "receiver rejection must not mutate the actual sibling owner");
}

static void TestPartialPublicationAndNoCallbackOwnerBorrow() {
	SquirrelVm vm;
	ScriptObjectBindings objects(vm);
	vm.NewSlot(vm.ConstantTable(), std::string("EMOTE"), std::int32_t{7});
	RequireFailure([&] {
		ScriptEmoteRegistrationFacet rejected(vm, objects);
	},
	    "not an object");
	const auto class_value = vm.GetSlot(vm.RootTable(), std::string("Emote"));
	Require(vm.Type(class_value) == SquirrelValueType::Class && vm.ClassTypeTag(class_value) != nullptr, "a genuine late constant failure must retain the earlier published class");
	Require(std::get<std::int32_t>(vm.GetSlot(vm.ConstantTable(), std::string("EMOTE"))) == 7, "invalid destination must preserve its actual existing constant cell");
	const auto instance = vm.CreateInstance(class_value);
	const std::array<SquirrelValue, 1> arguments{instance};
	RequireFailure([&] {
		static_cast<void>(vm.Invoke(vm.GetSlot(instance, std::string("getSpeed")), arguments));
	},
	    "actual MEmote/player/control/resource lifetime producer");
	const auto weak = vm.Invoke(vm.GetSlot(instance, std::string("weakref")), arguments).Value;
	Require(vm.ResolveWeakReference(weak) == instance && !objects.HasInstance(instance), "callbacks after construction unwind must borrow only their actual VM values and static code data");
	RequireFailure([&] {
		ScriptEmoteRegistrationFacet duplicate(vm, objects);
	},
	    "unoccupied actual root class slot");
	Require(vm.GetSlot(vm.RootTable(), std::string("Emote")) == class_value, "repeat installation rejection must preserve the actual published sibling class");
	vm.ResetExecution();
}

int main() {
	try {
		TestRegistryConstantsAndActualDeclaration();
		TestOwnWeakrefAndExpiry();
		TestMissingProducerAndArity();
		TestPartialPublicationAndNoCallbackOwnerBorrow();
		std::cout << "Emote registration facet tests passed (4 fixture groups)\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
