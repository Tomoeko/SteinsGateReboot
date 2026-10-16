#pragma once

#include "sgre/resource/font.hpp"
#include "sgre/scene/motion_textures.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sgre::scene {

	class BitmapFontReceivingDocument;

	struct LoadedBitmapFontAsset {
		std::uint64_t DocumentIdentity = 0;
		const TextureCache* CacheOwner = nullptr;
		std::shared_ptr<const resource::PsbDocument> Document;
		std::shared_ptr<const std::vector<std::uint8_t>> Backing;
		std::shared_ptr<const resource::BitmapFont> Model;
	};

	class BitmapFontAssetCatalog {
	public:
		// One catalog serves font providers sharing this cache; the cache outlives loaded owners.
		explicit BitmapFontAssetCatalog(TextureCache& cache, std::size_t max_documents = 4096);
		~BitmapFontAssetCatalog();
		BitmapFontAssetCatalog(const BitmapFontAssetCatalog&) = delete;
		BitmapFontAssetCatalog& operator=(const BitmapFontAssetCatalog&) = delete;
		BitmapFontAssetCatalog(BitmapFontAssetCatalog&&) = delete;
		BitmapFontAssetCatalog& operator=(BitmapFontAssetCatalog&&) = delete;

		[[nodiscard]] std::shared_ptr<const LoadedBitmapFontAsset> Prepare(std::shared_ptr<const resource::PsbDocument> document, std::shared_ptr<const std::vector<std::uint8_t>> backing);
		void Prune();
		[[nodiscard]] std::size_t DocumentCount() const;
		[[nodiscard]] bool UsesCache(const TextureCache& cache) const noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

	class BitmapFontTextureOwner {
	public:
		// The cache outlives this provider. Each source owns its own native texture wrapper.
		BitmapFontTextureOwner(TextureCache& cache, std::shared_ptr<const LoadedBitmapFontAsset> asset);
		// A receiving document retains real preload references after pixel backing is purged.
		BitmapFontTextureOwner(TextureCache& cache, std::shared_ptr<const BitmapFontReceivingDocument> document);
		~BitmapFontTextureOwner();
		BitmapFontTextureOwner(const BitmapFontTextureOwner&) = delete;
		BitmapFontTextureOwner& operator=(const BitmapFontTextureOwner&) = delete;
		BitmapFontTextureOwner(BitmapFontTextureOwner&&) = delete;
		BitmapFontTextureOwner& operator=(BitmapFontTextureOwner&&) = delete;

		[[nodiscard]] const LoadedBitmapFontAsset& Asset() const;
		[[nodiscard]] std::size_t SourceCount() const;
		// Returned wrapper identity borrows this provider and expires when that source retires.
		[[nodiscard]] MotionTextureWrapper::Borrowed Source(std::size_t index) const;
		[[nodiscard]] std::int32_t Minification(std::size_t index) const;
		[[nodiscard]] std::int32_t Magnification(std::size_t index) const;
		void SetFiltering(std::int32_t minification, std::int32_t magnification);
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
