#include "sgre/render/graphics_default_state.hpp"

#include <limits>

namespace sgre::render {
	GraphicsDefaultRenderState PrepareDefaultGraphicsRenderState() {
		GraphicsDefaultRenderState state;
		state.Blend.Enabled = false;
		state.Blend.RgbOperation = scene::SpriteBlendOperation::Add;
		state.Blend.SourceRgb = scene::SpriteBlendFactor::One;
		state.Blend.DestinationRgb = scene::SpriteBlendFactor::Zero;
		state.Blend.AlphaOperation = scene::SpriteBlendOperation::Add;
		state.Blend.SourceAlpha = scene::SpriteBlendFactor::Zero;
		state.Blend.DestinationAlpha = scene::SpriteBlendFactor::One;
		state.Blend.WriteMask = 15;
		state.SampleMask = std::numeric_limits<std::uint32_t>::max();

		return state;
	}
} // namespace sgre::render
