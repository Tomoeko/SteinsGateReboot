#pragma once

#include "sgre/resource/motion.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace sgre::scene {

	struct MotionParameterRange {
		// The exact serialized parameter ID borrows the immutable raw timeline through all consumers.
		std::string_view Identifier;
		float Begin = 0;
		float End = 0;
		float Division = 0;
		bool Discrete = false;
	};

	// Native515910 ignores enabled; it reads every original array entry in serialized order.
	[[nodiscard]] std::vector<MotionParameterRange> ReadMotionParameterRanges(const resource::MotionTimeline& timeline, std::size_t max_parameters = 1024);
	// The input is a real resolved variable value. This does not create a variable/controller owner or admit a selected tree.
	[[nodiscard]] float EvaluateMotionParameterPosition(const MotionParameterRange& range, float value);

} // namespace sgre::scene
