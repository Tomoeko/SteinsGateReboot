#include "sgre/scene/picture_packet.hpp"

#include "sgre/scene/motion_shader_program.hpp"

#include <cmath>
#include <stdexcept>

namespace sgre::scene {

	RawTextureDrawPacket PreparePictureDrawPacket(const PictureLayerTaskState& state, const PictureRegion& region, resource::MotionPoint actual_texture_size) {
		if (state.Closed || !std::isfinite(actual_texture_size.X) || !std::isfinite(actual_texture_size.Y) || actual_texture_size.X <= 0 || actual_texture_size.Y <= 0) {
			throw std::runtime_error("Picture draw packet requires its live task and actual positive texture dimensions");
		}
		const float left = region.Destination[0] + state.Coordinate.X;
		const float top = region.Destination[1] + state.Coordinate.Y;
		const float right = region.Destination[2] + state.Coordinate.X;
		const float bottom = region.Destination[3] + state.Coordinate.Y;
		const float u0 = region.SourceBounds[0] / actual_texture_size.X;
		const float v0 = region.SourceBounds[1] / actual_texture_size.Y;
		const float u1 = region.SourceBounds[2] / actual_texture_size.X;
		const float v1 = region.SourceBounds[3] / actual_texture_size.Y;
		for (const auto value : {left, top, right, bottom, u0, v0, u1, v1}) {
			if (!std::isfinite(value)) {
				throw std::runtime_error("Picture draw packet requires finite native destination and normalized source words");
			}
		}

		RawTextureDrawPacket packet;
		packet.Color = ((state.Opacity & 255u) << 24) | 0x00ffffffu;
		packet.TextureAttributes = state.Opacity == 255 ? 4u : 0u;
		packet.Vertices = {RawTextureVertex{{left, top}, {u0, v0}}, RawTextureVertex{{left, bottom}, {u0, v1}}, RawTextureVertex{{right, top}, {u1, v0}}, RawTextureVertex{{right, bottom}, {u1, v1}}};

		return packet;
	}

	SpritePipeline PreparePicturePipeline(resource::PixelFormat format, std::uint32_t attributes, std::uint32_t blend_mode, std::int32_t minification, std::int32_t magnification) {
		if ((attributes != 0 && attributes != 4) || blend_mode > 10 || minification < 0 || minification > 1 || magnification < 0 || magnification > 1) {
			throw std::runtime_error("Picture pipeline requires native ordinary blend0..10, white opacity attributes and measured filtering words");
		}
		MotionSpriteMaterial material;
		material.TextureFormat = format;
		material.TextureAttributes = attributes;
		material.BlendMode = blend_mode;
		SpritePipelineOptions options;
		options.Sampling = minification == 1 && magnification == 1 ? TextureSampling::Linear : TextureSampling::Nearest;
		options.AddressU = TextureAddress::Mirror;
		options.AddressV = TextureAddress::Mirror;
		auto pipeline = SelectSpritePipeline(material, options);
		pipeline.NativeVertexShaderIndex = 4;
		if (minification != magnification) {
			pipeline.Sampler.Magnification = TextureSampling::Linear;
			pipeline.Sampler.NativeFilterCode = 4;
		}

		return pipeline;
	}

} // namespace sgre::scene
