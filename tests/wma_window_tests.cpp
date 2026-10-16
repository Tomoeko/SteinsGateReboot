#include "sgre/audio/wma_window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestComplementaryEqualWindows() {
	const sgre::audio::WmaWindowBank windows;
	for (const auto size : std::array<std::uint32_t, 5>{128, 256, 512, 1024, 2048}) {
		std::vector<float> samples(size * 2, 1.0f);
		windows.Apply(samples, size, size);
		for (std::size_t index = 0; index < size; ++index) {
			const double energy = static_cast<double>(samples[index]) * samples[index] + static_cast<double>(samples[size + index]) * samples[size + index];
			Require(std::abs(energy - 1.0) < 0.00000015, "equal WMA overlap windows are not complementary");
			Require(samples[index] > 0.0f && samples[index] <= 1.0f && samples[size + index] > 0.0f && samples[size + index] <= 1.0f, "WMA sine window exceeds its amplitude range");
		}
	}
}

static void TestVariableWindowPlacement() {
	const sgre::audio::WmaWindowBank windows;
	std::vector<float> samples(1024, 2.0f);
	windows.Apply(samples, 128, 256);
	Require(std::ranges::all_of(std::span(samples).first(192), [](float value) {
		return value == 0.0f;
	}),
	    "short previous block did not discard the initial current-block region");
	Require(std::ranges::all_of(std::span(samples).subspan(320, 320), [](float value) {
		return value == 2.0f;
	}),
	    "unequal overlap windows changed the plateau");
	Require(std::ranges::all_of(std::span(samples).subspan(896), [](float value) {
		return value == 0.0f;
	}),
	    "short next block did not discard the trailing current-block region");
	Require(samples[192] > 0.0f && samples[192] < samples[193] && samples[318] < samples[319] && samples[319] < 2.0f, "previous-block overlap ramp placement differs");
	Require(samples[640] < 2.0f && samples[640] > samples[641] && samples[894] > samples[895] && samples[895] > 0.0f, "next-block overlap ramp placement differs");
}

static void TestBounds() {
	const sgre::audio::WmaWindowBank windows;
	for (const auto count : std::array<std::uint32_t, 5>{0, 64, 129, 768, 4096}) {
		std::vector<float> samples(256, 1.0f);
		bool rejected = false;
		try {
			windows.Apply(samples, count, 128);
		} catch (const std::runtime_error&) {
			rejected = true;
		}

		Require(rejected && std::ranges::all_of(samples, [](float value) {
			return value == 1.0f;
		}),
		    "invalid previous block changed window samples");
	}

	std::vector<float> odd(257, 1.0f);
	bool rejected = false;
	try {
		windows.Apply(odd, 128, 128);
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "partial WMA time-domain block was accepted");
	std::vector<float> samples(256, 1.0f);
	samples[77] = std::numeric_limits<float>::quiet_NaN();
	rejected = false;
	try {
		windows.Apply(samples, 128, 128);
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected && samples[0] == 1.0f && samples[255] == 1.0f, "nonfinite input partially applied a WMA overlap window");
}

int main() {
	try {
		TestComplementaryEqualWindows();
		TestVariableWindowPlacement();
		TestBounds();
		std::cout << "WMA window tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
