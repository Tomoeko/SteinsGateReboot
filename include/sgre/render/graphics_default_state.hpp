#pragma once

#include "sgre/scene/sprite_pipeline.hpp"

#include <array>
#include <cstdint>

namespace sgre::render {
	enum class GraphicsRasterFill {
		Solid
	};

	enum class GraphicsRasterCull {
		None
	};

	struct GraphicsDefaultRasterState {
		GraphicsRasterFill Fill = GraphicsRasterFill::Solid;
		GraphicsRasterCull Cull = GraphicsRasterCull::None;
		bool FrontCounterClockwise = false;
		std::int32_t DepthBias = 0;
		float DepthBiasClamp = 0;
		float SlopeScaledDepthBias = 0;
		bool DepthClipEnabled = false;
		bool ScissorEnabled = true;
		bool MultisampleEnabled = false;
		bool AntialiasedLineEnabled = false;
	};

	struct GraphicsDefaultDepthState {
		bool DepthEnabled = false;
		bool DepthWriteEnabled = false;
		// Native comparison8 is Always. Disabled depth/stencil do not supply an attachment owner.
		std::uint32_t Comparison = 8;
		bool StencilEnabled = false;
		std::uint8_t StencilReadMask = 0;
		std::uint8_t StencilWriteMask = 0;
		// Disabled native face-operation words are zero, rather than inferred valid enabled-stencil defaults.
		std::array<std::uint32_t, 8> StencilFaceWords{};
		std::uint32_t StencilReference = 0;
	};

	struct GraphicsDefaultRenderState {
		bool AlphaToCoverageEnabled = false;
		bool IndependentBlendEnabled = false;
		scene::SpriteBlendState Blend;
		std::array<float, 4> BlendFactors{};
		std::uint32_t SampleMask = 0;
		GraphicsDefaultDepthState Depth;
		GraphicsDefaultRasterState Raster;
	};

	// Original4c56e0 descriptor construction only. Device creation/binding/failure and attachment ownership remain separate.
	[[nodiscard]] GraphicsDefaultRenderState PrepareDefaultGraphicsRenderState();
} // namespace sgre::render
