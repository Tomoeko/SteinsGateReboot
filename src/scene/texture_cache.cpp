#include "sgre/scene/texture_cache.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sgre::scene {

	struct TextureCacheRecord {
		CachedTexture Texture;
		CachedTextureAddress Address;
		std::size_t UploadBytes = 0;
		std::uint64_t Generation = 1;
		bool Active = false;
	};

	class TextureCache::Implementation {
	public:
		Implementation(Upload upload, Release release, TextureCacheLimits limits, UploadBc7 upload_bc7) : UploadTexture(std::move(upload)), UploadBlocks(std::move(upload_bc7)), DropTexture(std::move(release)), Limits(limits) {
			if (!UploadTexture || !DropTexture || limits.MaxTextures == 0 || limits.MaxTextures > 65536 || limits.MaxDecodedTextureBytes == 0 || limits.MaxDecodedTextureBytes > 256 * 1024 * 1024 || limits.MaxTextureBytes == 0) {
				throw std::runtime_error("texture cache requires upload/release services and bounded positive limits");
			}

			Records.resize(limits.MaxTextures);
			Order.reserve(limits.MaxTextures);
		}

		Upload UploadTexture;
		UploadBc7 UploadBlocks;
		Release DropTexture;
		TextureCacheLimits Limits;
		std::vector<TextureCacheRecord> Records;
		std::vector<std::uint32_t> Order;
		std::size_t UploadBytes = 0;
		std::uint64_t NextDocumentIdentity = 1;
		bool InPlatformCallback = false;
		bool Closing = false;
		bool Closed = false;

		void RequireMutation() const {
			if (InPlatformCallback || Closing || Closed) {
				throw std::runtime_error("texture cache mutation requires its live preparation owner outside a platform callback");
			}
		}

		bool Contains(CachedTextureHandle handle) const {
			return handle.Owner == this && handle.Index < Records.size() && Records[handle.Index].Active && Records[handle.Index].Generation == handle.Generation;
		}

		TextureCacheRecord& Record(CachedTextureHandle handle) {
			if (!Contains(handle)) {
				throw std::runtime_error("cached texture handle is stale or belongs to another cache");
			}

			return Records[handle.Index];
		}

		const TextureCacheRecord& Record(CachedTextureHandle handle) const {
			if (!Contains(handle)) {
				throw std::runtime_error("cached texture handle is stale or belongs to another cache");
			}

			return Records[handle.Index];
		}

		CachedTextureHandle Handle(std::uint32_t index) const {
			return {index, Records[index].Generation, this};
		}

		std::optional<CachedTextureHandle> TryAcquire(TextureValueIdentity identity) {
			if (identity.DocumentInstance == 0) {
				throw std::runtime_error("cached texture identity requires a loaded document instance");
			}

			for (const auto index : Order) {
				auto& record = Records[index];
				if (record.Texture.Identity != identity) {
					continue;
				}

				if (record.Texture.References == static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
					throw std::runtime_error("cached texture reference count exceeds its signed native range");
				}

				++record.Texture.References;

				return Handle(index);
			}

			return {};
		}

		void Drop(std::uint32_t index) {
			auto& record = Records[index];
			std::exception_ptr failure;
			InPlatformCallback = true;
			try {
				DropTexture(record.Texture.Backend);
			} catch (...) {
				failure = std::current_exception();
			}
			InPlatformCallback = false;

			// Native GPU destruction precedes ordered cache-vector compaction.
			Order.erase(std::find(Order.begin(), Order.end(), index));
			UploadBytes -= record.UploadBytes;
			record.Active = false;
			record.Texture = {};
			record.Address = {};
			record.UploadBytes = 0;
			++record.Generation;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}

		void Close() {
			if (Closed) {
				return;
			}

			RequireMutation();
			Closing = true;
			std::exception_ptr failure;
			while (!Order.empty()) {
				try {
					Drop(Order.front());
				} catch (...) {
					if (!failure) {
						failure = std::current_exception();
					}
				}
			}

			Closing = false;
			Closed = true;
			if (failure) {
				std::rethrow_exception(failure);
			}
		}
	};

	TextureCache::TextureCache(Upload upload, Release release, TextureCacheLimits limits, UploadBc7 upload_bc7) : m_Implementation(std::make_unique<Implementation>(std::move(upload), std::move(release), limits, std::move(upload_bc7))) {
	}

	TextureCache::~TextureCache() {
		try {
			m_Implementation->Close();
		} catch (...) {
			// Explicit Close reports service failures; the destructor must still retire every cache record.
		}
	}

	std::uint64_t TextureCache::AllocateDocumentIdentity() {
		auto& state = *m_Implementation;
		state.RequireMutation();
		if (state.NextDocumentIdentity == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("loaded texture document identity range is exhausted");
		}

		return state.NextDocumentIdentity++;
	}

	std::optional<CachedTextureHandle> TextureCache::TryAcquire(TextureValueIdentity identity) {
		m_Implementation->RequireMutation();

		return m_Implementation->TryAcquire(identity);
	}

	CachedTextureHandle TextureCache::Acquire(std::uint64_t document_instance, const resource::ImageTexture& texture, std::span<const std::uint8_t> psb_bytes) {
		auto& state = *m_Implementation;
		state.RequireMutation();
		if (!texture.SerializedValueOffset) {
			throw std::runtime_error("texture preparation requires its serialized PSB value identity");
		}

		const TextureValueIdentity identity{document_instance, *texture.SerializedValueOffset};
		if (const auto existing = state.TryAcquire(identity)) {
			return *existing;
		}

		const bool stored_bc7 = texture.Format == resource::PixelFormat::Bc7 && static_cast<bool>(state.UploadBlocks);
		// Preserve the existing decoded-extent domain even when this route allocates no RGBA pixels.
		const auto available_bytes = stored_bc7 ? state.Limits.MaxDecodedTextureBytes : std::min(state.Limits.MaxTextureBytes - state.UploadBytes, state.Limits.MaxDecodedTextureBytes);
		if (texture.Width == 0 || texture.Height == 0 || texture.Width > available_bytes / 4 / texture.Height) {
			throw std::runtime_error("texture preparation exceeds its decoded or aggregate upload storage limit");
		}

		const auto available = std::find_if(state.Records.begin(), state.Records.end(), [](const auto& record) {
			return !record.Active && record.Generation != std::numeric_limits<std::uint64_t>::max();
		});
		if (available == state.Records.end()) {
			throw std::runtime_error("texture cache owner capacity is exhausted");
		}

		const auto blocks = stored_bc7 ? resource::BorrowBc7Texture(texture, psb_bytes, state.Limits.MaxDecodedTextureBytes) : resource::Bc7TextureView{};
		const auto image = stored_bc7 ? resource::RgbaImage{} : resource::DecodeTexture(texture, psb_bytes, state.Limits.MaxDecodedTextureBytes);
		const auto upload_bytes = stored_bc7 ? blocks.Blocks.size() : image.Pixels.size();
		if (upload_bytes > state.Limits.MaxTextureBytes - state.UploadBytes) {
			throw std::runtime_error("texture preparation exceeds its aggregate upload storage limit");
		}
		state.InPlatformCallback = true;
		render::SpriteTextureHandle backend;
		try {
			if (stored_bc7) {
				backend = state.UploadBlocks(blocks);
			} else {
				backend = state.UploadTexture(texture.Format, image);
			}
		} catch (...) {
			state.InPlatformCallback = false;

			throw;
		}
		state.InPlatformCallback = false;

		if (!backend.Owner || backend.Generation == 0 || std::any_of(state.Order.begin(), state.Order.end(), [&](const auto index) {
			    return state.Records[index].Texture.Backend == backend;
		    })) {
			throw std::runtime_error("texture upload service must return a distinct live owned backend texture");
		}

		const auto index = static_cast<std::uint32_t>(available - state.Records.begin());
		available->Texture.Identity = identity;
		available->Texture.Backend = backend;
		available->Texture.Description = texture;
		available->Texture.References = 1;
		available->UploadBytes = upload_bytes;
		available->Active = true;
		state.Order.push_back(index);
		state.UploadBytes += available->UploadBytes;

		return state.Handle(index);
	}

	void TextureCache::ReleaseTexture(CachedTextureHandle texture) {
		auto& state = *m_Implementation;
		state.RequireMutation();
		auto& record = state.Record(texture);
		if (--record.Texture.References != 0) {
			return;
		}

		state.Drop(texture.Index);
	}

	bool TextureCache::Contains(CachedTextureHandle texture) const {
		return m_Implementation->Contains(texture);
	}

	CachedTexture TextureCache::Get(CachedTextureHandle texture) const {
		const auto& state = *m_Implementation;

		return state.Record(texture).Texture;
	}

	std::size_t TextureCache::Count() const {
		return m_Implementation->Order.size();
	}

	std::size_t TextureCache::UploadedBytes() const {
		return m_Implementation->UploadBytes;
	}

	std::size_t TextureCache::WriteRegistrationOrder(std::span<CachedTextureHandle> output) const {
		const auto& state = *m_Implementation;
		if (output.size() < state.Order.size()) {
			throw std::runtime_error("texture cache registration output is too small");
		}

		for (std::size_t index = 0; index < state.Order.size(); ++index) {
			output[index] = state.Handle(state.Order[index]);
		}

		return state.Order.size();
	}

	void TextureCache::SetFiltering(CachedTextureHandle texture, std::int32_t minification, std::int32_t magnification) {
		auto& state = *m_Implementation;
		state.RequireMutation();
		auto& record = state.Record(texture);
		record.Texture.Minification = minification;
		record.Texture.Magnification = magnification;
	}

	void TextureCache::SetAddress(CachedTextureHandle texture, std::uint32_t horizontal, std::uint32_t vertical) {
		auto& state = *m_Implementation;
		state.RequireMutation();
		auto& address = state.Record(texture).Address;
		if (horizontal > 2 || vertical > 2 || address.Revision == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("cached texture address exceeds its native indices or invalidation version domain");
		}

		address.Horizontal = horizontal;
		address.Vertical = vertical;
		++address.Revision;
	}

	CachedTextureAddress TextureCache::Address(CachedTextureHandle texture) const {
		return m_Implementation->Record(texture).Address;
	}

	void TextureCache::Close() {
		m_Implementation->Close();
	}

} // namespace sgre::scene
