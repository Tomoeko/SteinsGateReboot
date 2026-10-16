#include "sgre/render/bitmap_font_record_receiver.hpp"

#include "sgre/render/frame_host_pool.hpp"
#include "sgre/scene/bitmap_console.hpp"
#include "sgre/scene/indicator_placement.hpp"
#include "sgre/text/bitmap_element_draw.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace sgre::render {

	static std::size_t BitmapFormatIndex(resource::PixelFormat format) {
		switch (format) {
		case resource::PixelFormat::Bc7:
			return 0;
		case resource::PixelFormat::Alpha8:
			return 1;
		case resource::PixelFormat::Luminance8:
			return 2;
		default:
			throw std::runtime_error("bitmap font draw requires the actual recovered atlas channel formats");
		}
	}

	static scene::MotionShaderTexture BitmapTextureChannels(resource::PixelFormat format) {
		switch (format) {
		case resource::PixelFormat::Bc7:
			return scene::MotionShaderTexture::Rgba;
		case resource::PixelFormat::Alpha8:
			return scene::MotionShaderTexture::Alpha;
		case resource::PixelFormat::Luminance8:
			return scene::MotionShaderTexture::Luminance;
		default:
			throw std::runtime_error("bitmap font sampled atlas has unsupported actual channels");
		}
	}

	static float BitmapIntegerCoordinate(float value) {
		if (!std::isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f) {
			throw std::runtime_error("bitmap integer draw coordinate exceeds the measured native signed conversion domain");
		}

		return static_cast<float>(static_cast<std::int32_t>(value));
	}

	class BitmapFontRecordReceiver::Implementation {
	public:
		Implementation(SpriteRenderRecords& records, SpritePassRenderer& renderer, scene::TextureCache& cache, FrameHostPool& pool) : Records(records), Renderer(renderer), Cache(cache), Pool(pool), Positions(65536), LineWidths(65537) {
			Records.RequireRenderer(Renderer);
			Pool.RequireRecords(Records);
			if (Records.IsActive()) {
				throw std::runtime_error("bitmap glyph receiver must prepare its real pipelines outside the actual frame");
			}

			const auto description = Renderer.Description();
			Vertices.resize(description.MaxVertices);
			Indices.resize(description.MaxIndices);
			Inputs = std::vector<std::optional<SpriteHostInputs>>(description.MaxDraws);
			InputBorrows = std::vector<std::optional<FrameHostInputBorrow>>(description.MaxDraws);
			const std::array<resource::PixelFormat, 3> formats{resource::PixelFormat::Bc7, resource::PixelFormat::Alpha8, resource::PixelFormat::Luminance8};
			for (std::size_t format = 0; format < formats.size(); ++format) {
				for (std::size_t filter = 0; filter < 2; ++filter) {
					scene::MotionSpriteMaterial material;
					material.TextureFormat = formats[format];
					material.TextureAttributes = 0;
					material.BlendMode = 0;
					scene::SpritePipelineOptions options;
					options.Sampling = filter == 0 ? scene::TextureSampling::Nearest : scene::TextureSampling::Linear;
					Pipelines[format * 2 + filter] = Renderer.PreparePipeline(scene::SelectSpritePipeline(material, options));
				}
			}
			Output = [this](const auto& glyph) {
				Append(glyph);
			};
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
		scene::TextureCache& Cache;
		FrameHostPool& Pool;
		std::array<SpritePipelineHandle, 6> Pipelines;
		std::vector<text::TextCursor> Positions;
		std::vector<float> LineWidths;
		std::vector<scene::MotionSpriteVertex> Vertices;
		std::vector<std::uint32_t> Indices;
		std::vector<std::optional<SpriteHostInputs>> Inputs;
		std::vector<std::optional<FrameHostInputBorrow>> InputBorrows;
		std::function<void(const text::BitmapGlyphCommand&)> Output;
		const scene::BitmapFontTextureOwner* CurrentFont = nullptr;
		std::optional<scene::MotionTextureWrapper::Borrowed> BatchSource;
		std::size_t BatchVertices = 0;
		std::size_t BatchIndices = 0;
		std::size_t InputCount = 0;
		bool Operating = false;

		scene::CachedTexture ReadSource(std::size_t index) const {
			if (CurrentFont == nullptr || index >= CurrentFont->SourceCount()) {
				throw std::runtime_error("bitmap glyph source escaped its actual font provider");
			}
			const auto texture = Cache.Get(CurrentFont->Source(index).Texture());
			const auto address = Cache.Address(CurrentFont->Source(index).Texture());
			const auto& image = CurrentFont->Asset().Model->Sources[index];
			const auto sampled = Records.PreparedTexture(texture.Backend);
			if (texture.Description.Format != image.Format || texture.Description.Width != image.Width || texture.Description.Height != image.Height || sampled.Channels != BitmapTextureChannels(image.Format) || sampled.Width != image.Width || sampled.Height != image.Height || sampled.Mutable || address.Horizontal != 0 || address.Vertical != 0 || texture.Minification != texture.Magnification || texture.Minification < 0 || texture.Minification > 1) {
				throw std::runtime_error("bitmap glyph draw requires exact live atlas metadata and recovered clamp/equal filtering");
			}

			return texture;
		}

		void Flush() {
			if (BatchVertices == 0) {
				return;
			}
			if (!BatchSource || CurrentFont == nullptr || InputCount == Inputs.size()) {
				throw std::runtime_error("bitmap font draw exceeds its actual native source/input batch capacity");
			}

			const auto texture = Cache.Get(BatchSource->Texture());
			const auto pipeline = Pipelines[BitmapFormatIndex(texture.Description.Format) * 2 + static_cast<std::size_t>(texture.Minification)];
			Pool.EmplaceInputs(Inputs[InputCount], InputBorrows[InputCount], std::span<const scene::MotionSpriteVertex>(Vertices).first(BatchVertices), std::span<const std::uint32_t>(Indices).first(BatchIndices));
			const auto index = InputCount++;
			Records.AppendGlyphs(texture.Backend, pipeline, *Inputs[index]);
			BatchVertices = 0;
			BatchIndices = 0;
			BatchSource.reset();
		}

		void Append(const text::BitmapGlyphCommand& glyph) {
			if (!Operating || CurrentFont == nullptr || glyph.Font != CurrentFont->Asset().Model.get()) {
				throw std::runtime_error("bitmap glyph did not come from its actual containing font draw");
			}
			static_cast<void>(ReadSource(glyph.TextureSourceIndex));
			const auto source = CurrentFont->Source(glyph.TextureSourceIndex);
			if (BatchSource && !BatchSource->SameIdentity(source)) {
				Flush();
			}
			if (glyph.Vertices.size() > Vertices.size() - BatchVertices || glyph.Indices.size() > Indices.size() - BatchIndices) {
				throw std::runtime_error("bitmap font native source batch exceeds its prepared actual geometry capacity");
			}

			const auto base = static_cast<std::uint32_t>(BatchVertices);
			for (const auto& vertex : glyph.Vertices) {
				Vertices[BatchVertices++] = vertex;
			}
			for (const auto index : glyph.Indices) {
				Indices[BatchIndices++] = base + index;
			}
			BatchSource = source;
		}

		void Draw(const scene::BitmapConsole& console, const scene::IndicatorPlacement& placement, const scene::BitmapFontTextureOwner& actual_font, std::uint32_t argument) {
			// Native52eb40 does not use its containing Draw argument; its real camera/record owner still supplies the call.
			static_cast<void>(argument);
			Records.RequireRenderer(Renderer);
			Pool.RequireRecords(Records);
			if (Operating || !Records.IsActive() || CurrentFont != nullptr || BatchVertices != 0 || actual_font.Asset().CacheOwner != &Cache || actual_font.SourceCount() != actual_font.Asset().Model->Sources.size() || actual_font.SourceCount() > 1024 || placement.IsDetached() || !placement.Visible()) {
				throw std::runtime_error("Console glyph draw requires its actual active frame, font/cache, visible placement and quiescent receiver");
			}
			if (console.State().ScrollRemaining > 0) {
				throw std::runtime_error("Console scrolling draw requires its recovered offset and exact saved/restored scissor owner");
			}

			CurrentFont = &actual_font;
			bool begun = false;
			Operating = true;
			try {
				for (std::size_t index = 0; index < actual_font.SourceCount(); ++index) {
					static_cast<void>(ReadSource(index));
				}
				const auto bounds = placement.Bounds();
				text::TextCursor origin;
				origin.X = bounds.Left;
				origin.Y = bounds.Top;
				if (console.State().DrawAtIntegerCoordinates) {
					origin.X = BitmapIntegerCoordinate(origin.X);
					origin.Y = BitmapIntegerCoordinate(origin.Y);
				}
				text::BitmapElementDrawScratch scratch;
				scratch.Positions = Positions;
				scratch.LineWidths = LineWidths;
				Records.BeginRecord();
				begun = true;
				Records.SetBlend(0, 0);
				Records.SetFilterColor(0xffffffff);
				text::EmitBitmapElementGlyphs(*actual_font.Asset().Model, console.Displayed(), origin, console.State().Spacing, console.State().Opacity, console.State().Alignment, console.State().Extent.Width, scratch, Output);
				Flush();
				Records.EndRecord();
				begun = false;
				CurrentFont = nullptr;
				Operating = false;
			} catch (...) {
				const auto failure = std::current_exception();
				if (begun) {
					try {
						Records.AbortFrame();
					} catch (...) {
					}
				}
				BatchVertices = 0;
				BatchIndices = 0;
				BatchSource.reset();
				CurrentFont = nullptr;
				Operating = false;
				std::rethrow_exception(failure);
			}
		}

		void ReleaseFrameInputs() {
			if (Operating || Records.IsActive()) {
				throw std::runtime_error("bitmap glyph input retirement requires its actual completed or aborted outer frame");
			}
			for (std::size_t index = 0; index < InputCount; ++index) {
				InputBorrows[index].reset();
				Inputs[index].reset();
			}
			InputCount = 0;
		}
	};

	BitmapFontRecordReceiver::BitmapFontRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, scene::TextureCache& cache, FrameHostPool& pool) : m_Implementation(std::make_unique<Implementation>(records, renderer, cache, pool)) {
	}

	BitmapFontRecordReceiver::~BitmapFontRecordReceiver() = default;

	void BitmapFontRecordReceiver::Draw(const scene::BitmapConsole& console, const scene::IndicatorPlacement& placement, const scene::BitmapFontTextureOwner& actual_font, std::uint32_t argument) {
		m_Implementation->Draw(console, placement, actual_font, argument);
	}

	void BitmapFontRecordReceiver::ReleaseFrameInputs() {
		m_Implementation->ReleaseFrameInputs();
	}

} // namespace sgre::render
