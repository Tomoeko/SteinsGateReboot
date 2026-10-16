#pragma once

#include "sgre/scene/picture_layer_task.hpp"
#include "sgre/scene/raw_texture_packet.hpp"
#include "sgre/scene/sprite_pipeline.hpp"

#include <span>

namespace sgre::scene {

	struct PictureDrawFrame {
		PictureLayerTaskState State;
		// Actual Picture task geometry and Image wrapper identities enclose the synchronous receiving call.
		std::span<const PictureRegion> Regions;
		std::span<const MotionTextureWrapper::Borrowed> Sources;
	};

	[[nodiscard]] RawTextureDrawPacket PreparePictureDrawPacket(const PictureLayerTaskState& state, const PictureRegion& region, resource::MotionPoint actual_texture_size);
	// Native Image sources use mirror2/2. Mixed filtering01/10 selects native filter4, independently of format.
	[[nodiscard]] SpritePipeline PreparePicturePipeline(resource::PixelFormat format, std::uint32_t attributes, std::uint32_t blend_mode, std::int32_t minification, std::int32_t magnification);

} // namespace sgre::scene
