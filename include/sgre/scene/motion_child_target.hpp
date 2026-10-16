#pragma once

#include "sgre/resource/motion.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace sgre::scene {

	// The actual raw selected timeline owns this map; supplied count is its prepared nonroot layer span.
	// Returns the native root-containing ordinal: zero is the actual root, one is selected layer zero.
	// Actual target composition/atan2 and resource/task ownership remain separate required phases.
	[[nodiscard]] std::optional<std::uint32_t> ResolveMotionChildTargetIndex(const resource::MotionTimeline& timeline, std::string_view target, std::size_t selected_layer_count);

} // namespace sgre::scene
