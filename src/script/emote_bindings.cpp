#include "sgre/script/emote_bindings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace sgre::script {

	static constexpr std::uint8_t kEmoteClassTag = 0;

	struct EmoteMethodDefinition {
		std::string_view Name;
		std::uint8_t Descriptor;
	};

	static constexpr std::array<EmoteMethodDefinition, 88> kEmoteMethods = {{
	    {"setSpeed", 0},
	    {"getSpeed", 1},
	    {"setCoord", 2},
	    {"getLeft", 3},
	    {"getTop", 4},
	    {"setScale", 5},
	    {"getScale", 6},
	    {"setRot", 7},
	    {"getRot", 8},
	    {"setColor", 9},
	    {"getColor", 10},
	    {"setVariable", 11},
	    {"getVariable", 12},
	    {"setQueuing", 13},
	    {"getQueuing", 14},
	    {"setMeshDivisionRatio", 15},
	    {"getMeshDivisionRatio", 16},
	    {"setVelvetyTranslateLimitVelocity", 17},
	    {"getVelvetyTranslateLimitVelocity", 18},
	    {"startWind", 19},
	    {"stopWind", 20},
	    {"setHairScale", 21},
	    {"getHairScale", 22},
	    {"setPartsScale", 23},
	    {"getPartsScale", 24},
	    {"setBustScale", 25},
	    {"getBustScale", 26},
	    {"setHairScale", 21},
	    {"setMirror", 27},
	    {"getMirror", 28},
	    {"setBlinkEnabled", 29},
	    {"getBlinkEnabled", 30},
	    {"skip", 31},
	    {"pass", 32},
	    {"setVisible", 33},
	    {"getVisible", 34},
	    {"pause", 35},
	    {"clear", 36},
	    {"setRoundRobin", 37},
	    {"contains", 38},
	    {"hitTest", 39},
	    {"getMarkerPosition", 40},
	    {"playMotion", 41},
	    {"stopMotion", 42},
	    {"getMotionPlaying", 43},
	    {"setMotionBlendRatio", 44},
	    {"getMotionBlendRatio", 45},
	    {"fadeInMotion", 46},
	    {"fadeOutMotion", 47},
	    {"getLoopMotion", 48},
	    {"getPlayingMotionInfoList", 49},
	    {"getMainMotionLabelList", 50},
	    {"getDiffMotionLabelList", 51},
	    {"playTimeline", 52},
	    {"stopTimeline", 53},
	    {"getTimelinePlaying", 54},
	    {"setTimelineBlendRatio", 55},
	    {"getTimelineBlendRatio", 56},
	    {"fadeInTimeline", 57},
	    {"fadeOutTimeline", 58},
	    {"getLoopTimeline", 59},
	    {"getPlayingTimelineInfoList", 60},
	    {"getMainTimelineLabelList", 61},
	    {"getDiffTimelineLabelList", 62},
	    {"playMotion", 41},
	    {"stopMotion", 42},
	    {"getMotionPlaying", 43},
	    {"setMotionBlendRatio", 44},
	    {"getMotionBlendRatio", 45},
	    {"fadeInMotion", 46},
	    {"fadeOutMotion", 47},
	    {"getLoopMotion", 48},
	    {"getPlayingMotionInfoList", 49},
	    {"getMainMotionLabelList", 50},
	    {"getDiffMotionLabelList", 51},
	    {"assignState", 63},
	    {"getAnimating", 64},
	    {"startRecordAPILog", 65},
	    {"stopRecordAPILog", 66},
	    {"getRecordingAPILog", 67},
	    {"startReplayAPILog", 68},
	    {"stopReplayAPILog", 69},
	    {"getReplayingAPILog", 70},
	    {"clearAPILog", 71},
	    {"setAPILog", 72},
	    {"getAPILog", 73},
	    {"describeMeshChainStructure", 74},
	    {"configureCoordinateSystem", 75},
	}};

	static void RequireEmoteReceiver(SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
		if (arguments.empty() || !vm.InstanceClassHasTypeTag(arguments.front(), &kEmoteClassTag)) {
			throw std::runtime_error("invalid argument.");
		}
	}

	static SquirrelValue RejectedEmoteMethod(SquirrelVm& vm, EmoteMethodDefinition method) {
		// Native Sqrat registrations carry one eight-byte userdata capture. This portable code-route cookie
		// identifies the authored rejecting callback; it never represents a native function pointer or resource.
		const std::array<std::uint8_t, 8> descriptor{method.Descriptor, 0, 0, 0, 0, 0, 0, 0};
		const std::array<SquirrelValue, 1> captures{vm.CreateUserData(descriptor)};

		return vm.CreateNativeClosureWithCaptures("Emote." + std::string(method.Name), [method, descriptor](SquirrelVm& active, auto arguments) -> SquirrelNativeResult {
			RequireEmoteReceiver(active, arguments);
			if (arguments.size() < 2 || active.Type(arguments.back()) != SquirrelValueType::UserData) {
				throw std::runtime_error("Emote method has no actual code-route capture");
			}

			const auto bytes = active.UserDataBytes(arguments.back());
			if (bytes.size() != descriptor.size() || !std::equal(bytes.begin(), bytes.end(), descriptor.begin())) {
				throw std::runtime_error("Emote method code-route capture does not match its callback");
			}

			throw std::runtime_error("Emote." + std::string(method.Name) + " requires its actual MEmote/player/control/resource lifetime producer");
		},
		    captures);
	}

	static void InstallEmoteConstants(SquirrelVm& vm) {
		const auto destination = vm.ConstantTable();
		SquirrelValue constants;
		try {
			constants = vm.ReadSlot(destination, std::string("EMOTE"));
		} catch (const SquirrelVmException&) {
			// Native 2b1de8 creates the namespace only after general get returns failure.
			// VM safety failures remain distinct and must not be converted into a new table.
			constants = vm.CreateTable();
			vm.NewSlot(destination, std::string("EMOTE"), constants);
		}

		const std::array<std::pair<std::string_view, std::int32_t>, 13> values = {{
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
		for (const auto& [name, value] : values) {
			vm.NewSlot(constants, std::string(name), value);
		}
	}

	ScriptEmoteRegistrationFacet::ScriptEmoteRegistrationFacet(SquirrelVm& vm, ScriptObjectBindings& objects) {
		if (vm.HasSlot(vm.RootTable(), std::string("Emote"))) {
			throw std::runtime_error("Emote registration facet requires an unoccupied actual root class slot");
		}

		m_Class = vm.CreateClass(objects.ObjectClass());
		m_ClassRoot = vm.Retain(m_Class);
		vm.SetClassTypeTag(m_Class, &kEmoteClassTag);
		const auto constructor = vm.CreateNativeClosure("Emote.constructor", [](SquirrelVm& active, auto arguments) -> SquirrelNativeResult {
			RequireEmoteReceiver(active, arguments);
			if (arguments.size() < 3 || arguments.size() > 5) {
				throw std::runtime_error("invalid argument.");
			}

			// The native two typed arguments, both optional index 4 conversions and raw PSB +8 lifetime
			// cannot be replaced by Object attachment or an empty task. This profile never allocates a native owner.
			throw std::runtime_error("Emote constructor requires its actual MEmote/player/Layer/PSB lifetime producer");
		});
		vm.NewSlot(m_Class, std::string("constructor"), constructor);
		const auto weakref = vm.CreateNativeClosure("Emote.weakref", [](SquirrelVm& active, auto arguments) {
			if (arguments.empty()) {
				throw std::runtime_error("Emote weakref requires its actual last stack argument");
			}

			return SquirrelNativeResult::ReturnValue(active.CreateWeakReference(arguments.back()));
		});
		vm.NewSlot(m_Class, std::string("weakref"), weakref);
		// Native 2af360/2af3a4 publishes the class before its ordered method writes and EMOTE constants.
		vm.NewSlot(vm.RootTable(), std::string("Emote"), m_Class);
		for (const auto method : kEmoteMethods) {
			vm.NewSlot(m_Class, std::string(method.Name), RejectedEmoteMethod(vm, method));
		}

		InstallEmoteConstants(vm);
	}

	SquirrelValue ScriptEmoteRegistrationFacet::EmoteClass() const {
		return m_Class;
	}

} // namespace sgre::script
