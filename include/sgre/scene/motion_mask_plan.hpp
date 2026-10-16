#pragma once

#include "sgre/scene/motion_clip_bounds.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sgre::scene {

	struct MotionMaskSettings {
		std::uint8_t Operation = 0;
		float ThresholdMinimum = 0;
		float ThresholdExtent = 0;
	};

	enum class MotionMaskOperationKind {
		Prepare,
		Configure,
		Begin,
		End,
		VisitCommand,
		Resolve
	};

	struct MotionMaskOperation {
		MotionMaskOperationKind Kind = MotionMaskOperationKind::Prepare;
		std::uint32_t Reference = 0;
		std::uint8_t Operation = 0;
		float ThresholdMinimum = 0;
		float ThresholdMaximum = 0;
		// Node/member storage remains owned by the scene through plan consumption.
		const MotionClipCommand* Command = nullptr;
	};

	// Visits are visible candidates; downstream clipping may still suppress their geometry.
	[[nodiscard]] std::size_t PrepareMotionMaskPlan(std::span<const MotionClipNode> nodes, std::span<const MotionMaskSettings> settings, std::optional<std::uint32_t> first, std::span<MotionMaskOperation> output);

} // namespace sgre::scene
