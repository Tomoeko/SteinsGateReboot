#pragma once

#include "sgre/scene/bitmap_font_assets.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sgre::scene {

	class BitmapFontReceivingDocument {
	public:
		// Construct on the cache preparation owner. The cache and its platform services outlive all providers.
		BitmapFontReceivingDocument(TextureCache& cache, std::vector<std::uint8_t> decoded_psb);
		~BitmapFontReceivingDocument();
		BitmapFontReceivingDocument(const BitmapFontReceivingDocument&) = delete;
		BitmapFontReceivingDocument& operator=(const BitmapFontReceivingDocument&) = delete;
		BitmapFontReceivingDocument(BitmapFontReceivingDocument&&) = delete;
		BitmapFontReceivingDocument& operator=(BitmapFontReceivingDocument&&) = delete;

		[[nodiscard]] const resource::PsbDocument& Document() const;
		[[nodiscard]] const resource::BitmapFont& Font() const;
		// Metadata is complete through DataOffset; original chunk ranges remain logical offsets into the retired payload.
		[[nodiscard]] std::span<const std::uint8_t> MetadataBytes() const;
		[[nodiscard]] const std::vector<std::uint8_t>& MetadataStorage() const;
		[[nodiscard]] std::size_t SourceCount() const;
		[[nodiscard]] MotionTextureWrapper::Borrowed Source(std::size_t index) const;
		[[nodiscard]] bool UsesCache(const TextureCache& cache) const noexcept;

	private:
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
		[[nodiscard]] const LoadedBitmapFontAsset& PreparedAsset() const;
		friend class BitmapFontTextureOwner;
	};

} // namespace sgre::scene
