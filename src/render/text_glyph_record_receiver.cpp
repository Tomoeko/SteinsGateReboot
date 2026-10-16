#include "sgre/render/text_glyph_record_receiver.hpp"

#include "sgre/render/frame_host_pool.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sgre::render {

	class TextGlyphRecordReceiver::Implementation {
	public:
		Implementation(SpriteRenderRecords& records, SpritePassRenderer& renderer, FrameHostPool& pool) : Records(records), Renderer(renderer), Pool(pool) {
			Records.RequireRenderer(Renderer);
			Pool.RequireRecords(Records);
			if (Records.IsActive()) {
				throw std::runtime_error("text glyph pipelines require actual frame quiescence");
			}
			const auto description = Renderer.Description();
			Vertices.resize(std::min<std::size_t>(description.MaxVertices, 65536));
			Indices.resize(description.MaxIndices);
			Inputs = std::vector<std::optional<SpriteHostInputs>>(description.MaxDraws);
			InputBorrows = std::vector<std::optional<FrameHostInputBorrow>>(description.MaxDraws);
			for (std::size_t filter = 0; filter < Pipelines.size(); ++filter) {
				scene::MotionSpriteMaterial material;
				material.TextureFormat = resource::PixelFormat::Bc7;
				material.TextureAttributes = 0;
				material.BlendMode = 0;
				scene::SpritePipelineOptions options;
				options.Sampling = filter == 0 ? scene::TextureSampling::Nearest : scene::TextureSampling::Linear;
				Pipelines[filter] = Renderer.PreparePipeline(scene::SelectSpritePipeline(material, options));
			}
		}

		~Implementation() noexcept {
			try {
				if (InputCount != 0 && Records.IsActive()) {
					std::terminate();
				}
			} catch (...) {
				std::terminate();
			}
		}

		SpriteRenderRecords& Records;
		SpritePassRenderer& Renderer;
		FrameHostPool& Pool;
		std::array<SpritePipelineHandle, 2> Pipelines;
		std::vector<scene::MotionSpriteVertex> Vertices;
		std::vector<std::uint32_t> Indices;
		std::vector<std::optional<SpriteHostInputs>> Inputs;
		std::vector<std::optional<FrameHostInputBorrow>> InputBorrows;
		std::size_t InputCount = 0;
		bool Drawing = false;

		void Draw(text::TextGlyphAtlasOwner& atlas, std::span<const text::VectorGlyphDrawRectangle> rectangles) {
			Records.RequireRenderer(Renderer);
			Pool.RequireRecords(Records);
			if (Drawing || !Records.IsActive()) {
				throw std::runtime_error("text glyph submission requires one actual active serialized frame");
			}
			if (rectangles.empty()) {
				return;
			}
			const auto atlas_state = atlas.State();
			if (atlas_state.Closed || !atlas_state.Texture || atlas_state.Texture->Locked || atlas_state.Texture->Description.OriginalFormat != 87 || atlas_state.Texture->Description.Width != atlas_state.Packing.WidthPixels || atlas_state.Texture->Description.Height != atlas_state.Packing.HeightPixels) {
				throw std::runtime_error("text glyph submission requires its actual live unmapped BGRA atlas");
			}
			const auto batch_capacity = std::min(Vertices.size() / 4, Indices.size() / 6);
			if (batch_capacity == 0 || (rectangles.size() - 1) / batch_capacity + 1 > Inputs.size() - InputCount) {
				throw std::runtime_error("text glyph rectangles exceed actual prepared frame input capacity");
			}

			Drawing = true;
			try {
				const auto texture = Records.PrepareMutableTexture(atlas.Texture(), 0, scene::MutableTextureStage::Pixel);
				const auto metadata = Records.PreparedTexture(texture);
				if (!metadata.Mutable || metadata.OriginalDxgiFormat != 87 || metadata.Channels != scene::MotionShaderTexture::Rgba || metadata.Width != atlas_state.Packing.WidthPixels || metadata.Height != atlas_state.Packing.HeightPixels) {
					throw std::runtime_error("text glyph atlas backend differs from the genuine prepared texture");
				}
				Records.BeginRecord();
				Records.SetBlend(0, 0);
				Records.SetFilterColor(0xffffffff);
				std::size_t first = 0;
				while (first < rectangles.size()) {
					const auto count = std::min(batch_capacity, rectangles.size() - first);
					for (std::size_t index = 0; index < count; ++index) {
						const auto vertices = text::PrepareVectorGlyphVertices(rectangles[first + index], metadata.Width, metadata.Height);
						std::copy(vertices.begin(), vertices.end(), Vertices.begin() + static_cast<std::ptrdiff_t>(index * 4));
						static constexpr std::array<std::uint32_t, 6> kIndices{0, 1, 2, 1, 3, 2};
						for (std::size_t element = 0; element < kIndices.size(); ++element) {
							Indices[index * 6 + element] = static_cast<std::uint32_t>(index * 4) + kIndices[element];
						}
					}
					Pool.EmplaceInputs(Inputs[InputCount], InputBorrows[InputCount], std::span<const scene::MotionSpriteVertex>(Vertices).first(count * 4), std::span<const std::uint32_t>(Indices).first(count * 6));
					const auto input = InputCount++;
					Records.AppendGlyphs(texture, Pipelines[atlas_state.Filtering ? 1 : 0], *Inputs[input]);
					first += count;
				}
				Records.EndRecord();
			} catch (...) {
				Drawing = false;
				throw;
			}
			Drawing = false;
		}

		void ReleaseFrameInputs() {
			if (Drawing || Records.IsActive()) {
				throw std::runtime_error("text glyph input retirement requires its actual completed or aborted frame");
			}
			for (std::size_t index = 0; index < InputCount; ++index) {
				InputBorrows[index].reset();
				Inputs[index].reset();
			}
			InputCount = 0;
		}
	};

	TextGlyphRecordReceiver::TextGlyphRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, FrameHostPool& pool) : m_Implementation(std::make_unique<Implementation>(records, renderer, pool)) {
	}

	TextGlyphRecordReceiver::~TextGlyphRecordReceiver() = default;

	void TextGlyphRecordReceiver::Draw(text::TextGlyphAtlasOwner& actual_atlas, std::span<const text::VectorGlyphDrawRectangle> rectangles) {
		m_Implementation->Draw(actual_atlas, rectangles);
	}

	void TextGlyphRecordReceiver::ReleaseFrameInputs() {
		m_Implementation->ReleaseFrameInputs();
	}

} // namespace sgre::render
