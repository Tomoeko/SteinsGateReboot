#include "sgre/scene/emote_easing.hpp"

#include "fixtures/emote_easing_native.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

static void TestOriginalNamedEasing() {
	for (std::size_t index = 0; index < kNativeEmoteEasingCases.size(); ++index) {
		const auto& expected = kNativeEmoteEasingCases[index];
		const auto easing = std::bit_cast<float>(expected.Easing);
		const auto ratio = std::bit_cast<float>(expected.Ratio);
		const auto actual = std::bit_cast<std::uint32_t>(sgre::scene::PrepareEmoteEasing(easing, ratio));
		if (actual != expected.Output) {
			throw std::runtime_error("native easing word differs: case=" + std::to_string(index) + " easing=" + std::to_string(easing) + " ratio=" + std::to_string(ratio) + " expected=" + std::to_string(expected.Output) + " actual=" + std::to_string(actual));
		}
	}
}

static void TestFiniteBoundary() {
	for (const auto value : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
		bool control_rejected = false;
		bool ratio_rejected = false;
		try {
			(void)sgre::scene::PrepareEmoteEasing(value, .5f);
		} catch (const std::runtime_error&) {
			control_rejected = true;
		}
		try {
			(void)sgre::scene::PrepareEmoteEasing(101, value);
		} catch (const std::runtime_error&) {
			ratio_rejected = true;
		}
		if (!control_rejected || !ratio_rejected) {
			throw std::runtime_error("easing finite admission changed");
		}
	}
}

int main() {
	try {
		TestOriginalNamedEasing();
		TestFiniteBoundary();
		std::cout << "800 original easing words and finite boundary passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
