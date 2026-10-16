#include "sgre/render/screen_projection_record_receiver.hpp"

#include "sgre/scene/motion_shader_program.hpp"
#include "sgre/scene/sprite_pipeline.hpp"

#include <array>
#include <exception>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sgre::render {
	class ScreenProjectionRecordReceiver::Implementation {
	public:
		Implementation(SpriteRenderRecords& records, SpritePassRenderer& renderer, FrameHostPool& pool) : Records(records), Renderer(renderer), Pool(pool) {
			Records.RequireRenderer(Renderer);
			Pool.RequireRecords(Records);
			if (Records.IsActive()) {
				throw std::runtime_error("ScreenProjection pipelines require idle actual graphics setup");
			}
			Inputs = std::vector<std::optional<SpriteHostInputs>>(Renderer.Description().MaxDraws);
			Borrows = std::vector<std::optional<FrameHostInputBorrow>>(Inputs.size());
			for (std::size_t index = 0; index < Pipelines.size(); ++index) {
				scene::MotionSpriteMaterial material;
				material.TextureFormat = resource::PixelFormat::Bc7;
				material.TextureAttributes = index % 2 == 0 ? 4u : 0u;
				material.BlendMode = static_cast<std::uint32_t>(index / 4);
				scene::SpritePipelineOptions options;
				options.Sampling = (index % 4) / 2 == 0 ? scene::TextureSampling::Nearest : scene::TextureSampling::Linear;
				auto pipeline = scene::SelectSpritePipeline(material, options);
				scene::MotionShaderProgramInput input;
				input.Layout = scene::MotionVertexLayout::XyzUv;
				input.Texture = scene::MotionShaderTexture::Rgba;
				input.TextureAttributes = material.TextureAttributes;
				const auto program = scene::SelectOrdinaryMotionShaderProgram(input);
				pipeline.NativeVertexShaderIndex = program.VertexIndex;
				pipeline.NativePixelShaderIndex = program.PixelIndex;
				Pipelines[index] = Renderer.PreparePipeline(pipeline);
			}
		}

		SpriteRenderRecords& Records;
		SpritePassRenderer& Renderer;
		FrameHostPool& Pool;
		std::array<SpritePipelineHandle, 8> Pipelines;
		std::vector<std::optional<SpriteHostInputs>> Inputs;
		std::vector<std::optional<FrameHostInputBorrow>> Borrows;
		std::size_t Count = 0;
		bool Operating = false;
	};

	ScreenProjectionRecordReceiver::ScreenProjectionRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, FrameHostPool& pool) : m_Implementation(std::make_unique<Implementation>(records, renderer, pool)) {
	}

	ScreenProjectionRecordReceiver::~ScreenProjectionRecordReceiver() {
		try {
			ReleaseFrameInputs();
		} catch (...) {
			std::terminate();
		}
	}

	void ScreenProjectionRecordReceiver::Draw(SpriteTextureHandle actual_screen_texture, const scene::ScreenProjectionPacket& packet) {
		auto& state = *m_Implementation;
		if (state.Operating || !state.Records.IsActive() || state.Count == state.Inputs.size() || packet.BlendMode > 1 || packet.TextureAttributes != (packet.Color == 0xffffffff ? 4u : 0u)) {
			throw std::runtime_error("ScreenProjection drawing requires its genuine non-reentrant live frame and bounded native packet");
		}
		if (packet.Vertices.empty() && packet.Indices.empty()) {
			return;
		}
		state.Operating = true;
		try {
			state.Pool.EmplaceProjectionInputs(state.Inputs[state.Count], state.Borrows[state.Count], packet.Vertices, packet.Indices);
			const auto& input = *state.Inputs[state.Count];
			++state.Count;
			state.Renderer.SetSampledTargetFiltering(actual_screen_texture, packet.Smoothing ? 1u : 0u, packet.Smoothing ? 1u : 0u);
			state.Records.BeginRecord();
			try {
				state.Records.SetBlend(packet.BlendMode, 0);
				state.Records.AppendProjection(actual_screen_texture, state.Pipelines[packet.BlendMode * 4 + (packet.Smoothing ? 2 : 0) + (packet.Color == 0xffffffff ? 0 : 1)], input, packet.Color);
			} catch (...) {
				state.Records.EndRecord();
				throw;
			}
			state.Records.EndRecord();
		} catch (...) {
			state.Operating = false;
			throw;
		}
		state.Operating = false;
	}

	void ScreenProjectionRecordReceiver::ReleaseFrameInputs() {
		auto& state = *m_Implementation;
		if (state.Operating || (state.Count != 0 && state.Records.IsActive())) {
			throw std::runtime_error("ScreenProjection input retirement requires the actual completed or aborted frame");
		}
		while (state.Count != 0) {
			--state.Count;
			state.Borrows[state.Count].reset();
			state.Inputs[state.Count].reset();
		}
	}
} // namespace sgre::render
