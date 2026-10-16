#pragma once

#include "sgre/resource/image.hpp"
#include "sgre/scene/motion_textures.hpp"
#include "sgre/scene/picture_regions.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace sgre::scene {

	class ImageReceivingDocument;

	struct LoadedImageAsset {
		std::uint64_t DocumentIdentity = 0;
		const TextureCache* CacheOwner = nullptr;
		std::shared_ptr<const resource::PsbDocument> Document;
		std::shared_ptr<const std::vector<std::uint8_t>> Backing;
		std::shared_ptr<const ImageReceivingDocument> Receiving;
		resource::ImageEntry Model;
		std::vector<PictureTileBounds> TileBounds;
		bool Valid = false;
	};

	class ImageAssetCatalog {
	public:
		// A shared catalog keeps one monotonic cache identity per live immutable PSB document.
		explicit ImageAssetCatalog(TextureCache& cache, std::size_t max_documents = 4096);
		~ImageAssetCatalog();
		ImageAssetCatalog(const ImageAssetCatalog&) = delete;
		ImageAssetCatalog& operator=(const ImageAssetCatalog&) = delete;
		[[nodiscard]] std::shared_ptr<const LoadedImageAsset> Prepare(std::shared_ptr<const resource::PsbDocument> document, std::shared_ptr<const std::vector<std::uint8_t>> backing, std::string_view label = {});
		[[nodiscard]] std::shared_ptr<const LoadedImageAsset> Prepare(std::shared_ptr<const ImageReceivingDocument> receiving, std::string_view label = {});
		void Prune();
		[[nodiscard]] std::size_t DocumentCount() const;
		[[nodiscard]] bool UsesCache(const TextureCache& cache) const noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
		[[nodiscard]] std::shared_ptr<const LoadedImageAsset> PrepareDocument(std::shared_ptr<const resource::PsbDocument> document, std::shared_ptr<const std::vector<std::uint8_t>> backing, std::string_view label, std::shared_ptr<const ImageReceivingDocument> receiving);
	};

	class ImageTextureOwner {
	public:
		// Native sources borrow immutable PSB bytes; the script Image separately retains its PSBObject root.
		ImageTextureOwner(TextureCache& cache, std::shared_ptr<const LoadedImageAsset> asset);
		~ImageTextureOwner();
		ImageTextureOwner(const ImageTextureOwner&) = delete;
		ImageTextureOwner& operator=(const ImageTextureOwner&) = delete;
		[[nodiscard]] const LoadedImageAsset& Asset() const;
		[[nodiscard]] std::size_t SourceCount() const;
		[[nodiscard]] MotionTextureWrapper::Borrowed Source(std::size_t index) const;
		[[nodiscard]] std::uint32_t AddressU(std::size_t index) const;
		[[nodiscard]] std::uint32_t AddressV(std::size_t index) const;
		void SetFiltering(std::int32_t minification, std::int32_t magnification);
		void SetAddress(std::uint32_t horizontal, std::uint32_t vertical);
		void Close();

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::scene
