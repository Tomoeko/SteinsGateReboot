#pragma once

#include "sgre/scene/motion_scissor.hpp"

#include <array>
#include <cstdint>

namespace sgre::scene {

	struct RenderTargetViewport {
		float X = 0;
		float Y = 0;
		float Width = 0;
		float Height = 0;
		float MinimumDepth = 0;
		float MaximumDepth = 1;
	};

	struct RenderTargetLayout {
		RenderTargetViewport Viewport;
		MotionBounds ViewportBounds;
		MotionBounds FullScissor;
		MotionScissorMatrix Projection;
	};

	// Dimensions and type belong to the actual allocated target; the eye comes from its containing graphics owner.
	// This recovers layout arithmetic only. It does not bind a target, reset device state or create a graphics owner.
	[[nodiscard]] RenderTargetLayout PrepareRenderTargetLayout(std::uint32_t width, std::uint32_t height, std::uint32_t native_target_type, std::uint32_t native_eye);

	// Screen background fields retain raw signed words; native clear zero-extends each word before conversion.
	[[nodiscard]] std::array<float, 4> PrepareRenderTargetClearColor(const std::array<std::int32_t, 4>& actual_words);

} // namespace sgre::scene
