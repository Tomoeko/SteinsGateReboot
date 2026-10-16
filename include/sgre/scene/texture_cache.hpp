#pragma once

#include "sgre/render/sprite_pass.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace sgre::scene {

	struct TextureValueIdentity {
		std::uint64_t DocumentInstance = 0;
		std::uint32_t ValueOffset = 0;
		bool operator==(const TextureValueIdentity&) const = default;
	};

	struct CachedTextureHandle {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const CachedTextureHandle&) const = default;
	};

	struct CachedTexture {
		TextureValueIdentity Identity;
		render::SpriteTextureHandle Backend;
		resource::ImageTexture Description;
		std::uint32_t References = 0;
		std::int32_t Minification = 0;
		std::int32_t Magnification = 0;
	};

	struct CachedTextureAddress {
		std::uint32_t Horizontal = 0;
		std::uint32_t Vertical = 0;
		// Authored version records every native address invalidation, including repeated values.
		std::uint64_t Revision = 0;
	};

	struct TextureCacheLimits {
		std::size_t MaxTextures = 1024;
		std::size_t MaxDecodedTextureBytes = 64 * 1024 * 1024;
		std::size_t MaxTextureBytes = 256 * 1024 * 1024;
	};

	class TextureCache {
	public:
		using Upload = std::function<render::SpriteTextureHandle(resource::PixelFormat, const resource::RgbaImage&)>;
		using UploadBc7 = std::function<render::SpriteTextureHandle(const resource::Bc7TextureView&)>;
		using Release = std::function<void(render::SpriteTextureHandle)>;
		// Optional BC7 service copies original blocks synchronously; failure never selects the RGBA fallback.
		// Upload/release services outlive this cache. Only the preparation owner mutates it.
		// Loaded document IDs remain unique while their native resource owners retain any reference.
		TextureCache(Upload upload, Release release, TextureCacheLimits limits = {}, UploadBc7 upload_bc7 = {});
		~TextureCache();
		TextureCache(const TextureCache&) = delete;
		TextureCache& operator=(const TextureCache&) = delete;
		TextureCache(TextureCache&&) = delete;
		TextureCache& operator=(TextureCache&&) = delete;

		// Loaded-resource owners use this cache-scoped allocator; IDs are never recycled or serialized.
		[[nodiscard]] std::uint64_t AllocateDocumentIdentity();
		// Reuse compares the document and serialized texture value, independently of pixel contents.
		[[nodiscard]] std::optional<CachedTextureHandle> TryAcquire(TextureValueIdentity identity);
		// Decode/upload occurs only on a cache miss, outside ordinary frames. Bytes are borrowed for this call.
		[[nodiscard]] CachedTextureHandle Acquire(std::uint64_t document_instance, const resource::ImageTexture& texture, std::span<const std::uint8_t> psb_bytes);
		void ReleaseTexture(CachedTextureHandle texture);
		[[nodiscard]] bool Contains(CachedTextureHandle texture) const;
		[[nodiscard]] CachedTexture Get(CachedTextureHandle texture) const;
		[[nodiscard]] std::size_t Count() const;
		[[nodiscard]] std::size_t UploadedBytes() const;
		[[nodiscard]] std::size_t WriteRegistrationOrder(std::span<CachedTextureHandle> output) const;
		// Native sampler writes affect the shared uploaded texture. Unsupported filter pairs stay explicit at draw selection.
		void SetFiltering(CachedTextureHandle texture, std::int32_t minification, std::int32_t magnification);
		// Native address indices are clamp0, wrap1, mirror2. GPU consumption is a separate backend contract.
		void SetAddress(CachedTextureHandle texture, std::uint32_t horizontal, std::uint32_t vertical);
		[[nodiscard]] CachedTextureAddress Address(CachedTextureHandle texture) const;
		// Retire all uploaded owners before their platform services expire; handles become stale.
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
