#include "sgre/scene/raw_texture_packet.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {
	RawTextureDrawPacket PrepareRawTextureDrawPacket(resource::MotionPoint position, resource::MotionPoint size, std::uint32_t packed_argb) {
		if (!std::isfinite(position.X) || !std::isfinite(position.Y) || !std::isfinite(size.X) || !std::isfinite(size.Y)) {
			throw std::runtime_error("raw texture packet requires finite position and size");
		}
		const auto right = position.X + size.X;
		const auto bottom = position.Y + size.Y;
		if (!std::isfinite(right) || !std::isfinite(bottom)) {
			throw std::runtime_error("raw texture packet extent is not finite");
		}
		RawTextureDrawPacket packet;
		packet.Color = packed_argb;
		packet.TextureAttributes = packed_argb == 0xffffffff ? 4u : 0u;
		packet.Vertices = {RawTextureVertex{{position.X, position.Y}, {0, 0}}, RawTextureVertex{{position.X, bottom}, {0, 1}}, RawTextureVertex{{right, position.Y}, {1, 0}}, RawTextureVertex{{right, bottom}, {1, 1}}};
		return packet;
	}
} // namespace sgre::scene
