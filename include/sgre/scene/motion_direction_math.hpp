#pragma once

namespace sgre::scene {

	// Recovered original scalar/SSE CRT atan2 output for finite float32 inputs, in radians.
	// CPU-selected alternative kernels and CRT error/floating-status side effects remain separate domains.
	[[nodiscard]] float EvaluateMotionDirectionRadians(float vertical, float horizontal);

} // namespace sgre::scene
