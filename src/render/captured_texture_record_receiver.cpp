#include "sgre/render/captured_texture_record_receiver.hpp"

#include "sgre/scene/motion_shader_program.hpp"
#include "sgre/scene/sprite_pipeline.hpp"

#include <cstddef>
#include <stdexcept>

namespace sgre::render {
	static scene::SpritePipeline PrepareCapturedPipeline(std::uint32_t attributes) {
		scene::MotionSpriteMaterial material;
		material.TextureFormat = resource::PixelFormat::Bc7;
		material.TextureAttributes = attributes;
		material.BlendMode = 0;
		auto pipeline = scene::SelectSpritePipeline(material, {});
		scene::MotionShaderProgramInput input;
		input.Layout = scene::MotionVertexLayout::XyUv;
		input.Texture = scene::MotionShaderTexture::Rgba;
		input.TextureAttributes = attributes;
		const auto program = scene::SelectOrdinaryMotionShaderProgram(input);
		pipeline.NativeVertexShaderIndex = program.VertexIndex;
		if (program.VertexIndex != 4 || pipeline.NativePixelShaderIndex != program.PixelIndex) {
			throw std::runtime_error("Captured requires its native ordinary format2 white/opacity shader pair");
		}

		return pipeline;
	}

	static scene::SpritePipeline PrepareCapturedOpacityMapPipeline(std::uint32_t address_u, std::uint32_t address_v) {
		scene::MotionSpriteMaterial material;
		material.TextureFormat = resource::PixelFormat::Bc7;
		material.TextureAttributes = 0;
		material.BlendMode = 0;
		scene::SpritePipelineOptions options;
		options.Sampling = scene::TextureSampling::Linear;
		options.AddressU = static_cast<scene::TextureAddress>(address_u);
		options.AddressV = static_cast<scene::TextureAddress>(address_v);
		auto pipeline = scene::SelectSpritePipeline(material, options);
		pipeline.NativeVertexShaderIndex = 2;
		pipeline.NativePixelShaderIndex = 2040;
		return pipeline;
	}

	CapturedTextureRecordReceiver::CapturedTextureRecordReceiver(SpriteRenderRecords& actual_records, SpritePassRenderer& actual_renderer, scene::MutableTextureBackend& actual_capture_backend) : m_Records(actual_records), m_Renderer(actual_renderer), m_Backend(actual_capture_backend) {
		m_Records.RequireRenderer(m_Renderer);
		if (m_Records.IsActive()) {
			throw std::runtime_error("Captured pipelines require actual outer-frame quiescence");
		}
		m_Pipelines[0] = m_Renderer.PreparePipeline(PrepareCapturedPipeline(0));
		m_Pipelines[1] = m_Renderer.PreparePipeline(PrepareCapturedPipeline(4));
		for (std::uint32_t vertical = 0; vertical != 3; ++vertical) {
			for (std::uint32_t horizontal = 0; horizontal != 3; ++horizontal) {
				m_MapPipelines[vertical * 3 + horizontal] = m_Renderer.PreparePipeline(PrepareCapturedOpacityMapPipeline(horizontal, vertical));
			}
		}
	}

	void CapturedTextureRecordReceiver::Draw(scene::MutableTextureOwner& texture, const scene::RawTextureDrawPacket& packet) {
		m_Records.RequireRenderer(m_Renderer);
		const auto state = texture.State();
		if (m_Drawing || !m_Records.IsActive() || !texture.UsesBackend(m_Backend) || state.Storage != scene::MutableTextureStorage::StagingCompanion || !state.HasCompanion || state.Locked || state.Description.OriginalFormat != 28 || (packet.Color & 0x00ffffffu) != 0x00ffffffu || packet.TextureAttributes != (packet.Color == 0xffffffffu ? 4u : 0u)) {
			throw std::runtime_error("Captured drawing requires its actual live staging source and native ordinary white-opacity packet");
		}
		m_Drawing = true;
		try {
			texture.SetFiltering(0, 0);
			const auto sampled = m_Records.PrepareMutableTexture(texture, 0, scene::MutableTextureStage::Pixel);
			m_Records.BeginRecord();
			m_Records.SetBlend(0, 0);
			// Native strip indices[0,1,2,3] name these same four format2 vertices in their authored order.
			m_Records.AppendRaw(sampled, m_Pipelines[packet.TextureAttributes == 4 ? 1u : 0u], packet);
			m_Records.EndRecord();
		} catch (...) {
			m_Drawing = false;
			throw;
		}
		m_Drawing = false;
	}
	void CapturedTextureRecordReceiver::DrawOpacityMap(scene::MutableTextureOwner& texture, scene::TextureCache& cache, scene::MotionTextureWrapper::Borrowed map, const scene::CapturedOpacityMapDrawPacket& packet) {
		m_Records.RequireRenderer(m_Renderer);
		const auto state = texture.State();
		if (m_Drawing || !m_Records.IsActive() || m_Records.TextureMaskPhase() != scene::MotionMaskPhase::Idle || !texture.UsesBackend(m_Backend) || state.Storage != scene::MutableTextureStorage::StagingCompanion || !state.HasCompanion || state.Locked || state.Description.OriginalFormat != 28 || !map.IsLive() || packet.AddressU > 2 || packet.AddressV > 2) {
			throw std::runtime_error("Captured opacity map drawing requires its live Capture/Image owners, exact active frame and idle mask");
		}
		const auto handle = map.Texture();
		const auto actual = cache.Get(handle);
		const auto address = cache.Address(handle);
		if (actual.Minification != 1 || actual.Magnification != 1 || address.Horizontal != packet.AddressU || address.Vertical != packet.AddressV) {
			throw std::runtime_error("Captured opacity map sampler must be written on its actual shared Image wrapper before drawing");
		}
		// Cached A8 uploads retain native raw red zero; L8 retains its actual luminance in red.
		// No ordinary shader channel expansion or additional Image/cache ownership is introduced here.
		m_Drawing = true;
		try {
			texture.SetFiltering(0, 0);
			const auto sampled = m_Records.PrepareMutableTexture(texture, 0, scene::MutableTextureStage::Pixel);
			m_Records.BeginRecord();
			m_Records.SetBlend(0, 0);
			m_Records.AppendCapturedOpacityMap(sampled, actual.Backend, m_MapPipelines[packet.AddressV * 3 + packet.AddressU], packet);
			m_Records.EndRecord();
		} catch (...) {
			m_Drawing = false;
			throw;
		}
		m_Drawing = false;
	}
} // namespace sgre::render
