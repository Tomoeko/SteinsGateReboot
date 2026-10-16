#include "sgre/scene/emote_color_transition.hpp"
#include "sgre/scene/emote_coordinate_transition.hpp"
#include "sgre/scene/emote_rotation_transition.hpp"
#include "sgre/scene/emote_scale_transition.hpp"
#include "sgre/scene/emote_timeline_transition.hpp"

#include "fixtures/emote_skip_native.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireWord(float actual, std::uint32_t expected, std::string_view field) {
	Require(std::bit_cast<std::uint32_t>(actual) == expected, field);
}

static std::uint32_t NativeByteColor(const std::array<std::uint32_t, 4>& words) {
	std::array<std::uint32_t, 4> bytes{};
	for (std::size_t index = 0; index != words.size(); ++index) {
		const auto value = std::bit_cast<float>(words[index]);
		Require(std::isfinite(value) && value >= 0 && value <= 255 && std::trunc(value) == value, "projected native color is outside this independent byte-valued fixture");
		bytes[index] = static_cast<std::uint32_t>(value);
	}
	return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24;
}

static void TestOriginalCurrentProjection() {
	for (const auto& expected : sgre::test::kEmoteSkipNativeCases) {
		sgre::scene::EmoteCoordinateTransition coordinate;
		sgre::scene::EmoteScaleTransition scale;
		sgre::scene::EmoteColorTransition color;
		sgre::scene::EmoteRotationTransition rotation;
		scale.Set(0);
		color.Set(0);
		const auto angle = std::bit_cast<float>(expected.AngleWord);
		if (expected.Phase != sgre::test::EmoteSkipNativePhase::Idle) {
			coordinate.Set({8, 9}, 4, .5f);
			scale.Set(8, 4, .5f);
			color.Set(0x0b0a0908u, 4, .5f);
			rotation.Set(angle, 4, .5f);
			if (expected.Phase == sgre::test::EmoteSkipNativePhase::Active || expected.Phase == sgre::test::EmoteSkipNativePhase::ActiveWithPending) {
				static_cast<void>(coordinate.Advance(1));
				static_cast<void>(scale.Advance(1));
				static_cast<void>(color.Advance(1));
				static_cast<void>(rotation.Advance(1));
			}
			if (expected.Phase == sgre::test::EmoteSkipNativePhase::ActiveWithPending) {
				rotation.Set(-angle, 3, 0);
			}
		}
		coordinate.FinishTransitions();
		scale.FinishTransitions();
		color.FinishTransitions();
		rotation.FinishTransitions();
		for (std::size_t index = 0; index != expected.Coordinate.size(); ++index) {
			RequireWord(coordinate.Current()[index], expected.Coordinate[index], "original coordinate Current word");
		}
		RequireWord(scale.Current(), expected.Scale, "original scale Current word");
		RequireWord(rotation.Current(), expected.Rotation, "original primary rotation Current word");
		// Native Skip does not pack/publish color. This independent byte projection observes the completed leaf later.
		Require(color.Advance(0) == NativeByteColor(expected.Color), "original color Current components observed through later packing");
		Require(!coordinate.Changing() && !scale.Changing() && !rotation.Changing(), "native finish clears changing flags without starting pending work");
		rotation.FinishTransitions();
		RequireWord(rotation.Advance(17), expected.Rotation, "repeated finish retains completed angular Current");
	}
}

static void TestScalarCopiesTargetWithoutEndpointArithmetic() {
	sgre::scene::EmoteTimelineTransition scalar;
	scalar.Set(4);
	scalar.Set(-0.0f, 8, 109);
	static_cast<void>(scalar.Advance(1));
	scalar.FinishTransitions();
	RequireWord(scalar.Current(), 0x80000000u, "scalar finish copies negative-zero target without eased endpoint arithmetic");
	Require(!scalar.Changing(), "finished scalar must be idle");
	scalar.Set(2.5f);
	scalar.FinishTransitions();
	Require(scalar.Current() == 2.5f, "idle completion must not copy a stale retained target");
	scalar.Set(6.5f, 4);
	Require(scalar.Advance(1) == 3.5f, "a later real transition starts from completed Current with its own duration");
}

static void TestCoordinatePolicyAndRejectedSetRemainIntact() {
	sgre::scene::EmoteCoordinateTransition coordinate(sgre::scene::EmoteCoordinateMode::Meters, 4);
	coordinate.Set({2, -3}, 9);
	coordinate.FinishTransitions();
	Require(coordinate.Current() == std::array<float, 2>{2, -3}, "finishing retains actual coordinate policy while completing pixel-valued components");
	Require(coordinate.Advance(0) == std::array<float, 2>{8, -12}, "only the property consumer receives pixel-valued completed components");
	bool rejected = false;
	try {
		coordinate.Set({std::numeric_limits<float>::infinity(), 3}, 1);
	} catch (const std::runtime_error&) {
		rejected = true;
	}
	Require(rejected && coordinate.Current() == std::array<float, 2>{2, -3} && !coordinate.Changing(), "completion does not weaken finite setter validation or commit a rejected target");
}

int main() {
	try {
		TestOriginalCurrentProjection();
		TestScalarCopiesTargetWithoutEndpointArithmetic();
		TestCoordinatePolicyAndRejectedSetRemainIntact();
		std::cout << "Emote Skip transition groups=3 native_count1_plans=12 exact_current_words passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
