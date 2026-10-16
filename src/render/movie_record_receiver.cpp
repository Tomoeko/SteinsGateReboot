#include "sgre/render/movie_record_receiver.hpp"

#include "sgre/scene/movie_packet.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace sgre::render {

	static scene::SpritePipeline PrepareMoviePipeline(bool alpha) {
		scene::MotionSpriteMaterial material;
		material.TextureFormat = resource::PixelFormat::Bc7;
		material.TextureAttributes = 0;
		material.BlendMode = 0;
		auto pipeline = scene::SelectSpritePipeline(material, {});
		pipeline.NativeVertexShaderIndex = alpha ? 2u : 4u;
		pipeline.NativePixelShaderIndex = alpha ? 2041u : 240u;

		return pipeline;
	}

	MovieRecordReceiver::MovieRecordReceiver(SpriteRenderRecords& records, SpritePassRenderer& renderer, std::size_t maximum_owners) : m_Records(records), m_Renderer(renderer), m_MaximumOwners(maximum_owners) {
		m_Records.RequireRenderer(m_Renderer);
		if (m_Records.IsActive() || maximum_owners == 0 || maximum_owners > 1024) {
			throw std::runtime_error("movie preparation requires its actual idle frame owner and bounded owner capacity");
		}
		m_Backend = m_Renderer.CreateMutableTextureBackend();
		m_Pipelines[0] = m_Renderer.PreparePipeline(PrepareMoviePipeline(false));
		m_Pipelines[1] = m_Renderer.PreparePipeline(PrepareMoviePipeline(true));
		m_Owners.reserve(maximum_owners);
	}

	MovieRecordReceiver::Owner& MovieRecordReceiver::PrepareOwner(const media::MovieDrawFrame& frame) {
		const auto& sample = *frame.Sample.Rgb32;
		auto found = std::find_if(m_Owners.begin(), m_Owners.end(), [&](const auto& owner) {
			return owner.Identity == frame.OwnerIdentity;
		});
		if (found == m_Owners.end()) {
			if (m_Owners.size() == m_MaximumOwners) {
				throw std::runtime_error("movie renderer exceeds its configured live owner capacity");
			}
			Owner next;
			next.Identity = frame.OwnerIdentity;
			next.Texture = m_Records.PrepareMovieTexture(*m_Backend, sample.PaddedWidth, sample.PaddedHeight);
			m_Owners.push_back(std::move(next));
			found = std::prev(m_Owners.end());
		} else if (found->Texture->State().Description.Width != sample.PaddedWidth || found->Texture->State().Description.Height != sample.PaddedHeight) {
			auto replacement = m_Records.PrepareMovieTexture(*m_Backend, sample.PaddedWidth, sample.PaddedHeight);
			found->Texture = std::move(replacement);
			found->Uploaded = false;
		}
		if (!found->Uploaded || found->FrameSerial != frame.FrameSerial) {
			auto mapping = found->Texture->Map();
			if (!mapping.Succeeded || mapping.RowPitch != sample.RowBytes || mapping.Bytes.size() != sample.Bytes.size()) {
				if (mapping.Succeeded) {
					found->Texture->Unmap();
				}
				throw std::runtime_error("movie RGB32 upload mapping differs from its actual worker-prepared padded rows");
			}
			std::memcpy(mapping.Bytes.data(), sample.Bytes.data(), sample.Bytes.size());
			found->Texture->Unmap();
			found->Sample = frame.Sample.Rgb32;
			found->FrameSerial = frame.FrameSerial;
			found->Uploaded = true;
		} else if (found->Sample != frame.Sample.Rgb32) {
			throw std::runtime_error("movie sample identity changed without its actual frame serial");
		}

		return *found;
	}

	void MovieRecordReceiver::Draw(const media::MovieDrawFrame& frame) {
		m_Records.RequireRenderer(m_Renderer);
		if (m_Drawing || !m_Records.IsActive() || frame.OwnerIdentity == nullptr || !frame.Sample.Rgb32 || m_Records.TextureMaskPhase() != scene::MotionMaskPhase::Idle) {
			throw std::runtime_error("movie drawing requires its actual active frame, live sample owner and idle mask");
		}
		const auto& sample = *frame.Sample.Rgb32;
		if (sample.Width == 0 || sample.Height == 0 || sample.Width > sample.PaddedWidth || sample.Height > sample.PaddedHeight || sample.PaddedWidth > 16384 || sample.PaddedHeight > 16384 || sample.RowBytes != static_cast<std::size_t>(sample.PaddedWidth) * 4 || sample.RowBytes * sample.PaddedHeight != sample.Bytes.size()) {
			throw std::runtime_error("movie drawing received inconsistent worker RGB32 dimensions or byte extent");
		}
		scene::MovieQuadDescription description;
		description.Position = {frame.Position[0], frame.Position[1]};
		description.Extent = {frame.Extent[0], frame.Extent[1]};
		description.SourceWidth = sample.Width;
		description.SourceHeight = sample.Height;
		description.TextureWidth = sample.PaddedWidth;
		description.TextureHeight = sample.PaddedHeight;
		description.Opacity = frame.Opacity;
		// Validate geometry before first-use allocation or publication of a new texture owner.
		const auto ordinary = scene::PrepareMovieDrawPacket(description);
		const auto alpha = frame.UseAlpha ? scene::PrepareMovieAlphaDrawPacket(description) : scene::MovieAlphaDrawPacket{};
		m_Drawing = true;
		try {
			auto& owner = PrepareOwner(frame);
			owner.Texture->SetFiltering(frame.FilteringX, frame.FilteringY);
			const auto texture = m_Records.PrepareMutableTexture(*owner.Texture, 0, scene::MutableTextureStage::Pixel);
			m_Records.BeginRecord();
			m_Records.SetBlend(0, 0);
			if (frame.UseAlpha) {
				m_Records.AppendMovieAlpha(texture, m_Pipelines[1], alpha);
			} else {
				m_Records.AppendMovieRaw(texture, m_Pipelines[0], ordinary);
			}
			m_Records.EndRecord();
		} catch (...) {
			m_Drawing = false;
			throw;
		}
		m_Drawing = false;
	}

	bool MovieRecordReceiver::CanRetire(const void*) const {
		m_Records.RequireRenderer(m_Renderer);

		return !m_Drawing && !m_Records.IsActive();
	}

	void MovieRecordReceiver::Retire(const void* owner) {
		if (!CanRetire(owner)) {
			throw std::runtime_error("movie texture retirement requires completed actual outer-frame encoding");
		}
		std::erase_if(m_Owners, [&](const auto& entry) {
			return entry.Identity == owner;
		});
	}

	bool MovieRecordReceiver::Empty() const noexcept {
		return m_Owners.empty();
	}

} // namespace sgre::render
