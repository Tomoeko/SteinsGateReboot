#include "sgre/render/raw_texture_record_receiver.hpp"

#include "sgre/scene/motion_shader_program.hpp"
#include "sgre/scene/sprite_pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>

namespace sgre::render {

	static std::size_t RawPipelineIndex(std::uint32_t blend_mode, std::uint32_t attributes) {
		if ((blend_mode > 5 && (blend_mode < 16 || blend_mode > 21)) || (attributes != 0 && attributes != 4)) {
			throw std::runtime_error("RawTex frame requires ordinary blend0..5/16..21 and its actual white/tinted attributes");
		}

		return static_cast<std::size_t>((blend_mode & 15) + ((blend_mode & 16) != 0 ? 6 : 0)) * 2 + (attributes == 4 ? 1 : 0);
	}

	static scene::SpritePipeline PrepareRawPipeline(std::uint32_t blend_mode, std::uint32_t attributes) {
		scene::MotionShaderProgramInput input;
		input.Layout = scene::MotionVertexLayout::XyUv;
		input.Texture = scene::MotionShaderTexture::Rgba;
		input.TextureAttributes = attributes;
		input.BlendMode = blend_mode;
		const auto shaders = scene::SelectOrdinaryMotionShaderProgram(input);
		scene::MotionSpriteMaterial material;
		// This selector tag supplies RGBA channel semantics; mutable DXGI storage is checked at submission.
		material.TextureFormat = resource::PixelFormat::Bc7;
		material.TextureAttributes = attributes;
		material.BlendMode = blend_mode;
		auto pipeline = scene::SelectSpritePipeline(material, {});
		if (shaders.VertexIndex != 4 || pipeline.NativePixelShaderIndex != shaders.PixelIndex) {
			throw std::runtime_error("RawTex ordinary shader selection differs from its XY/UV and uniform-color interface");
		}
		pipeline.NativeVertexShaderIndex = shaders.VertexIndex;

		return pipeline;
	}

	RawTextureRecordReceiver::RawTextureRecordReceiver(SpriteRenderRecords& actual_records, SpritePassRenderer& actual_renderer) : m_Records(actual_records), m_Renderer(actual_renderer) {
		m_Records.RequireRenderer(m_Renderer);
		if (m_Records.IsActive()) {
			throw std::runtime_error("RawTex backend/pipeline preparation requires actual outer-frame quiescence");
		}

		m_Backend = m_Renderer.CreateMutableTextureBackend();
		for (std::uint32_t index = 0; index < 12; ++index) {
			const auto mode = index < 6 ? index : index + 10;
			for (const auto attributes : {0u, 4u}) {
				m_Pipelines[RawPipelineIndex(mode, attributes)] = m_Renderer.PreparePipeline(PrepareRawPipeline(mode, attributes));
			}
		}
	}

	scene::MutableTextureBackend& RawTextureRecordReceiver::Backend() {
		m_Records.RequireRenderer(m_Renderer);
		if (m_Drawing || m_Records.IsActive()) {
			throw std::runtime_error("RawTex backend preparation requires its inactive frame receiver");
		}

		return *m_Backend;
	}

	void RawTextureRecordReceiver::Draw(scene::RawTextureTask& actual_task, const scene::RawTextureDrawPacket& actual_packet) {
		m_Records.RequireRenderer(m_Renderer);
		if (m_Drawing || !m_Records.IsActive()) {
			throw std::runtime_error("RawTex submission requires one serialized operation in its actual outer frame");
		}
		const auto state = actual_task.State();
		if (state.Closed || !actual_task.Texture().UsesBackend(*m_Backend)) {
			throw std::runtime_error("RawTex submission requires the live texture owned by this exact containing backend");
		}
		if (state.Task.Status == scene::TaskStatus::Retire || state.Task.DrawPaused) {
			return;
		}

		const auto pipeline = m_Pipelines[RawPipelineIndex(state.BlendMode, actual_packet.TextureAttributes)];
		m_Drawing = true;
		try {
			const auto texture = m_Records.PrepareMutableTexture(actual_task.Texture(), 0, scene::MutableTextureStage::Pixel);
			m_Records.BeginRecord();
			m_Records.SetBlend(state.BlendMode, 0);
			m_Records.AppendRaw(texture, pipeline, actual_packet);
			m_Records.EndRecord();
		} catch (...) {
			m_Drawing = false;

			throw;
		}
		m_Drawing = false;
	}

} // namespace sgre::render
