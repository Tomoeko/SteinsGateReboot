#pragma once

#include "sgre/scene/motion_player_commands.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace sgre::scene {

	struct MotionChildFrameControl {
		MotionPlayFlags Flags = MotionPlayFlags::None;
		std::uint32_t DirectionMode = 0;
		float DirectionOffset = 0;
		bool CompleteDirection = false;
		// Native target strings borrow immutable raw frame/resource metadata through every selected use.
		std::string_view DirectionTarget;
		float TimeOffset = 0;
		bool Specified = false;
	};

	// Kind 3 initially zeroes two 32-byte controls. Hidden/no-motion frames leave reused slot controls intact.
	// A missing update means no write; a present update contains the native motion-object defaults.
	// This metadata does not admit child layers or implement direction/root binding/publication.
	[[nodiscard]] std::optional<MotionChildFrameControl> PrepareMotionChildFrameControl(const resource::MotionLayer& layer, const resource::MotionFrame& frame);
	std::optional<MotionChildFrameControl> PrepareMotionChildFrameControl(const resource::MotionLayer& layer, resource::MotionFrame&& frame) = delete;
	// Immutable raw metadata outlives the update and the actual selected slot retaining its target view.
	void ApplyMotionChildFrameControl(const std::optional<MotionChildFrameControl>& update, MotionChildFrameControl& slot);

} // namespace sgre::scene
