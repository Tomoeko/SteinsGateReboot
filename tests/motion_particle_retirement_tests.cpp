#include "sgre/scene/motion_particle_retirement.hpp"

#include "motion_particle_retirement_native_vectors.hpp"

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

static sgre::scene::MotionParticleScreenRectangle Rectangle(const std::array<float, 4>& words) {
	sgre::scene::MotionParticleScreenRectangle result;
	result.Left = words[0];
	result.Top = words[1];
	result.Right = words[2];
	result.Bottom = words[3];

	return result;
}

static void TestNativeScreenStates() {
	for (std::size_t index = 0; index < kNativeMotionParticleScreenCases.size(); ++index) {
		const auto& sample = kNativeMotionParticleScreenCases[index];
		sgre::scene::MotionParticleDefinition definition;
		definition.DeleteOutsideScreen = sample.Enabled;
		definition.Screen = Rectangle(sample.Screen);
		definition.ExpandedScreen = Rectangle(sample.Expanded);
		sgre::scene::MotionParticleScreenState state;
		state.EnteredScreen = sample.Entered;
		sgre::resource::MotionTimelineBounds bounds;
		bounds.Left = sample.Bounds[0];
		bounds.Top = sample.Bounds[1];
		bounds.Right = sample.Bounds[2];
		bounds.Bottom = sample.Bounds[3];
		// Native first prunes nonplaying children; their screen latch must never be tested or mutated.
		const bool retire = !sample.Playing || state.ShouldRetire(definition, {sample.Coordinate[0], sample.Coordinate[1]}, bounds);
		if (retire == sample.Survives || state.EnteredScreen != sample.EnteredAfter) {
			throw std::runtime_error("native particle retirement or entered-screen latch differs in case " + std::to_string(index));
		}
	}
}

static void TestStickyHistoryAndUnsignedMaximum() {
	sgre::scene::MotionParticleDefinition definition;
	definition.DeleteOutsideScreen = true;
	definition.Screen = Rectangle({-10, -10, 10, 10});
	definition.ExpandedScreen = Rectangle({-11, -11, 11, 11});
	sgre::resource::MotionTimelineBounds bounds;
	bounds.Left = -1;
	bounds.Top = -1;
	bounds.Right = 1;
	bounds.Bottom = 1;
	sgre::scene::MotionParticleScreenState state;
	Require(!state.ShouldRetire(definition, {-50, 0}, bounds) && !state.EnteredScreen, "outside-before-entry child must survive without latching");
	Require(!state.ShouldRetire(definition, {-11, 0}, bounds) && !state.EnteredScreen, "touching the original screen boundary is not entry");
	Require(!state.ShouldRetire(definition, {-10, 0}, bounds) && state.EnteredScreen, "strict overlap must latch this real child entry");
	Require(!state.ShouldRetire(definition, {-11, 0}, bounds) && state.EnteredScreen, "entered child may leave the original screen while still inside the expanded screen");
	Require(state.ShouldRetire(definition, {-12, 0}, bounds) && state.EnteredScreen, "touching expanded screen retires an entered child and retains its sticky latch");
	definition.DeleteOutsideScreen = false;
	definition.Screen.reset();
	definition.ExpandedScreen.reset();
	Require(!state.ShouldRetire(definition, {0, 0}, bounds) && state.EnteredScreen, "disabled deletion does not consult screen data or reset actual entered state");
	Require(sgre::scene::MotionParticleCountExceedsMaximum(1, 0), "zero maximum evicts the first newly pushed child immediately");
	Require(!sgre::scene::MotionParticleCountExceedsMaximum(32, 32) && sgre::scene::MotionParticleCountExceedsMaximum(33, 32), "maximum comparison follows each actual push and only over-capacity evicts oldest");
	Require(!sgre::scene::MotionParticleCountExceedsMaximum(5, -1) && !sgre::scene::MotionParticleCountExceedsMaximum(5, std::numeric_limits<std::int32_t>::min()), "negative mode words remain zero-extended native unsigned maxima rather than zero clamps");
}

int main() {
	try {
		TestNativeScreenStates();
		TestStickyHistoryAndUnsignedMaximum();
		std::cout << "particle retirement: 96 original screen/prune states and sticky/unsigned boundaries PASS\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
