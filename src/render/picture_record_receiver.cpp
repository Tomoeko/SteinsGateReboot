#include "sgre/render/picture_record_receiver.hpp"

#include <stdexcept>

namespace sgre::render {

	static std::size_t PicturePipelineIndex(resource::PixelFormat format, std::uint32_t blend, std::uint32_t attributes, std::int32_t minimum, std::int32_t maximum) {
		std::size_t index = 0;
		switch (format) {
		case resource::PixelFormat::Alpha8:
			index = 0;
			break;
		case resource::PixelFormat::Luminance8:
			index = 1;
			break;
		case resource::PixelFormat::Bc7:
			index = 2;
			break;
		default:
			throw std::runtime_error("Picture source format has no recovered native shader class");
		}
		if (blend > 10 || (attributes != 0 && attributes != 4) || minimum < 0 || minimum > 1 || maximum < 0 || maximum > 1) {
			throw std::runtime_error("Picture receiving requires its measured ordinary blend, opacity attributes and sampler words");
		}

		return ((index * 11 + blend) * 2 + (attributes == 4 ? 1u : 0u)) * 4 + static_cast<std::size_t>(minimum) * 2 + static_cast<std::size_t>(maximum);
	}

	PictureRecordReceiver::PictureRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, scene::TextureCache& textures)
	    : m_Records(records), m_Renderer(renderer), m_Textures(textures) {
		m_Records.RequireRenderer(m_Renderer);
		if (m_Records.IsActive()) {
			throw std::runtime_error("Picture pipeline preparation requires actual outer-frame quiescence");
		}
		for (const auto format : {resource::PixelFormat::Alpha8, resource::PixelFormat::Luminance8, resource::PixelFormat::Bc7}) {
			for (std::uint32_t blend = 0; blend != 11; ++blend) {
				for (const auto attributes : {0u, 4u}) {
					for (std::int32_t minimum = 0; minimum != 2; ++minimum) {
						for (std::int32_t maximum = 0; maximum != 2; ++maximum) {
							m_Pipelines[PicturePipelineIndex(format, blend, attributes, minimum, maximum)] = m_Renderer.PreparePipeline(scene::PreparePicturePipeline(format, attributes, blend, minimum, maximum));
						}
					}
				}
			}
		}
	}

	void PictureRecordReceiver::Draw(const scene::PictureDrawFrame& frame) {
		m_Records.RequireRenderer(m_Renderer);
		if (m_Drawing || !m_Records.IsActive() || frame.State.Closed || m_Records.TextureMaskPhase() != scene::MotionMaskPhase::Idle) {
			throw std::runtime_error("Picture drawing requires its live task, actual active frame and idle mask");
		}
		const auto attributes = frame.State.Opacity == 255 ? 4u : 0u;
		// Preflight without allocating or changing shared source sampler state.
		for (const auto& region : frame.Regions) {
			if (region.Source >= frame.Sources.size() || !frame.Sources[region.Source].IsLive()) {
				throw std::runtime_error("Picture piece requires its exact live Image source wrapper");
			}
			const auto handle = frame.Sources[region.Source].Texture();
			const auto source = m_Textures.Get(handle);
			const auto address = m_Textures.Address(handle);
			if (address.Horizontal != 2 || address.Vertical != 2) {
				throw std::runtime_error("Picture requires the recovered native Image mirror-addressed source");
			}
			static_cast<void>(m_Records.PreparedTexture(source.Backend));
			static_cast<void>(PicturePipelineIndex(source.Description.Format, frame.State.BlendMode, attributes, frame.State.Minification, frame.State.Magnification));
			static_cast<void>(scene::PreparePictureDrawPacket(frame.State, region, {static_cast<float>(source.Description.Width), static_cast<float>(source.Description.Height)}));
		}
		m_Drawing = true;
		try {
			// Native4f6570 begins/ends its ordinary record even when the piece vector is empty.
			m_Records.BeginRecord();
			m_Records.SetPictureBlend(frame.State.BlendMode);
			for (const auto& region : frame.Regions) {
				const auto& wrapper = frame.Sources[region.Source];
				wrapper.SetFiltering(frame.State.Minification, frame.State.Magnification);
				const auto source = m_Textures.Get(wrapper.Texture());
				const auto packet = scene::PreparePictureDrawPacket(frame.State, region, {static_cast<float>(source.Description.Width), static_cast<float>(source.Description.Height)});
				const auto pipeline = m_Pipelines[PicturePipelineIndex(source.Description.Format, frame.State.BlendMode, attributes, frame.State.Minification, frame.State.Magnification)];
				m_Records.AppendPicture(source.Backend, pipeline, packet, frame.State.Opacity);
			}
			m_Records.EndRecord();
		} catch (...) {
			m_Drawing = false;
			throw;
		}
		m_Drawing = false;
	}

} // namespace sgre::render
