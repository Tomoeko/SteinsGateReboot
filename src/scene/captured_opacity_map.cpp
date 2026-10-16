#include "sgre/scene/captured_opacity_map.hpp"

#include "sgre/scene/captured_task.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace sgre::scene {
	CapturedOpacityMapDrawPacket PrepareCapturedOpacityMapDrawPacket(const CapturedTaskState& actual_state, const CapturedOpacityMapState& actual_map) {
		if (actual_map.Width < 1 || actual_map.Width > 765 || actual_map.AddressU > 2 || actual_map.AddressV > 2 || !std::isfinite(actual_map.ImageRatio.X) || !std::isfinite(actual_map.ImageRatio.Y) || actual_map.ImageRatio.X <= 0 || actual_map.ImageRatio.Y <= 0 || !std::isfinite(actual_map.Repeat.X) || !std::isfinite(actual_map.Repeat.Y)) {
			throw std::runtime_error("Captured opacity map requires actual bounded width, image ratios, repeat and native address selectors");
		}
		const auto ordinary = PrepareCapturedDrawPacket(actual_state);
		const auto right = actual_map.Repeat.X * actual_map.ImageRatio.X;
		const auto bottom = actual_map.Repeat.Y * actual_map.ImageRatio.Y;
		if (!std::isfinite(right) || !std::isfinite(bottom)) {
			throw std::runtime_error("Captured opacity map UV extent is not finite");
		}
		CapturedOpacityMapDrawPacket packet;
		const std::array<resource::MotionPoint, 4> map_uv{{{0, 0}, {0, bottom}, {right, 0}, {right, bottom}}};
		for (std::size_t index = 0; index < packet.Vertices.size(); ++index) {
			packet.Vertices[index] = {ordinary.Vertices[index].Position, 0xffffffff, ordinary.Vertices[index].Uv, map_uv[index]};
		}
		// Original4fb572..5c7 divides the expanded width before multiplying the opacity complement.
		const auto opacity = actual_state.Opacity / 255.0f;
		const auto complement = 1.0f - opacity;
		const auto expanded_width = static_cast<float>(actual_map.Width + 255u) / 255.0f;
		packet.Phase = complement * expanded_width;
		packet.InverseVague = (1.0f / static_cast<float>(actual_map.Width)) * 255.0f;
		packet.AddressU = actual_map.AddressU;
		packet.AddressV = actual_map.AddressV;

		return packet;
	}
} // namespace sgre::scene
